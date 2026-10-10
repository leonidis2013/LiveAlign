#pragma once
// Single-channel real-time analyser: overlapped Hann FFT frames, exponential time averaging,
// power per bin scaled so the bins of a frame sum to the signal's mean square (Parseval).
#include <algorithm>
#include <cmath>
#include <vector>
#include "Fft.h"
#include "Banding.h"

namespace la
{
class PowerSpectrum
{
public:
    // tauSeconds: averaging time constant (0 = no averaging).
    void prepare (double sampleRate, int fftSize, int hopSize)
    {
        sr = sampleRate;
        n = isPowerOfTwo (fftSize) ? fftSize : nextPowerOfTwo (fftSize);
        hop = std::clamp (hopSize, 1, n);
        fft.setSize (n);
        window = hannWindow (n);
        double w2 = 0.0;
        for (double w : window) w2 += w * w;
        scale = 1.0 / ((double) n * w2);
        ring.assign ((size_t) n, 0.0f);
        buf.resize ((size_t) n);
        frame.assign ((size_t) n / 2 + 1, 0.0);
        reset();
    }

    void reset()
    {
        avg.assign ((size_t) n / 2 + 1, 0.0);
        peak.assign ((size_t) n / 2 + 1, 0.0);
        pos = 0; filled = 0; since = 0; frames = 0;
    }

    void setTimeConstant (double tauSeconds) { tau = std::max (0.0, tauSeconds); }

    // Optional per-bin power gain (mic calibration and/or weighting). Empty = none.
    void setBinGains (std::vector<double> g) { gains = std::move (g); }

    // Returns the number of new frames computed.
    int push (const float* x, int count)
    {
        int made = 0;
        for (int i = 0; i < count; ++i)
        {
            ring[(size_t) pos] = x[i];
            pos = (pos + 1) % n;
            if (filled < n) ++filled;
            if (++since >= hop && filled >= n)
            {
                since = 0;
                processFrame();
                ++made;
            }
        }
        return made;
    }

    int frameCount() const { return frames; }
    int fftSize() const { return n; }
    int hopSize() const { return hop; }
    double binHz() const { return sr / n; }
    double sampleRate() const { return sr; }
    const std::vector<double>& averaged() const { return avg; }
    const std::vector<double>& lastFrame() const { return frame; }
    const std::vector<double>& peakHold() const { return peak; }
    void clearPeak() { std::fill (peak.begin(), peak.end(), 0.0); }

    // Band powers (mean square) for 1/N-octave bands centred on `centres`.
    static void bands (const std::vector<double>& power, double binHz, const std::vector<double>& centres, double fraction,
                       std::vector<double>& out)
    {
        BinIntegrator integ;
        integ.build (power, binHz);
        out.resize (centres.size());
        for (size_t i = 0; i < centres.size(); ++i)
        {
            double lo, hi;
            bandEdges (centres[i], fraction, 0.0, lo, hi);
            out[i] = integ.integrate (lo, hi);
        }
    }

private:
    void processFrame()
    {
        for (int i = 0; i < n; ++i)
            buf[(size_t) i] = cplx (ring[(size_t) ((pos + i) % n)] * window[(size_t) i], 0.0);
        fft.forward (buf.data());

        ++frames;
        const double hopSec = hop / sr;
        double alpha = tau > 0.0 ? 1.0 - std::exp (-hopSec / tau) : 1.0;
        alpha = std::max (alpha, 1.0 / frames);
        const size_t bins = avg.size();
        for (size_t k = 0; k < bins; ++k)
        {
            const bool edge = (k == 0 || k == bins - 1);
            double p = std::norm (buf[k]) * scale * (edge ? 1.0 : 2.0);
            if (! gains.empty() && k < gains.size()) p *= gains[k];
            frame[k] = p;
            avg[k] += alpha * (p - avg[k]);
            peak[k] = std::max (peak[k], avg[k]);
        }
    }

    double sr = 48000.0, scale = 1.0, tau = 1.0;
    int n = 0, hop = 1, pos = 0, filled = 0, since = 0, frames = 0;
    Fft fft;
    std::vector<double> window, frame, avg, peak, gains;
    std::vector<float> ring;
    std::vector<cplx> buf;
};

// AES17 convention: a full-scale sine reads 0 dBFS (mean square 0.5).
inline double powerToDbfs (double meanSquare) { return 10.0 * std::log10 (std::max (2.0 * meanSquare, 1e-20)); }
} // namespace la
