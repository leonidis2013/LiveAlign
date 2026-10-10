#pragma once
// Finds how far the measurement mic lags the reference: generalised cross-correlation with partial phase transform
// (PHAT-beta), which gives a sharp peak with full-range signals and still works when the speaker only covers part
// of the band (a sub). Also reports polarity (sign of the peak) and a peak-to-noise confidence.
#include <algorithm>
#include <cmath>
#include <vector>
#include "Fft.h"

namespace la
{
struct DelayResult
{
    bool valid = false;
    double samples = 0.0;      // positive: mic arrives after the reference
    double peakToNoiseDb = 0.0;
    bool inverted = false;     // peak is negative: polarity looks inverted
};

inline DelayResult findDelay (const float* ref, const float* mic, int count, double maxLagSamples, double maxLeadSamples = 0.0,
                              double beta = 0.75)
{
    DelayResult r;
    if (count < 64) return r;

    const int n = nextPowerOfTwo (2 * count);
    Fft fft (n);
    std::vector<cplx> x ((size_t) n), y ((size_t) n);
    double ex = 0.0, ey = 0.0;
    for (int i = 0; i < count; ++i)
    {
        x[(size_t) i] = cplx (ref[i], 0.0);
        y[(size_t) i] = cplx (mic[i], 0.0);
        ex += (double) ref[i] * ref[i];
        ey += (double) mic[i] * mic[i];
    }
    if (ex <= 1e-12 || ey <= 1e-12) return r;

    fft.forward (x.data());
    fft.forward (y.data());

    double maxMag = 0.0;
    for (int k = 0; k < n; ++k)
    {
        x[(size_t) k] = std::conj (x[(size_t) k]) * y[(size_t) k];
        maxMag = std::max (maxMag, std::abs (x[(size_t) k]));
    }
    const double eps = maxMag * 1e-9 + 1e-30;
    for (int k = 0; k < n; ++k)
    {
        const double m = std::abs (x[(size_t) k]);
        x[(size_t) k] /= std::pow (m + eps, beta);
    }
    fft.inverse (x.data());

    const int maxLag = std::clamp ((int) maxLagSamples, 0, count - 1);
    const int maxLead = std::clamp ((int) maxLeadSamples, 0, count - 1);

    auto at = [&] (int lag) { return x[(size_t) ((lag % n + n) % n)].real(); };

    int best = 0;
    double bestAbs = -1.0;
    double sumSq = 0.0;
    int cnt = 0;
    for (int lag = -maxLead; lag <= maxLag; ++lag)
    {
        const double v = at (lag);
        sumSq += v * v;
        ++cnt;
        if (std::abs (v) > bestAbs) { bestAbs = std::abs (v); best = lag; }
    }
    if (cnt == 0 || bestAbs <= 0.0) return r;

    // Noise floor: RMS of the correlation away from the peak.
    double noise = 0.0;
    int nn = 0;
    for (int lag = -maxLead; lag <= maxLag; ++lag)
    {
        if (std::abs (lag - best) < 64) continue;
        const double v = at (lag);
        noise += v * v;
        ++nn;
    }
    noise = nn > 0 ? std::sqrt (noise / nn) : 1e-30;

    // Parabolic interpolation on |r| around the peak for a sub-sample estimate.
    const double ym = std::abs (at (best - 1)), y0 = bestAbs, yp = std::abs (at (best + 1));
    const double den = ym - 2.0 * y0 + yp;
    double frac = std::abs (den) > 1e-30 ? 0.5 * (ym - yp) / den : 0.0;
    frac = std::clamp (frac, -0.5, 0.5);

    r.samples = best + frac;
    r.inverted = at (best) < 0.0;
    r.peakToNoiseDb = 20.0 * std::log10 (bestAbs / std::max (noise, 1e-30));
    r.valid = r.peakToNoiseDb >= 12.0;
    return r;
}
} // namespace la
