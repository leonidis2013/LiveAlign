// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// STIPA, the speech transmission index for public address (IEC 60268-16, the direct method): a test signal that is
// speech-like in spectrum, seven half-octave bands of pink noise (125 Hz - 8 kHz) at male speech levels, each band's
// intensity modulated at two frequencies (depth 0.55 each, the second 180 degrees from the first). Played through the
// system and picked up at a seat, how much of that modulation survives (the room's reverberation and the noise fill
// the gaps in) gives the STI: 0 to 1, 0.5 fair, 0.6 good. Measured over at least 15 s (18 s here).
//
// The analysis works on any recording of the signal: each octave band is filtered, squared (intensity) and its
// modulation depth read at the two frequencies over a whole number of periods. Divided by the signal's own depth (0.55,
// or the depth found on the reference input when the generator is looped back) it is the modulation transfer; with the
// band levels (a calibrated mic) the auditory masking and hearing threshold corrections of IEC 60268-16 ed. 4 / 5 apply.
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>
#include "Fft.h"

namespace la::stipa
{
constexpr int kBands = 7;
inline const std::array<double, kBands>& centres() { static const std::array<double, kBands> f { 125, 250, 500, 1000, 2000, 4000, 8000 }; return f; }
// the two modulation frequencies of each band (Hz)
inline const std::array<std::array<double, 2>, kBands>& modFreqs()
{
    static const std::array<std::array<double, 2>, kBands> m { { { 1.6, 8.0 }, { 1.0, 5.0 }, { 0.63, 3.15 }, { 2.0, 10.0 }, { 1.25, 6.25 }, { 0.8, 4.0 }, { 2.5, 12.5 } } };
    return m;
}
constexpr double kDepth = 0.55;
// male speech: octave levels against the A-weighted level (dB)
inline const std::array<double, kBands>& speechLevels() { static const std::array<double, kBands> l { 2.9, 2.9, -0.8, -6.8, -12.8, -18.8, -24.8 }; return l; }
constexpr double kSeconds = 18.0;   // a measurement

// ---- the test signal
class Signal
{
public:
    // Builds the carriers (a few seconds of noise per band, looped). Not on the audio thread.
    void prepare (double sampleRate, unsigned seed = 0x5713A)
    {
        sr = sampleRate;
        const int n = 1 << 18;
        Fft f;
        f.setSize (n);
        unsigned st = seed;
        auto rnd = [&st] { st = st * 1664525u + 1013904223u; return (double) (st >> 8) / 16777216.0; };
        double total = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            std::vector<cplx> X ((size_t) n);
            const double lo = centres()[(size_t) b] * std::pow (2.0, -0.25), hi = centres()[(size_t) b] * std::pow (2.0, 0.25);   // half an octave
            for (int k = 1; k < n / 2; ++k)
            {
                const double fk = k * sr / n;
                if (fk < lo || fk > hi) continue;
                const double ph = 2.0 * kPi * rnd();
                X[(size_t) k] = std::polar (1.0 / std::sqrt (fk), ph);   // pink within the band
                X[(size_t) (n - k)] = std::conj (X[(size_t) k]);
            }
            f.inverse (X.data());
            auto& c = carrier[(size_t) b];
            c.resize ((size_t) n);
            double ms = 0.0;
            for (int i = 0; i < n; ++i) { c[(size_t) i] = (float) X[(size_t) i].real(); ms += (double) c[(size_t) i] * c[(size_t) i]; }
            const double g = std::pow (10.0, speechLevels()[(size_t) b] / 20.0) / std::sqrt (std::max (1e-30, ms / n));
            for (auto& v : c) v = (float) (v * g);
            total += std::pow (10.0, speechLevels()[(size_t) b] / 10.0);
        }
        norm = 1.0 / std::sqrt (total);   // the whole signal at an RMS of 1 before the level
        t = 0;
    }
    bool ready() const { return ! carrier[0].empty(); }

