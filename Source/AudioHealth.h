// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Audio health: is the audio arriving whole and on time? Watched all the time, with nothing played and nothing to
// set up, from what the inputs already carry (a quiet room is enough):
//  - the sample count against the computer's clock: a chunk that never arrived is a dropout (the computer, driver or
//    USB missed it), and over minutes the count is the interface's real sample rate (a clock that is off);
//  - each input for things real sound never does: a run of exact zeros in the middle of a signal, or a block that
//    repeats the one before exactly (the interface filled a gap);
//  - each input for clicks (a jump the signal's own movement cannot explain), and whether they come at a steady
//    interval: a click every few seconds, regular as a clock, is two digital devices not locked to one clock
//    (a console's digital feed slipping a sample each time).
// The audio thread only counts and looks; the message thread (poll) times, judges and reports.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>

namespace la
{
class AudioHealth
{
public:
    static constexpr int kMaxChannels = 10;
    enum Kind { Dropout = 0, ZeroGap, Repeat, Click, DriverOverload };

    struct Event
    {
        double t = 0.0;      // seconds since 1970
        int kind = Dropout;
        int channel = -1;    // the input (index into the names), -1 for the whole interface
        int samples = 0;     // how many were lost (a dropout, a gap)
    };

    // ---- audio thread
    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate > 0.0 ? sampleRate : 48000.0;
        for (auto& c : chans) c = {};
        for (auto& c : chans) c.prev.assign ((size_t) juce::jmax (64, maxBlock), 0.0f);
        clickGapSamples = (juce::int64) (0.05 * fs);
        maxGapSamples = (int) (0.25 * fs);
        resetTiming.store (true);
    }

    // Once per audio callback: the inputs watched (each mic in use, the console feed), `n` samples each. `nowMs`: the
    // computer's clock (milliseconds, high resolution) at the callback.
    void process (const float* const* in, int numCh, int n, double nowMs)
    {
        // a long pause between callbacks (the device stopped, the computer slept): start the timing again, not a dropout
        if (prevMs > 0.0 && nowMs - prevMs > 1000.0) resetTiming.store (true);
        prevMs = nowMs;
        // the sample count and the time it was reached, as one consistent pair for the message thread
        total += n;
        seq.fetch_add (1, std::memory_order_acq_rel);
        stampSamples.store (total, std::memory_order_relaxed);
        stampMs.store (nowMs, std::memory_order_relaxed);
        seq.fetch_add (1, std::memory_order_acq_rel);
        lastBlock.store (n, std::memory_order_relaxed);

        numCh = juce::jmin (numCh, kMaxChannels);
        for (int c = 0; c < numCh; ++c)
        {
            const float* x = in[c];
            if (x == nullptr) continue;
            auto& s = chans[(size_t) c];
            bool any = false;
            for (int i = 0; i < n; ++i)
            {
                const float v = x[i];
                // digital silence inside a signal
                if (v == 0.0f) ++s.zeroRun;
                else
                {
                    any = true;
                    if (s.zeroRun >= kMinZeroRun && s.zeroRun < maxGapSamples && s.live) post (ZeroGap, c, total - n + i, s.zeroRun);
                    s.zeroRun = 0;
                }
                // a click: the second difference far beyond the signal's own (the last ~10 ms)
                const float d2 = v - 2.0f * s.x1 + s.x2;
                const float e = d2 * d2;
                if (s.live && e > 40.0f * s.d2Ms && std::abs (d2) > 2e-3f && total - n + i - s.lastClick > clickGapSamples && s.x1 != 0.0f && v != 0.0f)
                {
                    s.lastClick = total - n + i;
                    post (Click, c, s.lastClick, 0);
                }
                s.d2Ms += 0.002f * (e - s.d2Ms);
                s.ms += 0.0005f * (v * v - s.ms);
                s.x2 = s.x1;
                s.x1 = v;
            }
            s.live = s.ms > 1e-12f;   // above -120 dBFS: something is connected and running
            // the same block twice, exactly
            if (any && n >= 32 && n <= (int) s.prev.size())
            {
                if (s.prevN == n && std::equal (x, x + n, s.prev.begin())) post (Repeat, c, total - n, n);
                std::copy (x, x + n, s.prev.begin());
                s.prevN = n;
            }
        }
    }

    // The sample count against the clock only means something when the audio runs in real time on a device of our
    // own (the app). In a plug-in, a host may render offline or in bursts: then only the inputs are checked.
    void setTimingChecks (bool on) { timingOn.store (on); if (on) resetTiming.store (true); }

