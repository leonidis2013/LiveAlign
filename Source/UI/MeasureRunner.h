// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include "../Clock.h"
// One measurement of whatever plays from one output, at one mic: pink noise for a few seconds, the arrival found, then
// the transfer function averaged timed to that arrival. Level balance and the quick re-check use it; Speaker response
// and Room EQ have their own (they measure several mics, or keep the timing locked). Message thread only: the page
// that owns it passes it the analysis snapshot and calls tick() from its timer.
#include <functional>
#include "../PluginProcessor.h"

namespace la::ui
{
class MeasureRunner
{
public:
    struct Result
    {
        bool ok = false;
        juce::String error;
        FreqTable tf;                 // dB, phase and coherence (extra), timed to the arrival
        bool hasArrival = false;
        double arrivalMs = 0.0;
        bool peakKnown = false, peakInverted = false;
        int mic = 0;
    };

    explicit MeasureRunner (LiveAlignProcessor& p) : proc (p), settings (p.settings) {}
    ~MeasureRunner() { if (busy()) stop(); }

    void setSnapshot (const Snapshot* s) { snap = s; }

    // Plays pink noise from `output` (0: all) and measures at `mic`, the mic aimed `aim` (calibration angle).
    bool start (int output, int mic, int aim, std::function<void (const Result&)> done)
    {
        if (busy()) return false;
        if (settings.refSource.load() == 0) { error = "Set a reference on Mics first (the loopback, or the generator copy)."; return false; }
        onDone = std::move (done);
        micNow = juce::jlimit (0, juce::jmax (0, settings.micCount.load() - 1), mic);
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        proc.setAimOverride (aim);
        settings.genOutput = output;
        settings.genType = 0;   // pink noise
        settings.genOn = true;
        settings.running = true;
        phase = Phase::Settling;
        phaseStart = la::clock::ms();
        settleUntil = proc.analyzer.samplesWritten() + (juce::uint64) (kSettleSeconds * rate());
        progressValue = 0.0f;
        error.clear();
        return true;
    }

    void stop (const juce::String& why = "Stopped.")
    {
        if (phase == Phase::Idle) return;
        settings.genOn = false;
        proc.setAimOverride (-1);
        for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        phase = Phase::Idle;
        error = why;
    }

    bool busy() const { return phase != Phase::Idle; }
    float progress() const { return progressValue; }
    const juce::String& lastError() const { return error; }
    juce::String phaseText (const juce::String& who) const
    {
        switch (phase)
        {
            case Phase::Settling: return "Playing pink noise through " + who + "...";
            case Phase::FindingTime: return "Finding when " + who + " arrives...";
            case Phase::Averaging: return "Averaging " + who + "...";
            case Phase::Idle:
            default: return {};
        }
    }

    // Call from the page's timer (15 Hz or so).
    void tick()
    {
        if (phase == Phase::Idle) return;
        if (! settings.genOn.load()) { finish ("Stopped: the generator was turned off."); return; }
        const double elapsed = la::clock::ms() - phaseStart;
        if (phase == Phase::Settling)
        {
            const auto done = proc.analyzer.samplesAnalysed();
            progressValue = 0.15f * (float) juce::jlimit (0.0, 1.0, 1.0 - ((double) settleUntil - (double) done) / (kSettleSeconds * rate()));
            if (done >= settleUntil)
            {
                delaySerial = proc.analyzer.readDelayReport().serial;
                settings.requestFindDelay = true;
                phase = Phase::FindingTime;
                phaseStart = la::clock::ms();
            }
        }
        else if (phase == Phase::FindingTime)
        {
            progressValue = 0.15f + (float) juce::jmin (1.0, elapsed / 2000.0) * 0.1f;
            const auto rep = proc.analyzer.readDelayReport();
            if (rep.serial != delaySerial)
            {
                const bool valid = micNow < rep.micCount && rep.valid[(size_t) micNow];
                for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
                if (! valid) { finish ("No clear arrival at " + proc.mics.nameOf (micNow) + ": check that it is playing and the mic hears it."); return; }
                result = {};
                result.mic = micNow;
                result.hasArrival = true;
                result.arrivalMs = rep.ms[(size_t) micNow];
                result.peakKnown = rep.confidenceDb[(size_t) micNow] >= 12.0;
                result.peakInverted = rep.inverted[(size_t) micNow];
                settings.micDelayMs[micNow] = (float) result.arrivalMs;
                settings.requestResetAverages = true;
                sawReset = false;
                overflowsSeen = proc.analyzer.overflowCount();
                restarts = 0;
                phase = Phase::Averaging;
                phaseStart = la::clock::ms();
            }
            else if (elapsed > 10000.0) finish ("The timing search did not answer. Check the audio device and try again.");
        }
        else if (phase == Phase::Averaging)
        {
            const int want = juce::jmax (4, settings.averages.load());
            const int frames = snap != nullptr && micNow < (int) snap->mics.size() ? snap->mics[(size_t) micNow].tfFrames : 0;
            if (proc.analyzer.overflowCount() != overflowsSeen)
            {
                overflowsSeen = proc.analyzer.overflowCount();
                if (++restarts > 3) { finish ("The computer could not keep up with the analysis. Close other programs, then measure again."); return; }
                settings.requestResetAverages = true;
                sawReset = false;
                phaseStart = la::clock::ms();
            }
            if (! sawReset && frames < want) sawReset = true;
            progressValue = 0.25f + 0.75f * (sawReset ? juce::jlimit (0.0f, 1.0f, (float) frames / (float) want) : 0.0f);
            if ((sawReset && frames >= want && elapsed > 1500.0) || (elapsed > 30000.0 && sawReset && frames > 0)) capture();
            else if (elapsed > 30000.0) finish ("No transfer function: check the reference and that the speaker is playing.");
        }
    }

private:
    enum class Phase { Idle, Settling, FindingTime, Averaging };
    static constexpr double kSettleSeconds = 3.0;

