// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The quick re-check of a system already tuned: one guided pass that measures each speaker close up, then each one
// and the whole system at FOH, and flags only what has drifted since it was tuned: polarity, level, delay, EQ. Each
// result is compared with what LiveAlign worked out then, allowing for the settings it gave (the expected response is
// the stored one with today's processing on top).
#include <functional>
#include <set>
#include "GuidedMeasure.h"
#include "MeasureRunner.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class RecheckGuide : public GuidedSource, private juce::Timer
{
public:
    std::function<void()> onChanged;

    explicit RecheckGuide (LiveAlignProcessor& p) : proc (p), runner (p) {}
    ~RecheckGuide() override { stopTimer(); }

    void setSnapshot (const Snapshot& s) { runner.setSnapshot (&s); }

    // A fresh pass: the earlier results are cleared.
    void begin()
    {
        done.clear();
        redo = -1;
        proc.recheck = {};
        proc.recheck.when = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        status.clear();
    }

    juce::String fohPoint() const
    {
        if (! proc.level.points.isEmpty()) return proc.level.points[0];
        if (proc.positions.contains ("FOH")) return "FOH";
        return proc.positions.isEmpty() ? juce::String ("FOH") : proc.positions[0];
    }

    // Where a speaker is checked against the level balance: the first point the balance measured it at.
    juce::String pointOf (int spk) const
    {
        for (const auto& p : proc.level.points) if (proc.level.latest (spk, p) != nullptr) return p;
        return fohPoint();
    }

    // ---- the points: 0 each speaker close up, 1 each speaker at FOH, 2 the whole system at FOH ----
    struct Task { int kind, spk; int id() const { return kind * 100 + spk; } };
    std::vector<Task> tasks() const
    {
        std::vector<Task> out;
        const auto& sys = proc.system;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
            if (sys.sources[(size_t) i].hasResponse() && ! sys.sources[(size_t) i].imported) out.push_back ({ 0, i });
        for (int i = 0; i < (int) sys.sources.size(); ++i) out.push_back ({ 1, i });
        out.push_back ({ 2, 0 });
        return out;
    }
    bool isDone (const Task& t) const { return std::find (done.begin(), done.end(), t.id()) != done.end() && redo != t.id(); }
    int current() const
    {
        const auto ts = tasks();
        for (size_t k = 0; k < ts.size(); ++k) if (ts[k].id() == redo) return (int) k;
        for (size_t k = 0; k < ts.size(); ++k) if (! isDone (ts[k])) return (int) k;
        return -1;
    }
    int micFor (const Task& t) const
    {
        if (t.kind == 0) return proc.system.micFor (t.spk, proc.tuningMicCount());
        const auto pt = pointFor (t);
        for (int m = 0; m < proc.tuningMicCount(); ++m) if (proc.mics.position[(size_t) m] == pt) return m;
        return 0;
    }
    juce::String pointFor (const Task& t) const { return t.kind == 1 ? pointOf (t.spk) : fohPoint(); }

    juce::String guidedTitle() const override { return "Quick re-check"; }

    std::vector<GuideSpot> guidedSpots() override
    {
        std::vector<GuideSpot> out;
        const auto ts = tasks();
        const int now = current();
        const auto& sys = proc.system;
        const int foh = proc.plan.ensureMarker (fohPoint());
        for (int k = 0; k < (int) ts.size(); ++k)
        {
            const auto& t = ts[(size_t) k];
            GuideSpot sp;
            sp.id = t.id();
            sp.state = k == now ? GuideSpot::Now : isDone (t) ? GuideSpot::Done : GuideSpot::ToDo;
            sp.problem = problems.count (t.id()) > 0;
            if (t.kind == 0)
            {
                const auto& sym = proc.plan.speakers[(size_t) juce::jmin (t.spk, VenuePlan::kMaxSpeakers - 1)];
                const float a = juce::degreesToRadians (sym.angle);
                sp.x = juce::jlimit (0.02f, 0.98f, sym.x + std::cos (a) * 0.06f);
                sp.y = juce::jlimit (0.02f, 0.98f, sym.y + std::sin (a) * 0.09f);
                sp.up = false;
                sp.aimAt = t.spk;
                sp.label = sys.nameOf (t.spk) + " close up";
            }
            else
            {
                const int mk = t.kind == 1 ? proc.plan.ensureMarker (pointFor (t)) : foh;
                sp.marker = mk;
                sp.x = proc.plan.items[(size_t) mk].x;
                sp.y = proc.plan.items[(size_t) mk].y;
                sp.label = (t.kind == 1 ? sys.nameOf (t.spk) : juce::String ("Whole system")) + " at " + pointFor (t);
            }
            if (sp.state == GuideSpot::Now)
            {
                sp.mic = micFor (t);
                sp.speaker = t.kind == 2 ? -1 : t.spk;
                sp.how = t.kind == 0 ? proc.closeUpText() + " from the cone, " + aimHowText (proc, sp.mic, SpeakerAim)
                                     : "seated ear height, " + aimHowText (proc, sp.mic, RoomAim);
            }
            out.push_back (sp);
        }
        return out;
    }

    juce::String guidedHowTo() const override
    {
        const auto ts = tasks();
        const int k = current();
        if (k < 0) return "Every point is checked. The results are on Home.";
        const auto& t = ts[(size_t) k];
        if (t.kind == 2) return "First: unmute every speaker. The whole system plays, with the processor as it was left.";
        if (const auto m = proc.system.muteText ({ t.spk }); m.isNotEmpty()) return "First: " + m;
        return "Leave the processor as it was tuned (filters, delays, EQ in place). Only " + proc.system.nameOf (t.spk) + " plays.";
    }

    bool guidedStart() override
    {
        const auto ts = tasks();
        const int k = current();
        if (k < 0) { status = "Every point is checked."; return false; }
        const auto t = ts[(size_t) k];
        const int mic = micFor (t);
        if (t.kind > 0) proc.setMicPosition (mic, pointFor (t));
        now = t;
        const int out = t.kind == 2 ? 0 : proc.system.sources[(size_t) t.spk].output;
        const bool ok = runner.start (out, mic, t.kind == 0 ? proc.speakerAim : -1, [this, t] (const MeasureRunner::Result& r) { finished (t, r); });
        if (! ok) status = runner.lastError();
        else startTimerHz (15);
        return ok;
    }
    void guidedStop() override { runner.stop(); stopTimer(); }
    bool guidedBusy() const override { return runner.busy(); }
    float guidedProgress() const override { return runner.progress(); }
    juce::String guidedPhase() const override { return runner.phaseText (now.kind == 2 ? juce::String ("the whole system") : proc.system.nameOf (now.spk)); }
    int guidedSerial() const override { return serial; }
    juce::String guidedStatus() const override { return status; }
    void guidedRedo (const std::vector<GuideSpot>& was) override { if (! was.empty()) redo = was.front().id; }
    void guidedClosed() override { redo = -1; if (onChanged) onChanged(); }
    int guidedAimKind() const override
    {
        const auto ts = tasks();
        const int k = current();
        return k >= 0 && ts[(size_t) k].kind == 0 ? SpeakerAim : RoomAim;
    }
    int guidedAimMic() const override
    {
        const auto ts = tasks();
        const int k = current();
        return k >= 0 ? micFor (ts[(size_t) k]) : 0;
    }

private:
    void timerCallback() override { runner.tick(); if (! runner.busy()) stopTimer(); }

    void add (int level, const juce::String& text)
    {
        proc.recheck.lines.push_back ({ text, level });
        if (level > 0) problems.insert (now.id());
    }

    // The stored close-up response with today's processing on top: what the speaker should measure now.
    FreqTable expected (int i) const
    {
        const auto& s = proc.system.sources[(size_t) i];
        FreqTable e;
        for (size_t k = 0; k < s.response.freq.size(); ++k)
        {
            const double f = s.response.freq[k];
            const auto h = s.proc.at (f) * std::pow (10.0, s.trimDb / 20.0);
            e.freq.push_back (f);
            e.db.push_back (s.response.db[k] + 20.0 * std::log10 (std::abs (h) + 1e-12));
            if (s.response.hasPhase())
            {
                // without the delay: the capture is timed to its own arrival
                const auto hz = s.proc.at (f) * std::polar (1.0, 2.0 * 3.14159265358979 * f * s.proc.delayMs * 0.001);
                e.phase.push_back (std::remainder (s.response.phase[k] + std::arg (hz) * 180.0 / 3.14159265358979, 360.0));
            }
            if (s.response.extra.size() == s.response.freq.size()) e.extra.push_back (s.response.extra[k]);
        }
        if (e.phase.size() != e.freq.size()) e.phase.clear();
        return e;
    }

    void finished (const Task& t, const MeasureRunner::Result& r)
    {
        stopTimer();
        if (! r.ok) { status = r.error; return; }
        problems.erase (t.id());
        // a re-measured point replaces its earlier lines
        const auto who = t.kind == 2 ? juce::String ("Whole system") : proc.system.nameOf (t.spk);
        auto& lines = proc.recheck.lines;
        const auto tag = t.kind == 0 ? who + " close up" : who + " at " + pointFor (t);
        lines.erase (std::remove_if (lines.begin(), lines.end(), [&tag] (const Recheck::Line& l) { return l.text.startsWith (tag); }), lines.end());
        auto f1 = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 1); };
        const double minCoh = juce::jmax (0.2f, proc.settings.coherenceBlank.load());
        if (t.kind == 0)
        {
            const auto e = expected (t.spk);
            double lo, hi;
            proc.system.playRange (t.spk, lo, hi);
            const double d = SpeakerSystem::bandLevel (r.tf, lo, hi) - SpeakerSystem::bandLevel (e, lo, hi);
            // a sub's capture is timed less exactly (its impulse is long): over its two octaves a timing slip cannot pass for 180 degrees
            const bool subRole = proc.system.sources[(size_t) t.spk].role == (int) SpeakerRole::Sub;
            const auto rel = polarity::relative (r.tf, e, minCoh, subRole ? 6.0 : 0.5);
            if (rel.verdict == polarity::Inverted) add (2, tag + ": the polarity has flipped since it was tuned. Check its wiring and the processor's polarity.");
            else if (rel.verdict == polarity::Unsure) add (1, tag + ": the polarity could not be confirmed against when it was tuned. Measure it again with the mic close to the cone.");
            const double tolClose = proc.pref ("recheckClose");
            if (std::isfinite (d) && std::abs (d) > tolClose) add (std::abs (d) > 2.0 * tolClose ? 2 : 1, tag + ": " + f1 (d) + " dB against when it was tuned. Check its amp, gain and level settings.");
            if (rel.verdict != polarity::Inverted && rel.verdict != polarity::Unsure && (! std::isfinite (d) || std::abs (d) <= tolClose))
                add (0, tag + ": as tuned (" + (std::isfinite (d) ? f1 (d) + " dB" : juce::String ("level not clear")) + ", polarity as before).");
        }
        else if (t.kind == 1)
        {
            const auto* was = proc.level.latest (t.spk, pointFor (t));
            const auto& s = proc.system.sources[(size_t) t.spk];
            if (was == nullptr) add (0, tag + ": measured (no level balance there to compare with).");
            else
            {
                double lo, hi;
                proc.system.playRange (t.spk, lo, hi);
                const auto tf = twelfthOctave (r.tf);
                // allowing for the trim and the room EQ entered since the level balance
                const double d = SpeakerSystem::bandLevel (tf, lo, hi)
                                 - (was->band (lo, hi) + (s.trimDb - was->trimAtDb) + (LevelBalance::eqInRange (proc.system, t.spk) - was->eqAtDb));
                const double expArrival = was->arrivalMs - was->delayAtMs + s.proc.delayMs;
                const double dt = r.arrivalMs - expArrival;
                bool fine = true;
                const double tolFoh = proc.pref ("recheckFoh");
                if (std::isfinite (d) && std::abs (d) > tolFoh) { add (std::abs (d) > 2.0 * tolFoh ? 2 : 1, tag + ": level " + f1 (d) + " dB against the level balance."); fine = false; }
                // a sub's arrival is only known roughly (its low frequencies smear the peak), so it gets more room
                const bool isSub = s.role == (int) SpeakerRole::Sub;
                const double tDelay = proc.pref ("recheckDelay");
                const double tol = isSub ? 3.0 * tDelay : tDelay, bad = isSub ? 10.0 * tDelay : 4.0 * tDelay;
                if (was->hasArrival && r.hasArrival && std::abs (dt) > tol)
                {
                    const bool missing = dt < 0.0 && s.proc.delayMs > 0.5 && std::abs (std::abs (dt) - s.proc.delayMs) < juce::jmax (0.3, tol * 0.5);
                    if (missing)
                        add (2, tag + ": arrives " + juce::String (std::abs (dt), 2) + " ms early - its " + juce::String (s.proc.delayMs, 2)
                                    + " ms delay does not seem to be in the processor.");
                    else
                        add (std::abs (dt) > bad ? 2 : 1, tag + ": arrives " + juce::String (std::abs (dt), 2) + " ms " + (dt > 0.0 ? "later" : "earlier")
                                                              + " than its delay of " + juce::String (s.proc.delayMs, 2) + " ms should give"
                                                              + (isSub ? " (a sub's arrival is approximate)" : "") + ". Check the delay in the processor.");
                    fine = false;
                }
                if (fine) add (0, tag + ": level and delay as set (" + f1 (std::isfinite (d) ? d : 0.0) + " dB, " + juce::String (dt, 2) + " ms).");
            }
        }
        else
        {
            // the whole system against the Room EQ measurement at that point (the latest round)
            const RoomMeasurement* was = nullptr;
            for (const auto& m : proc.room.meas) if (m.position == fohPoint() && (was == nullptr || m.after)) was = &m;
            if (was == nullptr) add (0, tag + ": measured (no Room EQ measurement there to compare with).");
            else
            {
                double lo, hi;
                proc.room.updateSystem (proc.system);
                proc.room.eqRange (proc.system, lo, hi);
                std::vector<double> diff;
                for (double f = juce::jmax (lo, 40.0); f <= juce::jmin (hi, 12000.0); f *= std::pow (2.0, 1.0 / 3.0))
                {
                    double a = 0.0, b = 0.0;
                    int n = 0;
                    for (double x = f / 1.12; x <= f * 1.12; x *= 1.02) { a += r.tf.dbAt (x); b += was->tf.dbAt (x); ++n; }
                    if (n > 0 && std::isfinite (a) && std::isfinite (b)) diff.push_back ((a - b) / n);
                }
                const double off = eq::median (diff);
                double ss = 0.0, mx = 0.0;
                for (double v : diff) { ss += (v - off) * (v - off); mx = std::max (mx, std::abs (v - off)); }
                const double rms = diff.empty() ? NAN : std::sqrt (ss / (double) diff.size());
                const double tolTonal = proc.pref ("recheckTonal");
                if (std::isfinite (rms) && rms > tolTonal) add (rms > 1.75 * tolTonal ? 2 : 1, tag + ": the tonal balance has moved " + juce::String (rms, 1) + " dB RMS (worst "
                                                                                    + juce::String (mx, 1) + " dB) since Room EQ. Check the EQ in the processor.");
                if (std::abs (off) > 2.0) add (1, tag + ": " + f1 (off) + " dB louder than at Room EQ (the same generator level). Check the master and amp levels.");
                if ((! std::isfinite (rms) || rms <= tolTonal) && std::abs (off) <= 2.0) add (0, tag + ": as left (" + (std::isfinite (rms) ? juce::String (rms, 1) : juce::String ("-")) + " dB RMS).");
            }
        }
        done.push_back (t.id());
        redo = -1;
        ++serial;
        status = proc.recheck.lines.empty() ? juce::String ("Measured.") : proc.recheck.lines.back().text;
        if (onChanged) onChanged();
    }

    LiveAlignProcessor& proc;
    MeasureRunner runner;
    std::vector<int> done;
    std::set<int> problems;
    Task now { 0, 0 };
    int redo = -1, serial = 0;
    juce::String status;
};
} // namespace la::ui
