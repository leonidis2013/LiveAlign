// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Crossover design for any pair of speakers (sub / top, low-mid / high ...), on any speaker processor:
// analog-prototype responses of the usual crossover filters, measured responses brought to one timing reference,
// and a search for the frequency, filter, delay, polarity and level that sum flattest. Plain C++, unit tested.
#include <algorithm>
#include <cmath>
#include <complex>
#include <string>
#include <vector>
#include "Fft.h"
#include "FreqTable.h"
#include "EqFit.h"

namespace la::xo
{
enum FilterType { None = 0, Butterworth = 1, LinkwitzRiley = 2, Bessel = 3 };

inline const char* typeName (int t)
{
    static const char* n[] = { "Off", "Butterworth", "Linkwitz-Riley", "Bessel" };
    return n[std::clamp (t, 0, 3)];
}
inline const char* typeShort (int t)
{
    static const char* n[] = { "Off", "BW", "LR", "Bessel" };
    return n[std::clamp (t, 0, 3)];
}

// Slopes each type can have, in dB/octave.
inline bool slopeValid (int type, int slope)
{
    if (slope % 6 != 0 || slope < 6 || slope > 48) return false;
    if (type == LinkwitzRiley) return slope % 12 == 0;
    if (type == Bessel) return slope <= 24;
    return type == Butterworth;
}

struct Filter
{
    int type = None;
    int slope = 24;      // dB/octave
    double freq = 100.0;
    bool active() const { return type != None && slopeValid (type, slope); }
};

namespace detail
{
// Analog low-pass prototype at s (normalised so the corner is s = j).
inline cplx butterworthLp (int order, cplx s)
{
    cplx h (1.0, 0.0);
    for (int k = 0; k < order; ++k)
    {
        const cplx pk = std::polar (1.0, kPi * (2.0 * k + order + 1.0) / (2.0 * order));
        h *= -pk / (s - pk);
    }
    return h;
}

inline const std::vector<double>& besselCoeffs (int order)   // reverse Bessel polynomial, highest power first
{
    static const std::vector<double> c[] = { { 1, 1 }, { 1, 1 }, { 1, 3, 3 }, { 1, 6, 15, 15 }, { 1, 10, 45, 105, 105 } };
    return c[std::clamp (order, 1, 4)];
}

inline cplx besselPoly (int order, cplx s)
{
    cplx v (0.0, 0.0);
    for (double c : besselCoeffs (order)) v = v * s + c;
    return v;
}

// Frequency scale that puts the Bessel low-pass -3 dB point at the corner.
inline double besselScale (int order)
{
    static double cache[5] {};
    order = std::clamp (order, 1, 4);
    if (cache[order] > 0.0) return cache[order];
    const double target = 1.0 / std::sqrt (2.0);
    double lo = 0.1, hi = 10.0;
    const double b0 = besselCoeffs (order).back();
    for (int i = 0; i < 80; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        const double mag = b0 / std::abs (besselPoly (order, cplx (0.0, mid)));
        if (mag > target) lo = mid; else hi = mid;
    }
    cache[order] = 0.5 * (lo + hi);
    return cache[order];
}

inline cplx besselLp (int order, cplx s)
{
    const double k = besselScale (order);
    return besselCoeffs (order).back() / besselPoly (order, s * k);
}
} // namespace detail

// Response of a high-pass or low-pass filter at frequency f.
inline cplx response (const Filter& flt, bool highPass, double f)
{
    if (! flt.active() || f <= 0.0) return { 1.0, 0.0 };
    const cplx s (0.0, f / flt.freq);
    const cplx sx = highPass ? 1.0 / s : s;   // low-pass to high-pass: s -> 1/s
    const int order = flt.slope / 6;
    switch (flt.type)
    {
        case Butterworth: return detail::butterworthLp (order, sx);
        case LinkwitzRiley: { const cplx h = detail::butterworthLp (order / 2, sx); return h * h; }
        case Bessel: return detail::besselLp (order, sx);
        default: return { 1.0, 0.0 };
    }
}

// A speaker's complex response on a frequency grid.
struct Response
{
    std::vector<double> f;
    std::vector<cplx> h;
    std::vector<double> coh;   // coherence of the measurement at each point, 0..1 (empty: treated as fully reliable)
    bool empty() const { return f.empty(); }
    double cohAt (size_t i) const { return i < coh.size() ? coh[i] : 1.0; }
};

inline std::vector<double> analysisGrid() { return [] { std::vector<double> g; for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 48.0)) g.push_back (f); return g; }(); }