    // ---- message thread
    // Looks at the timing, takes the inputs' findings, and judges. Call a few times a second. `nowSec`: seconds since
    // 1970 (for the events' times). `driverOverloads`: the driver's own count of missed deadlines, -1 if it has none.
    // `clockMs`: the clock process() is given, now (default: the computer's high resolution clock).
    void poll (double nowSec, int driverOverloads = -1, double clockMs = -1.0)
    {
        if (clockMs < 0.0) clockMs = juce::Time::getMillisecondCounterHiRes();
        // a consistent (samples, time) pair
        juce::int64 s = 0;
        double ms = 0.0;
        for (int tries = 0; tries < 8; ++tries)
        {
            const auto a = seq.load (std::memory_order_acquire);
            if ((a & 1) != 0) continue;
            s = stampSamples.load (std::memory_order_relaxed);
            ms = stampMs.load (std::memory_order_relaxed);
            if (seq.load (std::memory_order_acquire) == a) break;
        }
        const double t = ms / 1000.0;
        // no audio for a second: stopped (a closed device, the computer asleep), not a dropout
        stopped = s == 0 || clockMs - ms > 1000.0;
        if (! timingOn.load()) { refS = s; refT = t; measuredRate = 0.0; }
        else if (resetTiming.exchange (false) || s == 0) { refS = s; refT = t; pendingShort = 0.0; fitS.clear(); fitT.clear(); lastFitT = -1.0; }
        else if (stopped || s == refS) {}
        else
        {
            // where the count should be by now, at the rate measured (the nominal one to start with)
            const double rate = measuredRate > 0.0 ? measuredRate : fs;
            const double expect = (double) refS + rate * (t - refT);
            const double shortBy = expect - (double) s;
            const double slack = juce::jmax (3.0 * lastBlock.load(), 0.02 * fs);
            if (shortBy > slack)
            {
                // short twice in a row: samples went missing (once can be a late callback)
                if (pendingShort > 0.0) { addEvent ({ nowSec, Dropout, -1, (int) shortBy }); refS = s; refT = t; pendingShort = 0.0; }
                else pendingShort = shortBy;
            }
            else
            {
                pendingShort = 0.0;
                refS = s; refT = t;
            }
            // the real rate: a straight line through a point a second over the last ten minutes
            if (t - lastFitT >= 1.0)
            {
                lastFitT = t;
                fitT.push_back (t);
                fitS.push_back ((double) s);
                if (fitT.size() > 600) { fitT.erase (fitT.begin()); fitS.erase (fitS.begin()); }
                if (fitT.size() >= 20 && fitT.back() - fitT.front() >= 20.0) measuredRate = slope();
            }
        }

        if (driverOverloads >= 0)
        {
            if (lastDriverCount >= 0 && driverOverloads > lastDriverCount)
                for (int k = 0; k < juce::jmin (10, driverOverloads - lastDriverCount); ++k) addEvent ({ nowSec, DriverOverload, -1, 0 });
            lastDriverCount = driverOverloads;
        }

        // the inputs' findings, timed from their sample position
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        auto take = [&] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
            {
                const auto& p = posted[(size_t) (start + i)];
                const double when = nowSec - (double) (s - p.at) / fs;
                if (p.kind == Click) clicks[(size_t) juce::jlimit (0, kMaxChannels - 1, p.channel)].push_back (when);
                else addEvent ({ when, p.kind, p.channel, p.samples });
            }
        };
        take (s1, n1);
        take (s2, n2);
        fifo.finishedRead (n1 + n2);
        for (auto& c : clicks) while (! c.empty() && nowSec - c.front() > 180.0) c.erase (c.begin());
        judgeSync (nowSec);
    }

    // ---- what was found
    const std::vector<Event>& events() const { return all; }
    double rate() const { return measuredRate; }                  // the interface's real sample rate (0 until known)
    double nominalRate() const { return fs; }
    double ppm() const { return measuredRate > 0.0 ? (measuredRate / fs - 1.0) * 1e6 : 0.0; }
    bool isStopped() const { return stopped; }
    // A channel clicking at a steady interval (a clock not locked): the interval in seconds, 0 if none.
    // Clicks heard on a channel in the last 3 minutes (sharp sounds in the music count too: a hint, not a finding).
    int recentClicks (int channel) const { return channel >= 0 && channel < kMaxChannels ? (int) clicks[(size_t) channel].size() : 0; }
    double syncInterval (int channel) const { return channel >= 0 && channel < kMaxChannels ? sync[(size_t) channel] : 0.0; }
    int count (int kind, double since = 0.0) const
    {
        int n = 0;
        for (const auto& e : all) n += e.kind == kind && e.t >= since ? 1 : 0;
        return n;
    }
    // New events since the last call (for the show log).
    std::vector<Event> takeNew()
    {
        std::vector<Event> out (all.begin() + (long) juce::jmin (handedOut, all.size()), all.end());
        handedOut = all.size();
        return out;
    }
    void clear() { all.clear(); handedOut = 0; for (auto& c : clicks) c.clear(); sync.fill (0.0); syncSince.fill (0.0); }

    static juce::String kindName (int k)
    {
        switch (k)
        {
            case Dropout: return "dropout";
            case ZeroGap: return "gap of digital silence";
            case Repeat: return "repeated block";
            case Click: return "regular clicks (clock not locked)";
            case DriverOverload: return "driver overload";
            default: return {};
        }
    }

