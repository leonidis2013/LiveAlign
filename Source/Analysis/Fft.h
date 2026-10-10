#pragma once
// Plain C++ radix-2 FFT (no JUCE), so the analysis code can be unit tested on its own.
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>

namespace la
{
using cplx = std::complex<double>;

constexpr double kPi = 3.14159265358979323846;

inline bool isPowerOfTwo (int n) { return n > 0 && (n & (n - 1)) == 0; }

inline int nextPowerOfTwo (int n)
{
    int p = 1;
    while (p < n) p <<= 1;
    return p;
}

class Fft
{
public:
    Fft() = default;
    explicit Fft (int size) { setSize (size); }

    void setSize (int size)
    {
        if (size == n) return;
        n = isPowerOfTwo (size) ? size : nextPowerOfTwo (size);
        twiddle.resize ((size_t) n / 2);
        for (int k = 0; k < n / 2; ++k)
            twiddle[(size_t) k] = std::polar (1.0, -2.0 * kPi * k / n);

        rev.resize ((size_t) n);
        int bits = 0;
        while ((1 << bits) < n) ++bits;
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < bits; ++b)
                if (i & (1 << b)) r |= 1 << (bits - 1 - b);
            rev[(size_t) i] = r;
        }
    }

    int size() const { return n; }

    // In-place forward transform (no scaling).
    void forward (cplx* d) const { transform (d, false); }
    // In-place inverse transform, scaled by 1/N.
    void inverse (cplx* d) const { transform (d, true); }

private:
    void transform (cplx* d, bool inv) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int j = rev[(size_t) i];
            if (i < j) std::swap (d[i], d[j]);
        }

        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int k = 0; k < half; ++k)
                {
                    cplx w = twiddle[(size_t) (k * step)];
                    if (inv) w = std::conj (w);
                    const cplx u = d[i + k];
                    const cplx v = d[i + k + half] * w;
                    d[i + k] = u + v;
                    d[i + k + half] = u - v;
                }
            }
        }

        if (inv)
        {
            const double s = 1.0 / n;
            for (int i = 0; i < n; ++i) d[i] *= s;
        }
    }

    int n = 0;
    std::vector<cplx> twiddle;
    std::vector<int> rev;
};

// Periodic Hann window, the usual choice for overlapped spectral averaging.
inline std::vector<double> hannWindow (int n)
{
    std::vector<double> w ((size_t) n);
    for (int i = 0; i < n; ++i)
        w[(size_t) i] = 0.5 - 0.5 * std::cos (2.0 * kPi * i / n);
    return w;
}
} // namespace la