    // One sample; `rms` the level wanted (a full-scale sine's RMS is 0.707).
    void setModulated (bool on) { modulated = on; }
    double next (double rms)
    {
        if (! ready()) return 0.0;
        const double time = (double) t / sr;
        const size_t i = (size_t) (t % (long long) carrier[0].size());
        ++t;
        double s = 0.0;
        for (int b = 0; b < kBands; ++b)
        {
            const auto& m = modFreqs()[(size_t) b];
            const double I = modulated ? 1.0 + kDepth * std::cos (2.0 * kPi * m[0] * time) - kDepth * std::cos (2.0 * kPi * m[1] * time) : 1.0;
            s += carrier[(size_t) b][i] * std::sqrt (std::max (0.0, I));
        }
        return s * norm * rms;
    }
    void restart() { t = 0; }

private:
    double sr = 48000.0, norm = 1.0;
    long long t = 0;
    bool modulated = true;
    std::array<std::vector<float>, kBands> carrier;
};

// ---- the analysis
struct Result
{
    bool valid = false;
    std::array<std::array<double, 2>, kBands> m {};     // modulation transfer at each band's two frequencies (0..1)
    std::array<double, kBands> mti {}, levelDb {};       // per band: transmission index, level (dB SPL with the offset, else dBFS)
    double sti = NAN;
    bool corrected = false;                              // masking and threshold corrections applied (calibrated levels)
    std::string note;
};

namespace detail
{
// Each band's modulation depth at its two frequencies, and its level (dBFS, AES17).
inline void depths (const float* x, int len, double sr, std::array<std::array<double, 2>, kBands>& mOut, std::array<double, kBands>& dbOut)
{
    const int n = (int) [len] { int p = 1; while (p < len) p <<= 1; return p; }();
    Fft f;
    f.setSize (n);
    std::vector<cplx> X ((size_t) n);
    for (int i = 0; i < len; ++i) X[(size_t) i] = cplx (x[i], 0.0);
    f.forward (X.data());
    std::vector<cplx> Y ((size_t) n);
    for (int b = 0; b < kBands; ++b)
    {
        // the octave band, with soft edges (a sixth of an octave)
        const double fc = centres()[(size_t) b];
        std::fill (Y.begin(), Y.end(), cplx {});
        for (int k = 1; k < n / 2; ++k)
        {
            const double fk = k * sr / n;
            const double o = std::abs (std::log2 (fk / fc));
            const double w = o <= 0.5 - 1.0 / 12.0 ? 1.0 : o >= 0.5 + 1.0 / 12.0 ? 0.0 : 0.5 + 0.5 * std::cos (kPi * (o - (0.5 - 1.0 / 12.0)) * 6.0);
            if (w <= 0.0) continue;
            Y[(size_t) k] = X[(size_t) k] * w;
            Y[(size_t) (n - k)] = std::conj (Y[(size_t) k]);
        }
        f.inverse (Y.data());
        // intensity, without the first and last half second (the filter's wrap-around)
        const int skip = std::min (len / 8, (int) (0.5 * sr));
        const int a0 = skip, a1 = len - skip;
        double sumI = 0.0;
        for (int i = a0; i < a1; ++i) sumI += Y[(size_t) i].real() * Y[(size_t) i].real();
        dbOut[(size_t) b] = sumI > 0.0 ? 10.0 * std::log10 (2.0 * sumI / (a1 - a0)) : -200.0;
        for (int j = 0; j < 2; ++j)
        {
            const double fm = modFreqs()[(size_t) b][(size_t) j];
            // a whole number of the modulation's periods
            const int periods = (int) std::floor ((a1 - a0) * fm / sr);
            const int use = (int) std::floor (periods * sr / fm);
            double re = 0.0, im = 0.0, s = 0.0;
            for (int i = 0; i < use; ++i)
            {
                const double I = Y[(size_t) (a0 + i)].real() * Y[(size_t) (a0 + i)].real();
                const double ph = 2.0 * kPi * fm * (a0 + i) / sr;
                re += I * std::cos (ph);
                im += I * std::sin (ph);
                s += I;
            }
            mOut[(size_t) b][(size_t) j] = s > 0.0 && periods > 0 ? 2.0 * std::sqrt (re * re + im * im) / s : 0.0;
        }
    }
}

inline double transmissionIndex (double m)
{
    m = std::clamp (m, 1e-6, 1.0 - 1e-6);
    const double snr = std::clamp (10.0 * std::log10 (m / (1.0 - m)), -15.0, 15.0);
    return (snr + 15.0) / 30.0;
}
} // namespace detail