// From a measurement (dB + phase) taken with `delayUsedMs` of compensation, expressed relative to the common
// timing reference `refMs`, on `grid`. Phase is unwrapped before interpolation.
inline Response fromTable (const FreqTable& t, double delayUsedMs, double refMs, const std::vector<double>& grid)
{
    Response r;
    if (t.freq.size() < 2) return r;
    std::vector<double> ph (t.freq.size(), 0.0);
    if (t.hasPhase())
    {
        ph[0] = t.phase[0];
        for (size_t i = 1; i < ph.size(); ++i)
        {
            double d = t.phase[i] - t.phase[i - 1];
            while (d > 180.0) d -= 360.0;
            while (d < -180.0) d += 360.0;
            ph[i] = ph[i - 1] + d;
        }
    }
    r.f = grid;
    r.h.resize (grid.size());
    const bool hasCoh = t.extra.size() == t.freq.size();
    if (hasCoh) r.coh.resize (grid.size(), 0.0);
    const double shift = (delayUsedMs - refMs) * 0.001;
    for (size_t i = 0; i < grid.size(); ++i)
    {
        const double f = grid[i];
        double db, p, c = 0.0;
        if (f <= t.freq.front()) { db = t.db.front(); p = ph.front(); if (hasCoh) c = t.extra.front(); }
        else if (f >= t.freq.back()) { db = t.db.back(); p = ph.back(); if (hasCoh) c = t.extra.back(); }
        else
        {
            const size_t k = (size_t) (std::upper_bound (t.freq.begin(), t.freq.end(), f) - t.freq.begin());
            const double a = (std::log (f) - std::log (t.freq[k - 1])) / (std::log (t.freq[k]) - std::log (t.freq[k - 1]));
            db = t.db[k - 1] + a * (t.db[k] - t.db[k - 1]);
            p = ph[k - 1] + a * (ph[k] - ph[k - 1]);
            if (hasCoh) c = t.extra[k - 1] + a * (t.extra[k] - t.extra[k - 1]);
        }
        // outside the measured range the speaker is treated as silent
        const bool inside = f >= t.freq.front() * 0.999 && f <= t.freq.back() * 1.001;
        const double mag = inside ? std::pow (10.0, db / 20.0) : 1e-6;
        r.h[i] = std::polar (mag, (p - 360.0 * f * shift) * kPi / 180.0);
        if (hasCoh) r.coh[i] = inside ? std::clamp (c, 0.0, 1.0) : 0.0;
    }
    return r;
}

// Minimum phase for a magnitude-only table (a spec-sheet or manufacturer FRD without phase), by the folded
// cepstrum. Close to a speaker's own phase in its passband and roll-offs; it carries no arrival time.
inline void addMinimumPhase (FreqTable& t, double rate = 96000.0, int n = 1 << 17)
{
    if (t.freq.size() < 2 || t.hasPhase()) return;
    Fft fft (n);
    std::vector<cplx> d ((size_t) n);
    for (int k = 0; k <= n / 2; ++k)
    {
        const double f = std::max (k * rate / n, 1e-3);
        d[(size_t) k] = std::log (std::pow (10.0, t.dbAt (f) / 20.0));
    }
    for (int k = n / 2 + 1; k < n; ++k) d[(size_t) k] = d[(size_t) (n - k)];
    fft.inverse (d.data());                       // real cepstrum
    for (int i = 1; i < n / 2; ++i) d[(size_t) i] *= 2.0;
    for (int i = n / 2 + 1; i < n; ++i) d[(size_t) i] = 0.0;
    for (auto& v : d) v = cplx (v.real(), 0.0);
    fft.forward (d.data());
    std::vector<double> ph ((size_t) n / 2 + 1);
    for (int k = 0; k <= n / 2; ++k) ph[(size_t) k] = d[(size_t) k].imag() * 180.0 / kPi;
    t.phase.clear();
    for (double f : t.freq)
    {
        const double x = std::clamp (f * n / rate, 0.0, n / 2.0 - 1.0);
        const size_t k = (size_t) x;
        double p = ph[k] + (x - (double) k) * (ph[k + 1] - ph[k]);
        p = std::remainder (p, 360.0);
        t.phase.push_back (p);
    }
}

