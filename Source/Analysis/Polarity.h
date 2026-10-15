// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Polarity of a speaker from its measurement: does a positive signal push the air out (normal) or pull it in
// (inverted)? Measured close up and timed to its own arrival, a speaker's phase through the middle of its range sits
// near 0 degrees when it is wired the right way and near 180 when it is not. Two speakers of the same model can also
// be compared directly: their phase matches, or is 180 degrees apart.
//
// Two independent signs are used: the excess phase through the woofer's loudest octaves (the measured phase less the
// minimum phase its own roll-offs give, and less a small timing error), and the sign of the impulse peak from the
// timing search. "Inverted" needs both; one sign alone, or a spread-out phase, is "check". Comparing two speakers
// of the same model is the firmest test. Plain C++, no JUCE.
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>
#include "Crossover.h"
#include "FreqTable.h"

namespace la::polarity
{
enum Verdict { Unknown = 0, Normal = 1, Inverted = 2, Unsure = 3, Skipped = 4 };

inline const char* verdictName (int v)
{
    switch (v)
    {
        case Normal: return "normal";
        case Inverted: return "inverted";
        case Unsure: return "check";
        case Skipped: return "skipped";
        default: return "not checked";
    }
}

struct BandPhase
{
    bool valid = false;
    double phaseDeg = 0.0;   // circular mean through the band, -180..180
    double spread = 1.0;     // 0 = every point agrees, 1 = no agreement
    double lo = 0.0, hi = 0.0;
    double residualMs = 0.0; // the small timing error taken out
};

// The loudest part of the response: points within `windowDb` of the smoothed peak, reliable (coherence in `extra`
// at least `minCoh` when given). Weighted per octave so a wide top counts its mids and highs alike.
inline std::vector<size_t> passband (const FreqTable& t, double minCoh, double windowDb = 6.0)
{
    std::vector<size_t> out;
    const size_t n = t.freq.size();
    if (n < 8 || ! t.hasPhase()) return out;
    // 1/3-octave smoothed level, to find the peak without one sharp resonance setting it
    std::vector<double> sm (n, -300.0);
    for (size_t i = 0; i < n; ++i)
    {
        double sum = 0.0;
        int k = 0;
        for (size_t j = 0; j < n; ++j)
            if (t.freq[j] > t.freq[i] * 0.891 && t.freq[j] < t.freq[i] * 1.122) { sum += t.db[j]; ++k; }
        if (k > 0) sm[i] = sum / k;
    }
    double peak = -300.0;
    for (size_t i = 0; i < n; ++i)
        if (t.freq[i] >= 20.0 && t.freq[i] <= 16000.0 && (t.extra.size() != n || t.extra[i] >= minCoh)) peak = std::max (peak, sm[i]);
    for (size_t i = 0; i < n; ++i)
    {
        if (t.freq[i] < 20.0 || t.freq[i] > 16000.0) continue;
        if (t.extra.size() == n && t.extra[i] < minCoh) continue;
        if (sm[i] < peak - windowDb) continue;
        if (! std::isfinite (t.phase[i])) continue;
        out.push_back (i);
    }
    return out;
}

// Circular mean of `phaseDeg[k] + 360 f tau` over the points (on a log grid: equal weight per octave).
inline std::complex<double> phasor (const std::vector<double>& f, const std::vector<double>& ph, const std::vector<double>& w, double tauMs)
{
    std::complex<double> s (0.0, 0.0);
    for (size_t k = 0; k < f.size(); ++k)
    {
        const double a = (ph[k] + 360.0 * f[k] * tauMs * 0.001) * 3.14159265358979323846 / 180.0;
        s += w[k] * std::complex<double> (std::cos (a), std::sin (a));
    }
    return s;
}

// The phase through the band, after the timing error (within +/- maxTauMs) that makes the band's phase most
// consistent. The window is kept small so a delay cannot pass for an inversion: at 100 Hz, 0.25 ms is 9 degrees.
inline BandPhase bandPhase (const std::vector<double>& f, const std::vector<double>& ph, double maxTauMs)
{
    BandPhase r;
    if (f.size() < 4) return r;
    std::vector<double> w (f.size());
    double wsum = 0.0;
    for (size_t k = 0; k < f.size(); ++k) { w[k] = 1.0; wsum += w[k]; }
    double bestTau = 0.0, bestMag = -1.0;
    const int steps = std::max (200, (int) std::ceil (maxTauMs * 100.0));
    for (int s = -steps; s <= steps; ++s)
    {
        const double tau = maxTauMs * (double) s / steps;
        const double m = std::abs (phasor (f, ph, w, tau));
        if (m > bestMag + 1e-12) { bestMag = m; bestTau = tau; }
    }
    const auto z = phasor (f, ph, w, bestTau);
    r.valid = true;
    r.phaseDeg = std::arg (z) * 180.0 / 3.14159265358979323846;
    r.spread = 1.0 - std::abs (z) / std::max (1e-12, wsum);
    r.residualMs = bestTau;
    r.lo = f.front();
    r.hi = f.back();
    return r;
}

// From the phase alone: within 60 degrees of 0 normal, within 60 of 180 inverted, otherwise not clear.
inline int fromPhase (const BandPhase& b, double maxSpread = 0.6)
{
    if (! b.valid || b.spread > maxSpread) return Unsure;
    const double a = std::abs (b.phaseDeg);
    if (a <= 60.0) return Normal;
    if (a >= 120.0) return Inverted;
    return Unsure;
}

// Carries the roll-off at each end on beyond what was measured (at the slope of its last half octave), so the
// minimum phase is not bent by a magnitude that suddenly stops falling.
inline void extendRollOffs (FreqTable& m)
{
    const size_t n = m.freq.size();
    if (n < 8) return;
    auto slopeOver = [&m] (double f0, double f1)
    {
        const double a = m.dbAt (f0), b = m.dbAt (f1);
        return (b - a) / std::log2 (f1 / f0);   // dB per octave
    };
    const double lo = m.freq.front(), hi = m.freq.back();
    const double sLo = std::clamp (slopeOver (lo, lo * 1.414), 0.0, 48.0);     // rising toward the passband
    const double sHi = std::clamp (slopeOver (hi / 1.414, hi), -48.0, 0.0);
    FreqTable out;
    for (int k = 6; k >= 1; --k)
    {
        out.freq.push_back (lo / std::pow (2.0, k));
        out.db.push_back (m.db.front() - sLo * k);
    }
    out.freq.insert (out.freq.end(), m.freq.begin(), m.freq.end());
    out.db.insert (out.db.end(), m.db.begin(), m.db.end());
    for (int k = 1; k <= 3; ++k)
    {
        out.freq.push_back (hi * std::pow (2.0, k));
        out.db.push_back (m.db.back() + sHi * k);
    }
    m = std::move (out);
}

struct Result
{
    int verdict = Unknown;
    BandPhase band;
    bool peakInverted = false;
    bool peakKnown = false;
    bool phaseSaysInverted = false;   // the phase alone pointed to inverted (shown as "check")
};

// Absolute polarity of a measurement timed to the speaker's own arrival. `peakKnown` / `peakInverted` come from
// the timing search; `maxTauMs` is how far the timing may be off (larger when measured from one spot).
inline Result absolute (const FreqTable& t, double minCoh, bool peakKnown, bool peakInverted, double maxTauMs = 1.0)
{
    Result r;
    r.peakKnown = peakKnown;
    r.peakInverted = peakInverted;
    auto idx = passband (t, minCoh, 10.0);
    if (idx.size() < 4) { r.verdict = peakKnown ? Unsure : Unknown; return r; }
    // Only the woofer's own range: a two-way box has its own crossover (woofer to horn, often near 1.5 - 3 kHz) whose
    // all-pass phase turn is not minimum phase, and read across it the phase can average near 180 degrees on a box
    // wired correctly. A speaker that starts low is read up to 1.2 kHz; one that starts high (a mid / high box) over
    // its lowest 1.5 octaves.
    {
        const double fLo = t.freq[idx.front()];
        const double cap = fLo < 600.0 ? 1200.0 : fLo * 2.8;
        std::vector<size_t> kept;
        for (auto i : idx) if (t.freq[i] <= cap) kept.push_back (i);
        if (kept.size() >= 4) idx = std::move (kept);
    }
    // the minimum phase of its reliable magnitude: what its roll-offs alone do to the phase
    FreqTable mag;
    for (size_t i = 0; i < t.freq.size(); ++i)
        if (t.extra.size() != t.freq.size() || t.extra[i] >= minCoh) { mag.freq.push_back (t.freq[i]); mag.db.push_back (t.db[i]); }
    if (mag.freq.size() < 8) { r.verdict = peakKnown ? Unsure : Unknown; return r; }
    extendRollOffs (mag);
    xo::addMinimumPhase (mag, 48000.0, 1 << 16);
    std::vector<double> f, ph;
    for (auto i : idx)
    {
        double d = t.phase[i] - mag.phaseAt (t.freq[i]);
        d = std::remainder (d, 360.0);
        f.push_back (t.freq[i]);
        ph.push_back (d);
    }
    // A sub's timing from the search can be several ms late (its impulse is long). Over a band of 1.5 octaves or more a
    // delay cannot pass for a constant 180 degrees, so the timing is allowed to move further there.
    if (f.back() >= 2.8 * f.front()) maxTauMs = std::max (maxTauMs, 8.0);
    r.band = bandPhase (f, ph, maxTauMs);
    const int v = fromPhase (r.band);
    // The impulse peak only means something for a wide-band speaker: a sub's impulse rings, and its biggest swing can
    // go either way. Below about three octaves of range, the phase decides on its own.
    if (f.back() < 8.0 * f.front()) peakKnown = false;
    r.peakKnown = peakKnown;
    // "Inverted" needs two independent signs that agree (the phase and the impulse peak). The phase alone can say
    // "normal", but an inversion from one sign only is "check": the room, a late arrival or the box's own crossover
    // can turn the phase without anything being wired wrong.
    r.phaseSaysInverted = v == Inverted;
    if (! peakKnown) r.verdict = v == Inverted ? Unsure : v;
    else if (v == Unsure) r.verdict = Unsure;
    else r.verdict = (v == Inverted) == peakInverted ? v : Unsure;   // the two signs must agree
    return r;
}

// One measurement against another of the same model (Main R against Main L, the second sub against the first):
// the phase difference through the band both are loud in. Normal = same polarity as `ref`, Inverted = opposite.
inline Result relative (const FreqTable& t, const FreqTable& ref, double minCoh, double maxTauMs = 0.5)
{
    Result r;
    const auto a = passband (t, minCoh), b = passband (ref, minCoh);
    if (a.size() < 4 || b.size() < 4 || ! ref.hasPhase()) { r.verdict = Unknown; return r; }
    const double lo = std::max (t.freq[a.front()], ref.freq[b.front()]), hi = std::min (t.freq[a.back()], ref.freq[b.back()]);
    if (hi <= lo * 1.5) { r.verdict = Unknown; return r; }
    std::vector<double> f, ph;
    for (auto i : a)
    {
        const double fr = t.freq[i];
        if (fr < lo || fr > hi) continue;
        // the reference's phase at this frequency (nearest point; both are on a fine log grid)
        size_t j = 0;
        double best = 1e9;
        for (size_t k = 0; k < ref.freq.size(); ++k)
        {
            const double d = std::abs (std::log (ref.freq[k] / fr));
            if (d < best) { best = d; j = k; }
        }
        if (best > 0.03 || ! std::isfinite (ref.phase[j])) continue;
        double d = t.phase[i] - ref.phase[j];
        while (d > 180.0) d -= 360.0;
        while (d < -180.0) d += 360.0;
        f.push_back (fr);
        ph.push_back (d);
    }
    r.band = bandPhase (f, ph, maxTauMs);
    r.verdict = fromPhase (r.band);
    return r;
}
} // namespace la::polarity
