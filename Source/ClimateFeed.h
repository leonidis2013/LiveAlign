// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Live temperature and humidity from an MQTT broker (Home Assistant's Mosquitto add-on, Zigbee2MQTT, a weather
// station...). A small MQTT 3.1.1 client on its own thread: connects (optionally with a user name and password),
// subscribes to a temperature topic and a humidity topic at QoS 0, and keeps the latest reading of each. The payload
// can be a plain number ("21.4") or JSON ({"temperature": 21.4, "humidity": 45}), read by a key that may be a dotted
// path ("climate.temperature"). Plain TCP only (port 1883 on the local network); no TLS.
// Nothing changes on its own: Venue, Details captures a reading as the session's conditions for tuning (an edit, so it
// can be undone), and Monitoring shows the reading live with what it does to the delays and the top end at the back.
#include <atomic>
#include <cmath>
#include <mutex>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

namespace la
{
namespace mqtt
{
// MQTT topic filter matching: '+' is one level, '#' (last) is the rest.
inline bool topicMatches (const juce::String& filter, const juce::String& topic)
{
    auto f = juce::StringArray::fromTokens (filter, "/", {});
    auto t = juce::StringArray::fromTokens (topic, "/", {});
    for (int i = 0; i < f.size(); ++i)
    {
        if (f[i] == "#") return true;
        if (i >= t.size()) return false;
        if (f[i] != "+" && f[i] != t[i]) return false;
    }
    return f.size() == t.size();
}

// A number from a payload: plain ("21.4", "\"21.4\"", "21.4 °C") or a JSON object read by key (dotted path).
// NAN when there is no number ("unavailable", "unknown", an empty payload, a missing key).
inline double readNumber (const juce::String& payloadIn, const juce::String& key)
{
    const auto payload = payloadIn.trim();
    if (payload.isEmpty()) return NAN;
    auto fromText = [] (juce::String s) -> double
    {
        s = s.trim().unquoted().trim();
        const auto num = s.initialSectionContainingOnly ("0123456789.-+eE");
        if (num.isEmpty() || ! num.containsAnyOf ("0123456789")) return NAN;
        const double v = num.getDoubleValue();
        return std::isfinite (v) ? v : NAN;
    };
    if (payload.startsWithChar ('{'))
    {
        auto v = juce::JSON::parse (payload);
        const auto path = juce::StringArray::fromTokens (key.trim().isEmpty() ? juce::String ("value") : key.trim(), ".", {});
        for (const auto& part : path)
        {
            if (auto* o = v.getDynamicObject())
            {
                const juce::var next = o->getProperty (juce::Identifier (part));   // a copy: v owns the object it points into
                v = next;
            }
            else return NAN;
        }
        if (v.isDouble() || v.isInt() || v.isInt64()) return (double) v;
        if (v.isString()) return fromText (v.toString());
        return NAN;
    }
    return fromText (payload);
}

// ---- packet building (MQTT 3.1.1) ----
inline void putLength (juce::MemoryBlock& b, size_t len)
{
    do
    {
        auto byte = (juce::uint8) (len % 128);
        len /= 128;
        if (len > 0) byte |= 0x80;
        b.append (&byte, 1);
    } while (len > 0);
}
inline void putString (juce::MemoryBlock& b, const juce::String& s)
{
    const auto utf = s.toRawUTF8();
    const auto n = (juce::uint16) juce::jmin ((size_t) 65535, strlen (utf));
    const juce::uint8 hdr[] = { (juce::uint8) (n >> 8), (juce::uint8) (n & 0xff) };
    b.append (hdr, 2);
    b.append (utf, n);
}
inline juce::MemoryBlock packet (juce::uint8 type, const juce::MemoryBlock& body)
{
    juce::MemoryBlock p;
    p.append (&type, 1);
    putLength (p, body.getSize());
    p.append (body.getData(), body.getSize());
    return p;
}
inline juce::MemoryBlock connectPacket (const juce::String& clientId, const juce::String& user, const juce::String& pass, int keepAliveSec)
{
    juce::MemoryBlock v;
    putString (v, "MQTT");
    juce::uint8 flags = 0x02;   // clean session
    if (user.isNotEmpty()) flags |= 0x80;
    if (user.isNotEmpty() && pass.isNotEmpty()) flags |= 0x40;
    const juce::uint8 lvl[] = { 4, flags, (juce::uint8) (keepAliveSec >> 8), (juce::uint8) (keepAliveSec & 0xff) };
    v.append (lvl, 4);
    putString (v, clientId);
    if (user.isNotEmpty()) putString (v, user);
    if (user.isNotEmpty() && pass.isNotEmpty()) putString (v, pass);
    return packet (0x10, v);
}
inline juce::MemoryBlock subscribePacket (int id, const juce::StringArray& topics)
{
    juce::MemoryBlock v;
    const juce::uint8 pid[] = { (juce::uint8) (id >> 8), (juce::uint8) (id & 0xff) };
    v.append (pid, 2);
    for (const auto& t : topics)
    {
        putString (v, t);
        const juce::uint8 qos = 0;
        v.append (&qos, 1);
    }
    return packet (0x82, v);
}
} // namespace mqtt

class ClimateFeed : private juce::Thread
{
public:
    struct Config
    {
        bool enabled = false;
        juce::String host;
        int port = 1883;
        juce::String user, pass;
        juce::String tempTopic, tempKey { "temperature" }, humTopic, humKey { "humidity" };
        bool fahrenheit = false;   // the temperature topic's unit
        bool operator== (const Config& o) const
        {
            return enabled == o.enabled && host == o.host && port == o.port && user == o.user && pass == o.pass && tempTopic == o.tempTopic
                   && tempKey == o.tempKey && humTopic == o.humTopic && humKey == o.humKey && fahrenheit == o.fahrenheit;
        }
        bool usable() const { return enabled && host.trim().isNotEmpty() && (tempTopic.trim().isNotEmpty() || humTopic.trim().isNotEmpty()); }
    };

