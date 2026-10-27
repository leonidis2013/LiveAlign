#pragma once
// Sound level meter built on short FFT frames: A, C and Z weighting applied exactly per bin (IEC 61672 curves),
// Fast (125 ms) and Slow (1 s) exponential time weighting, and Leq since the last reset. Peak levels (LCpeak, LZpeak:
// the largest instantaneous value, no time weighting) need every sample, so they run in the time domain: the C
// weighting as a filter (IEC 61672's poles at 20.6 Hz and 12.2 kHz, two of each), the Z peak on the signal itself.
// Levels are dBFS (AES17) plus the SPL calibration offset.
#include <array>
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

// The C weighting as a time-domain filter: two first-order high-passes at 20.6 Hz and two first-order low-passes at
// 12194 Hz (bilinear, not pre-warped: exact to 0.05 dB up to 4 kHz, and within the class 1 tolerance of IEC 61672
// above: -0.6 dB at 8 kHz, -2.7 dB at 12.5 kHz at 48 kHz), 0 dB at 1 kHz.
class CWeightFilter
{
public:
    void prepare (double sr)
    {
        const double K = 2.0 * sr;
        const double wl = 2.0 * kPiC * 20.598997, wh = 2.0 * kPiC * 12194.217;
        hp = { K / (K + wl), -K / (K + wl), (wl - K) / (K + wl) };
        lp = { wh / (K + wh), wh / (K + wh), (wh - K) / (K + wh) };
        // the gain at 1 kHz, to normalise
        const double w = 2.0 * kPiC * 1000.0 / sr;
        auto mag = [w] (const Sec& c)
        {
            const double re = c.b0 + c.b1 * std::cos (w), im = -c.b1 * std::sin (w);
            const double dr = 1.0 + c.a1 * std::cos (w), di = -c.a1 * std::sin (w);
            return std::sqrt ((re * re + im * im) / (dr * dr + di * di));
        };
        const double g = mag (hp) * mag (hp) * mag (lp) * mag (lp);
        norm = g > 0.0 ? 1.0 / g : 1.0;
        reset();
    }
    void reset() { for (auto& z : state) z = {}; }
    double process (double x)
    {
        x = run (hp, state[0], x);
        x = run (hp, state[1], x);
        x = run (lp, state[2], x);
        x = run (lp, state[3], x);
        return x * norm;
    }

private:
    static constexpr double kPiC = 3.14159265358979323846;
    struct Sec { double b0 = 1.0, b1 = 0.0, a1 = 0.0; };
    struct Z { double x1 = 0.0, y1 = 0.0; };
    static double run (const Sec& c, Z& z, double x)
    {
        const double y = c.b0 * x + c.b1 * z.x1 - c.a1 * z.y1;
        z.x1 = x;
        z.y1 = y;
        return y;
    }
    Sec hp, lp;
    std::array<Z, 4> state {};
    double norm = 1.0;
};

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
        cFilter.prepare (sr);
        peakBlock = (int) sr;   // the recent peak: the largest in the last one to two seconds
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
        cFilter.reset();
        resetPeaks();
        resetInterval();
    }

    void resetLeq()
    {
        for (int w = 0; w < 3; ++w) { energy[w] = 0.0; maxFast[w] = 0.0; }
        seconds = 0.0;
        resetPeaks();
    }

    // Per-bin power correction for the measurement mic (from its calibration file). Empty = flat.
    void setMicCorrection (std::vector<double> powerGain) { micGain = std::move (powerGain); }

    void push (const float* x, int count)
    {
        // peaks: every sample, C weighted and unweighted
        for (int i = 0; i < count; ++i)
        {
            const double c = std::abs (cFilter.process (x[i]));
            const double z = std::abs ((double) x[i]);
            if (c > blockPk[0]) blockPk[0] = c;
            if (z > blockPk[1]) blockPk[1] = z;
            if (c > ivPeakC) ivPeakC = c;
            if (++inBlock >= peakBlock)
            {
                for (int k = 0; k < 2; ++k) { lastPk[k] = blockPk[k]; maxPk[k] = std::max (maxPk[k], blockPk[k]); blockPk[k] = 0.0; }
                inBlock = 0;
            }
        }
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
    // Peak levels in the same units (a full-scale sine's peak reads +3.01 dB above its level, as on a sound level
    // meter): the recent one (the last second or two) and the largest since the Leq was reset. C or Z only.
    double peakDb (Weight w) const { const int k = w == Z ? 1 : 0; return peakToDb (std::max (blockPk[k], lastPk[k])); }
    double maxPeakDb (Weight w) const { const int k = w == Z ? 1 : 0; return peakToDb (std::max ({ maxPk[k], blockPk[k], lastPk[k] })); }

    // ---- the logging interval (IEC 61672-1): LAeq and LCeq integrate the squared signal over the interval, LAFmax is
    // the running maximum of the Fast meter and LCpeak the largest instantaneous value, every sample. Separate from
    // the Leq since the reset above, so the log's points do not move when the user resets the meter.
    struct Interval
    {
        double laeq = -200.0, lceq = -200.0, lafMax = -200.0, lcPeak = -200.0;   // dBFS (plus the calibration offset for dB SPL)
        double seconds = 0.0;                                                     // audio in the interval
    };
    double intervalSeconds() const { return ivSeconds; }
    // The interval's values, and the start of the next one.
    Interval takeInterval()
    {
        Interval iv;
        iv.seconds = ivSeconds;
        if (ivSeconds > 0.0)
        {
            iv.laeq = powerToDbfs (ivEnergy[A] / ivSeconds);
            iv.lceq = powerToDbfs (ivEnergy[C] / ivSeconds);
            iv.lafMax = powerToDbfs (ivMaxFastA);
            iv.lcPeak = peakToDb (ivPeakC);
        }
        resetInterval();
        return iv;
    }

private:
    void resetInterval() { for (int w = 0; w < 3; ++w) ivEnergy[w] = 0.0; ivSeconds = 0.0; ivMaxFastA = 0.0; ivPeakC = 0.0; }
    static double peakToDb (double pk) { return pk > 1e-10 ? 20.0 * std::log10 (pk) + 3.0103 : -200.0; }
    void resetPeaks() { for (int k = 0; k < 2; ++k) { blockPk[k] = lastPk[k] = maxPk[k] = 0.0; } inBlock = 0; }

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
            ivEnergy[w] += sum * hopSec;
            maxFast[w] = std::max (maxFast[w], fast[w]);
        }
        ivMaxFastA = std::max (ivMaxFastA, fast[A]);
        seconds += hopSec;
        ivSeconds += hopSec;
    }

    double sr = 48000.0;
    PowerSpectrum spec;
    std::vector<double> weight[3], micGain;
    double fast[3] {}, slow[3] {}, energy[3] {}, maxFast[3] {};
    double seconds = 0.0;
    CWeightFilter cFilter;
    double blockPk[2] {}, lastPk[2] {}, maxPk[2] {};   // C, Z
    int inBlock = 0, peakBlock = 48000;
    double ivEnergy[3] {}, ivSeconds = 0.0, ivMaxFastA = 0.0, ivPeakC = 0.0;   // the logging interval
};
} // namespace la
