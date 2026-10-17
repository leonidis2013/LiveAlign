// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <cmath>
// Venue and session data that is not an analysis setting. Message thread only.
//
// A venue file holds what stays with the room and the system (mics and their calibration, positions, the speaker
// setup) and a list of sessions. Each session is one visit: its own checks, delays and traces.
#include <array>
#include <memory>
#include <vector>
#include <juce_core/juce_core.h>
#include "Settings.h"
#include "MicCalibration.h"

namespace la
{
// What belongs to the venue for each mic.
struct VenueMics
{
    std::array<juce::String, kMaxMics> name, position;
    std::array<juce::String, kMaxMics> lockerId;
    std::array<int, kMaxMics> aim { 1, 1, 1, 1, 1, 1, 1, 1 };   // which calibration: 0 = at the speaker (0 deg), 1 = pointing up (90 deg)     // linked mic in this computer's mic locker (empty: none)
    std::array<bool, kMaxMics> noCalFile {};    // "this mic has no calibration file"
    bool noCalibrator = false;                  // levels stay in dBFS; no SPL calibration

    juce::String nameOf (int m) const { return name[(size_t) m].isNotEmpty() ? name[(size_t) m] : "Mic " + juce::String (m + 1); }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("Mics");
        e->setAttribute ("noCalibrator", noCalibrator);
        for (int m = 0; m < kMaxMics; ++m)
        {
            auto* c = e->createNewChildElement ("Mic");
            c->setAttribute ("index", m);
            c->setAttribute ("name", name[(size_t) m]);
            c->setAttribute ("position", position[(size_t) m]);
            c->setAttribute ("noCalFile", noCalFile[(size_t) m]);
            c->setAttribute ("lockerId", lockerId[(size_t) m]);
            c->setAttribute ("aim", calSlotDegrees (aim[(size_t) m]));
        }
    }

    void load (const juce::XmlElement& parent)
    {
        *this = {};
        if (const auto* e = parent.getChildByName ("Mics"))
        {
            noCalibrator = e->getBoolAttribute ("noCalibrator");
            for (const auto* c : e->getChildWithTagNameIterator ("Mic"))
            {
                const int m = c->getIntAttribute ("index", -1);
                if (m < 0 || m >= kMaxMics) continue;
                name[(size_t) m] = c->getStringAttribute ("name");
                position[(size_t) m] = c->getStringAttribute ("position");
                noCalFile[(size_t) m] = c->getBoolAttribute ("noCalFile");
                lockerId[(size_t) m] = c->getStringAttribute ("lockerId");
                aim[(size_t) m] = calSlotFromDegrees (c->getIntAttribute ("aim", 90));
            }
        }
    }
};

// The speaker setup is done once per venue and stays valid until something changes.
struct SpeakerStatus
{
    bool configured = false;
    juce::String configuredOn;
    bool changed = false;          // the user marked the system as changed since it was set up

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("Speakers");
        e->setAttribute ("configured", configured);
        e->setAttribute ("configuredOn", configuredOn);
        e->setAttribute ("changed", changed);
    }

    void load (const juce::XmlElement& parent)
    {
        *this = {};
        if (const auto* e = parent.getChildByName ("Speakers"))
        {
            configured = e->getBoolAttribute ("configured");
            configuredOn = e->getStringAttribute ("configuredOn");
            changed = e->getBoolAttribute ("changed");
        }
    }
};

// Checks that are redone (quickly) in every session.
struct SessionChecks
{
    std::array<bool, kMaxMics> splVerified {};   // calibrated or confirmed in this session

    bool loopbackSkipped = false;
    bool loopbackDone = false;
    double loopbackLatencyMs = 0.0;
    bool loopbackInverted = false;
    float loopbackLevelDbfs = -200.0f;

    bool noiseDone = false;
    int noiseMics = 0;
    std::array<float, kMaxMics> noiseA {}, noiseC {};
    std::array<bool, kMaxMics> noiseIsSpl {};
    // octave band noise levels 63 Hz - 8 kHz per mic (for NC / NR), -200 = not measured
    std::array<std::array<float, 8>, kMaxMics> noiseOct = [] { std::array<std::array<float, 8>, kMaxMics> a {}; for (auto& r : a) r.fill (-200.0f); return a; }();