    double rate() const
    {
        if (snap != nullptr && snap->sampleRate > 1000.0) return snap->sampleRate;
        return proc.getSampleRate() > 1000.0 ? proc.getSampleRate() : 48000.0;
    }

    void capture()
    {
        if (snap == nullptr || micNow >= (int) snap->mics.size()) { finish ("No data from the mic."); return; }
        const auto& r = snap->mics[(size_t) micNow];
        for (size_t i = 0; i < snap->tfFreq.size() && i < r.tfMag.size(); ++i)
        {
            if (! std::isfinite (r.tfMag[i])) continue;
            result.tf.freq.push_back (snap->tfFreq[i]);
            result.tf.db.push_back (r.tfMag[i]);
            result.tf.phase.push_back (i < r.tfPhase.size() && std::isfinite (r.tfPhase[i]) ? r.tfPhase[i] : 0.0);
            result.tf.extra.push_back (i < r.tfCoh.size() ? r.tfCoh[i] : 0.0);
        }
        if (result.tf.freq.size() < 16) { finish ("Not enough of the response was clear: raise the level or reduce the noise."); return; }
        result.ok = true;
        finish ({});
    }

    void finish (const juce::String& why)
    {
        settings.genOn = false;
        proc.setAimOverride (-1);
        for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        phase = Phase::Idle;
        error = why;
        if (! why.isEmpty()) { result.ok = false; result.error = why; }
        auto cb = onDone;
        const auto res = result;
        if (cb) cb (res);
    }

    LiveAlignProcessor& proc;
    Settings& settings;
    const Snapshot* snap = nullptr;
    std::function<void (const Result&)> onDone;
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    juce::uint64 settleUntil = 0, delaySerial = 0;
    int micNow = 0, restarts = 0;
    juce::uint64 overflowsSeen = 0;
    bool sawReset = false;
    float progressValue = 0.0f;
    std::array<float, kMaxMics> savedDelays {};
    Result result;
    juce::String error;
};

// A transfer function cut down to 1/12 octave (dB only), for keeping many of them in a session.
inline FreqTable twelfthOctave (const FreqTable& t)
{
    FreqTable out;
    if (t.freq.size() < 4) return out;
    for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 12.0))
    {
        if (f < t.freq.front() || f > t.freq.back()) continue;
        double p = 0.0;
        int n = 0;
        for (size_t i = 0; i < t.freq.size(); ++i)
            if (t.freq[i] >= f / 1.03 && t.freq[i] < f * 1.03 && std::isfinite (t.db[i])) { p += std::pow (10.0, t.db[i] / 10.0); ++n; }
        if (n == 0) { const double v = t.dbAt (f); if (std::isfinite (v)) { out.freq.push_back (f); out.db.push_back (v); } continue; }
        out.freq.push_back (f);
        out.db.push_back (10.0 * std::log10 (p / n));
    }
    return out;
}
} // namespace la::ui
