#pragma once
// Built-in target curves, as dB relative to 1 kHz. They are drawn over the transfer function and the RTA,
// lined up with the live measurement's average level between 500 Hz and 2 kHz.
// Only the cinema X-curve is a published standard; the others are common tuning starting points.
#include <cmath>
#include <vector>

namespace la::targets
{
struct Info
{
    const char* name;
    const char* description;
};

inline const std::vector<Info>& list()
{
    static const std::vector<Info> l {
        { "None", "" },
        { "Flat", "Ruler-flat response. A neutral reference for checking EQ moves." },
        { "Speech", "Flat from 150 Hz to 6 kHz, rolled off below 100 Hz for clarity, slight top-end easing. Spoken word and sermons." },
        { "Gentle tilt (-1 dB/oct)", "A steady downward slope across the range. A natural-sounding default for many rooms." },
        { "Worship / live music", "About +6 dB of low end below 100 Hz and a -1 dB/octave slope above 1 kHz. Contemporary worship and bands." },
        { "Concert / high energy", "About +10 dB of low end below 100 Hz and a -1.5 dB/octave slope above 1 kHz. Rock, pop and EDM." },
        { "Cinema X-curve (SMPTE ST 202)", "Flat to 2 kHz, then -3 dB/octave, as in ISO 2969 / SMPTE ST 202 for large rooms." },
        { "Harman-style in-room (approx.)", "Bass shelf of about +6 dB below 100 Hz with a gentle downward tilt. Approximation of the published research target." },
    };
    return l;
}

inline int count() { return (int) list().size(); }

namespace detail
{
inline double lowShelfDb (double f, double fc, double gainDb) { return gainDb / (1.0 + std::pow (f / fc, 2.0)); }
inline double highPassDb (double f, double fc, int order) { return -10.0 * std::log10 (1.0 + std::pow (fc / f, 2.0 * order)); }
// Smooth slope of `dbPerOct` above f0 (softplus corner, about half an octave wide).
inline double slopeAboveDb (double f, double f0, double dbPerOct)
{
    const double x = std::log2 (f / f0);
    const double k = 4.0;
    return dbPerOct * std::log1p (std::exp (k * x)) / k;
}

inline double raw (int id, double f)
{
    switch (id)
    {
        case 1: return 0.0;
        case 2: return highPassDb (f, 100.0, 2) + slopeAboveDb (f, 6000.0, -2.0);
        case 3: return -1.0 * std::log2 (f / 1000.0);
        case 4: return lowShelfDb (f, 100.0, 6.0) + slopeAboveDb (f, 1000.0, -1.0);
        case 5: return lowShelfDb (f, 100.0, 10.0) + slopeAboveDb (f, 1000.0, -1.5);
        case 6: return f <= 2000.0 ? 0.0 : -3.0 * std::log2 (f / 2000.0);
        case 7: return lowShelfDb (f, 100.0, 6.0) - 0.8 * std::log2 (f / 1000.0);
        default: return 0.0;
    }
}
} // namespace detail

// Target level at f, 0 dB at 1 kHz.
inline double db (int id, double f)
{
    if (id <= 0 || id >= count() || f <= 0.0) return 0.0;
    return detail::raw (id, f) - detail::raw (id, 1000.0);
}
} // namespace la::targets
