// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The analysis thread: takes the mics, the reference and the generator signal from the audio thread through a
// lock-free FIFO and runs, for every mic, the transfer function, the RTA and the sound level meter, plus the delay
// finder, loopback check and noise floor measurement, away from the audio thread.
// The UI reads the latest results as a Snapshot.
#include <array>
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
// Results for one mic (levels in dB SPL when that mic is calibrated, else dBFS).
struct MicResult
{
    std::vector<float> tfMag, tfPhase, tfCoh;
    std::vector<float> rta, rtaPeak;
    int tfFrames = 0;
    float splAS = -200, splAF = -200, splCS = -200, splZS = -200, leqA = -200, leqC = -200, maxAF = -200;
    float rawZSlowDbfs = -200;   // uncalibrated Z slow, used by the calibrate button
    bool isSpl = false;
};

struct Snapshot
{
    juce::uint64 serial = 0;
    double sampleRate = 48000.0;
    int fftSize = 0;
    int micCount = 1;
    std::vector<double> tfFreq, rtaFreq;
    std::vector<MicResult> mics;
    std::vector<float> rtaRef;

    // ---- the selected mic (or the average of all mics), which the graphs show ----
    int shownMic = 0;             // kAverageMic for the average
    std::vector<float> tfMag, tfPhase, tfCoh;   // phase is NaN for the average
    int tfFrames = 0;
    std::vector<float> rtaMic, rtaPeak;
    bool rtaIsSpl = false;
    std::vector<float> ir;        // impulse response of the selected mic (mic 1 for the average)
    int irZero = 0;
    float splAS = -200, splAF = -200, splCS = -200, splZS = -200, leqA = -200, leqC = -200, maxAF = -200;
    double leqSeconds = 0.0;
    float rawZSlowDbfs = -200;
    bool splIsSpl = false;

    // Waterfall: the newest unaveraged spectrum of the selected mic (or the average) on a 1/24-octave grid.
    std::vector<double> wfFreq;
    std::vector<float> wfLine;
    juce::uint64 wfFrame = 0;     // changes when there is a new spectrum frame
};

// Loopback check: the reference input compared with the generator signal that was sent out.
struct LoopbackReport
{
    juce::uint64 serial = 0;
    bool valid = false;          // the generator was found in the reference input
    double latencyMs = 0.0;      // output-to-input round trip of the interface
    bool inverted = false;
    float refLevelDbfs = -200.0f;
    float gainDb = 0.0f;         // reference level relative to the generator
};

// Background noise of the room, measured on every mic with the generator off.
struct NoiseReport
{
    juce::uint64 serial = 0;
    bool measuring = false;
    float progress = 0.0f;       // 0..1 while measuring
    bool done = false;
    int micCount = 0;
    std::array<float, kMaxMics> laeq {}, lceq {};
    std::array<bool, kMaxMics> isSpl {};
    std::vector<double> bandFreq;                    // 1/3-octave bands
    std::vector<std::vector<float>> bandDb;          // per mic
};

struct DelayReport
{
    juce::uint64 serial = 0;
    int micCount = 0;
    std::array<bool, kMaxMics> valid {};
    std::array<double, kMaxMics> ms {};
    std::array<bool, kMaxMics> inverted {};
    std::array<double, kMaxMics> confidenceDb {};
};

class Analyzer : private juce::Thread
{
public:
    explicit Analyzer (Settings& s) : juce::Thread ("LiveAlign analysis"), settings (s)
    {
        for (auto& c : fifoData) c.resize (kFifoSize);
        startThread (juce::Thread::Priority::normal);
    }

    ~Analyzer() override { stopThread (2000); }

