#pragma once
// Dual-channel FFT transfer function (reference -> measurement mic), the core of system tuning.
// H = Gxy / Gxx and coherence = |Gxy|^2 / (Gxx * Gyy), from overlapped Hann frames with exponential averaging.
// The reference passes through a delay line so the two signals line up (delay compensation).
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>
#include "Fft.h"
#include "Banding.h"

namespace la
{
class TransferEngine
{
public:
    // maxDelaySeconds bounds the delay compensation line.
    void prepare (double sampleRate, int fftSize, double maxDelaySeconds = 2.0)
    {
        sr = sampleRate;
        n = isPowerOfTwo (fftSize) ? fftSize : nextPowerOfTwo (fftSize);
        hop = n / 4;
        fft.setSize (n);
        window = hannWindow (n);
        micRing.assign ((size_t) n, 0.0f);
        refRing.assign ((size_t) n, 0.0f);
        ringPos = 0;
        const int maxDelay = (int) std::ceil (maxDelaySeconds * sr) + 1;
        delayLine.assign ((size_t) nextPowerOfTwo (maxDelay + 1), 0.0f);
        delayMask = (int) delayLine.size() - 1;
        delayWrite = 0;
        bufX.resize ((size_t) n);
        bufY.resize ((size_t) n);
        reset();
    }

    void reset()
    {
        const size_t bins = (size_t) n / 2 + 1;
        gxx.assign (bins, 0.0);
        gyy.assign (bins, 0.0);
        gxy.assign (bins, cplx {});
        frameCount = 0;
        sinceFrame = 0;
        filled = 0;
    }

    void setAverages (int frames) { averages = std::max (1, frames); }
    int getAverages() const { return averages; }

    // Delay applied to the reference, in samples. Changing it restarts the average.
    void setDelaySamples (int d)
    {
        d = std::clamp (d, 0, maxDelaySamples());
        if (d != delay) { delay = d; reset(); }
    }
    int getDelaySamples() const { return delay; }
    int maxDelaySamples() const { return std::max (0, (int) delayLine.size() - 2); }

    void push (const float* mic, const float* ref, int count)
    {
        for (int i = 0; i < count; ++i)
        {
            delayLine[(size_t) delayWrite] = ref[i];
            const float delayedRef = delayLine[(size_t) ((delayWrite - delay) & delayMask)];
            delayWrite = (delayWrite + 1) & delayMask;

            micRing[(size_t) ringPos] = mic[i];
            refRing[(size_t) ringPos] = delayedRef;
            ringPos = (ringPos + 1) % n;
            if (filled < n) ++filled;

            if (++sinceFrame >= hop && filled >= n)
            {
                sinceFrame = 0;
                processFrame();
            }
        }
    }

    int frames() const { return frameCount; }
    int fftSize() const { return n; }
    double sampleRate() const { return sr; }
    double binHz() const { return sr / n; }
    const std::vector<double>& getGxx() const { return gxx; }
    const std::vector<double>& getGyy() const { return gyy; }
    const std::vector<cplx>& getGxy() const { return gxy; }

