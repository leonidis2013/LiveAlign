// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Room acoustics from an impulse response (ISO 3382-1 style): per octave band the early decay time (EDT), reverberation
// time from the -5 to -25 dB (T20) and -5 to -35 dB (T30) part of the decay, clarity C50 (speech) and C80 (music),
// definition D50, and a speech transmission index (STI, IEC 60268-16 indirect method, no masking or noise
// corrections). The decay is found by backward (Schroeder) integration with the noise floor taken off.
// Also the noise criteria: NC (tangency to the NC curves) and NR (ISO R1996) from octave band noise levels.
// Plain C++, unit tested.
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <vector>

namespace la::acoustics
{
constexpr int kBands = 7;
inline const std::array<double, kBands>& octaves() { static const std::array<double, kBands> f { 125, 250, 500, 1000, 2000, 4000, 8000 }; return f; }

struct Band
{
    double edt = NAN, t20 = NAN, t30 = NAN, c50 = NAN, c80 = NAN, d50 = NAN, mti = NAN;
    double t15 = NAN;       // -5 to -20 dB, when the decay is too short for T20 (a short measurement or a noisy room)
    double rangeDb = NAN;   // decay range above the noise
    // Reverberation time to quote: T30 when the decay range allows, else T20.
    double rt() const { return std::isfinite (t30) ? t30 : std::isfinite (t20) ? t20 : t15; }
};

struct Result
{
    std::array<Band, kBands> band {};
    double sti = NAN;
    bool valid = false;

    // Average of 500 Hz and 1 kHz (the usual single numbers).
    double mid (double Band::* m) const
    {
        const double a = band[2].*m, b = band[3].*m;
        if (std::isfinite (a) && std::isfinite (b)) return 0.5 * (a + b);
        return std::isfinite (a) ? a : b;
    }
    double rtMid() const
    {
        const double a = band[2].rt(), b = band[3].rt();
        if (std::isfinite (a) && std::isfinite (b)) return 0.5 * (a + b);
        return std::isfinite (a) ? a : b;
    }
};

namespace detail
{
constexpr double kPi = 3.14159265358979323846;

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    // band-pass, 0 dB at the centre (RBJ), one octave wide
    static Biquad bandPass (double fs, double fc)
    {
        Biquad q;
        const double w = 2.0 * kPi * fc / fs, Q = std::sqrt (2.0);   // an octave
        const double al = std::sin (w) / (2.0 * Q), c = std::cos (w), a0 = 1.0 + al;
        q.b0 = al / a0; q.b1 = 0.0; q.b2 = -al / a0; q.a1 = -2.0 * c / a0; q.a2 = (1.0 - al) / a0;
        return q;
    }
    void run (std::vector<double>& x) const
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (auto& v : x)
        {
            const double y = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = v; y2 = y1; y1 = y;
            v = y;
        }
    }
};

// Octave band of the response: the band-pass twice (4th order).
inline std::vector<double> octaveBand (const std::vector<double>& h, double fs, double fc)
{
    auto x = h;
    const auto bp = Biquad::bandPass (fs, fc);
    bp.run (x);
    bp.run (x);
    return x;
}

// Energy decay curve (dB, 0 at the start) from the squared response from `start`, with the noise floor (mean energy
// of the last tenth) taken off and the integration stopped where the decay meets the noise. Also the usable range.
inline std::vector<double> decayCurve (const std::vector<double>& h, int start, double fs, double& rangeDb)
{
    const int n = (int) h.size();
    std::vector<double> e2 ((size_t) std::max (0, n - start));
    for (int i = start; i < n; ++i) e2[(size_t) (i - start)] = h[(size_t) i] * h[(size_t) i];
    const int m = (int) e2.size();
    if (m < 16) { rangeDb = 0.0; return {}; }
    double noise = 0.0;
    const int tail = std::max (1, m / 10);
    for (int i = m - tail; i < m; ++i) noise += e2[(size_t) i];
    noise /= tail;

    // smoothed envelope in 10 ms windows, to find where the decay reaches the noise (3 dB above it)
    const int win = std::max (1, (int) (0.01 * fs));
    int cut = m;
    double peak = 0.0;
    for (int i = 0; i + win <= m; i += win)
    {
        double s = 0.0;
        for (int k = i; k < i + win; ++k) s += e2[(size_t) k];
        s /= win;
        peak = std::max (peak, s);
        if (i > win * 3 && s < noise * 2.0) { cut = i; break; }
    }
    rangeDb = peak > 0.0 && noise > 0.0 ? 10.0 * std::log10 (peak / noise) : 100.0;

    std::vector<double> edc ((size_t) cut, 0.0);
    double acc = 0.0;
    for (int i = cut - 1; i >= 0; --i)
    {
        acc += std::max (0.0, e2[(size_t) i] - noise);
        edc[(size_t) i] = acc;
    }
    const double total = edc.empty() ? 0.0 : edc[0];
    if (total <= 0.0) return {};
    for (auto& v : edc) v = v > 0.0 ? 10.0 * std::log10 (v / total) : -200.0;
    return edc;
}

