#pragma once
// Dual-channel FFT transfer function (reference -> measurement mic), the core of system tuning.
// H = Gxy / Gxx and coherence = |Gxy|^2 / (Gxx * Gyy), from overlapped Hann frames with exponential averaging.
// The reference passes through a delay line so the two signals line up (delay compensation).
#include <algorithm>
#include <array>
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
        binFrames.assign (bins, 0);
        frameCount = 0;
        sinceFrame = 0;
        filled = 0;
    }

    void setAverages (int frames) { averages = std::max (1, frames); }
    // Magnitude threshold: a frequency where the reference is quieter than this in a frame is left out of the average
    // for that frame (music leaves gaps in the spectrum; without it they average in as noise). dB, the level a sine
    // at that frequency would read (dBFS); 0 = off.
    void setMagnitudeThreshold (double db)
    {
        const double a = db < 0.0 ? std::pow (10.0, db / 20.0) * n / 4.0 : 0.0;   // a Hann frame's bin for a sine of that peak
        thresholdPower = a * a;
    }
    int getAverages() const { return averages; }

    // Delay applied to the reference, in samples. Changing it restarts the average.
    void setDelaySamples (int d)
    {
        d = std::clamp (d, 0, maxDelaySamples());
        if (d != delay) { delay = d; reset(); }
    }
    int getDelaySamples() const { return delay; }
    int maxDelaySamples() const { return std::max (0, (int) delayLine.size() - 2); }

    // The delay as the tracking found it again on the programme. A change within the dead band (3 samples or
    // 0.25 ms, whichever is more) is left alone, so the average is not disturbed for nothing. A small change (under
    // 5 % of the window) is applied without restarting the average: the accumulated cross-spectrum is a product of
    // the delayed reference's conjugate and the mic, so moving the reference by dt turns each bin by e^(j 2 pi f dt).
    // Returns true when the delay changed.
    bool setDelaySamplesTracked (int d)
    {
        d = std::clamp (d, 0, maxDelaySamples());
        if (std::abs (d - delay) <= trackedDeadBand (sr)) return false;
        const int change = d - delay;
        if (std::abs (change) >= n / 20 || frameCount == 0) { setDelaySamples (d); return true; }
        delay = d;
        for (size_t k = 0; k < gxy.size(); ++k)
            gxy[k] *= std::polar (1.0, 2.0 * kPi * (double) k * (double) change / (double) n);
        return true;
    }
    // The dead band of the tracked delay, in samples: 3 samples or 0.25 ms, whichever is more.
    static int trackedDeadBand (double sampleRate) { return std::max (3, (int) std::ceil (0.25e-3 * sampleRate)); }

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
        for (size_t k = 0; k < gxx.size(); ++k)
        {
            const cplx x = bufX[k], y = bufY[k];
            const double xx = std::norm (x);
            if (thresholdPower > 0.0 && xx < thresholdPower) continue;   // no reference here this time: keep what was there
            const int nb = ++binFrames[k];
            const double alpha = std::max (1.0 / averages, 1.0 / nb);
            const double keep = 1.0 - alpha;
            gxx[k] = keep * gxx[k] + alpha * xx;
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
    std::vector<int> binFrames;   // frames averaged into each bin (the magnitude threshold leaves some out)
    double thresholdPower = 0.0;
    int frameCount = 0;
};

