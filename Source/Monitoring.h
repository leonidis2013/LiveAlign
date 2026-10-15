// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Monitoring has its own mic, separate from the tuning mics: usually one mic at FOH, left there for the event.
// It has its own input, calibration files, sound level calibration and delay (saved with the venue), and the sound
// level is logged during the event for the monitoring report (saved with the session).
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
    juce::String splDate;
    float delayMs = 0.0f;
    bool configured = false;        // set up on the Monitor mic page (else it is taken from the tuning mics once)

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

// Sound level during the event: one point every few seconds (energy averages), for the monitoring report.
struct SplLog
{
    struct Point { double t; float la, lc, laMax; };   // t: seconds since 1970; LAeq, LCeq over the interval; max LAF
    std::vector<Point> points;
    double limitLaeq = 0.0;        // the limit over `limitMinutes` (0 = none), A or C weighted (`limitC`)
    int limitMinutes = 15;
    bool limitC = false;           // the limit is an LCeq (bass included) rather than an LAeq
    double interval = 5.0;         // seconds per logged point

    juce::String limitName() const { return limitC ? "LCeq" : "LAeq"; }
    juce::String limitUnit() const { return limitC ? "dBC" : "dBA"; }

    // A reading (A and C slow, A fast, dB) taken about once a second.
    void add (double t, float laS, float lcS, float laF)
    {
        if (! std::isfinite (laS) || laS < -150.0f) return;
        if (pending == 0) pendingStart = t;
        ea += std::pow (10.0, laS / 10.0);
        ec += std::pow (10.0, lcS / 10.0);
        mx = std::max (mx, laF);
        ++pending;
        if (t - pendingStart >= interval - 0.5)
        {
            points.push_back ({ pendingStart, (float) (10.0 * std::log10 (ea / pending)), (float) (10.0 * std::log10 (ec / pending)), mx });
            pending = 0; ea = ec = 0.0; mx = -200.0f;
        }
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
    double maxLaf() const { float m = -200.0f; for (const auto& p : points) m = std::max (m, p.laMax); return points.empty() ? NAN : m; }

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

    // Minutes the rolling LAeq was over the limit.
    double minutesOver() const
    {
        if (limitLaeq <= 0.0) return 0.0;
        int n = 0;
        for (double v : rollingLaeq()) n += v > limitLaeq ? 1 : 0;
        return n * interval / 60.0;
    }

    void clear() { points.clear(); pending = 0; ea = ec = 0.0; mx = -200.0f; }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("SplLog");
        e->setAttribute ("limitLaeq", limitLaeq);
        e->setAttribute ("limitMinutes", limitMinutes);
        e->setAttribute ("limitC", limitC);
        e->setAttribute ("interval", interval);
        juce::String text;
        for (const auto& p : points)
            text << juce::String ((juce::int64) p.t) << ' ' << juce::String (p.la, 1) << ' ' << juce::String (p.lc, 1) << ' ' << juce::String (p.laMax, 1) << '\n';
        e->addTextElement (text);
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
        juce::StringArray lines;
        lines.addLines (e->getAllSubText());
        for (const auto& l : lines)
        {
            juce::StringArray v;
            v.addTokens (l, " ", {});
            v.removeEmptyStrings();
            if (v.size() < 4) continue;
            points.push_back ({ v[0].getDoubleValue(), v[1].getFloatValue(), v[2].getFloatValue(), v[3].getFloatValue() });
        }
    }

private:
    int pending = 0;
    double pendingStart = 0.0, ea = 0.0, ec = 0.0;
    float mx = -200.0f;
};
} // namespace la