// Time for 60 dB of decay, from a straight line fitted to the curve between `fromDb` and `toDb` (negative).
inline double decayTime (const std::vector<double>& edc, double fs, double fromDb, double toDb)
{
    int a = -1, b = -1;
    for (int i = 0; i < (int) edc.size(); ++i)
    {
        if (a < 0 && edc[(size_t) i] <= fromDb) a = i;
        if (edc[(size_t) i] <= toDb) { b = i; break; }
    }
    if (a < 0 || b < 0 || b - a < 8) return NAN;
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double cnt = b - a + 1;
    for (int i = a; i <= b; ++i)
    {
        const double x = i / fs, y = edc[(size_t) i];
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    const double slope = (cnt * sxy - sx * sy) / (cnt * sxx - sx * sx);
    return slope < 0.0 ? -60.0 / slope : NAN;
}

// Energy ratio early / late around `ms` (dB), and early / total.
inline void clarity (const std::vector<double>& h, int start, double fs, double ms, double& cDb, double& d)
{
    const int split = start + (int) (ms * 0.001 * fs);
    double early = 0.0, late = 0.0;
    const int n = (int) h.size();
    // noise: the last tenth, taken off the late part
    double noise = 0.0;
    const int tail = std::max (1, (n - start) / 10);
    for (int i = n - tail; i < n; ++i) noise += h[(size_t) i] * h[(size_t) i];
    noise /= tail;
    for (int i = start; i < n; ++i)
    {
        const double e = std::max (0.0, h[(size_t) i] * h[(size_t) i] - noise);
        (i < split ? early : late) += e;
    }
    cDb = late > 0.0 && early > 0.0 ? 10.0 * std::log10 (early / late) : NAN;
    d = early + late > 0.0 ? early / (early + late) : NAN;
}

// Modulation transfer index for one band: modulation reduction at 14 frequencies (0.63 - 12.5 Hz).
inline double mti (const std::vector<double>& h, int start, double fs)
{
    static const double fm[14] = { 0.63, 0.8, 1.0, 1.25, 1.6, 2.0, 2.5, 3.15, 4.0, 5.0, 6.3, 8.0, 10.0, 12.5 };
    double total = 0.0;
    for (int i = start; i < (int) h.size(); ++i) total += h[(size_t) i] * h[(size_t) i];
    if (total <= 0.0) return NAN;
    double ti = 0.0;
    for (double F : fm)
    {
        std::complex<double> acc (0.0, 0.0);
        for (int i = start; i < (int) h.size(); ++i)
        {
            const double t = (i - start) / fs;
            acc += h[(size_t) i] * h[(size_t) i] * std::polar (1.0, -2.0 * kPi * F * t);
        }
        const double m = std::min (0.9999, std::abs (acc) / total);
        const double snr = std::clamp (10.0 * std::log10 (m / (1.0 - m)), -15.0, 15.0);
        ti += (snr + 15.0) / 30.0;
    }
    return ti / 14.0;
}
} // namespace detail

// `ir`: impulse response at `fs`; the direct sound is found from its peak.
inline Result analyse (const std::vector<float>& ir, double fs)
{
    Result r;
    if (ir.size() < (size_t) (0.1 * fs) || fs <= 0.0) return r;
    std::vector<double> h (ir.begin(), ir.end());
    int peak = 0;
    for (int i = 1; i < (int) h.size(); ++i) if (std::abs (h[(size_t) i]) > std::abs (h[(size_t) peak])) peak = i;
    if (std::abs (h[(size_t) peak]) < 1e-30) return r;
    // start: where the response first comes within 20 dB of its peak (ISO 3382-1)
    int start = peak;
    const double thr = std::abs (h[(size_t) peak]) * 0.1;
    for (int i = peak; i >= 0 && peak - i < (int) (0.01 * fs); --i) if (std::abs (h[(size_t) i]) >= thr) start = i;
    start = std::max (0, start - (int) (0.0005 * fs));

    for (int b = 0; b < kBands; ++b)
    {
        const double fc = octaves()[(size_t) b];
        if (fc * 1.42 >= fs * 0.5) continue;
        const auto x = detail::octaveBand (h, fs, fc);
        auto& o = r.band[(size_t) b];
        const auto edc = detail::decayCurve (x, start, fs, o.rangeDb);
        if (edc.empty()) continue;
        o.edt = detail::decayTime (edc, fs, -0.0001, -10.0);
        if (o.rangeDb >= 25.0) o.t15 = detail::decayTime (edc, fs, -5.0, -20.0);
        if (o.rangeDb >= 35.0) o.t20 = detail::decayTime (edc, fs, -5.0, -25.0);
        if (o.rangeDb >= 45.0) o.t30 = detail::decayTime (edc, fs, -5.0, -35.0);
        detail::clarity (x, start, fs, 50.0, o.c50, o.d50);
        double d80 = 0.0;
        detail::clarity (x, start, fs, 80.0, o.c80, d80);
        o.mti = detail::mti (x, start, fs);
    }
    // STI from the band transmission indices (male speech weights, IEC 60268-16 ed. 4)
    static const double alpha[kBands] = { 0.085, 0.127, 0.230, 0.233, 0.309, 0.224, 0.173 };
    static const double beta[kBands - 1] = { 0.085, 0.078, 0.065, 0.011, 0.047, 0.095 };
    bool all = true;
    for (const auto& o : r.band) all = all && std::isfinite (o.mti);
    if (all)
    {
        double sti = 0.0;
        for (int b = 0; b < kBands; ++b) sti += alpha[b] * r.band[(size_t) b].mti;
        for (int b = 0; b < kBands - 1; ++b) sti -= beta[b] * std::sqrt (r.band[(size_t) b].mti * r.band[(size_t) b + 1].mti);
        r.sti = std::clamp (sti, 0.0, 1.0);
    }
    r.valid = std::isfinite (r.rtMid()) || std::isfinite (r.mid (&Band::edt));
    return r;
}