// How much of a measured speaker can be trusted: the share of the points in its useful range (within 15 dB of its
// loudest, half-octave smoothed) measured with at least `minCoh` coherence. -1 when the table has no coherence.
inline double usableReliability (const FreqTable& t, double minCoh)
{
    if (t.extra.size() != t.freq.size() || t.freq.size() < 4) return -1.0;
    double top = -1e9;
    std::vector<double> sm (t.freq.size());
    for (size_t i = 0; i < t.freq.size(); ++i)
    {
        double sum = 0.0;
        int n = 0;
        for (size_t k = 0; k < t.freq.size(); ++k)
            if (std::abs (std::log2 (t.freq[k] / t.freq[i])) <= 0.25) { sum += std::pow (10.0, t.db[k] / 10.0); ++n; }
        sm[i] = 10.0 * std::log10 (std::max (sum / std::max (n, 1), 1e-18));
        top = std::max (top, sm[i]);
    }
    int used = 0, good = 0;
    for (size_t i = 0; i < t.freq.size(); ++i)
    {
        if (sm[i] < top - 15.0 || t.freq[i] < 20.0 || t.freq[i] > 18000.0) continue;
        ++used;
        if (t.extra[i] >= minCoh) ++good;
    }
    return used > 0 ? (double) good / used : -1.0;
}

// Applies a source's processing: high-pass, low-pass, gain, delay, polarity.
struct Processing
{
    Filter hp, lp;
    double gainDb = 0.0, delayMs = 0.0;
    bool invert = false;
    std::vector<eq::Band> eq;   // parametric EQ from room tuning

    cplx at (double f) const
    {
        cplx g = response (hp, true, f) * response (lp, false, f) * std::pow (10.0, gainDb / 20.0);
        g *= std::polar (1.0, -2.0 * kPi * f * delayMs * 0.001);
        for (const auto& b : eq) g *= eq::response (b, f);
        return invert ? -g : g;
    }
};

inline Response processed (const Response& r, const Processing& p)
{
    Response out = r;
    for (size_t i = 0; i < r.f.size(); ++i) out.h[i] = r.h[i] * p.at (r.f[i]);
    return out;
}

