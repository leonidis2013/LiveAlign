#pragma once
// The analysis thread: takes mic and reference samples from the audio thread through a lock-free FIFO and runs the
// transfer function, the RTAs, the sound level meter and the delay finder away from the audio thread.
// The UI reads the latest results as a Snapshot.
#include <atomic>
#include <mutex>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Settings.h"
#include "Analysis/TransferEngine.h"
#include "Analysis/RtaEngine.h"
#include "Analysis/SplMeter.h"
#include "Analysis/DelayFinder.h"
#include "Analysis/FreqTable.h"

namespace la
{
struct Snapshot
{
    juce::uint64 serial = 0;
    double sampleRate = 48000.0;

    // Transfer function on the log display grid (NaN = no data)
    std::vector<double> tfFreq;
    std::vector<float> tfMag, tfPhase, tfCoh;
    int tfFrames = 0;
    int fftSize = 0;

    // RTA (band centres, levels in dB SPL when calibrated, else dBFS)
    std::vector<double> rtaFreq;
    std::vector<float> rtaMic, rtaRef, rtaPeak;
    bool rtaIsSpl = false;

    // Impulse response, rotated so index irZero is t = 0
    std::vector<float> ir;
    int irZero = 0;

    // Sound level (dB SPL when calibrated, else dBFS)
    float splAS = -200, splAF = -200, splCS = -200, splZS = -200, leqA = -200, leqC = -200, maxAF = -200;
    double leqSeconds = 0.0;
    float rawZSlowDbfs = -200;   // uncalibrated Z slow, used by the calibrate button
    bool splIsSpl = false;
};

struct DelayReport
{
    juce::uint64 serial = 0;
    bool valid = false;
    double ms = 0.0;
    bool inverted = false;
    double confidenceDb = 0.0;
};

class Analyzer : private juce::Thread
{
public:
    explicit Analyzer (Settings& s) : juce::Thread ("LiveAlign analysis"), settings (s)
    {
        micFifo.resize (kFifoSize);
        refFifo.resize (kFifoSize);
        startThread (juce::Thread::Priority::normal);
    }

    ~Analyzer() override { stopThread (2000); }

    // ---- audio thread ----
    void push (const float* mic, const float* ref, int n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 + n2 < n) overflowed = true;
        if (n1 > 0) { std::copy (mic, mic + n1, micFifo.begin() + s1); std::copy (ref, ref + n1, refFifo.begin() + s1); }
        if (n2 > 0) { std::copy (mic + n1, mic + n1 + n2, micFifo.begin() + s2); std::copy (ref + n1, ref + n1 + n2, refFifo.begin() + s2); }
        fifo.finishedWrite (n1 + n2);
    }

    void setSampleRate (double rate) { pendingRate = rate; }

    // ---- message thread ----
    void setCalibration (const FreqTable& t, const juce::String& name)
    {
        {
            const std::lock_guard<std::mutex> lock (calMutex);
            cal = t;
            calName = name;
        }
        ++calVersion;
    }

    void clearCalibration() { setCalibration (FreqTable {}, {}); }

    juce::String getCalibrationName() const
    {
        const std::lock_guard<std::mutex> lock (calMutex);
        return calName;
    }

    FreqTable getCalibration() const
    {
        const std::lock_guard<std::mutex> lock (calMutex);
        return cal;
    }

    // Copies the latest results if they are newer than `serialSeen`. Returns true when copied.
    bool readSnapshot (Snapshot& out, juce::uint64 serialSeen) const
    {
        const std::lock_guard<std::mutex> lock (snapMutex);
        if (published.serial == serialSeen) return false;
        out = published;
        return true;
    }

    DelayReport readDelayReport() const
    {
        const std::lock_guard<std::mutex> lock (snapMutex);
        return delayReport;
    }

    bool hasOverflowed() { return overflowed.exchange (false); }

private:
    static constexpr int kFifoSize = 1 << 18;
    static constexpr int kChunk = 4096;

    void run() override
    {
        std::vector<float> mic (kChunk), ref (kChunk);
        while (! threadShouldExit())
        {
            wait (10);
            configure();

            for (;;)
            {
                int s1, n1, s2, n2;
                fifo.prepareToRead (kChunk, s1, n1, s2, n2);
                const int got = n1 + n2;
                if (got == 0) break;
                std::copy (micFifo.begin() + s1, micFifo.begin() + s1 + n1, mic.begin());
                std::copy (refFifo.begin() + s1, refFifo.begin() + s1 + n1, ref.begin());
                if (n2 > 0)
                {
                    std::copy (micFifo.begin() + s2, micFifo.begin() + s2 + n2, mic.begin() + n1);
                    std::copy (refFifo.begin() + s2, refFifo.begin() + s2 + n2, ref.begin() + n1);
                }
                fifo.finishedRead (got);
                process (mic.data(), ref.data(), got);
                if (threadShouldExit()) return;
            }

            handleRequests();

            const auto now = juce::Time::getMillisecondCounter();
            if (dirty && now - lastPublish >= 40)
            {
                publish();
                lastPublish = now;
                dirty = false;
            }
        }
    }