// `mic`: the recording at the seat (at least 15 s). `ref`: the generator looped back, the same length (or null: the
// signal's own depth, 0.55, is assumed). `splOffset`: dB SPL at 0 dBFS when the mic is calibrated (NaN: no level
// corrections).
inline Result analyse (const float* mic, int len, double sr, const float* ref = nullptr, double splOffset = NAN)
{
    Result r;
    if (len < (int) (12.0 * sr)) { r.note = "Too short: STIPA needs at least 15 seconds."; return r; }
    std::array<std::array<double, 2>, kBands> mm {}, m0 {};
    std::array<double, kBands> lv {}, lv0 {};
    detail::depths (mic, len, sr, mm, lv);
    bool refOk = false;
    if (ref != nullptr)
    {
        detail::depths (ref, len, sr, m0, lv0);
        refOk = true;
        for (const auto& b : m0) for (double v : b) refOk = refOk && v > 0.3 && v < 0.8;   // it is the STIPA signal
    }
    if (! refOk) for (auto& b : m0) b.fill (kDepth);

    // the band levels, for the corrections (calibrated only)
    const bool cal = std::isfinite (splOffset);
    r.corrected = cal;
    for (int k = 0; k < kBands; ++k) r.levelDb[(size_t) k] = lv[(size_t) k] + (cal ? splOffset : 0.0);
    static const double threshold[kBands] = { 46.0, 27.0, 12.0, 6.5, 7.5, 8.0, 12.0 };   // absolute reception threshold
    for (int k = 0; k < kBands; ++k)
    {
        double f = 1.0;
        if (cal)
        {
            const double I = std::pow (10.0, r.levelDb[(size_t) k] / 10.0);
            const double Irt = std::pow (10.0, threshold[k] / 10.0);
            double Iam = 0.0;
            if (k > 0)
            {
                const double Lb = r.levelDb[(size_t) k - 1];
                const double amdB = Lb < 63.0 ? 0.5 * Lb - 65.0 : Lb < 67.0 ? 1.8 * Lb - 146.9 : Lb < 100.0 ? 0.5 * Lb - 59.8 : -10.0;
                Iam = std::pow (10.0, Lb / 10.0) * std::pow (10.0, amdB / 10.0);
            }
            f = I / (I + Irt + Iam);
        }
        double ti = 0.0;
        for (int j = 0; j < 2; ++j)
        {
            const double mtf = std::clamp (mm[(size_t) k][(size_t) j] / std::max (1e-6, m0[(size_t) k][(size_t) j]), 0.0, 1.0) * f;
            r.m[(size_t) k][(size_t) j] = mtf;
            ti += detail::transmissionIndex (mtf);
        }
        r.mti[(size_t) k] = ti / 2.0;
    }
    static const double alpha[kBands] = { 0.085, 0.127, 0.230, 0.233, 0.309, 0.224, 0.173 };
    static const double beta[kBands - 1] = { 0.085, 0.078, 0.065, 0.011, 0.047, 0.095 };
    double sti = 0.0;
    for (int b = 0; b < kBands; ++b) sti += alpha[b] * r.mti[(size_t) b];
    for (int b = 0; b < kBands - 1; ++b) sti -= beta[b] * std::sqrt (r.mti[(size_t) b] * r.mti[(size_t) b + 1]);
    r.sti = std::clamp (sti, 0.0, 1.0);
    r.valid = true;
    r.note = std::string (refOk ? "Modulation against the reference input." : "Modulation against the STIPA signal's own depth (0.55).")
             + (cal ? " Masking and hearing threshold corrections applied." : " The mic is not calibrated: no level corrections.");
    return r;
}
} // namespace la::stipa
