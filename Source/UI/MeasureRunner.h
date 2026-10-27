// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include "../Clock.h"
// One measurement of whatever plays from one output, at one mic or at every placed mic in one pass: pink noise for a
// few seconds, each mic's arrival found, then the transfer functions averaged timed to those arrivals. Level balance,
// Aim and coverage and the quick re-check use it; Speaker response and Room EQ have their own (they keep the timing
// locked, or capture impulse responses). Message thread only: the page that owns it passes it the analysis snapshot
// and calls tick() from its timer.
//
// Time is saved without losing accuracy in two ways. When the same mics were just measured at the same spots from the
// same output, their arrivals are already known to the sample: the averaging starts after a short settle timed to
// those, and the finder runs once the buffer holds three seconds of noise to confirm them (a changed arrival restarts
// the average, so nothing is kept that was mistimed). And the averaging stops early once the result has settled: a
// few frames in, high coherence through the range that matters and no change from the frame before.
#include <functional>
#include <map>
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
        juce::String point;           // where the mic stood (its position on the plan)
        double genDb = NAN;           // the generator's level during the pass (dBFS)
        double splDbA = NAN;          // the A-weighted Leq at the mic while averaging, when it is SPL-calibrated
    };

    explicit MeasureRunner (LiveAlignProcessor& p) : proc (p), settings (p.settings) {}
    ~MeasureRunner() { if (busy()) stop(); }

    void setSnapshot (const Snapshot* s) { snap = s; }

    // Plays pink noise from `output` (0: all) and measures at every mic in `mics` (the first is the one the words are
    // about; the pass fails only when that one has no clear arrival), the mics aimed `aim` (calibration angle, -1:
    // as set). `rangeLo`..`rangeHi`: where the result has to be clean before the averaging may stop early (the
    // speaker's play range). One result per mic, in the order given.
    bool start (int output, const std::vector<int>& mics, int aim, std::function<void (const std::vector<Result>&)> done,
                double rangeLo = 60.0, double rangeHi = 12000.0)
    {
        if (busy()) return false;
        if (settings.refSource.load() == 0) { error = "Set a reference on Mics first (the loopback, or the generator copy)."; return false; }
        if (mics.empty()) { error = "No mic to measure with."; return false; }
        onDone = std::move (done);
        const int count = juce::jmax (1, settings.micCount.load());
        micsNow.clear();
        for (int m : mics)
        {
            const int mm = juce::jlimit (0, count - 1, m);
            if (std::find (micsNow.begin(), micsNow.end(), mm) == micsNow.end()) micsNow.push_back (mm);
        }
        micNow = micsNow.front();
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        proc.setAimOverride (aim);
        outputNow = output;
        lo = rangeLo;
        hi = juce::jmax (rangeLo * 2.0, rangeHi);
        settings.genOutput = output;
        settings.genType = 0;   // pink noise
        settings.genOn = true;
        settings.running = true;
        phase = Phase::Settling;
        phaseStart = la::clock::ms();
        startedAt = proc.analyzer.samplesWritten();
        knownDelays = delaysKnown();
        settleSeconds = knownDelays ? kShortSettleSeconds : kSettleSeconds;
        settleUntil = startedAt + (juce::uint64) (settleSeconds * rate());
        confirmPending = confirmRequested = false;
        results.clear();
        progressValue = 0.0f;
        error.clear();
        return true;
    }

    // One mic: the result on its own.
    bool start (int output, int mic, int aim, std::function<void (const Result&)> done, double rangeLo = 60.0, double rangeHi = 12000.0)
    {
        return start (output, std::vector<int> { mic }, aim, [done] (const std::vector<Result>& rs) { if (done && ! rs.empty()) done (rs.front()); }, rangeLo, rangeHi);
    }

    // A page that knows a mic's arrival from an earlier pass (the same speaker at the same point) says so, and the
    // next pass from that output starts its averaging early. The finder confirms it before anything is kept.
    void hintDelay (int mic, int output, const juce::String& position, double ms)
    {
        if (mic < 0 || mic >= kMaxMics || ! std::isfinite (ms)) return;
        if (last.output != output) { last = {}; last.output = output; }
        last.position[(size_t) mic] = position;
        last.ms[(size_t) mic] = ms;
        last.valid[(size_t) mic] = true;
        last.when = la::clock::ms();
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
            case Phase::Averaging: return "Averaging " + who + (micsNow.size() > 1 ? " at " + juce::String ((int) micsNow.size()) + " mics..." : juce::String ("..."));
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
            progressValue = 0.15f * (float) juce::jlimit (0.0, 1.0, 1.0 - ((double) settleUntil - (double) done) / (settleSeconds * rate()));
            if (done >= settleUntil)
            {
                if (knownDelays)
                {
                    // timed to the arrivals found last time; the finder confirms them once the buffer is all noise
                    for (int m : micsNow) settings.micDelayMs[m] = (float) last.ms[(size_t) m];
                    beginResults();
                    for (auto& r : results)
                    {
                        r.hasArrival = true;
                        r.arrivalMs = last.ms[(size_t) r.mic];
                    }
                    confirmPending = true;
                    confirmRequested = false;
                    confirmAt = startedAt + (juce::uint64) (kSettleSeconds * rate());
                    beginAveraging();
                }
                else
                {
                    delaySerial = proc.analyzer.readDelayReport().serial;
                    settings.requestFindDelay = true;
                    phase = Phase::FindingTime;
                    phaseStart = la::clock::ms();
                }
            }
        }
        else if (phase == Phase::FindingTime)
        {
            progressValue = 0.15f + (float) juce::jmin (1.0, elapsed / 2000.0) * 0.1f;
            const auto rep = proc.analyzer.readDelayReport();
            if (rep.serial != delaySerial)
            {
                for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
                if (! (micNow < rep.micCount && rep.valid[(size_t) micNow]))
                {
                    finish ("No clear arrival at " + proc.mics.nameOf (micNow) + ": check that it is playing and the mic hears it.");
                    return;
                }
                beginResults();
                takeReport (rep);
                for (const auto& r : results) if (r.hasArrival) settings.micDelayMs[r.mic] = (float) r.arrivalMs;
                beginAveraging();
            }
            else if (elapsed > 10000.0) finish ("The timing search did not answer. Check the audio device and try again.");
        }
        else if (phase == Phase::Averaging)
        {
            const int want = juce::jmax (4, settings.averages.load());
            if (proc.analyzer.overflowCount() != overflowsSeen)
            {
                overflowsSeen = proc.analyzer.overflowCount();
                if (++restarts > 3) { finish ("The computer could not keep up with the analysis. Close other programs, then measure again."); return; }
                settings.requestResetAverages = true;
                sawReset = false;
                lastMag.clear();
                phaseStart = la::clock::ms();
            }
            // the confirmation of arrivals carried over from the last pass
            if (confirmPending)
            {
                if (! confirmRequested && proc.analyzer.samplesAnalysed() >= confirmAt)
                {
                    delaySerial = proc.analyzer.readDelayReport().serial;
                    settings.requestFindDelay = true;
                    confirmRequested = true;
                    confirmAsked = la::clock::ms();
                }
                else if (confirmRequested)
                {
                    const auto rep = proc.analyzer.readDelayReport();
                    if (rep.serial != delaySerial)
                    {
                        if (! (micNow < rep.micCount && rep.valid[(size_t) micNow]))
                        {
                            finish ("No clear arrival at " + proc.mics.nameOf (micNow) + ": check that it is playing and the mic hears it.");
                            return;
                        }
                        // a mic whose arrival moved by a sample or more had its average restarted by the analysis
                        // (its timing changed): wait for the fresh frames
                        bool moved = false;
                        for (const auto& r : results)
                            if (r.hasArrival && r.mic < rep.micCount && rep.valid[(size_t) r.mic]
                                && std::lround (rep.ms[(size_t) r.mic] * 0.001 * rate()) != std::lround (r.arrivalMs * 0.001 * rate())) moved = true;
                        takeReport (rep);
                        for (const auto& r : results) if (r.hasArrival) settings.micDelayMs[r.mic] = (float) r.arrivalMs;
                        if (moved)
                        {
                            settings.requestResetAverages = true;
                            sawReset = false;
                            lastMag.clear();
                            phaseStart = la::clock::ms();
                        }
                        confirmPending = false;
                    }
                    else if (la::clock::ms() - confirmAsked > 10000.0) { finish ("The timing search did not answer. Check the audio device and try again."); return; }
                }
            }
            const int frames = minFrames();
            if (! sawReset && frames < want) sawReset = true;
            progressValue = 0.25f + 0.75f * (sawReset ? juce::jlimit (0.0f, 1.0f, (float) frames / (float) want) : 0.0f);
            const bool settled = sawReset && frames >= 4 && converged();
            const bool enough = sawReset && (settled || frames >= 2 * want);
            if (! confirmPending && ((enough && (la::clock::ms() - phaseStart) > 1500.0) || (elapsed > 30000.0 && sawReset && frames > 0))) capture();
            else if (elapsed > 30000.0) finish ("No transfer function: check the reference and that the speaker is playing.");
        }
    }