inline double median (std::vector<double> v)
{
    if (v.empty()) return 0.0;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

inline double medianDb (const Response& r, double fLo, double fHi)
{
    std::vector<double> v;
    for (size_t i = 0; i < r.f.size(); ++i)
        if (r.f[i] >= fLo && r.f[i] <= fHi) v.push_back (20.0 * std::log10 (std::max (std::abs (r.h[i]), 1e-9)));
    return median (v);
}

struct Capabilities
{
    bool types[4] { false, true, true, false };          // Butterworth and Linkwitz-Riley by default
    bool slopes[8] { false, true, true, true, false, true, false, true };   // 6..48 in steps of 6: 12, 18, 24, 36, 48
};

struct Options
{
    double fMin = 40.0, fMax = 250.0;     // search range for the crossover frequency
    double delayMinMs = -15.0, delayMaxMs = 15.0;
    bool matchLevels = true;
    double minCoherence = 0.0;            // points measured with less coherence than this are not trusted
};

// Passband level of the high source of a pair, as the optimiser uses it.
inline double passbandDb (const Response& high, const Options& opt) { return medianDb (high, opt.fMax * 1.5, opt.fMax * 6.0); }

struct Result
{
    bool valid = false;
    Filter filter;                // same type and slope on both sides, at the crossover frequency
    double delayLowMs = 0.0;      // delay of the low source relative to the high one (negative: delay the high one)
    bool invertLow = false;
    double gainLowDb = 0.0;
    double costDb = 0.0;          // weighted mean deviation of the sum from the ideal, in dB (lower is better)
    double efficiency = 0.0;      // |a + b| / (|a| + |b|) through the crossover region, 0..1
    double highUsableFrom = 0.0;  // where the high source falls 6 dB below its passband
    double lowUsableTo = 0.0;     // where the low source falls 6 dB below its passband
    double reliability = 1.0;     // share of the crossover region measured with enough coherence, 0..1
    std::string note;
};

namespace detail
{
struct Band { std::vector<size_t> idx; std::vector<double> w; };

inline Band bandAround (const std::vector<double>& f, double fc)
{
    Band b;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (f[i] < fc / 2.0 || f[i] > fc * 2.0) continue;
        b.idx.push_back (i);
        b.w.push_back (1.0);
    }
    return b;
}

// The points of `band` that can be trusted: wherever a source contributes (within 10 dB of the louder one after
// filtering), its measurement must have at least `minCoh` coherence. Returns the share of points kept.
inline double reliableBand (Band& band, const Response& high, const Response& low, const std::vector<cplx>& a,
                            const std::vector<cplx>& b, double minCoh)
{
    if (minCoh <= 0.0 || band.idx.empty()) return 1.0;
    std::vector<size_t> kept;
    for (size_t i : band.idx)
    {
        const double ma = std::abs (a[i]), mb = std::abs (b[i]), top = std::max (ma, mb);
        const bool needA = ma >= top * 0.316, needB = mb >= top * 0.316;
        if ((needA && high.cohAt (i) < minCoh) || (needB && low.cohAt (i) < minCoh)) continue;
        kept.push_back (i);
    }
    const double share = (double) kept.size() / (double) band.idx.size();
    band.idx = std::move (kept);
    return share;
}

// Smoothed level in dB on the response's grid (moving average over `octaves`), so a narrow room notch does
// not decide where a speaker is usable.
inline std::vector<double> smoothedDb (const Response& r, double octaves)
{
    std::vector<double> db (r.f.size()), out (r.f.size());
    for (size_t i = 0; i < r.f.size(); ++i) db[i] = 20.0 * std::log10 (std::max (std::abs (r.h[i]), 1e-9));
    for (size_t i = 0; i < r.f.size(); ++i)
    {
        double sum = 0.0;
        int n = 0;
        const double lo = r.f[i] * std::pow (2.0, -octaves / 2.0), hi = r.f[i] * std::pow (2.0, octaves / 2.0);
        for (size_t k = i; k-- > 0 && r.f[k] >= lo;) { sum += std::pow (10.0, db[k] / 10.0); ++n; }
        for (size_t k = i; k < r.f.size() && r.f[k] <= hi; ++k) { sum += std::pow (10.0, db[k] / 10.0); ++n; }
        out[i] = 10.0 * std::log10 (std::max (sum / std::max (n, 1), 1e-18));
    }
    return out;
}

// Cost: mean |deviation| (dB) of the summed response from what the pair should give there (`ref`: the louder of
// the two unfiltered, level-matched sources, so room effects both share do not count), over an octave each side
// of the crossover, plus a penalty when the sources meet out of phase (a flat sum from sources in quadrature
// wastes headroom and changes around the room). Also returns the summation efficiency |a + b| / (|a| + |b|),
// weighted toward the overlap.
inline double evaluate (const std::vector<double>& f, const std::vector<cplx>& a, const std::vector<cplx>& b,
                        const std::vector<double>& ref, const Band& band, double tauMs, double pol, double& efficiency)
{
    double cost = 0.0, wsum = 0.0, eff = 0.0, esum = 0.0;
    for (size_t j = 0; j < band.idx.size(); ++j)
    {
        const size_t i = band.idx[j];
        if (ref[i] < 1e-9) continue;
        const cplx bb = b[i] * std::polar (pol, -2.0 * kPi * f[i] * tauMs * 0.001);
        const double ma = std::abs (a[i]), mb = std::abs (bb);
        const double hi = std::max (ma, mb), lo = std::min (ma, mb);
        const double sum = std::abs (a[i] + bb);
        cost += std::min (std::abs (20.0 * std::log10 (std::max (sum, 1e-9) / ref[i])), 30.0);
        wsum += 1.0;
        if (hi > 1e-9)
        {
            const double overlap = lo / hi;
            eff += overlap * sum / (ma + mb);
            esum += overlap;
        }
    }
    efficiency = esum > 0.0 ? eff / esum : 0.0;
    return wsum > 0.0 ? cost / wsum + 1.5 * (1.0 - efficiency) : 1e9;
}
} // namespace detail

