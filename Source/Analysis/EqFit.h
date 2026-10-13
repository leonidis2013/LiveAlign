// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Suggested EQ for room tuning: parametric (peaking) bands that bring a measured average toward a target curve.
// Cuts are preferred; boosts are small and only for wide dips (a narrow dip is usually a reflection or a room mode
// that EQ cannot fill). Plain C++, unit tested.
#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

namespace la::eq
{
struct Band
{
    double freq = 1000.0, gainDb = 0.0, q = 1.0;
    bool active() const { return std::abs (gainDb) > 0.05 && freq > 0.0 && q > 0.0; }
};

// Analog peaking filter (the usual parametric EQ band) at frequency f: complex response, minimum phase.
inline std::complex<double> response (const Band& b, double f)
{
    if (! b.active() || f <= 0.0) return { 1.0, 0.0 };
    const double A = std::pow (10.0, b.gainDb / 40.0);
    const std::complex<double> s (0.0, f / b.freq);
    return (s * s + s * (A / b.q) + 1.0) / (s * s + s / (A * b.q) + 1.0);
}

inline double responseDb (const Band& b, double f) { return 20.0 * std::log10 (std::abs (response (b, f))); }

inline double totalDb (const std::vector<Band>& bands, double f)
{
    double d = 0.0;
    for (const auto& b : bands) d += responseDb (b, f);
    return d;
}

struct Options
{
    double fLo = 60.0, fHi = 12000.0;   // range to correct
    int maxBands = 8;
    double maxCutDb = 12.0, maxBoostDb = 3.0;
    double minQ = 0.4, maxQ = 8.0;
    double toleranceDb = 1.0;           // stop when everything left is within this
    double minBoostWidthOct = 0.33;     // narrower dips are not boosted
    double minGainDb = 1.0;             // smaller bands are not worth a filter
    double narrowQ = 4.0, minNarrowGainDb = 3.0;   // a narrow band has to do real work to be kept
};

struct Fit
{
    std::vector<Band> bands;
    double offsetDb = 0.0;              // level of the measurement against the target (removed before fitting)
    double rmsBefore = 0.0, rmsAfter = 0.0, maxBefore = 0.0, maxAfter = 0.0;   // deviation from the target in range, dB
};

// Moving average over `octaves` on a log-spaced grid (NaN points ignored).
inline std::vector<double> smooth (const std::vector<double>& f, const std::vector<double>& v, double octaves)
{
    std::vector<double> out (v.size(), NAN);
    for (size_t i = 0; i < f.size(); ++i)
    {
        const double lo = f[i] * std::pow (2.0, -octaves / 2.0), hi = f[i] * std::pow (2.0, octaves / 2.0);
        double s = 0.0;
        int n = 0;
        for (size_t k = 0; k < f.size(); ++k)
            if (f[k] >= lo && f[k] <= hi && std::isfinite (v[k])) { s += v[k]; ++n; }
        if (n > 0) out[i] = s / n;
    }
    return out;
}

inline double median (std::vector<double> v)
{
    v.erase (std::remove_if (v.begin(), v.end(), [] (double x) { return ! std::isfinite (x); }), v.end());
    if (v.empty()) return 0.0;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

namespace detail
{
inline double error (const std::vector<double>& f, const std::vector<double>& resid, const Band& b, const Options& o)
{
    double e = 0.0;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (f[i] < o.fLo || f[i] > o.fHi || ! std::isfinite (resid[i])) continue;
        const double r = resid[i] + responseDb (b, f[i]);
        e += r * r;
    }
    return e;
}

inline void stats (const std::vector<double>& f, const std::vector<double>& d, const Options& o, double& rms, double& mx)
{
    double s = 0.0;
    int n = 0;
    mx = 0.0;
    for (size_t i = 0; i < f.size(); ++i)
    {
        if (f[i] < o.fLo || f[i] > o.fHi || ! std::isfinite (d[i])) continue;
        s += d[i] * d[i];
        mx = std::max (mx, std::abs (d[i]));
        ++n;
    }
    rms = n > 0 ? std::sqrt (s / n) : 0.0;
}
} // namespace detail

// `measured` and `target` in dB on the log grid `f` (NaN = no data). The target is matched in level first.
inline Fit fit (const std::vector<double>& f, const std::vector<double>& measured, const std::vector<double>& target, const Options& o)
{
    Fit r;
    if (f.size() < 8 || measured.size() != f.size() || target.size() != f.size()) return r;
    std::vector<double> raw (f.size(), NAN);
    std::vector<double> inRange;
    for (size_t i = 0; i < f.size(); ++i)
        if (std::isfinite (measured[i]) && std::isfinite (target[i]))
        {
            raw[i] = measured[i] - target[i];
            if (f[i] >= o.fLo && f[i] <= o.fHi) inRange.push_back (raw[i]);
        }
    r.offsetDb = median (inRange);
    for (auto& v : raw) if (std::isfinite (v)) v -= r.offsetDb;

    // fit to the broad shape: 1/6 octave
    auto resid = smooth (f, raw, 1.0 / 6.0);
    detail::stats (f, resid, o, r.rmsBefore, r.maxBefore);

    std::vector<char> skip (f.size(), 0);   // regions where a band did not help
    for (int attempt = 0; attempt < o.maxBands * 4 && (int) r.bands.size() < o.maxBands; ++attempt)
    {
        // the worst point: peaks count fully, dips less and only when wide enough to fill
        int best = -1;
        double bestScore = o.toleranceDb;
        for (size_t i = 0; i < f.size(); ++i)
        {
            if (f[i] < o.fLo || f[i] > o.fHi || ! std::isfinite (resid[i]) || skip[i]) continue;
            const double d = resid[i];
            double score = std::abs (d);
            if (d < 0.0)
            {
                if (o.maxBoostDb <= 0.0) continue;
                // width of the whole dip (measured at half the depth of its bottom, not of this point on its side)
                size_t j = i;
                for (;;)
                {
                    if (j > 0 && std::isfinite (resid[j - 1]) && resid[j - 1] < resid[j]) --j;
                    else if (j + 1 < f.size() && std::isfinite (resid[j + 1]) && resid[j + 1] < resid[j]) ++j;
                    else break;
                }
                const double half = resid[j] * 0.5;
                size_t a = j, b = j;
                while (a > 0 && std::isfinite (resid[a - 1]) && resid[a - 1] < half) --a;
                while (b + 1 < f.size() && std::isfinite (resid[b + 1]) && resid[b + 1] < half) ++b;
                if (std::log2 (f[b] / f[a]) < o.minBoostWidthOct) continue;
                score *= 0.6;
            }
            if (score > bestScore) { bestScore = score; best = (int) i; }
        }
        if (best < 0) break;

        // starting band: centre on the point, width from the half-height points
        const double d = resid[(size_t) best];
        size_t a = (size_t) best, b = (size_t) best;
        while (a > 0 && std::isfinite (resid[a - 1]) && std::abs (resid[a - 1]) > std::abs (d) * 0.5 && resid[a - 1] * d > 0.0) --a;
        while (b + 1 < f.size() && std::isfinite (resid[b + 1]) && std::abs (resid[b + 1]) > std::abs (d) * 0.5 && resid[b + 1] * d > 0.0) ++b;
        const double bw = std::max (0.1, std::log2 (f[b] / f[a]));
        const double p2 = std::pow (2.0, bw);
        Band band { f[(size_t) best], std::clamp (-d, -o.maxCutDb, o.maxBoostDb), std::clamp (std::sqrt (p2) / (p2 - 1.0), o.minQ, o.maxQ) };

        // refine frequency, Q and gain together
        double e = detail::error (f, resid, band, o);
        for (int it = 0; it < 60; ++it)
        {
            bool improved = false;
            const Band tries[] = {
                { band.freq * std::pow (2.0, 1.0 / 24.0), band.gainDb, band.q }, { band.freq * std::pow (2.0, -1.0 / 24.0), band.gainDb, band.q },
                { band.freq, band.gainDb, band.q * 1.12 }, { band.freq, band.gainDb, band.q / 1.12 },
                { band.freq, band.gainDb + 0.25, band.q }, { band.freq, band.gainDb - 0.25, band.q } };
            for (auto t : tries)
            {
                t.q = std::clamp (t.q, o.minQ, o.maxQ);
                t.gainDb = std::clamp (t.gainDb, -o.maxCutDb, o.maxBoostDb);
                t.freq = std::clamp (t.freq, o.fLo, o.fHi);
                const double et = detail::error (f, resid, t, o);
                if (et < e - 1e-9) { e = et; band = t; improved = true; }
            }
            if (! improved) break;
        }
        // a band that does not help is not kept; look elsewhere instead
        const double g = std::abs (band.gainDb);
        if (g < o.minGainDb || (band.q > o.narrowQ && g < o.minNarrowGainDb) || e >= detail::error (f, resid, Band { band.freq, 0.0, band.q }, o) - 1e-6)
        {
            for (size_t i = a; i <= b; ++i) skip[i] = 1;
            continue;
        }
        band.gainDb = std::round (band.gainDb * 10.0) / 10.0;
        band.freq = std::round (band.freq);
        band.q = std::round (band.q * 100.0) / 100.0;
        r.bands.push_back (band);
        for (size_t i = 0; i < f.size(); ++i) if (std::isfinite (resid[i])) resid[i] += responseDb (band, f[i]);
    }
    std::sort (r.bands.begin(), r.bands.end(), [] (const Band& x, const Band& y) { return x.freq < y.freq; });
    detail::stats (f, resid, o, r.rmsAfter, r.maxAfter);
    return r;
}
} // namespace la::eq