    void configure()
    {
        const double newRate = pendingRate.load();
        const int newFft = settings.fftSize.load();
        const int newSpeed = settings.rtaSpeed.load();
        const int cv = calVersion.load();
        const bool rateChanged = std::abs (newRate - sr) > 0.5;

        if (rateChanged || newFft != tfSize)
        {
            sr = newRate;
            tfSize = newFft;
            tf.prepare (sr, tfSize, 2.0);
            grid = logGrid (10.0, std::min (24000.0, sr * 0.48), 48);
        }
        if (rateChanged)
        {
            const int rtaN = sr > 60000.0 ? 32768 : 16384;
            rtaMic.prepare (sr, rtaN, rtaN / 8);
            rtaRef.prepare (sr, rtaN, rtaN / 8);
            spl.prepare (sr);
            rawLen = nextPowerOfTwo ((int) (sr * 2.7));
            rawMic.assign ((size_t) rawLen, 0.0f);
            rawRef.assign ((size_t) rawLen, 0.0f);
            rawPos = 0;
            appliedCal = -1;
            dirty = true;
        }
        if (newSpeed != rtaSpeed || rateChanged)
        {
            rtaSpeed = newSpeed;
            static const double taus[] = { 0.25, 1.0, 3.0 };
            rtaMic.setTimeConstant (taus[juce::jlimit (0, 2, rtaSpeed)]);
            rtaRef.setTimeConstant (taus[juce::jlimit (0, 2, rtaSpeed)]);
        }
        if (cv != appliedCal)
        {
            appliedCal = cv;
            FreqTable c = getCalibration();
            calLocal = c;
            rtaMic.setBinGains (binGains (c, rtaMic.fftSize()));
            spl.setMicCorrection (binGains (c, sr > 60000.0 ? 8192 : 4096));
            dirty = true;
        }

        tf.setAverages (settings.averages.load());
        tf.setDelaySamples ((int) std::lround (settings.delayMs.load() * 0.001 * sr));
    }

    std::vector<double> binGains (const FreqTable& c, int n) const
    {
        if (c.empty()) return {};
        std::vector<double> g ((size_t) n / 2 + 1, 1.0);
        for (size_t k = 1; k < g.size(); ++k)
            g[k] = std::pow (10.0, -c.dbAt (k * sr / n) / 10.0);
        g[0] = g[1];
        return g;
    }

    void process (const float* mic, const float* ref, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            rawMic[(size_t) rawPos] = mic[i];
            rawRef[(size_t) rawPos] = ref[i];
            rawPos = (rawPos + 1) & (rawLen - 1);
        }

