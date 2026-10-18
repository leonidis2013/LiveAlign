// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Monitoring has its own mic, separate from the tuning mics: usually one mic at FOH, left there for the event.
// It has its own input, calibration files, sound level calibration and delay (saved with the venue), and the sound
// level is logged during the event for the monitoring report (saved with the session).
#include <algorithm>
#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>
#include "MicCalibration.h"
#include "Prefs.h"

namespace la
{
struct MonitorMic
{
    juce::String name { "FOH mic" }, position { "FOH" }, lockerId;
    int input = 0;                  // interface input (0-based)
    int aim = Cal90;                // which calibration file: pointing up at FOH is usual
    MicCalSet cal;
    bool noCalFile = false;
    bool splCalibrated = false;
    float splOffset = 120.0f;
    juce::String splDate, splTime;  // when the sound level was calibrated (date, "HH:MM")
    float splRefDb = 0.0f;          // ... with a calibrator at this level (94, 114 dB SPL; 0 = not recorded)
    float delayMs = 0.0f;
    bool configured = false;        // set up on the Monitor mic page (else it is taken from the tuning mics once)

    // "With a 94 dB calibrator, 18 Oct 2026 10:12 (0 dBFS = 100.81 dB SPL)"; `offset` is the one in use now.
    juce::String calibrationText (float offset) const
    {
        if (! splCalibrated) return "Not calibrated (levels in dBFS)";
        juce::String s;
        s << (splRefDb > 0.0f ? "With a " + juce::String (splRefDb, std::abs (splRefDb - std::round (splRefDb)) < 0.05f ? 0 : 1) + " dB calibrator" : juce::String ("Calibrated"));
        if (splDate.isNotEmpty()) s << ", " << splDate << (splTime.isNotEmpty() ? " " + splTime : juce::String());
        s << " (0 dBFS = " << juce::String (offset, 2) << " dB SPL)";
        return s;
    }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("MonitorMic");
        e->setAttribute ("name", name);
        e->setAttribute ("position", position);
        e->setAttribute ("lockerId", lockerId);
        e->setAttribute ("input", input);
        e->setAttribute ("aim", calSlotDegrees (aim));
        e->setAttribute ("noCalFile", noCalFile);
        e->setAttribute ("splCalibrated", splCalibrated);
        e->setAttribute ("splOffset", splOffset);
        e->setAttribute ("splDate", splDate);
        e->setAttribute ("splTime", splTime);
        e->setAttribute ("splRefDb", splRefDb);
        e->setAttribute ("delayMs", delayMs);
        e->setAttribute ("configured", configured);
        for (int a = 0; a < kCalSlots; ++a)
        {
            if (! cal.has (a)) continue;
            auto* c = e->createNewChildElement ("Calibration");
            c->setAttribute ("angle", calSlotDegrees (a));
            c->setAttribute ("name", cal.name[a]);
            MicCalSet::saveTable (*c, cal.table[a]);
        }
    }

    void load (const juce::XmlElement& parent)
    {
        *this = {};
        const auto* e = parent.getChildByName ("MonitorMic");
        if (e == nullptr) return;
        name = e->getStringAttribute ("name", "FOH mic");
        position = e->getStringAttribute ("position", "FOH");
        lockerId = e->getStringAttribute ("lockerId");
        input = juce::jlimit (0, 15, e->getIntAttribute ("input"));
        aim = calSlotFromDegrees (e->getIntAttribute ("aim", 90));
        noCalFile = e->getBoolAttribute ("noCalFile");
        splCalibrated = e->getBoolAttribute ("splCalibrated");
        splOffset = juce::jlimit (0.0f, 200.0f, (float) e->getDoubleAttribute ("splOffset", 120.0));
        splDate = e->getStringAttribute ("splDate");
        splTime = e->getStringAttribute ("splTime");
        splRefDb = (float) e->getDoubleAttribute ("splRefDb", 0.0);
        delayMs = juce::jlimit (0.0f, 2000.0f, (float) e->getDoubleAttribute ("delayMs"));
        configured = e->getBoolAttribute ("configured");
        for (const auto* c : e->getChildWithTagNameIterator ("Calibration"))
        {
            auto t = MicCalSet::loadTable (*c);
            if (t.freq.size() < 3) continue;
            const int a = calSlotFromDegrees (c->getIntAttribute ("angle"));
            cal.table[a] = std::move (t);
            cal.name[a] = c->getStringAttribute ("name");
        }
    }
};