    // Smoothed result on an arbitrary frequency grid. fraction is the band width in octaves (1/24 -> 0.0417).
    // Points without reference energy come back as NaN.
    void banded (const std::vector<double>& freqs, double fraction,
                 std::vector<float>& magDb, std::vector<float>& phaseDeg, std::vector<float>& coherence) const
    {
        const size_t m = freqs.size();
        magDb.assign (m, std::numeric_limits<float>::quiet_NaN());
        phaseDeg.assign (m, std::numeric_limits<float>::quiet_NaN());
        coherence.assign (m, std::numeric_limits<float>::quiet_NaN());
        if (frameCount == 0) return;

        std::vector<double> re (gxy.size()), im (gxy.size());
        for (size_t k = 0; k < gxy.size(); ++k) { re[k] = gxy[k].real(); im[k] = gxy[k].imag(); }

        BinIntegrator ixx, iyy, ire, iim;
        ixx.build (gxx, binHz());
        iyy.build (gyy, binHz());
        ire.build (re, binHz());
        iim.build (im, binHz());

        double maxXX = 0.0;
        for (double v : gxx) maxXX = std::max (maxXX, v);
        const double floorXX = maxXX * 1e-12 + 1e-30;

        for (size_t i = 0; i < m; ++i)
        {
            double lo, hi;
            bandEdges (freqs[i], fraction, binHz(), lo, hi);
            const double sxx = ixx.integrate (lo, hi);
            const double syy = iyy.integrate (lo, hi);
            const cplx sxy (ire.integrate (lo, hi), iim.integrate (lo, hi));
            if (sxx <= floorXX * (hi - lo) / binHz()) continue;

            const cplx h = sxy / sxx;
            const double mag = std::abs (h);
            magDb[i] = (float) (20.0 * std::log10 (std::max (mag, 1e-12)));
            phaseDeg[i] = (float) (std::arg (h) * 180.0 / kPi);
            coherence[i] = syy > 0.0 ? (float) std::clamp (std::norm (sxy) / (sxx * syy), 0.0, 1.0) : 0.0f;
        }
    }

    // Impulse response (length = FFT size, circular: negative times sit at the end of the buffer).
    void impulse (std::vector<float>& ir) const
    {
        ir.assign ((size_t) n, 0.0f);
        if (frameCount == 0) return;

        double meanXX = 0.0;
        for (double v : gxx) meanXX += v;
        meanXX /= (double) gxx.size();
        const double reg = meanXX * 1e-4 + 1e-30;

        std::vector<cplx> h ((size_t) n);
        const int half = n / 2;
        for (int k = 0; k <= half; ++k)
        {
            const double xx = gxx[(size_t) k];
            // Tikhonov-style regularisation keeps bins without reference energy from blowing up.
            h[(size_t) k] = gxy[(size_t) k] * (xx / (xx * xx + reg * reg));
        }
        h[0] = cplx (h[0].real(), 0.0);
        h[(size_t) half] = cplx (h[(size_t) half].real(), 0.0);
        for (int k = 1; k < half; ++k)
            h[(size_t) (n - k)] = std::conj (h[(size_t) k]);

        fft.inverse (h.data());
        for (int i = 0; i < n; ++i)
            ir[(size_t) i] = (float) h[(size_t) i].real();
    }

private:
    void processFrame()
    {
        for (int i = 0; i < n; ++i)
        {
            const size_t idx = (size_t) ((ringPos + i) % n);
            const double w = window[(size_t) i];
            bufX[(size_t) i] = cplx (refRing[idx] * w, 0.0);
            bufY[(size_t) i] = cplx (micRing[idx] * w, 0.0);
        }
        fft.forward (bufX.data());
        fft.forward (bufY.data());

        ++frameCount;
        const double alpha = std::max (1.0 / averages, 1.0 / frameCount);
        const double keep = 1.0 - alpha;
        for (size_t k = 0; k < gxx.size(); ++k)
        {
            const cplx x = bufX[k], y = bufY[k];
            gxx[k] = keep * gxx[k] + alpha * std::norm (x);
            gyy[k] = keep * gyy[k] + alpha * std::norm (y);
            gxy[k] = keep * gxy[k] + alpha * (std::conj (x) * y);
        }
    }

    double sr = 48000.0;
    int n = 0, hop = 0, averages = 8, delay = 0;
    Fft fft;
    std::vector<double> window;
    std::vector<float> micRing, refRing, delayLine;
    int ringPos = 0, filled = 0, sinceFrame = 0, delayWrite = 0, delayMask = 0;
    std::vector<cplx> bufX, bufY;
    std::vector<double> gxx, gyy;
    std::vector<cplx> gxy;
    int frameCount = 0;
};
} // namespace la
