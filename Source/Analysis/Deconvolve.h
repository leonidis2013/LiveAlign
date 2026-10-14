// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The impulse response of a long recording: what the mic heard, divided by what was sent (the reference), over the
// whole recording at once. With a sweep this gives a long, clean impulse response (seconds, with a wide dynamic
// range) for reverberation time; the live transfer function only covers its analysis window. Plain C++.
#include <algorithm>
#include <cmath>
#include <vector>
#include "Fft.h"

namespace la::deconv
{
// `ref` and `mic` are `n` samples recorded together. Returns `keepSeconds` of impulse response starting a little
// before its arrival (the arrival is searched in the first `maxDelaySeconds`), or empty when there is no signal.
// Frequencies the reference did not cover are left out rather than divided by nothing.
inline std::vector<float> impulse (const float* ref, const float* mic, int n, double fs, double keepSeconds, double maxDelaySeconds = 1.0)
{
    std::vector<float> out;
    if (n < 1024 || fs <= 0.0) return out;
    int len = 1;
    while (len < 2 * n) len <<= 1;
    Fft fft (len);
    std::vector<cplx> x ((size_t) len, cplx (0.0, 0.0)), y ((size_t) len, cplx (0.0, 0.0));
    for (int i = 0; i < n; ++i) { x[(size_t) i] = cplx (ref[i], 0.0); y[(size_t) i] = cplx (mic[i], 0.0); }
    fft.forward (x.data());
    fft.forward (y.data());
    double peak = 0.0;
    for (const auto& v : x) peak = std::max (peak, std::norm (v));
    if (peak <= 1e-20) return out;
    const double floor = peak * 1e-6;   // 60 dB below the strongest part of the reference
    for (int k = 0; k < len; ++k)
    {
        const double p = std::norm (x[(size_t) k]);
        y[(size_t) k] = p > floor ? y[(size_t) k] * std::conj (x[(size_t) k]) / (p + floor * 0.01) : cplx (0.0, 0.0);
    }
    fft.inverse (y.data());   // y is now the impulse response, zero lag at index 0
    const int search = std::min (len / 2, (int) (maxDelaySeconds * fs));
    int at = 0;
    for (int i = 1; i < search; ++i) if (std::abs (y[(size_t) i].real()) > std::abs (y[(size_t) at].real())) at = i;
    const int start = std::max (0, at - (int) (0.005 * fs));
    const int keep = std::min ((int) (keepSeconds * fs), len / 2 - start);
    out.resize ((size_t) std::max (0, keep));
    for (int i = 0; i < keep; ++i) out[(size_t) i] = (float) y[(size_t) (start + i)].real();
    return out;
}
} // namespace la::deconv