    // ---- audio thread ----
    // mics: kMaxMics pointers (silence for unused ones). gen: the generator signal as sent to the outputs.
    void push (const float* const* mics, const float* ref, const float* gen, int n)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        if (n1 + n2 < n) { overflowed = true; ++overflows; }
        for (int c = 0; c < kChannels; ++c)
        {
            const float* src = c < kMaxMics ? mics[c] : (c == kMaxMics ? ref : gen);
            if (n1 > 0) std::copy (src, src + n1, fifoData[(size_t) c].begin() + s1);
            if (n2 > 0) std::copy (src + n1, src + n1 + n2, fifoData[(size_t) c].begin() + s2);
        }
        fifo.finishedWrite (n1 + n2);
        written += (juce::uint64) (n1 + n2);
    }

    // Samples handed to the analysis, and samples it has analysed (they differ while it catches up).
    juce::uint64 samplesWritten() const { return written.load(); }
    juce::uint64 samplesAnalysed() const { return analysed.load(); }
    // Times audio had to be dropped because the analysis fell behind (a measurement spanning one is not continuous).
    juce::uint32 overflowCount() const { return overflows.load(); }

    void setSampleRate (double rate) { pendingRate = rate; }

    // ---- message thread ----
    void requestLoopbackCheck() { loopbackRequested = true; }
    void requestNoiseMeasurement (double seconds) { noiseSeconds = seconds; noiseRequested = true; }

    void setCalibration (int mic, const FreqTable& t, const juce::String& name)
    {
        if (mic < 0 || mic >= kMaxMics) return;
        {
            const std::lock_guard<std::mutex> lock (calMutex);
            cal[(size_t) mic] = t;
            calName[(size_t) mic] = name;
        }
        ++calVersion;
    }

    void clearCalibration (int mic) { setCalibration (mic, FreqTable {}, {}); }

    juce::String getCalibrationName (int mic) const
    {
        const std::lock_guard<std::mutex> lock (calMutex);
        return mic >= 0 && mic < kMaxMics ? calName[(size_t) mic] : juce::String();
    }

    FreqTable getCalibration (int mic) const
    {
        const std::lock_guard<std::mutex> lock (calMutex);
        return mic >= 0 && mic < kMaxMics ? cal[(size_t) mic] : FreqTable {};
    }

    // Copies the latest results if they are newer than `serialSeen`. Returns true when copied.
    bool readSnapshot (Snapshot& out, juce::uint64 serialSeen) const
    {
        const std::lock_guard<std::mutex> lock (snapMutex);
        if (published.serial == serialSeen) return false;
        out = published;
        return true;
    }

    DelayReport readDelayReport() const { const std::lock_guard<std::mutex> lock (snapMutex); return delayReport; }
    LoopbackReport readLoopbackReport() const { const std::lock_guard<std::mutex> lock (snapMutex); return loopbackReport; }
    NoiseReport readNoiseReport() const { const std::lock_guard<std::mutex> lock (snapMutex); return noiseReport; }

    bool hasOverflowed() { return overflowed.exchange (false); }

