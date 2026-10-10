#pragma once
// Sound level meter built on short FFT frames: A, C and Z weighting applied exactly per bin (IEC 61672 curves),
// Fast (125 ms) and Slow (1 s) exponential time weighting, and Leq since the last reset.
// Levels are dBFS (AES17) plus the SPL calibration offset.
#include <cmath>
#include <vector>
#include "RtaEngine.h"

namespace la
{
// IEC 61672-1 analytic weighting curves, in dB.
inline double aWeightDb (double f)
{
    if (f <= 0.0) return -200.0;
    const double f2 = f * f;
    const double num = 12194.0 * 12194.0 * f2 * f2;
    const double den = (f2 + 20.6 * 20.6) * std::sqrt ((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0);
    return 20.0 * std::log10 (num / den) + 2.0;
}

inline double cWeightDb (double f)
{
    if (f <= 0.0) return -200.0;
    const double f2 = f * f;
    const double num = 12194.0 * 12194.0 * f2;
    const double den = (f2 + 20.6 * 20.6) * (f2 + 12194.0 * 12194.0);
    return 20.0 * std::log10 (num / den) + 0.06;
}

class SplMeter
{
public:
    enum Weight { A = 0, C = 1, Z = 2 };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int n = sr > 60000.0 ? 8192 : 4096;
        spec.prepare (sr, n, n / 4);
        spec.setTimeConstant (0.0);
        const size_t bins = (size_t) n / 2 + 1;
        for (int w = 0; w < 3; ++w) weight[w].assign (bins, 0.0);
        const double binHz = sr / n;
        for (size_t k = 0; k < bins; ++k)
        {
            const double f = k * binHz;
            const bool inBand = f >= 10.0 && f <= 22400.0;   // Z: band-limited 10 Hz to 22.4 kHz
            weight[A][k] = std::pow (10.0, aWeightDb (f) / 10.0);
            weight[C][k] = std::pow (10.0, cWeightDb (f) / 10.0);
            weight[Z][k] = inBand ? 1.0 : 0.0;
        }
        reset();
    }

    void reset()
    {
        for (int w = 0; w < 3; ++w) { fast[w] = slow[w] = 0.0; energy[w] = 0.0; maxFast[w] = 0.0; }
        seconds = 0.0;
        spec.reset();
    }

    void resetLeq()
    {
        for (int w = 0; w < 3; ++w) { energy[w] = 0.0; maxFast[w] = 0.0; }
        seconds = 0.0;
    }

    // Per-bin power correction for the measurement mic (from its calibration file). Empty = flat.
    void setMicCorrection (std::vector<double> powerGain) { micGain = std::move (powerGain); }

    void push (const float* x, int count)
    {
        // Feed in hop-sized pieces so every frame updates the time weighting.
        int done = 0;
        while (done < count)
        {
            const int chunk = std::min (count - done, spec.hopSize());
            if (spec.push (x + done, chunk) > 0) frameDone();
            done += chunk;
        }
    }

    // dBFS (AES17) for the given weighting; add the calibration offset for dB SPL.
    double fastDb (Weight w) const { return powerToDbfs (fast[w]); }
    double slowDb (Weight w) const { return powerToDbfs (slow[w]); }
    double leqDb (Weight w) const { return seconds > 0.0 ? powerToDbfs (energy[w] / seconds) : -200.0; }
    double maxFastDb (Weight w) const { return powerToDbfs (maxFast[w]); }
    double leqSeconds() const { return seconds; }

private:
    void frameDone()
    {
        const auto& p = spec.lastFrame();
        const double hopSec = spec.hopSize() / sr;
        const double aF = 1.0 - std::exp (-hopSec / 0.125);
        const double aS = 1.0 - std::exp (-hopSec / 1.0);
        for (int w = 0; w < 3; ++w)
        {
            double sum = 0.0;
            for (size_t k = 0; k < p.size(); ++k)
            {
                double v = p[k] * weight[w][k];
                if (! micGain.empty() && k < micGain.size()) v *= micGain[k];
                sum += v;
            }
            fast[w] += aF * (sum - fast[w]);
            slow[w] += aS * (sum - slow[w]);
            energy[w] += sum * hopSec;
            maxFast[w] = std::max (maxFast[w], fast[w]);
        }
        seconds += hopSec;
    }

    double sr = 48000.0;
    PowerSpectrum spec;
    std::vector<double> weight[3], micGain;
    double fast[3] {}, slow[3] {}, energy[3] {}, maxFast[3] {};
    double seconds = 0.0;
};
} // namespace la