private:
    static constexpr int kMinZeroRun = 16;
    struct Chan
    {
        int zeroRun = 0, prevN = 0;
        float x1 = 0.0f, x2 = 0.0f, d2Ms = 1.0f, ms = 0.0f;
        bool live = false;
        juce::int64 lastClick = -1000000;
        std::vector<float> prev;
    };
    struct Posted { int kind = 0, channel = 0, samples = 0; juce::int64 at = 0; };

    void post (int kind, int channel, juce::int64 at, int samples)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 > 0) posted[(size_t) s1] = { kind, channel, samples, at };
        else if (n2 > 0) posted[(size_t) s2] = { kind, channel, samples, at };
        fifo.finishedWrite (n1 + n2);   // full: dropped (the message thread is far behind)
    }

    void addEvent (const Event& e)
    {
        all.push_back (e);
        if (all.size() > 2000) { all.erase (all.begin(), all.begin() + 500); handedOut = handedOut > 500 ? handedOut - 500 : 0; }
    }

    double slope() const
    {
        const double n = (double) fitT.size();
        double mt = 0.0, ms2 = 0.0;
        for (size_t i = 0; i < fitT.size(); ++i) { mt += fitT[i]; ms2 += fitS[i]; }
        mt /= n; ms2 /= n;
        double num = 0.0, den = 0.0;
        for (size_t i = 0; i < fitT.size(); ++i) { num += (fitT[i] - mt) * (fitS[i] - ms2); den += (fitT[i] - mt) * (fitT[i] - mt); }
        return den > 0.0 ? num / den : 0.0;
    }

    // Clicks at a steady interval: at least 6 in the last 3 minutes, most intervals within 4 % of their median (or
    // a whole multiple of it: a slip can hide in loud music).
    void judgeSync (double nowSec)
    {
        for (int c = 0; c < kMaxChannels; ++c)
        {
            const auto& v = clicks[(size_t) c];
            double found = 0.0;
            if (v.size() >= 6)
            {
                std::vector<double> gaps;
                for (size_t i = 1; i < v.size(); ++i) gaps.push_back (v[i] - v[i - 1]);
                auto sorted = gaps;
                std::sort (sorted.begin(), sorted.end());
                const double med = sorted[sorted.size() / 2];
                if (med >= 0.2)
                {
                    int fit = 0;
                    for (double g : gaps)
                    {
                        const double k = std::round (g / med);
                        fit += k >= 1.0 && k <= 4.0 && std::abs (g - k * med) < 0.04 * med ? 1 : 0;
                    }
                    if (fit >= (int) std::ceil (0.8 * (double) gaps.size())) found = med;
                }
            }
            if (found > 0.0 && sync[(size_t) c] == 0.0) { syncSince[(size_t) c] = nowSec; addEvent ({ nowSec, Click, c, 0 }); }   // logged once as it starts
            sync[(size_t) c] = found;
        }
    }

    // audio thread
    double fs = 48000.0, prevMs = 0.0;
    juce::int64 total = 0, clickGapSamples = 2400;
    int maxGapSamples = 12000;
    std::array<Chan, kMaxChannels> chans;
    std::atomic<juce::uint32> seq { 0 };
    std::atomic<juce::int64> stampSamples { 0 };
    std::atomic<double> stampMs { 0.0 };
    std::atomic<int> lastBlock { 512 };
    std::atomic<bool> resetTiming { true }, timingOn { true };
    static constexpr int kFifo = 256;
    juce::AbstractFifo fifo { kFifo };
    std::array<Posted, kFifo> posted {};

    // message thread
    juce::int64 refS = 0;
    double refT = 0.0, pendingShort = 0.0, measuredRate = 0.0, lastFitT = -1.0;
    bool stopped = false;
    int lastDriverCount = -1;
    std::vector<double> fitT, fitS;
    std::vector<Event> all;
    size_t handedOut = 0;
    std::array<std::vector<double>, kMaxChannels> clicks;
    std::array<double, kMaxChannels> sync {}, syncSince {};
};
} // namespace la
