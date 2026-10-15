// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Everything the Room acoustics page, its decision tree, Room EQ and the report say about the room, worked out in one
// place from the sweeps (or the Room EQ estimate), the noise check, the room's size on the venue plan and the speakers.
#include <algorithm>
#include <cmath>
#include <vector>
#include "PluginProcessor.h"

namespace la
{
struct AcousticsSummary
{
    acoustics::Result avg;
    bool any = false, fromSweeps = false;
    int points = 0;
    double rtMid = NAN, edtMid = NAN, c50 = NAN, c80 = NAN, tsMid = NAN;
    double bassRatio = NAN, trebleRatio = NAN;
    double rt63 = NAN;                        // the 63 Hz octave, when its decay was clear
    bool rt63Reliable = false;
    double stiRaw = NAN, stiCorrected = NAN;  // without and with the noise, masking and hearing threshold
    bool noiseKnown = false;
    std::array<double, acoustics::kNoiseBands> noise {};   // octave noise, 63 Hz - 8 kHz (dB SPL)
    juce::String noiseMic;
    double nc = NAN, nr = NAN;
    double alcons = NAN;                      // from the corrected STI when there is one
    double itdgMs = NAN;
    bool echo = false;
    double echoMs = NAN, echoDb = NAN, echoExtraM = NAN;   // when, how loud, how much further it travelled
    double flutterMs = NAN, flutterSpacingM = NAN;
    int ringingThird = -1;                    // a 1/3 octave that rings well above its neighbours (index in thirds())
    double ringingRatio = NAN;
    double volume = 0.0, schroederHz = NAN;
    struct Dc { juce::String name; double q, metres; };
    std::vector<Dc> criticalDistance;
    struct SpreadRow { juce::String what, unit; int decimals; acoustics::Spread s; };
    std::vector<SpreadRow> spread;
    double coverage2k = NAN, coverage4k = NAN;   // max - min across the measured points (dB)
    int coveragePoints = 0;
};

// Octave band noise levels of the loudest mic in the noise floor check, when it was calibrated in dB SPL.
inline bool noiseOctavesOf (const LiveAlignProcessor& p, std::array<double, acoustics::kNoiseBands>& out, juce::String& who)
{
    const auto& c = p.checks;
    if (! c.noiseDone) return false;
    int best = -1;
    for (int m = 0; m < c.noiseMics; ++m)
        if (c.noiseIsSpl[(size_t) m] && c.noiseOct[(size_t) m][3] > -150.0f && (best < 0 || c.noiseA[(size_t) m] > c.noiseA[(size_t) best])) best = m;
    if (best < 0) return false;
    for (int b = 0; b < acoustics::kNoiseBands; ++b)
    {
        const float v = c.noiseOct[(size_t) best][(size_t) b];
        out[(size_t) b] = v > -150.0f ? (double) v : NAN;
    }
    who = p.mics.nameOf (best);
    return true;
}

// The level of a measurement in the octave around `fc` (dB, as measured: not normalised).
inline double octaveLevel (const RoomMeasurement& m, double fc)
{
    double s = 0.0;
    int n = 0;
    for (double f = fc / std::sqrt (2.0); f <= fc * std::sqrt (2.0); f *= 1.03)
    {
        if (m.tf.freq.empty() || f < m.tf.freq.front() || f > m.tf.freq.back()) continue;
        const double v = m.tf.dbAt (f);
        if (std::isfinite (v)) { s += std::pow (10.0, v / 10.0); ++n; }
    }
    return n > 0 ? 10.0 * std::log10 (s / n) : NAN;
}

inline AcousticsSummary summariseAcoustics (const LiveAlignProcessor& p)
{
    AcousticsSummary a;
    const auto& room = p.room;
    const auto from = room.acousticsResults();
    a.points = (int) from.size();
    a.any = ! from.empty();
    a.fromSweeps = room.acousticsFromSweeps();
    a.avg = RoomTuning::averageOf (from);
    const auto& r = a.avg;
    const double c = p.settings.speedOfSound();

    a.noiseKnown = noiseOctavesOf (p, a.noise, a.noiseMic);
    if (a.noiseKnown) { a.nc = acoustics::noiseCriterion (a.noise); a.nr = acoustics::noiseRating (a.noise); }
    a.volume = p.plan.volume();

    if (a.any)
    {
        a.rtMid = r.rtMid();
        a.edtMid = r.mid (&acoustics::Band::edt);
        a.c50 = r.mid (&acoustics::Band::c50);
        a.c80 = r.mid (&acoustics::Band::c80);
        a.tsMid = r.mid (&acoustics::Band::ts);
        a.bassRatio = acoustics::bassRatio (r);
        a.trebleRatio = acoustics::trebleRatio (r);
        a.rt63 = r.b63.rt();
        a.rt63Reliable = std::isfinite (a.rt63) && std::isfinite (r.b63.rangeDb) && r.b63.rangeDb >= 30.0;
        a.stiRaw = r.sti;
        std::array<double, acoustics::kBands> n7 {};
        for (int b = 0; b < acoustics::kBands; ++b) n7[(size_t) b] = a.noiseKnown ? a.noise[(size_t) b + 1] : NAN;   // 125 Hz - 8 kHz
        a.stiCorrected = acoustics::stiCorrected (r, p.pref ("speechDbA"), n7);
        a.alcons = acoustics::alcons (std::isfinite (a.stiCorrected) ? a.stiCorrected : a.stiRaw);
        a.itdgMs = r.itdgMs;
        a.echo = r.echoAudible();
        a.echoMs = r.echoMs;
        a.echoDb = r.echoDb;
        if (std::isfinite (a.echoMs)) a.echoExtraM = a.echoMs * 0.001 * c;
        if (std::isfinite (r.flutterMs)) { a.flutterMs = r.flutterMs; a.flutterSpacingM = r.flutterMs * 0.001 * c * 0.5; }
        // a third octave ringing well above its neighbours (the median of the two each side, so one odd neighbour
        // does not make it stand out)
        for (int b = 1; b + 1 < acoustics::kThirds; ++b)
        {
            const double v = r.rt3[(size_t) b];
            if (! std::isfinite (v)) continue;
            std::vector<double> nb;
            for (int k = b - 2; k <= b + 2; ++k)
                if (k != b && k >= 0 && k < acoustics::kThirds && std::isfinite (r.rt3[(size_t) k])) nb.push_back (r.rt3[(size_t) k]);
            if (nb.size() < 2) continue;
            std::sort (nb.begin(), nb.end());
            const double med = nb.size() % 2 == 1 ? nb[nb.size() / 2] : 0.5 * (nb[nb.size() / 2 - 1] + nb[nb.size() / 2]);
            const double ratio = med > 0.0 ? v / med : NAN;
            if (std::isfinite (ratio) && ratio >= 1.35 && (a.ringingThird < 0 || ratio > a.ringingRatio)) { a.ringingThird = b; a.ringingRatio = ratio; }
        }
        if (a.volume > 0.0 && std::isfinite (a.rtMid))
        {
            a.schroederHz = acoustics::schroederHz (a.rtMid, a.volume);
            for (int i = 0; i < (int) p.system.sources.size(); ++i)
            {
                const auto& s = p.system.sources[(size_t) i];
                if (s.role == (int) SpeakerRole::Sub) continue;
                a.criticalDistance.push_back ({ p.system.nameOf (i), s.qFactor(), acoustics::criticalDistanceM (s.qFactor(), a.volume, a.rtMid) });
            }
        }
        // across the points
        auto add = [&] (const juce::String& what, const juce::String& unit, int d, std::function<double (const acoustics::Result&)> f)
        {
            std::vector<double> v;
            for (const auto* x : from) v.push_back (f (*x));
            a.spread.push_back ({ what, unit, d, acoustics::spreadOf (v) });
        };
        add ("RT (500 Hz - 1 kHz)", "s", 2, [] (const acoustics::Result& x) { return x.rtMid(); });
        add ("EDT", "s", 2, [] (const acoustics::Result& x) { return x.mid (&acoustics::Band::edt); });
        add ("C50", "dB", 1, [] (const acoustics::Result& x) { return x.mid (&acoustics::Band::c50); });
        add ("C80", "dB", 1, [] (const acoustics::Result& x) { return x.mid (&acoustics::Band::c80); });
        add ("Ts", "ms", 0, [] (const acoustics::Result& x) { return x.mid (&acoustics::Band::ts); });
        add ("STI", "", 2, [] (const acoustics::Result& x) { return x.sti; });
    }
    // coverage at 2 and 4 kHz: the latest round of Room EQ (the whole system, the same generator level)
    {
        const bool after = room.count (true) > 0;
        std::vector<double> l2, l4;
        for (const auto& m : room.meas)
        {
            if (m.after != after || ! m.include) continue;
            l2.push_back (octaveLevel (m, 2000.0));
            l4.push_back (octaveLevel (m, 4000.0));
        }
        const auto s2 = acoustics::spreadOf (l2), s4 = acoustics::spreadOf (l4);
        a.coveragePoints = s2.n;
        if (s2.n > 1) a.coverage2k = s2.hi - s2.lo;
        if (s4.n > 1) a.coverage4k = s4.hi - s4.lo;
    }
    return a;
}
// Room EQ's allowance for the room acoustics, from the venue's switch and what was measured (see RoomTuning).
inline void updateRoomAllowance (LiveAlignProcessor& p)
{
    p.room.updateSystem (p.system);
    const auto sum = summariseAcoustics (p);
    juce::String why;
    const int verdict = p.room.acousticsVerdict (why);
    double rtLo, rtHi, stiMin;
    RoomTuning::useTargets (p.room.use, rtLo, rtHi, stiMin);
    p.room.updateAllowance (p.pref ("roomAllow") > 0.5, sum.schroederHz, std::isfinite (sum.stiCorrected) ? sum.stiCorrected : sum.stiRaw, stiMin, verdict,
                            p.pref ("eqMaxCut"), p.pref ("eqCutQ"));
}
} // namespace la