    ClimateFeed() : juce::Thread ("LiveAlign MQTT") {}
    ~ClimateFeed() override { stop(); }

    // Starts, restarts or stops the connection for this configuration (message thread).
    void configure (const Config& c)
    {
        {
            const std::lock_guard<std::mutex> l (cfgMutex);
            if (c == cfg && (isThreadRunning() || ! c.usable())) return;
        }
        stop();
        {
            const std::lock_guard<std::mutex> l (cfgMutex);
            cfg = c;
        }
        tempC = NAN; humidity = NAN; tempAt = 0; humAt = 0;
        setStatus (c.enabled ? (c.usable() ? "Connecting..." : "Enter the broker and a topic") : "Off");
        if (c.usable()) startThread();
    }
    void stop()
    {
        signalThreadShouldExit();
        notify();
        // a moment to say goodbye to the broker (DISCONNECT); then close the socket under it (a connect in progress)
        if (waitForThreadToExit (400)) return;
        {
            const std::lock_guard<std::mutex> l (sockMutex);
            if (socket != nullptr) socket->close();
        }
        stopThread (3000);
    }

    // The latest readings (NAN until one arrives) and when they came (ms counter; 0 never).
    std::atomic<float> tempC { NAN }, humidity { NAN };
    std::atomic<juce::uint32> tempAt { 0 }, humAt { 0 };
    std::atomic<int> serial { 0 };          // goes up with every reading
    std::atomic<bool> connected { false };

    // The screenshot run: a sensor that is set up and has just read tC and rh, without a broker.
    void simulateForPreview (float tC, float rh)
    {
        stop();
        {
            const std::lock_guard<std::mutex> l (cfgMutex);
            cfg = {};
            cfg.enabled = true; cfg.host = "preview"; cfg.tempTopic = "preview";
        }
        tempC = tC; humidity = rh;
        tempAt = humAt = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter());
        connected = true;
        ++serial;
        setStatus ("Connected to preview, receiving");
    }

    bool enabled() const { const std::lock_guard<std::mutex> l (cfgMutex); return cfg.usable(); }
    // Readings arriving, the last within ten minutes.
    bool live() const
    {
        const auto now = juce::Time::getMillisecondCounter();
        const auto fresh = [now] (juce::uint32 at) { return at != 0 && now - at < 10u * 60u * 1000u; };
        return enabled() && (fresh (tempAt.load()) || fresh (humAt.load()));
    }
    juce::String status() const { const std::lock_guard<std::mutex> l (statusMutex); return statusText; }

    // How old the newest reading is, as "12 s ago" (empty when none).
    juce::String ageText() const
    {
        const auto at = juce::jmax (tempAt.load(), humAt.load());
        if (at == 0) return {};
        const auto s = (juce::Time::getMillisecondCounter() - at) / 1000u;
        return s < 90 ? juce::String ((int) s) + " s ago" : juce::String ((int) (s / 60)) + " min ago";
    }

