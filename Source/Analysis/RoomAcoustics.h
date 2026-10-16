// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Room acoustics from an impulse response (ISO 3382-1 style): per octave band the early decay time (EDT), reverberation
// time from the -5 to -25 dB (T20) and -5 to -35 dB (T30) part of the decay, clarity C50 (speech) and C80 (music),
// definition D50, centre time Ts, and a speech transmission index (STI, IEC 60268-16 indirect method; the corrections
// for noise, auditory masking and the hearing threshold are applied separately, from the measured noise and a speech
// level). The decay is found by backward (Schroeder) integration with the noise floor taken off. Also the 63 Hz band,
// reverberation time in 1/3 octaves (100 Hz - 5 kHz), the energy-time curve with the initial time delay gap, echoes
// (Dietsch and Kraak's echo criterion, speech and music) and flutter echo; bass and treble ratios, %ALcons, the
// Schroeder frequency and the critical distance; the noise criteria NC (tangency to the NC curves) and NR (ISO R1996)
// from octave band noise levels; and the spread of any measure across the listening points.
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

constexpr int kModFreqs = 14;   // the STI modulation frequencies, 0.63 - 12.5 Hz
constexpr int kThirds = 18;     // 1/3-octave bands for the reverberation time, 100 Hz - 5 kHz (ISO 3382-2)
inline const std::array<double, kThirds>& thirds()
{
    static const std::array<double, kThirds> f { 100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000 };
    return f;
}

template <size_t N> std::array<double, N> nans() { std::array<double, N> a; a.fill (NAN); return a; }

struct Band
{
    double edt = NAN, t20 = NAN, t30 = NAN, c50 = NAN, c80 = NAN, d50 = NAN, mti = NAN;
    double t15 = NAN;       // -5 to -20 dB, when the decay is too short for T20 (a short measurement or a noisy room)
    double rangeDb = NAN;   // decay range above the noise
    double ts = NAN;        // centre time (ms): the "centre of gravity" of the energy, lower is clearer
    std::array<double, kModFreqs> m {};   // modulation transfer at each modulation frequency (for the corrected STI); 0 = not known
    // Reverberation time to quote: T30 when the decay range allows, else T20.
    double rt() const { return std::isfinite (t30) ? t30 : std::isfinite (t20) ? t20 : t15; }
};

struct Result
{
    std::array<Band, kBands> band {};
    Band b63;                                  // the 63 Hz octave (subs and room modes); unreliable when its decay range is short
    std::array<double, kThirds> rt3 = nans<kThirds>();   // reverberation time per 1/3 octave (NaN = not clear)
    double sti = NAN;
    bool valid = false;
    // reflections, from the broadband energy-time curve
    double itdgMs = NAN;                       // initial time delay gap: direct sound to the first strong reflection
    double echoSpeech = NAN, echoMusic = NAN;  // Dietsch / Kraak echo criterion EK (audible to half the listeners at 1.0 / 1.8)
    double echoMs = NAN, echoDb = NAN;         // when the strongest late reflection arrives, and its level against the direct sound
    double flutterMs = NAN;                    // repeat period of a flutter echo (NaN = none found)
    double flutterStrength = NAN;              // 0..1, how regular the repeats are
    std::vector<float> etc;                    // energy-time curve, dB against the direct sound, 0.5 ms steps from 5 ms before it