// Sound level during the event: one point every few seconds (energy averages), for the monitoring report. With it,
// the parts of the event marked as it ran (Worship, Sermon ...), the frequencies the feedback watch caught, and the
// room sensor's temperature and humidity when one is connected. Saved with the session.
struct SplLog
{
    struct Point { double t; float la, lc, laMax; float tempC = NAN, rh = NAN; };   // t: seconds since 1970; LAeq, LCeq over the interval; max LAF
    struct Part { double t; juce::String name; };       // a part of the event starts at t (and runs to the next)
    struct Hit { double t; float freq, cutDb; };         // the feedback watch caught a ring
    struct Glitch { double t; juce::String what; };     // the audio health found something ("dropout", "Mic 1: gap")
    std::vector<Point> points;
    std::vector<Part> parts;
    std::vector<Hit> hits;
    std::vector<Glitch> glitches;
    double limitLaeq = 0.0;        // the limit over `limitMinutes` (0 = none), A or C weighted (`limitC`)
    int limitMinutes = 15;
    bool limitC = false;           // the limit is an LCeq (bass included) rather than an LAeq
    double interval = 5.0;         // seconds per logged point

    juce::String limitName() const { return limitC ? "LCeq" : "LAeq"; }
    juce::String limitUnit() const { return limitC ? "dBC" : "dBA"; }

    // A reading (A and C slow, A fast, dB) taken about once a second, with the room sensor's reading if there is one.
    void add (double t, float laS, float lcS, float laF, float tempC = NAN, float rh = NAN)
    {
        if (! std::isfinite (laS) || laS < -150.0f) return;
        if (pending == 0) pendingStart = t;
        ea += std::pow (10.0, laS / 10.0);
        ec += std::pow (10.0, lcS / 10.0);
        mx = std::max (mx, laF);
        if (std::isfinite (tempC)) { st += tempC; ++nt; }
        if (std::isfinite (rh)) { sh += rh; ++nh; }
        ++pending;
        if (t - pendingStart >= interval - 0.5)
        {
            points.push_back ({ pendingStart, (float) (10.0 * std::log10 (ea / pending)), (float) (10.0 * std::log10 (ec / pending)), mx,
                                nt > 0 ? (float) (st / nt) : NAN, nh > 0 ? (float) (sh / nh) : NAN });
            pending = 0; ea = ec = 0.0; mx = -200.0f; st = sh = 0.0; nt = nh = 0;
        }
    }

    void markPart (double t, const juce::String& name) { if (name.trim().isNotEmpty()) parts.push_back ({ t, name.trim() }); }
    void addHit (double t, float freq, float cutDb) { hits.push_back ({ t, freq, cutDb }); }
    void addGlitch (double t, const juce::String& what) { if (glitches.size() < 5000) glitches.push_back ({ t, what }); }
    juce::String partAt (double t) const
    {
        juce::String n;
        for (const auto& p : parts) if (p.t <= t) n = p.name;
        return n;
    }

    bool empty() const { return points.empty(); }
    double start() const { return points.empty() ? 0.0 : points.front().t; }
    double end() const { return points.empty() ? 0.0 : points.back().t + interval; }

    // LAeq or LCeq over [from, to) (whole log by default).
    double leq (bool cWeighted, double from = 0.0, double to = 1e300) const
    {
        double e = 0.0;
        int n = 0;
        for (const auto& p : points)
            if (p.t >= from && p.t < to) { e += std::pow (10.0, (cWeighted ? p.lc : p.la) / 10.0); ++n; }
        return n > 0 ? 10.0 * std::log10 (e / n) : NAN;
    }
    double maxLaf (double from = 0.0, double to = 1e300) const
    {
        float m = -200.0f;
        bool any = false;
        for (const auto& p : points) if (p.t >= from && p.t < to) { m = std::max (m, p.laMax); any = true; }
        return any ? m : NAN;
    }