    // Reads the settings saved on this computer (UserPrefs keys mqtt*).
    template <typename Prefs>
    static Config fromPrefs (Prefs get)
    {
        Config c;
        c.enabled = get ("mqttOn", "0") == "1";
        c.host = get ("mqttHost", "");
        c.port = juce::jlimit (1, 65535, get ("mqttPort", "1883").getIntValue());
        c.user = get ("mqttUser", "");
        c.pass = get ("mqttPass", "");
        c.tempTopic = get ("mqttTempTopic", "");
        c.tempKey = get ("mqttTempKey", "temperature");
        c.humTopic = get ("mqttHumTopic", "");
        c.humKey = get ("mqttHumKey", "humidity");
        c.fahrenheit = get ("mqttTempUnit", "C") == "F";
        return c;
    }

private:
    void setStatus (const juce::String& s) { const std::lock_guard<std::mutex> l (statusMutex); statusText = s; }

    bool sendAll (const juce::MemoryBlock& b)
    {
        const std::lock_guard<std::mutex> l (sockMutex);
        return socket != nullptr && socket->write (b.getData(), (int) b.getSize()) == (int) b.getSize();
    }

    // One whole packet: type byte and body. False on a closed or broken connection.
    bool readPacket (juce::uint8& type, juce::MemoryBlock& body)
    {
        // the packet's bytes follow the first one at once: blocking reads (stop() closes the socket to end one)
        auto readExact = [this] (void* dst, int n) { return n == 0 || (! threadShouldExit() && socket->read (dst, n, true) == n); };
        if (! readExact (&type, 1)) return false;
        size_t len = 0;
        int shift = 0;
        for (int i = 0; i < 4; ++i)
        {
            juce::uint8 b = 0;
            if (! readExact (&b, 1)) return false;
            len |= (size_t) (b & 0x7f) << shift;
            shift += 7;
            if ((b & 0x80) == 0) break;
            if (i == 3) return false;   // malformed length
        }
        if (len > 256 * 1024) return false;   // nothing a sensor sends: not our broker
        body.setSize (len);
        return len == 0 || readExact (body.getData(), (int) len);
    }

    void handlePublish (juce::uint8 type, const juce::MemoryBlock& body, const Config& c)
    {
        const auto* d = static_cast<const juce::uint8*> (body.getData());
        const size_t n = body.getSize();
        if (n < 2) return;
        const size_t tl = ((size_t) d[0] << 8) | d[1];
        if (2 + tl > n) return;
        const auto topic = juce::String::fromUTF8 (reinterpret_cast<const char*> (d + 2), (int) tl);
        size_t at = 2 + tl;
        const int qos = (type >> 1) & 3;
        if (qos > 0)
        {
            if (at + 2 > n) return;
            const juce::uint8 ack[] = { 0x40, 0x02, d[at], d[at + 1] };   // PUBACK (QoS 1; QoS 2 is not asked for)
            juce::MemoryBlock a (ack, 4);
            sendAll (a);
            at += 2;
        }
        const auto payload = juce::String::fromUTF8 (reinterpret_cast<const char*> (d + at), (int) (n - at));
        const auto now = juce::jmax ((juce::uint32) 1, juce::Time::getMillisecondCounter());
        bool any = false;
        if (c.tempTopic.isNotEmpty() && mqtt::topicMatches (c.tempTopic.trim(), topic))
        {
            double v = mqtt::readNumber (payload, c.tempKey);
            if (std::isfinite (v))
            {
                if (c.fahrenheit) v = (v - 32.0) * 5.0 / 9.0;
                if (v > -40.0 && v < 60.0) { tempC = (float) v; tempAt = now; any = true; }
            }
        }
        if (c.humTopic.isNotEmpty() && mqtt::topicMatches (c.humTopic.trim(), topic))
        {
            const double v = mqtt::readNumber (payload, c.humKey);
            if (std::isfinite (v) && v >= 0.0 && v <= 100.0) { humidity = (float) v; humAt = now; any = true; }
        }
        if (any) ++serial;
    }