private:
    static constexpr int kFifoSize = 1 << 17;
    static constexpr int kChunk = 4096;
    static constexpr int kChannels = kMaxMics + 2;   // mics, reference, generator

    struct MicChain
    {
        TransferEngine tf;
        PowerSpectrum rta, noiseRta;
        SplMeter spl, noiseMeter;
        std::vector<float> raw;
        FreqTable cal;
    };

    void run() override
    {
        std::vector<std::vector<float>> buf ((size_t) kChannels, std::vector<float> (kChunk));
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
                for (int c = 0; c < kChannels; ++c)
                {
                    auto& src = fifoData[(size_t) c];
                    std::copy (src.begin() + s1, src.begin() + s1 + n1, buf[(size_t) c].begin());
                    if (n2 > 0) std::copy (src.begin() + s2, src.begin() + s2 + n2, buf[(size_t) c].begin() + n1);
                }
                fifo.finishedRead (got);
                process (buf, got);
                analysed += (juce::uint64) got;
                // also while catching up, so requests and the display are never starved
                handleRequests();
                publishIfDue();
                if (threadShouldExit()) return;
            }

            handleRequests();
            publishIfDue();
        }
    }

    int splFftSize() const { return sr > 60000.0 ? 8192 : 4096; }

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
            for (auto& m : chains) m.tf.prepare (sr, tfSize, 2.0);
            grid = logGrid (10.0, std::min (24000.0, sr * 0.48), 48);
            wfGrid = logGrid (20.0, std::min (20000.0, sr * 0.45), 24);
        }
        if (rateChanged)
        {
            const int rtaN = sr > 60000.0 ? 32768 : 16384;
            rawLen = nextPowerOfTwo ((int) (sr * 2.7));
            for (auto& m : chains)
            {
                m.rta.prepare (sr, rtaN, rtaN / 8);
                m.spl.prepare (sr);
                m.noiseMeter.prepare (sr);
                m.noiseRta.prepare (sr, rtaN, rtaN / 8);
                m.noiseRta.setTimeConstant (1000.0);
                m.raw.assign ((size_t) rawLen, 0.0f);
            }
            rtaRef.prepare (sr, rtaN, rtaN / 8);
            rawRef.assign ((size_t) rawLen, 0.0f);
            rawGen.assign ((size_t) rawLen, 0.0f);
            rawPos = 0;
            appliedCal = -1;
            dirty = true;
        }
        if (newSpeed != rtaSpeed || rateChanged)
        {
            rtaSpeed = newSpeed;
            static const double taus[] = { 0.25, 1.0, 3.0 };
            const double tau = taus[juce::jlimit (0, 2, rtaSpeed)];
            for (auto& m : chains) m.rta.setTimeConstant (tau);
            rtaRef.setTimeConstant (tau);
        }
        if (cv != appliedCal)
        {
            appliedCal = cv;
            for (int i = 0; i < kMaxMics; ++i)
            {
                auto& m = chains[(size_t) i];
                m.cal = getCalibration (i);
                m.rta.setBinGains (binGains (m.cal, m.rta.fftSize()));
                m.spl.setMicCorrection (binGains (m.cal, splFftSize()));
                m.noiseMeter.setMicCorrection (binGains (m.cal, splFftSize()));
                m.noiseRta.setBinGains (binGains (m.cal, m.noiseRta.fftSize()));
            }
            dirty = true;
        }

        for (int i = 0; i < kMaxMics; ++i)
        {
            auto& m = chains[(size_t) i];
            m.tf.setAverages (settings.averages.load());
            m.tf.setDelaySamples ((int) std::lround (settings.micDelayMs[i].load() * 0.001 * sr));
        }
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

    void process (const std::vector<std::vector<float>>& buf, int n)
    {
        const int count = juce::jlimit (1, kMaxMics, settings.micCount.load());
        const float* ref = buf[(size_t) kMaxMics].data();
        const float* gen = buf[(size_t) kMaxMics + 1].data();

        for (int i = 0; i < n; ++i)
        {
            const size_t pos = (size_t) ((rawPos + i) & (rawLen - 1));
            for (int m = 0; m < count; ++m) chains[(size_t) m].raw[pos] = buf[(size_t) m][(size_t) i];
            rawRef[pos] = ref[i];
            rawGen[pos] = gen[i];
        }
        rawPos = (rawPos + n) & (rawLen - 1);

        const bool running = settings.running.load();
        for (int m = 0; m < count; ++m)
        {
            auto& c = chains[(size_t) m];
            const float* mic = buf[(size_t) m].data();
            c.spl.push (mic, n);
            if (noiseActive) { c.noiseMeter.push (mic, n); c.noiseRta.push (mic, n); }
            if (running) { c.tf.push (mic, ref, n); c.rta.push (mic, n); }
        }
        if (running) rtaRef.push (ref, n);

        if (noiseActive)
        {
            noiseDone += n;
            const double total = noiseSeconds * sr;
            const std::lock_guard<std::mutex> lock (snapMutex);
            noiseReport.progress = (float) juce::jlimit (0.0, 1.0, noiseDone / total);
            if (noiseDone >= total) finishNoise (count);
        }
        dirty = true;
    }

    float offsetFor (int m) const { return settings.micSplCalibrated[m].load() ? settings.micSplOffset[m].load() : 0.0f; }

    void finishNoise (int count)   // called with snapMutex held
    {
        noiseActive = false;
        noiseReport.measuring = false;
        noiseReport.done = true;
        noiseReport.progress = 1.0f;
        noiseReport.micCount = count;
        noiseReport.bandFreq = octaveBandCentres (3, 20.0, std::min (20000.0, sr * 0.45));
        noiseReport.bandDb.assign ((size_t) count, {});
        for (int m = 0; m < count; ++m)
        {
            auto& c = chains[(size_t) m];
            const float offset = offsetFor (m);
            noiseReport.isSpl[(size_t) m] = settings.micSplCalibrated[m].load();
            noiseReport.laeq[(size_t) m] = (float) c.noiseMeter.leqDb (SplMeter::A) + offset;
            noiseReport.lceq[(size_t) m] = (float) c.noiseMeter.leqDb (SplMeter::C) + offset;
            std::vector<double> p;
            PowerSpectrum::bands (c.noiseRta.averaged(), c.noiseRta.binHz(), noiseReport.bandFreq, 1.0 / 3.0, p);
            auto& out = noiseReport.bandDb[(size_t) m];
            out.resize (p.size());
            for (size_t i = 0; i < p.size(); ++i) out[i] = (float) powerToDbfs (p[i]) + offset;
        }
        noiseReport.serial++;
    }

    // The last `n` samples of a raw ring buffer, oldest first.
    std::vector<float> lastSamples (const std::vector<float>& ring, int n) const
    {
        std::vector<float> out ((size_t) n);
        for (int i = 0; i < n; ++i) out[(size_t) i] = ring[(size_t) ((rawPos - n + i + rawLen) & (rawLen - 1))];
        return out;
    }

    void handleRequests()
    {
        const int count = juce::jlimit (1, kMaxMics, settings.micCount.load());

        if (noiseRequested.exchange (false))
        {
            for (auto& c : chains) { c.noiseMeter.reset(); c.noiseRta.reset(); }
            noiseDone = 0;
            noiseActive = true;
            const std::lock_guard<std::mutex> lock (snapMutex);
            noiseReport.measuring = true;
            noiseReport.done = false;
            noiseReport.progress = 0.0f;
            noiseReport.serial++;
        }

        if (loopbackRequested.exchange (false))
        {
            // The most recent ~1.4 s (the check runs the generator for about two seconds).
            const int n = rawLen / 2;
            const auto g = lastSamples (rawGen, n), r = lastSamples (rawRef, n);
            double eg = 0.0, er = 0.0;
            for (int i = 0; i < n; ++i) { eg += (double) g[(size_t) i] * g[(size_t) i]; er += (double) r[(size_t) i] * r[(size_t) i]; }
            LoopbackReport rep;
            rep.refLevelDbfs = (float) powerToDbfs (er / n);
            if (eg > 0.0 && er > 0.0)
            {
                const auto res = findDelay (g.data(), r.data(), n, 0.25 * sr, 0.0);
                rep.valid = res.valid;
                rep.latencyMs = res.samples * 1000.0 / sr;
                rep.inverted = res.inverted;
                rep.gainDb = (float) (10.0 * std::log10 (er / eg));
            }
            const std::lock_guard<std::mutex> lock (snapMutex);
            rep.serial = loopbackReport.serial + 1;
            loopbackReport = rep;
        }

        if (settings.requestResetAverages.exchange (false))
        {
            for (auto& c : chains) { c.tf.reset(); c.rta.reset(); }
            rtaRef.reset();
            dirty = true;
        }
        if (settings.requestResetLeq.exchange (false)) { for (auto& c : chains) c.spl.resetLeq(); dirty = true; }
        if (settings.requestClearPeak.exchange (false)) { for (auto& c : chains) c.rta.clearPeak(); dirty = true; }

        // Find delay: every mic at once, each gets its own compensation.
        if (settings.requestFindDelay.exchange (false))
        {
            const auto r = lastSamples (rawRef, rawLen);
            DelayReport rep;
            rep.micCount = count;
            for (int m = 0; m < count; ++m)
            {
                const auto mic = lastSamples (chains[(size_t) m].raw, rawLen);
                const auto res = findDelay (r.data(), mic.data(), rawLen, rawLen / 2, 0.0);
                rep.valid[(size_t) m] = res.valid;
                rep.ms[(size_t) m] = res.samples * 1000.0 / sr;
                rep.inverted[(size_t) m] = res.inverted;
                rep.confidenceDb[(size_t) m] = res.peakToNoiseDb;
                if (res.valid) settings.micDelayMs[m] = (float) juce::jlimit (0.0, 2000.0, rep.ms[(size_t) m]);
            }
            const std::lock_guard<std::mutex> lock (snapMutex);
            rep.serial = delayReport.serial + 1;
            delayReport = rep;
        }
    }

    void publishIfDue()
    {
        const auto now = juce::Time::getMillisecondCounter();
        if (dirty && now - lastPublish >= 40)
        {
            publish();
            lastPublish = now;
            dirty = false;
        }
    }

    void publish()
    {
        const int count = juce::jlimit (1, kMaxMics, settings.micCount.load());
        const int bands = juce::jmax (1, settings.rtaBands.load());
        const int smooth = juce::jmax (1, settings.smoothing.load());

        Snapshot s;
        s.sampleRate = sr;
        s.fftSize = tfSize;
        s.micCount = count;
        s.tfFreq = grid;
        s.rtaFreq = octaveBandCentres (bands, 20.0, std::min (20000.0, sr * 0.45));
        s.mics.resize ((size_t) count);

        std::vector<double> p;
        auto toDb = [] (const std::vector<double>& v, float offset, std::vector<float>& out)
        {
            out.resize (v.size());
            for (size_t i = 0; i < v.size(); ++i) out[i] = (float) powerToDbfs (v[i]) + offset;
        };

        for (int m = 0; m < count; ++m)
        {
            auto& c = chains[(size_t) m];
            auto& r = s.mics[(size_t) m];
            r.tfFrames = c.tf.frames();
            c.tf.banded (grid, 1.0 / smooth, r.tfMag, r.tfPhase, r.tfCoh);
            if (! c.cal.empty())
            {
                for (size_t i = 0; i < grid.size(); ++i)
                {
                    if (std::isnan (r.tfMag[i])) continue;
                    r.tfMag[i] -= (float) c.cal.dbAt (grid[i]);
                    if (c.cal.hasPhase())
                    {
                        float ph = r.tfPhase[i] - (float) c.cal.phaseAt (grid[i]);
                        while (ph > 180.0f) ph -= 360.0f;
                        while (ph < -180.0f) ph += 360.0f;
                        r.tfPhase[i] = ph;
                    }
                }
            }

            const float offset = offsetFor (m);
            r.isSpl = settings.micSplCalibrated[m].load();
            PowerSpectrum::bands (c.rta.averaged(), c.rta.binHz(), s.rtaFreq, 1.0 / bands, p);
            toDb (p, offset, r.rta);
            PowerSpectrum::bands (c.rta.peakHold(), c.rta.binHz(), s.rtaFreq, 1.0 / bands, p);
            toDb (p, offset, r.rtaPeak);

            r.splAS = (float) c.spl.slowDb (SplMeter::A) + offset;
            r.splAF = (float) c.spl.fastDb (SplMeter::A) + offset;
            r.splCS = (float) c.spl.slowDb (SplMeter::C) + offset;
            r.splZS = (float) c.spl.slowDb (SplMeter::Z) + offset;
            r.leqA = (float) c.spl.leqDb (SplMeter::A) + offset;
            r.leqC = (float) c.spl.leqDb (SplMeter::C) + offset;
            r.maxAF = (float) c.spl.maxFastDb (SplMeter::A) + offset;
            r.rawZSlowDbfs = (float) c.spl.slowDb (SplMeter::Z);
        }
        PowerSpectrum::bands (rtaRef.averaged(), rtaRef.binHz(), s.rtaFreq, 1.0 / bands, p);
        toDb (p, 0.0f, s.rtaRef);
        s.leqSeconds = chains[0].spl.leqSeconds();

        // The selected mic, or the power average of all mics.
        const int sel = settings.selectedMic.load();
        const bool average = sel >= kAverageMic && count > 1;
        const int one = average ? 0 : juce::jlimit (0, count - 1, sel);
        s.shownMic = average ? kAverageMic : one;
        if (! average)
        {
            const auto& r = s.mics[(size_t) one];
            s.tfMag = r.tfMag; s.tfPhase = r.tfPhase; s.tfCoh = r.tfCoh; s.tfFrames = r.tfFrames;
            s.rtaMic = r.rta; s.rtaPeak = r.rtaPeak; s.rtaIsSpl = r.isSpl;
            s.splAS = r.splAS; s.splAF = r.splAF; s.splCS = r.splCS; s.splZS = r.splZS;
            s.leqA = r.leqA; s.leqC = r.leqC; s.maxAF = r.maxAF; s.rawZSlowDbfs = r.rawZSlowDbfs; s.splIsSpl = r.isSpl;
        }
        else
        {
            averageInto (s, count);
        }

        // Waterfall line: the latest frame, not averaged
        {
            s.wfFreq = wfGrid;
            std::vector<double> sum (wfGrid.size(), 0.0);
            const int first = average ? 0 : one, last = average ? count - 1 : one;
            for (int m = first; m <= last; ++m)
            {
                auto& c = chains[(size_t) m];
                PowerSpectrum::bands (c.rta.lastFrame(), c.rta.binHz(), wfGrid, 1.0 / 24.0, p);
                const double gain = std::pow (10.0, offsetFor (m) / 10.0);
                for (size_t i = 0; i < p.size(); ++i) sum[i] += p[i] * gain;
            }
            s.wfLine.resize (wfGrid.size());
            const double n = (double) (last - first + 1);
            for (size_t i = 0; i < sum.size(); ++i) s.wfLine[i] = (float) powerToDbfs (sum[i] / n);
            s.wfFrame = (juce::uint64) chains[(size_t) first].rta.frameCount();
        }

        // Impulse response: show from -5 ms
        std::vector<float> ir;
        chains[(size_t) one].tf.impulse (ir);
        const int n = (int) ir.size();
        const int pre = juce::jmin (n / 4, (int) (0.005 * sr));
        s.ir.resize ((size_t) n);
        for (int i = 0; i < n; ++i) s.ir[(size_t) i] = ir[(size_t) ((i - pre + n) % n)];
        s.irZero = pre;

        const std::lock_guard<std::mutex> lock (snapMutex);
        s.serial = published.serial + 1;
        published = std::move (s);
    }

    // Spatial average: power average of magnitude and level, mean coherence. Phase is not averaged.
    static void averageInto (Snapshot& s, int count)
    {
        auto powAvg = [count] (auto get, size_t size, std::vector<float>& out)
        {
            out.assign (size, std::numeric_limits<float>::quiet_NaN());
            for (size_t i = 0; i < size; ++i)
            {
                double sum = 0.0;
                int used = 0;
                for (int m = 0; m < count; ++m)
                {
                    const float v = get (m, i);
                    if (std::isfinite (v)) { sum += std::pow (10.0, v / 10.0); ++used; }
                }
                if (used > 0) out[i] = (float) (10.0 * std::log10 (sum / used));
            }
        };
        const auto& m0 = s.mics[0];
        powAvg ([&s] (int m, size_t i) { return s.mics[(size_t) m].tfMag[i]; }, m0.tfMag.size(), s.tfMag);
        powAvg ([&s] (int m, size_t i) { return s.mics[(size_t) m].rta[i]; }, m0.rta.size(), s.rtaMic);
        powAvg ([&s] (int m, size_t i) { return s.mics[(size_t) m].rtaPeak[i]; }, m0.rtaPeak.size(), s.rtaPeak);
        s.tfPhase.assign (m0.tfMag.size(), std::numeric_limits<float>::quiet_NaN());
        s.tfCoh.assign (m0.tfCoh.size(), 0.0f);
        for (size_t i = 0; i < s.tfCoh.size(); ++i)
        {
            double sum = 0.0;
            int used = 0;
            for (int m = 0; m < count; ++m)
                if (std::isfinite (s.mics[(size_t) m].tfCoh[i])) { sum += s.mics[(size_t) m].tfCoh[i]; ++used; }
            s.tfCoh[i] = used > 0 ? (float) (sum / used) : std::numeric_limits<float>::quiet_NaN();
        }
        s.tfFrames = m0.tfFrames;

        bool allSpl = true;
        for (int m = 0; m < count; ++m) allSpl = allSpl && s.mics[(size_t) m].isSpl;
        s.rtaIsSpl = s.splIsSpl = allSpl;
        auto levelAvg = [&s, count] (float MicResult::* field)
        {
            double sum = 0.0;
            for (int m = 0; m < count; ++m) sum += std::pow (10.0, s.mics[(size_t) m].*field / 10.0);
            return (float) (10.0 * std::log10 (sum / count));
        };
        s.splAS = levelAvg (&MicResult::splAS);
        s.splAF = levelAvg (&MicResult::splAF);
        s.splCS = levelAvg (&MicResult::splCS);
        s.splZS = levelAvg (&MicResult::splZS);
        s.leqA = levelAvg (&MicResult::leqA);
        s.leqC = levelAvg (&MicResult::leqC);
        s.maxAF = levelAvg (&MicResult::maxAF);
        s.rawZSlowDbfs = s.mics[0].rawZSlowDbfs;
    }

    Settings& settings;

    juce::AbstractFifo fifo { kFifoSize };
    std::array<std::vector<float>, kChannels> fifoData;
    std::atomic<bool> overflowed { false };
    std::atomic<juce::uint64> written { 0 }, analysed { 0 };
    std::atomic<juce::uint32> overflows { 0 };
    std::atomic<double> pendingRate { 48000.0 };

    double sr = 0.0;
    int tfSize = 0, rtaSpeed = -1;
    std::array<MicChain, kMaxMics> chains;
    PowerSpectrum rtaRef;
    std::vector<double> grid, wfGrid;

    std::vector<float> rawRef, rawGen;
    int rawLen = 0, rawPos = 0;
    std::atomic<bool> loopbackRequested { false }, noiseRequested { false };
    std::atomic<double> noiseSeconds { 5.0 };
    bool noiseActive = false;
    double noiseDone = 0.0;

    mutable std::mutex calMutex;
    std::array<FreqTable, kMaxMics> cal;
    std::array<juce::String, kMaxMics> calName;
    std::atomic<int> calVersion { 0 };
    int appliedCal = -1;

    mutable std::mutex snapMutex;
    Snapshot published;
    DelayReport delayReport;
    LoopbackReport loopbackReport;
    NoiseReport noiseReport;
    bool dirty = true;
    juce::uint32 lastPublish = 0;
};
} // namespace la