    // From the 1/3 octave bands: each octave is the energy sum of its three thirds.
    void setNoiseOctaves (int m, const std::vector<double>& freq, const std::vector<float>& db)
    {
        static const double oct[8] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000 };
        for (int b = 0; b < 8; ++b)
        {
            double e = 0.0;
            int n = 0;
            for (size_t i = 0; i < freq.size() && i < db.size(); ++i)
                if (freq[i] >= oct[b] / 1.42 && freq[i] < oct[b] * 1.42 && std::isfinite (db[i])) { e += std::pow (10.0, db[i] / 10.0); ++n; }
            noiseOct[(size_t) m][(size_t) b] = n > 0 ? (float) (10.0 * std::log10 (e)) : -200.0f;
        }
    }

    juce::String setup;   // interface and wiring when the checks were done (see setupFingerprint)

    // The conditions in the room this session (NAN: not entered), and those when the delays and EQ were last set.
    float tempC = NAN, humidity = NAN;
    float tunedTempC = NAN, tunedHumidity = NAN;
    juce::String tunedConditionsOn;

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("Checks");
        e->setAttribute ("loopbackSkipped", loopbackSkipped);
        e->setAttribute ("loopbackDone", loopbackDone);
        e->setAttribute ("loopbackLatencyMs", loopbackLatencyMs);
        e->setAttribute ("loopbackInverted", loopbackInverted);
        e->setAttribute ("loopbackLevelDbfs", loopbackLevelDbfs);
        e->setAttribute ("noiseDone", noiseDone);
        e->setAttribute ("noiseMics", noiseMics);
        e->setAttribute ("setup", setup);
        auto putF = [e] (const char* k, float v) { if (std::isfinite (v)) e->setAttribute (k, v); };
        putF ("tempC", tempC);
        putF ("humidity", humidity);
        putF ("tunedTempC", tunedTempC);
        putF ("tunedHumidity", tunedHumidity);
        if (tunedConditionsOn.isNotEmpty()) e->setAttribute ("tunedConditionsOn", tunedConditionsOn);
        for (int m = 0; m < kMaxMics; ++m)
        {
            const juce::String k (m);
            e->setAttribute ("splVerified" + k, splVerified[(size_t) m]);
            e->setAttribute ("noiseA" + k, noiseA[(size_t) m]);
            e->setAttribute ("noiseC" + k, noiseC[(size_t) m]);
            e->setAttribute ("noiseIsSpl" + k, noiseIsSpl[(size_t) m]);
            juce::StringArray o;
            for (float v : noiseOct[(size_t) m]) o.add (juce::String (v, 1));
            e->setAttribute ("noiseOct" + k, o.joinIntoString (" "));
        }
    }

    void load (const juce::XmlElement& parent)
    {
        *this = {};
        if (const auto* e = parent.getChildByName ("Checks"))
        {
            loopbackSkipped = e->getBoolAttribute ("loopbackSkipped");
            loopbackDone = e->getBoolAttribute ("loopbackDone");
            loopbackLatencyMs = e->getDoubleAttribute ("loopbackLatencyMs");
            loopbackInverted = e->getBoolAttribute ("loopbackInverted");
            loopbackLevelDbfs = (float) e->getDoubleAttribute ("loopbackLevelDbfs", -200.0);
            noiseDone = e->getBoolAttribute ("noiseDone");
            noiseMics = juce::jlimit (0, kMaxMics, e->getIntAttribute ("noiseMics"));
            setup = e->getStringAttribute ("setup");
            auto getF = [e] (const char* k) { return e->hasAttribute (k) ? (float) e->getDoubleAttribute (k) : NAN; };
            tempC = getF ("tempC");
            humidity = getF ("humidity");
            tunedTempC = getF ("tunedTempC");
            tunedHumidity = getF ("tunedHumidity");
            tunedConditionsOn = e->getStringAttribute ("tunedConditionsOn");
            for (int m = 0; m < kMaxMics; ++m)
            {
                const juce::String k (m);
                splVerified[(size_t) m] = e->getBoolAttribute ("splVerified" + k);
                noiseA[(size_t) m] = (float) e->getDoubleAttribute ("noiseA" + k, -200.0);
                noiseC[(size_t) m] = (float) e->getDoubleAttribute ("noiseC" + k, -200.0);
                noiseIsSpl[(size_t) m] = e->getBoolAttribute ("noiseIsSpl" + k);
                juce::StringArray o;
                o.addTokens (e->getStringAttribute ("noiseOct" + k), " ", {});
                for (int b = 0; b < 8; ++b) noiseOct[(size_t) m][(size_t) b] = b < o.size() ? o[b].getFloatValue() : -200.0f;
            }
        }
        else if (const auto* old = parent.getChildByName ("MicSetup"))   // version 0.3
        {
            loopbackSkipped = old->getBoolAttribute ("loopbackSkipped");
            loopbackDone = old->getBoolAttribute ("loopbackDone");
            loopbackLatencyMs = old->getDoubleAttribute ("loopbackLatencyMs");
            loopbackInverted = old->getBoolAttribute ("loopbackInverted");
            loopbackLevelDbfs = (float) old->getDoubleAttribute ("loopbackLevelDbfs", -200.0);
            noiseDone = old->getBoolAttribute ("noiseDone");
            noiseMics = noiseDone ? 1 : 0;
            noiseA[0] = (float) old->getDoubleAttribute ("noiseA", -200.0);
            noiseC[0] = (float) old->getDoubleAttribute ("noiseC", -200.0);
            noiseIsSpl[0] = old->getBoolAttribute ("noiseIsSpl");
        }
    }
};

// Sessions other than the open one keep their contents as XML until they are opened.
struct SessionInfo
{
    juce::String id, name, created, notes;
    int kind = 0;                              // 0 a tuning visit, 1 show monitoring (the menu and the generator follow it)
    std::unique_ptr<juce::XmlElement> body;   // traces, checks, delays (null for the open session)

    SessionInfo() = default;
    SessionInfo (const SessionInfo& o) : id (o.id), name (o.name), created (o.created), notes (o.notes), kind (o.kind),
                                         body (o.body != nullptr ? std::make_unique<juce::XmlElement> (*o.body) : nullptr) {}
    SessionInfo& operator= (const SessionInfo& o)
    {
        id = o.id; name = o.name; created = o.created; notes = o.notes; kind = o.kind;
        body = o.body != nullptr ? std::make_unique<juce::XmlElement> (*o.body) : nullptr;
        return *this;
    }
    SessionInfo (SessionInfo&&) = default;
    SessionInfo& operator= (SessionInfo&&) = default;
};
} // namespace la