    void run() override
    {
        int backoffMs = 2000;
        while (! threadShouldExit())
        {
            Config c;
            {
                const std::lock_guard<std::mutex> l (cfgMutex);
                c = cfg;
            }
            {
                const std::lock_guard<std::mutex> l (sockMutex);
                socket = std::make_unique<juce::StreamingSocket>();
            }
            setStatus ("Connecting to " + c.host + "...");
            const bool ok = socket->connect (c.host.trim(), c.port, 4000) && runSession (c);
            connected = false;
            {
                const std::lock_guard<std::mutex> l (sockMutex);
                socket->close();
            }
            if (threadShouldExit()) break;
            if (! ok && status().startsWith ("Connecting")) setStatus ("Can't reach " + c.host + ":" + juce::String (c.port) + "; trying again");
            // wait and try again, longer each time (up to a minute)
            for (int waited = 0; waited < backoffMs && ! threadShouldExit(); waited += 100) wait (100);
            backoffMs = juce::jmin (60000, backoffMs * 2);
            if (ok) backoffMs = 2000;
        }
        setStatus ("Off");
    }

    // Connected: log in, subscribe, read until the connection drops. True if it got as far as subscribing.
    bool runSession (const Config& c)
    {
        constexpr int keepAlive = 60;
        const auto id = "LiveAlign-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt()).paddedLeft ('0', 8);
        if (! sendAll (mqtt::connectPacket (id, c.user, c.pass, keepAlive))) return false;
        juce::uint8 type = 0;
        juce::MemoryBlock body;
        if (socket->waitUntilReady (true, 5000) <= 0 || ! readPacket (type, body) || (type & 0xf0) != 0x20 || body.getSize() < 2)
        {
            setStatus ("No answer from " + c.host + " (is it an MQTT broker?)");
            return false;
        }
        const int rc = static_cast<const juce::uint8*> (body.getData())[1];
        if (rc != 0)
        {
            static const char* why[] = { "", "protocol not accepted", "client refused", "broker unavailable", "wrong user name or password", "not authorised" };
            setStatus (juce::String ("Broker refused: ") + (rc <= 5 ? why[rc] : "error " + juce::String (rc)));
            return false;
        }
        juce::StringArray topics;
        if (c.tempTopic.trim().isNotEmpty()) topics.add (c.tempTopic.trim());
        if (c.humTopic.trim().isNotEmpty()) topics.addIfNotAlreadyThere (c.humTopic.trim());
        if (! sendAll (mqtt::subscribePacket (1, topics))) return false;
        connected = true;
        setStatus ("Connected to " + c.host + ", waiting for a reading");
        auto lastSent = juce::Time::getMillisecondCounter(), lastHeard = lastSent;
        while (! threadShouldExit())
        {
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastSent > (juce::uint32) keepAlive * 500u)
            {
                const juce::uint8 ping[] = { 0xc0, 0x00 };
                if (! sendAll (juce::MemoryBlock (ping, 2))) break;
                lastSent = now;
            }
            if (now - lastHeard > (juce::uint32) keepAlive * 1500u) { setStatus ("The broker stopped answering; reconnecting"); break; }
            const int ready = socket->waitUntilReady (true, 200);
            if (ready < 0) break;
            if (ready == 0) continue;
            if (! readPacket (type, body)) { if (! threadShouldExit()) setStatus ("Connection to " + c.host + " closed; reconnecting"); break; }
            lastHeard = juce::Time::getMillisecondCounter();
            if ((type & 0xf0) == 0x30)
            {
                handlePublish (type, body, c);
                juce::String s = "Connected to " + c.host;
                const float t = tempC.load(), h = humidity.load();
                if (std::isfinite (t) || std::isfinite (h)) s << ", receiving";
                setStatus (s);
            }
            else if ((type & 0xf0) == 0x90 && body.getSize() >= 3)
            {
                const auto* d = static_cast<const juce::uint8*> (body.getData());
                for (size_t i = 2; i < body.getSize(); ++i)
                    if (d[i] == 0x80) setStatus ("Connected, but the broker refused a topic (check the topic and its access)");
            }
        }
        const juce::uint8 bye[] = { 0xe0, 0x00 };   // DISCONNECT
        sendAll (juce::MemoryBlock (bye, 2));
        return true;
    }

    mutable std::mutex cfgMutex, statusMutex, sockMutex;
    Config cfg;
    juce::String statusText { "Off" };
    std::unique_ptr<juce::StreamingSocket> socket;
};
} // namespace la