// How STI is usually described (IEC 60268-16).
inline const char* stiRating (double sti)
{
    if (! std::isfinite (sti)) return "-";
    return sti >= 0.75 ? "excellent" : sti >= 0.6 ? "good" : sti >= 0.45 ? "fair" : sti >= 0.3 ? "poor" : "bad";
}

// ---- noise ratings from octave band levels 63 Hz - 8 kHz (dB SPL; NaN = not measured) ----
constexpr int kNoiseBands = 8;
inline const std::array<double, kNoiseBands>& noiseOctaves() { static const std::array<double, kNoiseBands> f { 63, 125, 250, 500, 1000, 2000, 4000, 8000 }; return f; }

// NR (ISO R1996): the highest of (L - a) / b over the bands.
inline double noiseRating (const std::array<double, kNoiseBands>& L)
{
    static const double a[kNoiseBands] = { 35.5, 22.0, 12.0, 4.8, 0.0, -3.5, -6.1, -8.0 };
    static const double b[kNoiseBands] = { 0.790, 0.870, 0.930, 0.974, 1.0, 1.015, 1.030, 1.050 };
    double nr = -1e9;
    for (int i = 0; i < kNoiseBands; ++i) if (std::isfinite (L[(size_t) i])) nr = std::max (nr, (L[(size_t) i] - a[i]) / b[i]);
    return nr > -1e8 ? nr : NAN;
}

// NC (tangency): the lowest NC curve no band exceeds, interpolated between the curves 5 apart.
inline const std::array<std::array<double, kNoiseBands>, 11>& ncCurves()
{
    static const std::array<std::array<double, kNoiseBands>, 11> c { {
        { 47, 36, 29, 22, 17, 14, 12, 11 },   // NC-15
        { 51, 40, 33, 26, 22, 19, 17, 16 },   // NC-20
        { 54, 44, 37, 31, 27, 24, 22, 21 },   // NC-25
        { 57, 48, 41, 35, 31, 29, 28, 27 },   // NC-30
        { 60, 52, 45, 40, 36, 34, 33, 32 },   // NC-35
        { 64, 56, 50, 45, 41, 39, 38, 37 },   // NC-40
        { 67, 60, 54, 49, 46, 44, 43, 42 },   // NC-45
        { 71, 64, 58, 54, 51, 49, 48, 47 },   // NC-50
        { 74, 67, 62, 58, 56, 54, 53, 52 },   // NC-55
        { 77, 71, 67, 63, 61, 59, 58, 57 },   // NC-60
        { 80, 75, 71, 68, 66, 64, 63, 62 } } };   // NC-65
    return c;
}

inline double ncAt (int band, double nc)   // the curve level in a band for a fractional NC (15 - 65)
{
    const auto& c = ncCurves();
    const double pos = (nc - 15.0) / 5.0;
    const int i = std::clamp ((int) std::floor (pos), 0, 9);
    const double t = pos - i;
    return c[(size_t) i][(size_t) band] + t * (c[(size_t) i + 1][(size_t) band] - c[(size_t) i][(size_t) band]);
}

inline double noiseCriterion (const std::array<double, kNoiseBands>& L)
{
    bool any = false;
    for (double v : L) any = any || std::isfinite (v);
    if (! any) return NAN;
    for (double nc = 10.0; nc <= 70.0; nc += 1.0)
    {
        bool under = true;
        for (int b = 0; b < kNoiseBands && under; ++b)
        {
            if (! std::isfinite (L[(size_t) b])) continue;
            const double lim = nc < 15.0 ? ncCurves()[0][(size_t) b] - (15.0 - nc) : ncAt (b, std::min (nc, 65.0)) + std::max (0.0, nc - 65.0);
            under = L[(size_t) b] <= lim + 1e-9;
        }
        if (under) return nc;
    }
    return 70.0;
}
} // namespace la::acoustics