// Finds the crossover between `high` (e.g. the top) and `low` (e.g. the sub), both on the same grid and timing.
inline Result optimise (const Response& high, const Response& low, const Capabilities& caps, const Options& opt)
{
    Result best;
    if (high.empty() || low.empty() || high.f.size() != low.f.size()) { best.note = "Both speakers need a response."; return best; }
    const auto& f = high.f;

    // where each source is usable: within 6 dB of its own passband (half-octave smoothed)
    const double highRef = passbandDb (high, opt);
    const auto hs = detail::smoothedDb (high, 0.5), ls = detail::smoothedDb (low, 0.5);
    // the low source's passband: the loudest of its smoothed response through its own range (from the bottom of the
    // search up to 120 Hz for a sub). Not a median over a fixed octave: a sub that is already rolling off at 40 Hz
    // (a protective high-pass, or a small one) would read too quiet and be turned up too much.
    // The loudest of the octave-smoothed response: a single narrow bump (a port or room resonance) does not set it.
    double lowRef = -200.0;
    {
        const auto ls1 = detail::smoothedDb (low, 1.0);
        for (size_t i = 0; i < f.size(); ++i)
            if (f[i] >= std::max (25.0, opt.fMin) && f[i] <= std::max (120.0, opt.fMin * 3.0)) lowRef = std::max (lowRef, ls1[i]);
    }
    double highFrom = f.back(), lowTo = f.front();
    for (size_t i = f.size(); i-- > 0;)
    {
        if (f[i] > opt.fMax * 1.5) continue;
        if (hs[i] < highRef - 6.0) break;
        highFrom = f[i];
    }
    // the low source is usable from where it first comes within 6 dB of its passband to where it then falls 6 dB
    // below it (scanning up: a sub that starts already rolled off is not "done" at the first frequency)
    double lowFrom = 0.0;
    bool inBand = false;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (f[i] < opt.fMin / 1.6) continue;
        if (! inBand) { if (ls[i] >= lowRef - 6.0) { inBand = true; lowFrom = f[i]; lowTo = f[i]; } continue; }
        if (ls[i] < lowRef - 6.0) break;
        lowTo = f[i];
    }
    // where the sum should be flat to: from the low source's lower -3 dB point up (below it the system rolls off
    // with the sub, which is no fault of the crossover)
    double lowFlatFrom = lowFrom;
    for (size_t i = 0; i < f.size(); ++i) if (f[i] >= lowFrom && ls[i] >= lowRef - 3.0) { lowFlatFrom = f[i]; break; }
    best.highUsableFrom = highFrom;
    best.lowUsableTo = lowTo;

    // where each plays within 3 dB of full level: the crossover goes between these (crossing where one of them is
    // already well down its own roll-off leans on a speaker past its comfort; practice keeps both near full level)
    double highFrom3 = highFrom, lowTo3 = lowTo;
    for (size_t i = f.size(); i-- > 0;)
    {
        if (f[i] > opt.fMax * 1.5) continue;
        if (hs[i] < highRef - 3.0) break;
        highFrom3 = f[i];
    }
    {
        bool in3 = false;
        for (size_t i = 0; i < f.size(); ++i)
        {
            if (f[i] < opt.fMin / 1.6) continue;
            if (! in3) { if (ls[i] >= lowRef - 3.0) { in3 = true; lowTo3 = f[i]; } continue; }
            if (ls[i] < lowRef - 3.0) break;
            lowTo3 = f[i];
        }
    }
    // search the overlap inside the range; with a gap, around the gap
    double fLo, fHi;
    std::string note;
    if (highFrom3 <= lowTo3)
    {
        // both at full level here; keep clear of the top of the sub's range (where it is already turning down, and its
        // own roll-off adds phase), the usual practice: no higher than a third of an octave under its -3 dB point
        fLo = highFrom3;
        fHi = std::max (fLo * 1.12, lowTo3 / 1.26);
    }
    else if (highFrom <= lowTo) { fLo = lowTo3; fHi = highFrom3; }   // near full level they do not meet: between the two -3 dB points
    else
    {
        // a gap: cross inside it (a quarter of an octave either way at most); outside, one speaker is far down
        fLo = lowTo / 1.2;
        fHi = highFrom * 1.2;
        note = "The two speakers barely overlap (a gap from " + std::to_string ((int) std::lround (lowTo)) + " to " + std::to_string ((int) std::lround (highFrom))
               + " Hz): expect a dip around the crossover.";
    }
    fLo = std::max (fLo, opt.fMin);
    fHi = std::min (fHi, opt.fMax);
    if (fLo > fHi) { fLo = opt.fMin; fHi = opt.fMax; note = "The speakers' overlap is outside the search range."; }
    const double fMid = std::sqrt (fLo * fHi);
    const double gainDb = opt.matchLevels ? std::clamp (highRef - lowRef, -15.0, 15.0) : 0.0;
    if (opt.matchLevels && std::abs (highRef - lowRef) > 15.0)
        note = std::string (note.empty() ? "" : note + " ") + "The two were measured " + std::to_string ((int) std::lround (std::abs (highRef - lowRef)))
               + " dB apart, more than the 15 dB the level match covers: bring the " + (highRef > lowRef ? "low" : "high")
               + " speaker's amp up (or the other down) and measure again.";
    const double g = std::pow (10.0, gainDb / 20.0);
    // the target the sum is judged against: flat at the high source's level from the low source's -3 dB point up;
    // below it, the louder of the two (the system's own low-end roll-off)
    const double flat = std::pow (10.0, highRef / 20.0);
    std::vector<double> ref (f.size());
    for (size_t i = 0; i < f.size(); ++i) ref[i] = f[i] >= lowFlatFrom ? flat : std::max (std::abs (high.h[i]), std::abs (low.h[i]) * g);

    double bestCost = 1e9;
    for (int type = 1; type <= 3; ++type)
    {
        if (! caps.types[type]) continue;
        for (int si = 0; si < 8; ++si)
        {
            const int slope = (si + 1) * 6;
            if (! caps.slopes[si] || ! slopeValid (type, slope)) continue;
            for (double fc = fLo; fc <= fHi * 1.0001; fc *= std::pow (2.0, 1.0 / 24.0))
            {
                const Filter flt { type, slope, fc };
                const auto band = detail::bandAround (f, fc);
                if (band.idx.size() < 6) continue;

                std::vector<cplx> a (f.size()), b (f.size());
                for (size_t j : band.idx)
                {
                    a[j] = high.h[j] * response (flt, true, f[j]);
                    b[j] = low.h[j] * response (flt, false, f[j]) * g;
                }
                auto trusted = band;
                const double share = detail::reliableBand (trusted, high, low, a, b, opt.minCoherence);
                if (trusted.idx.size() < 6) continue;

                for (double pol : { 1.0, -1.0 })
                {
                    double localBest = 1e9, localTau = 0.0, eff = 0.0;
                    for (double tau = opt.delayMinMs; tau <= opt.delayMaxMs + 1e-9; tau += 0.25)
                    {
                        const double c = detail::evaluate (f, a, b, ref, trusted, tau, pol, eff);
                        if (c < localBest) { localBest = c; localTau = tau; }
                    }
                    for (double tau = localTau - 0.25; tau <= localTau + 0.25 + 1e-9; tau += 0.02)
                    {
                        const double c = detail::evaluate (f, a, b, ref, trusted, tau, pol, eff);
                        if (c < localBest) { localBest = c; localTau = tau; }
                    }
                    // gentle preference for the middle of the usable overlap, and for less delay
                    // prefer the middle of the overlap, less delay, and a region that was measured reliably
                    const double cost = localBest + 0.25 * std::abs (std::log2 (fc / fMid)) + 0.01 * std::abs (localTau) + 3.0 * (1.0 - share);
                    if (cost < bestCost)
                    {
                        bestCost = cost;
                        best.valid = true;
                        best.filter = flt;
                        best.delayLowMs = localTau;
                        best.invertLow = pol < 0.0;
                        best.gainLowDb = gainDb;
                        best.costDb = localBest;
                        best.reliability = share;
                        detail::evaluate (f, a, b, ref, trusted, localTau, pol, best.efficiency);
                    }
                }
            }
        }
    }
    if (! best.valid && note.empty())
        note = opt.minCoherence > 0.0 ? "No filter type and slope is enabled, or the crossover region was not measured reliably."
                                      : "No filter type and slope is enabled for your processor.";
    if (best.valid && best.reliability < 0.7)
        note += std::string (note.empty() ? "" : " ") + "Only " + std::to_string ((int) std::lround (best.reliability * 100.0))
              + "% of the crossover region was measured reliably: measure again with more level or less noise.";
    best.note = note;
    return best;
}