    // Rolling Leq (A, or C for a C-weighted limit) over the limit window, at each point.
    std::vector<double> rollingLaeq() const
    {
        const bool c = limitC;
        std::vector<double> out;
        const double w = limitMinutes * 60.0;
        size_t j = 0;
        double e = 0.0;
        for (size_t i = 0; i < points.size(); ++i)
        {
            e += std::pow (10.0, (c ? points[i].lc : points[i].la) / 10.0);
            while (points[i].t - points[j].t >= w) { e -= std::pow (10.0, (c ? points[j].lc : points[j].la) / 10.0); ++j; }
            out.push_back (10.0 * std::log10 (std::max (1e-30, e) / (double) (i - j + 1)));
        }
        return out;
    }

    // Minutes the rolling LAeq was over the limit (in [from, to)).
    double minutesOver (double from = 0.0, double to = 1e300) const
    {
        if (limitLaeq <= 0.0) return 0.0;
        const auto r = rollingLaeq();
        int n = 0;
        for (size_t i = 0; i < points.size(); ++i) n += points[i].t >= from && points[i].t < to && r[i] > limitLaeq ? 1 : 0;
        return n * interval / 60.0;
    }

    // ---- the data table: a row per minute, and a summary per part
    struct Row
    {
        double t = 0.0;                          // the minute's start
        float la = NAN, lc = NAN, laMax = NAN;   // LAeq, LCeq, LAF max over the minute
        double rolling = NAN;                    // the rolling Leq (limit window) at the minute's end
        bool over = false;
        juce::String feedback;                   // "377 Hz, 4.2 kHz"
        juce::String audio;                      // the audio health's findings ("dropout", "Console feed: regular clicks")
        float tempC = NAN, rh = NAN;
        juce::String part;
    };
    std::vector<Row> minuteRows() const
    {
        std::vector<Row> rows;
        if (points.empty()) return rows;
        const auto roll = rollingLaeq();
        size_t i = 0;
        while (i < points.size())
        {
            Row r;
            r.t = std::floor (points[i].t / 60.0) * 60.0;
            double ea2 = 0.0, ec2 = 0.0, ts = 0.0, hs = 0.0;
            int n = 0, nt2 = 0, nh2 = 0;
            float m = -200.0f;
            for (; i < points.size() && points[i].t < r.t + 60.0; ++i)
            {
                const auto& p = points[i];
                ea2 += std::pow (10.0, p.la / 10.0); ec2 += std::pow (10.0, p.lc / 10.0); ++n;
                m = std::max (m, p.laMax);
                if (std::isfinite (p.tempC)) { ts += p.tempC; ++nt2; }
                if (std::isfinite (p.rh)) { hs += p.rh; ++nh2; }
                r.rolling = roll[i];
            }
            r.la = (float) (10.0 * std::log10 (ea2 / n));
            r.lc = (float) (10.0 * std::log10 (ec2 / n));
            r.laMax = m;
            r.over = limitLaeq > 0.0 && r.rolling > limitLaeq;
            if (nt2 > 0) r.tempC = (float) (ts / nt2);
            if (nh2 > 0) r.rh = (float) (hs / nh2);
            juce::StringArray f;
            for (const auto& h : hits) if (h.t >= r.t && h.t < r.t + 60.0) f.addIfNotAlreadyThere (freqText (h.freq));
            r.feedback = f.joinIntoString (", ");
            juce::StringArray gl;
            for (const auto& gg : glitches) if (gg.t >= r.t && gg.t < r.t + 60.0) gl.addIfNotAlreadyThere (gg.what);
            r.audio = gl.joinIntoString (", ");
            r.part = partAt (r.t + 59.0);
            rows.push_back (r);
        }
        return rows;
    }