// The transfer function as the analyser runs it: one FFT size, or multi-time-window (MTW): four sizes side by side,
// each used where its window suits (the longest in the lows for resolution, short ones in the highs so the room's
// late reflections do not smear them), blended across a third of an octave at each hand-over.
class TransferAnalyzer
{
public:
    void prepare (double sampleRate, int fftSize, double maxDelaySeconds = 2.0)
    {
        sr = sampleRate;
        n0 = fftSize;
        maxDelay = maxDelaySeconds;
        engines[0].prepare (sr, n0, maxDelay);
        for (int k = 1; k < kWindows; ++k) engines[(size_t) k].prepare (sr, std::max (256, n0 >> shift (k)), maxDelay);
        applyAverages();
        for (auto& e : engines) e.setMagnitudeThreshold (thresholdDb);
    }
    void setMultiWindow (bool on) { if (on != mtw) { mtw = on; reset(); } }
    bool multiWindow() const { return mtw; }
    void reset() { for (auto& e : engines) e.reset(); }
    void setAverages (int frames) { if (frames != averages) { averages = std::max (1, frames); applyAverages(); } }
    void setDelaySamples (int d) { for (auto& e : engines) e.setDelaySamples (d); }
    // As found by the delay tracking: a small move keeps the averages (see TransferEngine).
    bool setDelaySamplesTracked (int d) { bool changed = false; for (auto& e : engines) changed = e.setDelaySamplesTracked (d) || changed; return changed; }
    int getDelaySamples() const { return engines[0].getDelaySamples(); }
    void setMagnitudeThreshold (double db) { thresholdDb = db; for (auto& e : engines) e.setMagnitudeThreshold (db); }
    void push (const float* mic, const float* ref, int count)
    {
        engines[0].push (mic, ref, count);
        if (mtw) for (int k = 1; k < kWindows; ++k) engines[(size_t) k].push (mic, ref, count);
    }
    int frames() const { return engines[0].frames(); }
    void impulse (std::vector<float>& ir) const { engines[0].impulse (ir); }
    const TransferEngine& longest() const { return engines[0]; }

    // Where each window takes over (Hz): about where its bins are half a 1/12-octave band apart.
    double handOver (int k) const { return k <= 0 ? 0.0 : 34.6 * sr / (double) engines[(size_t) k].fftSize(); }

    void banded (const std::vector<double>& freqs, double fraction, std::vector<float>& magDb, std::vector<float>& phaseDeg, std::vector<float>& coherence) const
    {
        engines[0].banded (freqs, fraction, magDb, phaseDeg, coherence);
        if (! mtw) return;
        std::vector<float> m, ph, co;
        for (int k = 1; k < kWindows; ++k)
        {
            if (engines[(size_t) k].frames() == 0) continue;
            engines[(size_t) k].banded (freqs, fraction, m, ph, co);
            const double f0 = handOver (k);
            for (size_t i = 0; i < freqs.size(); ++i)
            {
                // 0 below the hand-over, 1 a sixth of an octave above it, a smooth blend in between
                const double oct = std::log2 (freqs[i] / f0);
                const double w = std::clamp ((oct + 1.0 / 6.0) * 3.0, 0.0, 1.0);
                if (w <= 0.0 || ! std::isfinite (m[i])) continue;
                if (w >= 1.0 || ! std::isfinite (magDb[i])) { magDb[i] = m[i]; phaseDeg[i] = ph[i]; coherence[i] = co[i]; continue; }
                magDb[i] = (float) ((1.0 - w) * magDb[i] + w * m[i]);
                if (std::isfinite (coherence[i]) && std::isfinite (co[i])) coherence[i] = (float) ((1.0 - w) * coherence[i] + w * co[i]);
                if (std::isfinite (phaseDeg[i]) && std::isfinite (ph[i]))
                {
                    const double a = phaseDeg[i] * kPi / 180.0, b = ph[i] * kPi / 180.0;
                    const double re = (1.0 - w) * std::cos (a) + w * std::cos (b), im = (1.0 - w) * std::sin (a) + w * std::sin (b);
                    phaseDeg[i] = (float) (std::atan2 (im, re) * 180.0 / kPi);
                }
            }
        }
    }

private:
    static constexpr int kWindows = 4;
    static int shift (int k) { static const int sh[kWindows] = { 0, 2, 4, 5 }; return sh[k]; }   // N, N/4, N/16, N/32
    void applyAverages()
    {
        // the same averaging time in every window: a short one averages more of its frames
        for (int k = 0; k < kWindows; ++k) engines[(size_t) k].setAverages (std::min (256, averages << shift (k)));
    }
    double sr = 48000.0, maxDelay = 2.0, thresholdDb = 0.0;
    int n0 = 32768, averages = 8;
    bool mtw = false;
    std::array<TransferEngine, kWindows> engines;
};
} // namespace la
