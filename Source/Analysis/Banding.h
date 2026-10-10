#pragma once
// Frequency grids and fractional-octave banding of FFT bin data.
#include <algorithm>
#include <cmath>
#include <vector>

namespace la
{
// Log-spaced display grid: fMin * 2^(i / pointsPerOctave) up to fMax.
inline std::vector<double> logGrid (double fMin, double fMax, int pointsPerOctave)
{
    std::vector<double> f;
    if (fMin <= 0.0 || fMax <= fMin || pointsPerOctave <= 0) return f;
    const int count = (int) std::floor (std::log2 (fMax / fMin) * pointsPerOctave) + 1;
    f.reserve ((size_t) count);
    for (int i = 0; i < count; ++i)
        f.push_back (fMin * std::pow (2.0, (double) i / pointsPerOctave));
    return f;
}

// Band centres for 1/N-octave analysis, base-2 and anchored at 1 kHz (1/3 octave gives 1000, 1260, 1587 ...).
inline std::vector<double> octaveBandCentres (int bandsPerOctave, double fMin, double fMax)
{
    std::vector<double> f;
    if (bandsPerOctave <= 0) return f;
    // A band is included when its nominal frequency is in range (19.7 Hz counts as the "20 Hz" band).
    const int kLo = (int) std::ceil (std::log2 (fMin / 1000.0) * bandsPerOctave - 0.5);
    const int kHi = (int) std::floor (std::log2 (fMax / 1000.0) * bandsPerOctave + 0.5);
    for (int k = kLo; k <= kHi; ++k)
        f.push_back (1000.0 * std::pow (2.0, (double) k / bandsPerOctave));
    return f;
}

// Nominal label for a band centre: the familiar ISO-style rounding (31.5, 63, 125, 1k6 ...).
inline double nominalFrequency (double f)
{
    const double mag = std::pow (10.0, std::floor (std::log10 (f)));
    const double m = f / mag;
    static const double steps[] = { 1.0, 1.25, 1.6, 2.0, 2.5, 3.15, 4.0, 5.0, 6.3, 8.0, 10.0 };
    double best = steps[0];
    for (double s : steps)
        if (std::abs (std::log (m / s)) < std::abs (std::log (m / best))) best = s;
    return best * mag;
}

// Treats each FFT bin k as a constant density over [k - 0.5, k + 0.5) * binHz, so a band of any width (narrower than a bin
// included) integrates smoothly. Several quantities are integrated together with one set of prefix sums each.
class BinIntegrator
{
public:
    void build (const std::vector<double>& values, double binWidthHz)
    {
        binHz = binWidthHz;
        v = &values;
        prefix.assign (values.size() + 1, 0.0);
        for (size_t k = 0; k < values.size(); ++k)
            prefix[k + 1] = prefix[k] + values[k];
    }

    // Sum of bin values over [fLo, fHi) in bin-equivalents (fractional at the edges).
    double integrate (double fLo, double fHi) const
    {
        if (v == nullptr || v->empty() || fHi <= fLo) return 0.0;
        return cum (fHi / binHz + 0.5) - cum (fLo / binHz + 0.5);
    }

private:
    double cum (double p) const
    {
        const double nb = (double) v->size();
        p = std::clamp (p, 0.0, nb);
        const size_t i = (size_t) std::floor (p);
        if (i >= v->size()) return prefix.back();
        return prefix[i] + (p - (double) i) * (*v)[i];
    }

    double binHz = 1.0;
    const std::vector<double>* v = nullptr;
    std::vector<double> prefix;
};

// Edges of a 1/N-octave band around f. The band is never narrower than minWidthHz (usually one FFT bin).
inline void bandEdges (double f, double fraction, double minWidthHz, double& lo, double& hi)
{
    const double k = std::pow (2.0, 0.5 * fraction);
    lo = f / k;
    hi = f * k;
    if (hi - lo < minWidthHz)
    {
        lo = f - 0.5 * minWidthHz;
        hi = f + 0.5 * minWidthHz;
        if (lo < 0.0) { hi -= lo; lo = 0.0; }
    }
}
} // namespace la