private:
    enum class Phase { Idle, Settling, FindingTime, Averaging };
    // The finder reads the last ~2.7 s of raw audio, so a fresh arrival needs this much noise first. A known one
    // needs only the room to settle.
    static constexpr double kSettleSeconds = 3.0, kShortSettleSeconds = 1.5;
    static constexpr double kKnownForMs = 5.0 * 60.0 * 1000.0;   // how long a pass's arrivals count as "just measured"

    // What the last pass found: the arrivals of each mic from one output, and where the mics stood.
    struct LastPass
    {
        int output = -1;
        std::array<juce::String, kMaxMics> position;
        std::array<double, kMaxMics> ms {};
        std::array<bool, kMaxMics> valid {};
        double when = 0.0;
    };

    double rate() const
    {
        if (snap != nullptr && snap->sampleRate > 1000.0) return snap->sampleRate;
        return proc.getSampleRate() > 1000.0 ? proc.getSampleRate() : 48000.0;
    }

    // Every mic of this pass was just measured from this output standing where it stands now.
    bool delaysKnown() const
    {
        if (last.output != outputNow || la::clock::ms() - last.when > kKnownForMs) return false;
        for (int m : micsNow)
            if (! last.valid[(size_t) m] || last.position[(size_t) m] != proc.mics.position[(size_t) m]) return false;
        return true;
    }

    void beginResults()
    {
        results.clear();
        for (int m : micsNow)
        {
            Result r;
            r.mic = m;
            r.point = proc.mics.position[(size_t) m];
            results.push_back (r);
        }
    }

    void takeReport (const DelayReport& rep)
    {
        for (auto& r : results)
        {
            const bool valid = r.mic < rep.micCount && rep.valid[(size_t) r.mic];
            r.hasArrival = valid;
            if (! valid) { r.error = "No clear arrival at " + proc.mics.nameOf (r.mic) + "."; continue; }
            r.arrivalMs = rep.ms[(size_t) r.mic];
            r.peakKnown = rep.confidenceDb[(size_t) r.mic] >= 12.0;
            r.peakInverted = rep.inverted[(size_t) r.mic];
        }
    }

    void beginAveraging()
    {
        settings.requestResetAverages = true;
        settings.requestResetLeq = true;
        sawReset = false;
        lastMag.clear();
        overflowsSeen = proc.analyzer.overflowCount();
        restarts = 0;
        phase = Phase::Averaging;
        phaseStart = la::clock::ms();
    }

    int framesOf (int m) const { return snap != nullptr && m < (int) snap->mics.size() ? snap->mics[(size_t) m].tfFrames : 0; }
    int minFrames() const
    {
        int f = 1 << 30;
        for (const auto& r : results) if (r.hasArrival) f = juce::jmin (f, framesOf (r.mic));
        return f == (1 << 30) ? 0 : f;
    }

    // The result has settled at every mic: through the range, nine points in ten have coherence 0.9 or better, and
    // the magnitude moved under 0.3 dB RMS since the frame before. Judged on new frames only (a snapshot can repeat
    // the one before), so the change is between two different frame counts.
    bool converged()
    {
        if (snap == nullptr) return false;
        bool all = true;
        for (const auto& r : results)
        {
            if (! r.hasArrival || r.mic >= (int) snap->mics.size()) continue;
            const auto& mr = snap->mics[(size_t) r.mic];
            auto& prev = lastMag[r.mic];
            if (mr.tfFrames <= prev.frames) { all = all && prev.settled; continue; }
            int n = 0, good = 0;
            double ss = 0.0;
            int nd = 0;
            for (size_t i = 0; i < snap->tfFreq.size() && i < mr.tfMag.size(); ++i)
            {
                if (snap->tfFreq[i] < lo || snap->tfFreq[i] > hi || ! std::isfinite (mr.tfMag[i])) continue;
                ++n;
                if (i < mr.tfCoh.size() && mr.tfCoh[i] >= 0.9f) ++good;
                if (i < prev.mag.size() && std::isfinite (prev.mag[i])) { const double d = mr.tfMag[i] - prev.mag[i]; ss += d * d; ++nd; }
            }
            const bool coherent = n > 0 && good >= (n * 9 + 9) / 10;
            const bool still = nd > 0 && std::sqrt (ss / nd) < 0.3;
            prev.settled = coherent && still;
            prev.frames = mr.tfFrames;
            prev.mag = mr.tfMag;
            all = all && prev.settled;
        }
        return all;
    }

    void capture()
    {
        if (snap == nullptr) { finish ("No data from the mic."); return; }
        const double gen = settings.genLevelDb.load();
        for (auto& r : results)
        {
            if (! r.hasArrival || r.mic >= (int) snap->mics.size()) { r.ok = false; continue; }
            const auto& mr = snap->mics[(size_t) r.mic];
            for (size_t i = 0; i < snap->tfFreq.size() && i < mr.tfMag.size(); ++i)
            {
                if (! std::isfinite (mr.tfMag[i])) continue;
                r.tf.freq.push_back (snap->tfFreq[i]);
                r.tf.db.push_back (mr.tfMag[i]);
                r.tf.phase.push_back (i < mr.tfPhase.size() && std::isfinite (mr.tfPhase[i]) ? mr.tfPhase[i] : 0.0);
                r.tf.extra.push_back (i < mr.tfCoh.size() ? mr.tfCoh[i] : 0.0);
            }
            r.genDb = gen;
            r.splDbA = mr.isSpl && mr.leqA > -150.0f ? (double) mr.leqA : NAN;
            if (r.tf.freq.size() < 16) { r.ok = false; r.error = "Not enough of the response was clear at " + proc.mics.nameOf (r.mic) + ": raise the level or reduce the noise."; continue; }
            r.ok = true;
        }
        if (! results.front().ok) { finish (results.front().error.isNotEmpty() ? results.front().error : juce::String ("Not enough of the response was clear: raise the level or reduce the noise.")); return; }
        // what this pass found, for the next one from the same output
        last = {};
        last.output = outputNow;
        last.when = la::clock::ms();
        for (const auto& r : results)
            if (r.ok) { last.position[(size_t) r.mic] = r.point; last.ms[(size_t) r.mic] = r.arrivalMs; last.valid[(size_t) r.mic] = true; }
        finish ({});
    }

    void finish (const juce::String& why)
    {
        settings.genOn = false;
        proc.setAimOverride (-1);
        for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        phase = Phase::Idle;
        error = why;
        if (! why.isEmpty())
        {
            if (results.empty()) beginResults();
            for (auto& r : results) { r.ok = false; if (r.error.isEmpty()) r.error = why; }
            results.front().error = why;
        }
        auto cb = onDone;
        const auto res = results;
        if (cb) cb (res);
    }

    struct MagSeen { int frames = 0; bool settled = false; std::vector<float> mag; };

    LiveAlignProcessor& proc;
    Settings& settings;
    const Snapshot* snap = nullptr;
    std::function<void (const std::vector<Result>&)> onDone;
    Phase phase = Phase::Idle;
    double phaseStart = 0.0, settleSeconds = kSettleSeconds, confirmAsked = 0.0;
    double lo = 60.0, hi = 12000.0;
    juce::uint64 settleUntil = 0, delaySerial = 0, startedAt = 0, confirmAt = 0;
    int micNow = 0, outputNow = 0, restarts = 0;
    std::vector<int> micsNow;
    juce::uint64 overflowsSeen = 0;
    bool sawReset = false, knownDelays = false, confirmPending = false, confirmRequested = false;
    float progressValue = 0.0f;
    std::array<float, kMaxMics> savedDelays {};
    std::vector<Result> results;
    std::map<int, MagSeen> lastMag;
    LastPass last;
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