    struct PartSummary
    {
        juce::String name;
        double from = 0.0, to = 0.0;
        double la = NAN, lc = NAN, laMax = NAN, peakRolling = NAN, minutesOver = 0.0;
        int hits = 0;
    };
    // Each part (from its mark to the next); what was logged before the first mark is "Before <first part>".
    std::vector<PartSummary> partSummaries() const
    {
        std::vector<PartSummary> out;
        if (points.empty() || parts.empty()) return out;
        auto sorted = parts;
        std::sort (sorted.begin(), sorted.end(), [] (const Part& a, const Part& b) { return a.t < b.t; });
        std::vector<std::pair<juce::String, std::pair<double, double>>> spans;
        if (sorted.front().t - start() >= 60.0) spans.push_back ({ "Before " + sorted.front().name, { start(), sorted.front().t } });
        for (size_t k = 0; k < sorted.size(); ++k)
            spans.push_back ({ sorted[k].name, { sorted[k].t, k + 1 < sorted.size() ? sorted[k + 1].t : end() } });
        const auto roll = rollingLaeq();
        for (const auto& sp : spans)
        {
            PartSummary s;
            s.name = sp.first;
            s.from = sp.second.first;
            s.to = sp.second.second;
            s.la = leq (false, s.from, s.to);
            if (! std::isfinite (s.la)) continue;   // nothing logged in it
            s.lc = leq (true, s.from, s.to);
            s.laMax = maxLaf (s.from, s.to);
            double pk = -1e9;
            for (size_t i = 0; i < points.size(); ++i) if (points[i].t >= s.from && points[i].t < s.to) pk = std::max (pk, roll[i]);
            s.peakRolling = pk;
            s.minutesOver = minutesOver (s.from, s.to);
            for (const auto& h : hits) s.hits += h.t >= s.from && h.t < s.to ? 1 : 0;
            out.push_back (s);
        }
        return out;
    }

    // The table as CSV (opens in Excel): a row per minute. `fahrenheit`: the temperature unit.
    juce::String minutesCsv (bool fahrenheit) const
    {
        auto num = [] (double v, int d = 1) { return std::isfinite (v) ? juce::String (v, d) : juce::String(); };
        juce::String s;
        s << "Date,Time,Part,LAeq (dB),LCeq (dB),LC-LA (dB),LAF max (dB)," << juce::String (limitMinutes) << " min " << limitName() << " (dB),Over limit,Feedback,"
          << (fahrenheit ? "Temperature (F)" : "Temperature (C)") << ",Humidity (%),Audio\r\n";
        for (const auto& r : minuteRows())
        {
            const juce::Time tm ((juce::int64) (r.t * 1000.0));
            const double temp = std::isfinite (r.tempC) ? (fahrenheit ? r.tempC * 9.0 / 5.0 + 32.0 : r.tempC) : NAN;
            s << tm.formatted ("%Y-%m-%d") << ',' << tm.formatted ("%H:%M") << ',' << csvText (r.part) << ',' << num (r.la) << ',' << num (r.lc) << ','
              << num (r.lc - r.la) << ',' << num (r.laMax) << ',' << num (r.rolling) << ',' << (r.over ? "over" : "") << ',' << csvText (r.feedback) << ','
              << num (temp) << ',' << num (r.rh, 0) << ',' << csvText (r.audio) << "\r\n";
        }
        return s;
    }

    // ... and a row per part.
    juce::String partsCsv() const
    {
        auto num = [] (double v, int d = 1) { return std::isfinite (v) ? juce::String (v, d) : juce::String(); };
        juce::String s;
        s << "Part,Start,End,Minutes,LAeq (dB),LCeq (dB),LC-LA (dB),LAF max (dB),Highest " << juce::String (limitMinutes) << " min " << limitName()
          << " (dB),Minutes over limit,Feedback rings\r\n";
        for (const auto& p : partSummaries())
            s << csvText (p.name) << ',' << juce::Time ((juce::int64) (p.from * 1000.0)).formatted ("%H:%M") << ','
              << juce::Time ((juce::int64) (p.to * 1000.0)).formatted ("%H:%M") << ',' << juce::String (juce::roundToInt ((p.to - p.from) / 60.0)) << ','
              << num (p.la) << ',' << num (p.lc) << ',' << num (p.lc - p.la) << ',' << num (p.laMax) << ',' << num (p.peakRolling) << ','
              << num (p.minutesOver, 0) << ',' << juce::String (p.hits) << "\r\n";
        return s;
    }

    static juce::String freqText (double f) { return f >= 1000.0 ? juce::String (f / 1000.0, f >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (f)) + " Hz"; }
    static juce::String csvText (const juce::String& v) { return v.containsAnyOf (",\"\n") ? "\"" + v.replace ("\"", "\"\"") + "\"" : v; }