// Cost and efficiency of a given (manual) setting, for the live readout.
// Where a low source (a sub) is within 3 dB of its loudest, scanning up from `fMin` (its lower -3 dB point).
inline double lowerFlatFrom (const Response& low, double fMin = 25.0)
{
    const auto ls = detail::smoothedDb (low, 0.5);
    double top = -200.0;
    for (size_t i = 0; i < low.f.size(); ++i) if (low.f[i] >= fMin && low.f[i] <= 120.0) top = std::max (top, ls[i]);
    for (size_t i = 0; i < low.f.size(); ++i) if (low.f[i] >= fMin && ls[i] >= top - 3.0) return low.f[i];
    return fMin;
}

inline double assess (const Response& high, const Response& low, const Processing& ph, const Processing& pl, double fc, double& efficiency,
                      double minCoherence = 0.0, double* reliability = nullptr, double* peakFromFlatDb = nullptr)
{
    efficiency = 0.0;
    if (high.empty() || low.empty() || high.f.size() != low.f.size()) return 0.0;
    auto band = detail::bandAround (high.f, fc);
    std::vector<cplx> a (high.f.size()), b (high.f.size());
    std::vector<double> ref (high.f.size(), 0.0);
    const double gh = std::pow (10.0, ph.gainDb / 20.0), gl = std::pow (10.0, pl.gainDb / 20.0);
    for (size_t j : band.idx)
    {
        a[j] = high.h[j] * ph.at (high.f[j]);
        b[j] = low.h[j] * pl.at (high.f[j]);
        ref[j] = std::max (std::abs (high.h[j]) * gh, std::abs (low.h[j]) * gl);
    }
    const double share = detail::reliableBand (band, high, low, a, b, minCoherence);
    if (reliability != nullptr) *reliability = share;
    if (band.idx.size() < 3) return 0.0;
    if (peakFromFlatDb != nullptr)
    {
        // the sum against flat (the high source's own level), from the low source's -3 dB point up: what you hear
        const double flatDb = medianDb (high, 375.0, 1500.0) + ph.gainDb;
        const double from = lowerFlatFrom (low);
        double worst = 0.0;
        for (size_t j : band.idx)
            if (high.f[j] >= from) worst = std::max (worst, std::abs (20.0 * std::log10 (std::max (std::abs (a[j] + b[j]), 1e-9)) - flatDb));
        *peakFromFlatDb = worst;
    }
    return detail::evaluate (high.f, a, b, ref, band, 0.0, 1.0, efficiency);
}
} // namespace la::xo