    bool echoAudible() const { return (std::isfinite (echoSpeech) && echoSpeech >= 1.0) || (std::isfinite (echoMusic) && echoMusic >= 1.8); }

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

inline const double* modFreqs() { static const double fm[kModFreqs] = { 0.63, 0.8, 1.0, 1.25, 1.6, 2.0, 2.5, 3.15, 4.0, 5.0, 6.3, 8.0, 10.0, 12.5 }; return fm; }

// The transmission index of one modulation transfer value (IEC 60268-16: effective SNR clipped to +/-15 dB).
inline double transmissionIndex (double m)
{
    m = std::clamp (m, 1e-6, 0.9999);
    const double snr = std::clamp (10.0 * std::log10 (m / (1.0 - m)), -15.0, 15.0);
    return (snr + 15.0) / 30.0;
}

// Modulation transfer index for one band: modulation reduction at 14 frequencies (0.63 - 12.5 Hz). `mOut` gets each m.
inline double mti (const std::vector<double>& h, int start, double fs, std::array<double, kModFreqs>* mOut = nullptr)
{
    double total = 0.0;
    for (int i = start; i < (int) h.size(); ++i) total += h[(size_t) i] * h[(size_t) i];
    if (total <= 0.0) return NAN;
    double ti = 0.0;
    for (int k = 0; k < kModFreqs; ++k)
    {
        const double F = modFreqs()[k];
        std::complex<double> acc (0.0, 0.0);
        for (int i = start; i < (int) h.size(); ++i)
        {
            const double t = (i - start) / fs;
            acc += h[(size_t) i] * h[(size_t) i] * std::polar (1.0, -2.0 * kPi * F * t);
        }
        const double m = std::min (0.9999, std::abs (acc) / total);
        if (mOut != nullptr) (*mOut)[(size_t) k] = m;
        ti += transmissionIndex (m);
    }
    return ti / (double) kModFreqs;
}

// Band-pass of any relative width (RBJ, 0 dB at the centre), run twice.
inline std::vector<double> bandOf (const std::vector<double>& h, double fs, double fc, double Q)
{
    auto x = h;
    Biquad q;
    const double w = 2.0 * kPi * fc / fs;
    const double al = std::sin (w) / (2.0 * Q), c = std::cos (w), a0 = 1.0 + al;
    q.b0 = al / a0; q.b1 = 0.0; q.b2 = -al / a0; q.a1 = -2.0 * c / a0; q.a2 = (1.0 - al) / a0;
    q.run (x);
    q.run (x);
    return x;
}

// Centre time Ts (ms): sum of t * energy over the energy, from the direct sound until the decay reaches the noise.
inline double centreTime (const std::vector<double>& h, int start, double fs)
{
    const int n = (int) h.size();
    const int m = n - start;
    if (m < 16) return NAN;
    double noise = 0.0;
    const int tail = std::max (1, m / 10);
    for (int i = n - tail; i < n; ++i) noise += h[(size_t) i] * h[(size_t) i];
    noise /= tail;
    const int win = std::max (1, (int) (0.01 * fs));
    int cut = n;
    for (int i = start; i + win <= n; i += win)
    {
        double s = 0.0;
        for (int k = i; k < i + win; ++k) s += h[(size_t) k] * h[(size_t) k];
        if (i > start + win * 3 && s / win < noise * 2.0) { cut = i; break; }
    }
    double num = 0.0, den = 0.0;
    for (int i = start; i < cut; ++i)
    {
        const double e = std::max (0.0, h[(size_t) i] * h[(size_t) i] - noise);
        num += e * (i - start) / fs;
        den += e;
    }
    return den > 0.0 ? 1000.0 * num / den : NAN;
}

// One band's decay measures (EDT, T15 / T20 / T30 by the range there is, clarity, centre time, STI modulation).
inline void bandMeasures (const std::vector<double>& x, int start, double fs, Band& o, bool withMti)
{
    const auto edc = decayCurve (x, start, fs, o.rangeDb);
    if (edc.empty()) return;
    o.edt = decayTime (edc, fs, -0.0001, -10.0);
    if (o.rangeDb >= 25.0) o.t15 = decayTime (edc, fs, -5.0, -20.0);
    if (o.rangeDb >= 35.0) o.t20 = decayTime (edc, fs, -5.0, -25.0);
    if (o.rangeDb >= 45.0) o.t30 = decayTime (edc, fs, -5.0, -35.0);
    clarity (x, start, fs, 50.0, o.c50, o.d50);
    double d80 = 0.0;
    clarity (x, start, fs, 80.0, o.c80, d80);
    o.ts = centreTime (x, start, fs);
    if (withMti) o.mti = mti (x, start, fs, &o.m);
}

// Dietsch / Kraak echo criterion: the steepest rise of the running centre time tS(tau) over a window `dtMs`, with the
// response raised to the power `n` (2/3 for speech, 1 for music). Returns EK and where it happens (ms after the direct sound).
inline double echoCriterion (const std::vector<double>& h, int start, double fs, double n, double dtMs, double& atMs)
{
    atMs = NAN;
    const int len = (int) h.size() - start;
    const int maxI = std::min (len, (int) (0.6 * fs));
    if (maxI < (int) (0.05 * fs)) return NAN;
    // tS at each sample, by running sums
    std::vector<double> ts ((size_t) maxI, 0.0);
    double num = 0.0, den = 0.0;
    for (int i = 0; i < maxI; ++i)
    {
        const double p = std::pow (std::abs (h[(size_t) (start + i)]), n);
        num += p * i / fs;
        den += p;
        ts[(size_t) i] = den > 0.0 ? num / den : 0.0;
    }
    const int d = std::max (1, (int) (dtMs * 0.001 * fs));
    const int from = std::max (d, (int) (0.05 * fs));   // an earlier reflection fuses with the direct sound (the precedence effect)
    double best = 0.0;
    for (int i = from; i < maxI; ++i)
    {
        const double ek = (ts[(size_t) i] - ts[(size_t) (i - d)]) / (dtMs * 0.001);
        if (ek > best) { best = ek; atMs = 1000.0 * (i - d * 0.5) / fs; }
    }
    return best;
}

// The energy-time curve (dB against the direct sound) in 0.5 ms steps, from 5 ms before the direct sound to 400 ms after.
inline std::vector<float> energyTime (const std::vector<double>& h, int start, int peak, double fs)
{
    const int step = std::max (1, (int) std::lround (0.0005 * fs));
    const int from = std::max (0, start - (int) (0.005 * fs));
    const int to = std::min ((int) h.size(), start + (int) (0.4 * fs));
    double pk = 0.0;
    for (int i = std::max (0, peak - step); i < std::min ((int) h.size(), peak + step); ++i) pk = std::max (pk, h[(size_t) i] * h[(size_t) i]);
    std::vector<float> out;
    if (pk <= 0.0) return out;
    for (int i = from; i + step <= to; i += step)
    {
        double e = 0.0;
        for (int k = i; k < i + step; ++k) e = std::max (e, h[(size_t) k] * h[(size_t) k]);
        out.push_back ((float) std::max (-100.0, 10.0 * std::log10 (std::max (1e-30, e / pk))));
    }
    return out;
}

// The first strong reflection after the direct sound (within 20 dB of it), from the energy-time curve. ms or NaN.
inline double initialGap (const std::vector<float>& etc)
{
    // the direct sound is at 5 ms (10 steps); skip its own width (1.5 ms)
    const int d0 = 10;
    int i = d0;
    while (i + 1 < (int) etc.size() && i < d0 + 3) ++i;
    for (int k = std::max (i, d0 + 3); k + 1 < (int) etc.size() && k < d0 + 200; ++k)
        if (etc[(size_t) k] > -20.0f && etc[(size_t) k] >= etc[(size_t) k - 1] && etc[(size_t) k] >= etc[(size_t) k + 1]) return (k - d0) * 0.5;
    return NAN;
}

// Flutter echo: a regular repeat in the decay (between two parallel surfaces). The energy in 1 ms steps from 20 ms on,
// its straight-line decay taken off, and the autocorrelation of what is left: a strong peak at a lag of 4 - 100 ms that
// repeats at twice the lag. Returns the period (ms) or NaN, with how regular it is.
inline double flutter (const std::vector<double>& h, int start, double fs, double& strength)
{
    strength = 0.0;
    const int step = std::max (1, (int) (0.001 * fs));
    std::vector<double> env;
    for (int i = start + (int) (0.02 * fs); i + step <= (int) h.size() && (int) env.size() < 700; i += step)
    {
        double e = 0.0;
        for (int k = i; k < i + step; ++k) e += h[(size_t) k] * h[(size_t) k];
        env.push_back (10.0 * std::log10 (e / step + 1e-30));
    }
    // to where the decay is still 20 dB above its end
    const int n0 = (int) env.size();
    if (n0 < 120) return NAN;
    double tailLvl = 0.0;
    for (int i = n0 - n0 / 10; i < n0; ++i) tailLvl += env[(size_t) i];
    tailLvl /= std::max (1, n0 / 10);
    int n = n0;
    for (int i = 10; i < n0; ++i)
    {
        double a = 0.0;
        for (int k = i - 10; k < i; ++k) a += env[(size_t) k];
        if (a / 10.0 < tailLvl + 15.0) { n = i; break; }
    }
    if (n < 80) return NAN;
    env.resize ((size_t) n);
    // take off the straight-line decay
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; ++i) { sx += i; sy += env[(size_t) i]; sxx += (double) i * i; sxy += i * env[(size_t) i]; }
    const double b = (n * sxy - sx * sy) / (n * sxx - sx * sx), a = (sy - b * sx) / n;
    std::vector<double> r ((size_t) n);
    double var = 0.0;
    for (int i = 0; i < n; ++i) { r[(size_t) i] = env[(size_t) i] - (a + b * i); var += r[(size_t) i] * r[(size_t) i]; }
    if (var <= 0.0) return NAN;
    auto acf = [&] (int lag)
    {
        double s = 0.0;
        for (int i = 0; i + lag < n; ++i) s += r[(size_t) i] * r[(size_t) i + lag];
        return s / var * (double) n / (double) (n - lag);
    };
    double best = 0.0;
    int bestLag = -1;
    for (int lag = 4; lag <= std::min (100, n / 3); ++lag)
    {
        const double c = acf (lag);
        if (c > best && c >= acf (lag - 1) && c >= acf (lag + 1)) { best = c; bestLag = lag; }
    }
    if (bestLag < 0) return NAN;
    const double second = 2 * bestLag < n / 2 ? acf (2 * bestLag) : 0.0;
    strength = std::clamp (0.5 * (best + std::max (0.0, second)), 0.0, 1.0);
    if (best < 0.35 || second < 0.2) return NAN;
    return bestLag * (double) step / fs * 1000.0;
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
        detail::bandMeasures (detail::octaveBand (h, fs, fc), start, fs, r.band[(size_t) b], true);
    }
    detail::bandMeasures (detail::octaveBand (h, fs, 63.0), start, fs, r.b63, false);
    // reverberation in 1/3 octaves
    const double q3 = 1.0 / (std::pow (2.0, 1.0 / 6.0) - std::pow (2.0, -1.0 / 6.0));
    for (int b = 0; b < kThirds; ++b)
    {
        r.rt3[(size_t) b] = NAN;
        const double fc = thirds()[(size_t) b];
        if (fc * 1.2 >= fs * 0.5) continue;
        Band o;
        const auto x = detail::bandOf (h, fs, fc, q3);
        const auto edc = detail::decayCurve (x, start, fs, o.rangeDb);
        if (edc.empty()) continue;
        if (o.rangeDb >= 45.0) o.t30 = detail::decayTime (edc, fs, -5.0, -35.0);
        if (o.rangeDb >= 35.0) o.t20 = detail::decayTime (edc, fs, -5.0, -25.0);
        if (o.rangeDb >= 25.0) o.t15 = detail::decayTime (edc, fs, -5.0, -20.0);
        r.rt3[(size_t) b] = o.rt();
    }
    // reflections: the energy-time curve, the gap to the first reflection, echoes (speech from the 1 kHz octave, music
    // broadband) and flutter
    r.etc = detail::energyTime (h, start, peak, fs);
    r.itdgMs = detail::initialGap (r.etc);
    {
        double atS = NAN, atM = NAN;
        const auto x1k = detail::octaveBand (h, fs, 1000.0);
        r.echoSpeech = detail::echoCriterion (x1k, start, fs, 2.0 / 3.0, 9.0, atS);
        r.echoMusic = detail::echoCriterion (h, start, fs, 1.0, 14.0, atM);
        const bool speechWorse = std::isfinite (r.echoSpeech) && (! std::isfinite (r.echoMusic) || r.echoSpeech / 1.0 >= r.echoMusic / 1.8);
        r.echoMs = speechWorse ? atS : atM;
        if (std::isfinite (r.echoMs))
        {
            // its level: the loudest point within 5 ms of it, against the direct sound
            const int c = (int) std::lround (r.echoMs * 2.0) + 10;
            float mx = -100.0f;
            for (int k = std::max (0, c - 10); k < std::min ((int) r.etc.size(), c + 10); ++k) mx = std::max (mx, r.etc[(size_t) k]);
            r.echoDb = r.etc.empty() ? NAN : (double) mx;
        }
        double st = 0.0;
        r.flutterMs = detail::flutter (h, start, fs, st);
        r.flutterStrength = st;
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

// %ALcons (articulation loss of consonants) from STI (Farrel Becker's fit).
inline double alcons (double sti) { return std::isfinite (sti) ? 170.5405 * std::exp (-5.419 * sti) : NAN; }
inline const char* alconsRating (double a)
{
    if (! std::isfinite (a)) return "-";
    return a <= 3.0 ? "ideal" : a <= 7.0 ? "good" : a <= 11.0 ? "fair" : a <= 15.0 ? "poor" : "unacceptable";
}

// Bass ratio (warmth): RT at 125 + 250 Hz over 500 Hz + 1 kHz. Treble ratio (brilliance): 2 + 4 kHz over the same.
inline double bassRatio (const Result& r)
{
    const double a = r.band[0].rt(), b = r.band[1].rt(), m = r.band[2].rt() + r.band[3].rt();
    return std::isfinite (a) && std::isfinite (b) && std::isfinite (m) && m > 0.0 ? (a + b) / m : NAN;
}
inline double trebleRatio (const Result& r)
{
    const double a = r.band[4].rt(), b = r.band[5].rt(), m = r.band[2].rt() + r.band[3].rt();
    return std::isfinite (a) && std::isfinite (b) && std::isfinite (m) && m > 0.0 ? (a + b) / m : NAN;
}

// Schroeder frequency (Hz): below it single room modes rule, above it the reverberant field. Critical distance (m):
// where the direct sound of a speaker with directivity factor Q equals the reverberant sound. Volume in cubic metres.
inline double schroederHz (double rt, double volumeM3) { return rt > 0.0 && volumeM3 > 0.0 ? 2000.0 * std::sqrt (rt / volumeM3) : NAN; }
inline double criticalDistanceM (double q, double volumeM3, double rt) { return rt > 0.0 && volumeM3 > 0.0 && q > 0.0 ? 0.057 * std::sqrt (q * volumeM3 / rt) : NAN; }

// STI with the corrections of IEC 60268-16 (ed. 4 / 5): background noise, auditory masking from the band below, and
// the hearing threshold, for male speech at `speechDbA` (A-weighted, at the listener). `noise`: octave levels 125 Hz -
// 8 kHz in dB SPL (NaN = not known, then only masking and the threshold are applied). NaN without the modulation data.
inline double stiCorrected (const Result& r, double speechDbA, const std::array<double, kBands>& noise)
{
    static const double speech[kBands] = { 2.9, 2.9, -0.8, -6.8, -12.8, -18.8, -24.8 };   // male speech, re the A-weighted level
    static const double threshold[kBands] = { 46.0, 27.0, 12.0, 6.5, 7.5, 8.0, 12.0 };    // absolute reception threshold
    static const double alpha[kBands] = { 0.085, 0.127, 0.230, 0.233, 0.309, 0.224, 0.173 };
    static const double beta[kBands - 1] = { 0.085, 0.078, 0.065, 0.011, 0.047, 0.095 };
    if (! std::isfinite (speechDbA)) return NAN;
    std::array<double, kBands> L {}, mtiC {};
    for (int k = 0; k < kBands; ++k) L[(size_t) k] = speechDbA + speech[k];
    for (int k = 0; k < kBands; ++k)
    {
        const auto& o = r.band[(size_t) k];
        bool have = false;
        for (double v : o.m) have = have || v > 0.0;
        if (! have) return NAN;
        const double I = std::pow (10.0, L[(size_t) k] / 10.0);
        const double In = std::isfinite (noise[(size_t) k]) ? std::pow (10.0, noise[(size_t) k] / 10.0) : 0.0;
        const double Irt = std::pow (10.0, threshold[k] / 10.0);
        double Iam = 0.0;
        if (k > 0)
        {
            const double Lb = L[(size_t) k - 1];
            const double amdB = Lb < 63.0 ? 0.5 * Lb - 65.0 : Lb < 67.0 ? 1.8 * Lb - 146.9 : Lb < 100.0 ? 0.5 * Lb - 59.8 : -10.0;
            Iam = std::pow (10.0, Lb / 10.0) * std::pow (10.0, amdB / 10.0);
        }
        const double f = I / (I + In + Irt + Iam);
        double ti = 0.0;
        for (double m : o.m) ti += detail::transmissionIndex (m * f);
        mtiC[(size_t) k] = ti / (double) kModFreqs;
    }
    double sti = 0.0;
    for (int b = 0; b < kBands; ++b) sti += alpha[b] * mtiC[(size_t) b];
    for (int b = 0; b < kBands - 1; ++b) sti -= beta[b] * std::sqrt (mtiC[(size_t) b] * mtiC[(size_t) b + 1]);
    return std::clamp (sti, 0.0, 1.0);
}

// How a measure varies across the listening points.
struct Spread
{
    double mean = NAN, sd = NAN, lo = NAN, hi = NAN;
    int n = 0;
};
inline Spread spreadOf (const std::vector<double>& v)
{
    Spread s;
    double sum = 0.0, sum2 = 0.0;
    for (double x : v)
    {
        if (! std::isfinite (x)) continue;
        sum += x; sum2 += x * x;
        s.lo = s.n == 0 ? x : std::min (s.lo, x);
        s.hi = s.n == 0 ? x : std::max (s.hi, x);
        ++s.n;
    }
    if (s.n == 0) return s;
    s.mean = sum / s.n;
    s.sd = s.n > 1 ? std::sqrt (std::max (0.0, (sum2 - s.n * s.mean * s.mean) / (s.n - 1))) : 0.0;
    return s;
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