    void clear() { points.clear(); parts.clear(); hits.clear(); glitches.clear(); pending = 0; ea = ec = 0.0; mx = -200.0f; st = sh = 0.0; nt = nh = 0; }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("SplLog");
        e->setAttribute ("limitLaeq", limitLaeq);
        e->setAttribute ("limitMinutes", limitMinutes);
        e->setAttribute ("limitC", limitC);
        e->setAttribute ("interval", interval);
        juce::String text;
        for (const auto& p : points)
        {
            text << juce::String ((juce::int64) p.t) << ' ' << juce::String (p.la, 1) << ' ' << juce::String (p.lc, 1) << ' ' << juce::String (p.laMax, 1);
            if (std::isfinite (p.tempC) || std::isfinite (p.rh))
                text << ' ' << (std::isfinite (p.tempC) ? juce::String (p.tempC, 1) : juce::String ("-")) << ' ' << (std::isfinite (p.rh) ? juce::String (p.rh, 0) : juce::String ("-"));
            text << '\n';
        }
        e->addTextElement (text);
        for (const auto& p : parts)
        {
            auto* c = e->createNewChildElement ("Part");
            c->setAttribute ("t", (double) (juce::int64) p.t);
            c->setAttribute ("name", p.name);
        }
        for (const auto& gg : glitches)
        {
            auto* c = e->createNewChildElement ("Audio");
            c->setAttribute ("t", (double) (juce::int64) gg.t);
            c->setAttribute ("what", gg.what);
        }
        for (const auto& h : hits)
        {
            auto* c = e->createNewChildElement ("Feedback");
            c->setAttribute ("t", (double) (juce::int64) h.t);
            c->setAttribute ("freq", h.freq);
            c->setAttribute ("cut", h.cutDb);
        }
    }

    void load (const juce::XmlElement* parent)
    {
        clear();
        // a new session starts with this computer's defaults
        const auto& ap = AppPrefs::get();
        limitLaeq = ap.value ("eventLimit");
        limitMinutes = (int) ap.value ("eventMinutes");
        limitC = ap.value ("eventLimitC") > 0.5;
        interval = ap.value ("logSec");
        const auto* e = parent != nullptr ? parent->getChildByName ("SplLog") : nullptr;
        if (e == nullptr) return;
        limitLaeq = e->getDoubleAttribute ("limitLaeq");
        limitMinutes = juce::jlimit (1, 120, e->getIntAttribute ("limitMinutes", 15));
        limitC = e->getBoolAttribute ("limitC", false);
        interval = juce::jlimit (1.0, 60.0, e->getDoubleAttribute ("interval", 5.0));
        juce::String text;
        for (const auto* c = e->getFirstChildElement(); c != nullptr; c = c->getNextElement())
            if (c->isTextElement()) text << c->getText();
        juce::StringArray lines;
        lines.addLines (text);
        for (const auto& l : lines)
        {
            juce::StringArray v;
            v.addTokens (l, " ", {});
            v.removeEmptyStrings();
            if (v.size() < 4) continue;
            Point p { v[0].getDoubleValue(), v[1].getFloatValue(), v[2].getFloatValue(), v[3].getFloatValue() };
            if (v.size() >= 6)
            {
                if (v[4] != "-") p.tempC = v[4].getFloatValue();
                if (v[5] != "-") p.rh = v[5].getFloatValue();
            }
            points.push_back (p);
        }
        for (const auto* c : e->getChildWithTagNameIterator ("Part"))
            parts.push_back ({ c->getDoubleAttribute ("t"), c->getStringAttribute ("name") });
        for (const auto* c : e->getChildWithTagNameIterator ("Audio"))
            glitches.push_back ({ c->getDoubleAttribute ("t"), c->getStringAttribute ("what") });
        for (const auto* c : e->getChildWithTagNameIterator ("Feedback"))
            hits.push_back ({ c->getDoubleAttribute ("t"), (float) c->getDoubleAttribute ("freq"), (float) c->getDoubleAttribute ("cut") });
    }

private:
    int pending = 0, nt = 0, nh = 0;
    double pendingStart = 0.0, ea = 0.0, ec = 0.0, st = 0.0, sh = 0.0;
    float mx = -200.0f;
};
} // namespace la