        spl.push (mic, n);
        if (settings.running.load())
        {
            tf.push (mic, ref, n);
            rtaMic.push (mic, n);
            rtaRef.push (ref, n);
        }
        dirty = true;
    }

    void handleRequests()
    {
        if (settings.requestResetAverages.exchange (false)) { tf.reset(); rtaMic.reset(); rtaRef.reset(); dirty = true; }
        if (settings.requestResetLeq.exchange (false)) { spl.resetLeq(); dirty = true; }
        if (settings.requestClearPeak.exchange (false)) { rtaMic.clearPeak(); dirty = true; }
        if (settings.requestFindDelay.exchange (false))
        {
            std::vector<float> m ((size_t) rawLen), r ((size_t) rawLen);
            for (int i = 0; i < rawLen; ++i)
            {
                const size_t idx = (size_t) ((rawPos + i) & (rawLen - 1));
                m[(size_t) i] = rawMic[idx];
                r[(size_t) i] = rawRef[idx];
            }
            const auto res = findDelay (r.data(), m.data(), rawLen, rawLen / 2, 0.0);
            DelayReport rep;
            rep.valid = res.valid;
            rep.ms = res.samples * 1000.0 / sr;
            rep.inverted = res.inverted;
            rep.confidenceDb = res.peakToNoiseDb;
            if (rep.valid)
                settings.delayMs = (float) juce::jlimit (0.0, 2000.0, rep.ms);
            const std::lock_guard<std::mutex> lock (snapMutex);
            rep.serial = delayReport.serial + 1;
            delayReport = rep;
        }
    }

    void publish()
    {
        Snapshot s;
        s.sampleRate = sr;
        s.fftSize = tf.fftSize();
        s.tfFrames = tf.frames();

        // Transfer function, corrected for the mic's own response
        const int smooth = juce::jmax (1, settings.smoothing.load());
        s.tfFreq = grid;
        tf.banded (grid, 1.0 / smooth, s.tfMag, s.tfPhase, s.tfCoh);
        if (! calLocal.empty())
        {
            for (size_t i = 0; i < grid.size(); ++i)
            {
                if (std::isnan (s.tfMag[i])) continue;
                s.tfMag[i] -= (float) calLocal.dbAt (grid[i]);
                if (calLocal.hasPhase())
                {
                    float p = s.tfPhase[i] - (float) calLocal.phaseAt (grid[i]);
                    while (p > 180.0f) p -= 360.0f;
                    while (p < -180.0f) p += 360.0f;
                    s.tfPhase[i] = p;
                }
            }
        }

        // RTA
        const bool calibrated = settings.splCalibrated.load();
        const float offset = calibrated ? settings.splOffset.load() : 0.0f;
        s.rtaIsSpl = calibrated;
        const int bands = juce::jmax (1, settings.rtaBands.load());
        s.rtaFreq = octaveBandCentres (bands, 20.0, std::min (20000.0, sr * 0.45));
        std::vector<double> p;
        auto toDb = [offset] (const std::vector<double>& v, std::vector<float>& out)
        {
            out.resize (v.size());
            for (size_t i = 0; i < v.size(); ++i) out[i] = (float) powerToDbfs (v[i]) + offset;
        };
        PowerSpectrum::bands (rtaMic.averaged(), rtaMic.binHz(), s.rtaFreq, 1.0 / bands, p);
        toDb (p, s.rtaMic);
        PowerSpectrum::bands (rtaRef.averaged(), rtaRef.binHz(), s.rtaFreq, 1.0 / bands, p);
        toDb (p, s.rtaRef);
        PowerSpectrum::bands (rtaMic.peakHold(), rtaMic.binHz(), s.rtaFreq, 1.0 / bands, p);
        toDb (p, s.rtaPeak);

        // Impulse response: show from -5 ms
        std::vector<float> ir;
        tf.impulse (ir);
        const int n = (int) ir.size();
        const int pre = juce::jmin (n / 4, (int) (0.005 * sr));
        s.ir.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            s.ir[(size_t) i] = ir[(size_t) ((i - pre + n) % n)];
        s.irZero = pre;

        // Sound level
        s.splIsSpl = calibrated;
        s.splAS = (float) spl.slowDb (SplMeter::A) + offset;
        s.splAF = (float) spl.fastDb (SplMeter::A) + offset;
        s.splCS = (float) spl.slowDb (SplMeter::C) + offset;
        s.splZS = (float) spl.slowDb (SplMeter::Z) + offset;
        s.leqA = (float) spl.leqDb (SplMeter::A) + offset;
        s.leqC = (float) spl.leqDb (SplMeter::C) + offset;
        s.maxAF = (float) spl.maxFastDb (SplMeter::A) + offset;
        s.leqSeconds = spl.leqSeconds();
        s.rawZSlowDbfs = (float) spl.slowDb (SplMeter::Z);

        const std::lock_guard<std::mutex> lock (snapMutex);
        s.serial = published.serial + 1;
        published = std::move (s);
    }

    Settings& settings;

    juce::AbstractFifo fifo { kFifoSize };
    std::vector<float> micFifo, refFifo;
    std::atomic<bool> overflowed { false };
    std::atomic<double> pendingRate { 48000.0 };

    double sr = 0.0;
    int tfSize = 0, rtaSpeed = -1;
    TransferEngine tf;
    PowerSpectrum rtaMic, rtaRef;
    SplMeter spl;
    std::vector<double> grid;

    std::vector<float> rawMic, rawRef;
    int rawLen = 0, rawPos = 0;

    mutable std::mutex calMutex;
    FreqTable cal, calLocal;
    juce::String calName;
    std::atomic<int> calVersion { 0 };
    int appliedCal = -1;

    mutable std::mutex snapMutex;
    Snapshot published;
    DelayReport delayReport;
    bool dirty = true;
    juce::uint32 lastPublish = 0;
};
} // namespace la
