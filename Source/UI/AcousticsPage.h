// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <thread>
// Tuning step 5: room acoustics, before Room EQ (it decides whether EQ can help). A sweep at each ticked listening
// point, in the guided pop-up, gives a long impulse response: reverberation time and early decay per octave, clarity
// for speech (C50) and music (C80), a speech intelligibility estimate (STI). With the room modes, the coverage and the
// noise rating (from the noise check on Mics), a decision tree reads them: measured? usable? reverberation against the
// room's use, speech, and the verdict: room OK for EQ, EQ will help only partly, or treat before EQ.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "RoomPage.h"
#include "../PluginProcessor.h"
#include "../Analysis/Deconvolve.h"

namespace la::ui
{
// Octave band noise levels of the loudest mic in the noise floor check, when it was calibrated in dB SPL.
inline bool noiseOctaves (const LiveAlignProcessor& p, std::array<double, acoustics::kNoiseBands>& out, juce::String& who)
{
    const auto& c = p.checks;
    if (! c.noiseDone) return false;
    int best = -1;
    for (int m = 0; m < c.noiseMics; ++m)
        if (c.noiseIsSpl[(size_t) m] && c.noiseOct[(size_t) m][3] > -150.0f && (best < 0 || c.noiseA[(size_t) m] > c.noiseA[(size_t) best])) best = m;
    if (best < 0) return false;
    for (int b = 0; b < acoustics::kNoiseBands; ++b)
    {
        const float v = c.noiseOct[(size_t) best][(size_t) b];
        out[(size_t) b] = v > -150.0f ? (double) v : NAN;
    }
    who = p.mics.nameOf (best);
    return true;
}

class AcousticsChart : public juce::Component
{
public:
    explicit AcousticsChart (LiveAlignProcessor& p) : proc (p) {}
    void setMetric (int m) { metric = m; repaint(); }

    static juce::String metricName (int m)
    {
        static const char* n[] = { "Reverberation time (s)", "Early decay time (s)", "Clarity C50, speech (dB)", "Clarity C80, music (dB)" };
        return n[juce::jlimit (0, 3, m)];
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds().toFloat();
        drawPanel (g, r, metricName (metric) + " per octave");
        auto plot = r.reduced (14.0f).withTrimmedTop (30.0f).withTrimmedLeft (34.0f).withTrimmedBottom (18.0f);
        g.setColour (p.bg.withAlpha (0.5f));
        g.fillRect (plot);

        const auto& room = proc.room;
        const bool time = metric < 2;
        const double vMin = time ? 0.0 : -10.0, vMax = time ? 3.0 : 15.0, step = time ? 0.5 : 5.0;
        auto xAt = [&] (int b) { return plot.getX() + plot.getWidth() * ((float) b + 0.5f) / (float) acoustics::kBands; };
        auto yAt = [&] (double v) { return plot.getBottom() - (float) ((juce::jlimit (vMin, vMax, v) - vMin) / (vMax - vMin)) * plot.getHeight(); };

        // the typical range for the room's use (reverberation only)
        if (metric == 0)
        {
            double lo, hi, sti;
            RoomTuning::useTargets (room.use, lo, hi, sti);
            g.setColour (p.good.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yAt (hi), plot.getRight(), yAt (lo)));
            g.setColour (p.good.withAlpha (0.8f));
            g.setFont (font (10.5f));
            g.drawText ("typical for " + RoomTuning::useNames()[room.use].toLowerCase(), plot.reduced (6.0f).withHeight (14.0f).withY (yAt (hi) + 3.0f),
                        juce::Justification::centredRight);
        }
        g.setFont (font (10.5f));
        for (double v = vMin; v <= vMax + 1e-9; v += step)
        {
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) yAt (v), plot.getX(), plot.getRight());
            g.setColour (p.textDim);
            g.drawText (time ? juce::String (v, 1) : juce::String ((int) v), juce::Rectangle<float> (plot.getX() - 34.0f, yAt (v) - 7.0f, 30.0f, 14.0f),
                        juce::Justification::centredRight);
        }
        for (int b = 0; b < acoustics::kBands; ++b)
        {
            g.setColour (p.textDim);
            g.drawText (freqLabel (acoustics::octaves()[(size_t) b]), juce::Rectangle<float> (xAt (b) - 30.0f, plot.getBottom() + 2.0f, 60.0f, 16.0f),
                        juce::Justification::centred);
        }

        auto value = [this] (const acoustics::Band& o)
        {
            switch (metric)
            {
                case 0: return o.rt();
                case 1: return o.edt;
                case 2: return o.c50;
                default: return o.c80;
            }
        };
        auto line = [&] (const acoustics::Result& res, juce::Colour c, float w, bool dots)
        {
            juce::Path path;
            bool pen = false;
            for (int b = 0; b < acoustics::kBands; ++b)
            {
                const double v = value (res.band[(size_t) b]);
                if (! std::isfinite (v)) { pen = false; continue; }
                const float x = xAt (b), y = yAt (v);
                if (! pen) { path.startNewSubPath (x, y); pen = true; } else path.lineTo (x, y);
                if (dots) { g.setColour (c); g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f); }
            }
            strokeTrace (g, path, c, w);
        };
        int k = 0;
        if (room.acousticsFromSweeps())
            for (const auto& w : room.sweeps)
            {
                if (! w.ac.valid) continue;
                line (w.ac, positionColour (juce::jmax (0, proc.positions.indexOf (w.position))).withAlpha (0.45f), 1.2f, false);
                ++k;
            }
        else
            for (const auto& m : room.meas)
            {
                if (! m.ac.valid || ! m.include) continue;
                line (m.ac, positionColour (juce::jmax (0, proc.positions.indexOf (m.position))).withAlpha (0.45f), 1.2f, false);
                ++k;
            }
        if (k == 0)
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("Nothing measured yet. Press Measure with a sweep (the mics where they are attached on Mics), or measure the "
                              "listening positions on 4 Room EQ for a quick estimate.", plot.reduced (40.0f).toNearestInt(), juce::Justification::centred, 3);
            return;
        }
        line (room.acousticsAverage(), p.text, 2.6f, true);
    }

private:
    LiveAlignProcessor& proc;
    int metric = 0;
};

class AcousticsPage : public juce::Component, public GuidedSource, private juce::Timer
{
public:
    std::function<void()> onChanged, onOpenPlan;
    std::function<void (GuidedSource&)> onGuided;

    ~AcousticsPage() override { stopTimer(); proc.recorder.stop(); }

    explicit AcousticsPage (LiveAlignProcessor& p) : proc (p), chart (p)
    {
        addAndMakeVisible (chart);
        for (int i = 0; i < 4; ++i)
        {
            static const char* n[] = { "RT", "EDT", "C50", "C80" };
            metricButtons[i].setButtonText (n[i]);
            metricButtons[i].setRadioGroupId (8301);
            metricButtons[i].setClickingTogglesState (true);
            metricButtons[i].onClick = [this, i] { if (metricButtons[i].getToggleState()) chart.setMetric (i); };
            metricButtons[i].setTooltip (AcousticsChart::metricName (i));
            addAndMakeVisible (metricButtons[i]);
        }
        metricButtons[0].setToggleState (true, juce::dontSendNotification);
        useBox.addItemList (RoomTuning::useNames(), 1);
        useBox.setTooltip ("What the room is mostly used for: sets the typical reverberation time and intelligibility to compare with");
        useBox.onChange = [this] { proc.room.use = useBox.getSelectedItemIndex(); changed(); chart.repaint(); };
        addAndMakeVisible (useBox);
        sweepButton.setTooltip ("A 4-second sweep (twice) through the whole system at each ticked point, in the guided pop-up: a long "
                                "impulse response for the reverberation time of any room. Quiet room, please.");
        sweepButton.onClick = [this]
        {
            if (phase != Phase::Idle) { stopSweep ("Stopped."); return; }
            redo.clear();
            if (onGuided) onGuided (*this);
            else startSweep();
        };
        addAndMakeVisible (sweepButton);
        planButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Points on the plan  \xe2\x86\x92")));
        planButton.onClick = [this] { if (onOpenPlan) onOpenPlan(); };
        addAndMakeVisible (planButton);
        refresh();
    }

    // ---- the sweep: play, record, work out each mic's impulse response ----
    void startSweep()
    {
        auto& st = proc.settings;
        if (st.refSource.load() == 0) { status = "Set a reference on Mics first (the generator copy works)."; repaint(); return; }
        juce::StringArray where;
        for (int m = 0; m < st.micCount.load(); ++m)
            if (proc.mics.position[(size_t) m].isNotEmpty()) where.add (proc.mics.nameOf (m) + " at " + proc.mics.position[(size_t) m]);
        if (where.isEmpty()) { status = "Put a mic at a listening point first."; repaint(); return; }
        saved = { st.genType.load(), st.genSweepSec.load(), st.genOutput.load(), st.genOn.load() };
        st.genType = 3;            // log sweep
        st.genSweepSec = kSweepSec;
        st.genOutput = 0;          // the whole system
        st.genOn = true;
        st.running = true;
        phase = Phase::Lead;
        phaseStart = juce::Time::getMillisecondCounterHiRes();
        status = "Sweeping: " + where.joinIntoString (", ") + ". Keep the room quiet.";
        sweepButton.setButtonText ("Stop");
        startTimerHz (15);
        repaint();
    }

    void stopSweep (const juce::String& why)
    {
        proc.recorder.stop();
        restoreGenerator();
        phase = Phase::Idle;
        stopTimer();
        status = why;
        refreshButtons();
        repaint();
    }

    bool sweeping() const { return phase != Phase::Idle; }

    void refresh()
    {
        proc.syncPositionsWithPlan();
        useBox.setSelectedItemIndex (juce::jlimit (0, 3, proc.room.use), juce::dontSendNotification);
        refreshButtons();
        resized();
        repaint();
        chart.repaint();
    }

    // ---- what the navigation shows ----
    juce::StringArray points() const
    {
        juce::StringArray out;
        for (const auto& pt : proc.positions) if (! proc.room.acSkipped.contains (pt)) out.add (pt);
        return out;
    }
    bool sweptAt (const juce::String& pt) const
    {
        for (const auto& w : proc.room.sweeps) if (w.position == pt && w.ac.valid) return true;
        return false;
    }
    int sweptCount() const { int n = 0; for (const auto& pt : points()) n += sweptAt (pt) ? 1 : 0; return n; }
    juce::String nextHint() const
    {
        if (points().isEmpty()) return "Tick a listening point";
        if (sweptCount() < points().size()) return "Measure with a sweep at " + juce::String (points().size() - sweptCount()) + (points().size() - sweptCount() == 1 ? " point" : " points");
        return {};
    }
    void doNext() { if (phase == Phase::Idle && onGuided) onGuided (*this); }

    // ---- guided: one sweep at each ticked point ----
    juce::String guidedTitle() const override { return "Room acoustics"; }
    int currentPoint() const
    {
        const auto pts = points();
        for (int k = 0; k < pts.size(); ++k) if (pts[k] == redo) return k;
        for (int k = 0; k < pts.size(); ++k) if (! sweptAt (pts[k])) return k;
        return -1;
    }
    int micAt (const juce::String& pt) const
    {
        for (int m = 0; m < proc.tuningMicCount(); ++m) if (proc.mics.position[(size_t) m] == pt) return m;
        return 0;
    }
    std::vector<GuideSpot> guidedSpots() override
    {
        std::vector<GuideSpot> out;
        const auto pts = points();
        const int now = currentPoint();
        for (int k = 0; k < pts.size(); ++k)
        {
            GuideSpot sp;
            sp.id = k;
            sp.label = pts[k];
            sp.marker = proc.plan.ensureMarker (pts[k]);
            sp.x = proc.plan.items[(size_t) sp.marker].x;
            sp.y = proc.plan.items[(size_t) sp.marker].y;
            sp.state = k == now ? GuideSpot::Now : sweptAt (pts[k]) && pts[k] != redo ? GuideSpot::Done : GuideSpot::ToDo;
            if (sp.state == GuideSpot::Now) { sp.mic = micAt (pts[k]); sp.how = "seated ear height, " + aimHowText (proc, sp.mic, RoomAim); }
            out.push_back (sp);
        }
        return out;
    }
    juce::String guidedHowTo() const override
    {
        return "First: unmute every speaker and keep the room quiet. The sweep plays twice through the whole system (about 10 seconds).";
    }
    bool guidedStart() override
    {
        const auto pts = points();
        const int k = currentPoint();
        if (k < 0) { status = "Every ticked point is measured."; return false; }
        proc.setMicPosition (micAt (pts[k]), pts[k]);
        startSweep();
        return phase != Phase::Idle;
    }
    void guidedStop() override { stopSweep ("Stopped."); }
    bool guidedBusy() const override { return phase != Phase::Idle; }
    float guidedProgress() const override { return phase == Phase::Working ? 0.95f : phase == Phase::Lead ? 0.02f : proc.recorder.progress() * 0.9f; }
    juce::String guidedPhase() const override { return phase == Phase::Working ? juce::String ("Working out the impulse response...") : juce::String ("Sweeping..."); }
    int guidedSerial() const override { return serial; }
    juce::String guidedStatus() const override { return status; }
    void guidedRedo (const std::vector<GuideSpot>& was) override { if (! was.empty()) redo = was.front().label; }
    void guidedClosed() override { redo.clear(); refresh(); }
    int guidedAimKind() const override { return RoomAim; }
    int guidedAimMic() const override { const auto pts = points(); const int k = currentPoint(); return k >= 0 ? micAt (pts[k]) : 0; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto& room = proc.room;
        const auto avg = room.acousticsAverage();
        double rtLo, rtHi, stiMin;
        RoomTuning::useTargets (room.use, rtLo, rtHi, stiMin);
        const bool fromSweeps = room.acousticsFromSweeps();

        // tiles
        auto tile = [&] (int i, const juce::String& label, const juce::String& value, const juce::String& unit, juce::Colour c)
        {
            drawTile (g, tiles[(size_t) i].toFloat(), label, value, unit, c, 22.0f);
        };
        auto num = [] (double v, int d) { return ! std::isfinite (v) ? juce::String ("-") : d == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, d); };
        const double rt = avg.rtMid(), sti = avg.sti;
        tile (0, fromSweeps ? "REVERB (RT, 500-1k)" : "REVERB (ESTIMATE)", num (rt, 2), "s", ! std::isfinite (rt) ? p.textDim : rt < rtLo * 0.85 || rt > rtHi * 1.15 ? p.warn : p.good);
        tile (1, "EARLY DECAY (EDT)", num (avg.mid (&acoustics::Band::edt), 2), "s", p.text);
        tile (2, "CLARITY C50 / C80", num (avg.mid (&acoustics::Band::c50), 1) + " / " + num (avg.mid (&acoustics::Band::c80), 1), "dB", p.text);
        tile (3, "SPEECH (STI)", num (sti, 2), acoustics::stiRating (sti), ! std::isfinite (sti) ? p.textDim : sti >= stiMin ? p.good : p.warn);
        const double spread = room.levelSpread (room.count (true) > 0);
        tile (4, "COVERAGE (LEVEL SPREAD)", num (spread, 1), "dB", ! std::isfinite (spread) ? p.textDim : spread <= 6.0 ? p.good : p.warn);
        {
            std::array<double, acoustics::kNoiseBands> L {};
            juce::String who;
            const bool have = noiseOctaves (proc, L, who);
            const double nc = have ? acoustics::noiseCriterion (L) : NAN, nr = have ? acoustics::noiseRating (L) : NAN;
            tile (5, "NOISE (NC / NR)", have ? "NC " + juce::String (juce::roundToInt (nc)) + " / NR " + juce::String (juce::roundToInt (nr)) : juce::String ("-"),
                  "", ! have ? p.textDim : nc <= 35.0 ? p.good : p.warn);
        }

        // measuring
        {
            juce::String t = status;
            if (t.isEmpty())
                t = fromSweeps ? juce::String (sweptCount()) + " of " + juce::String (points().size()) + " ticked points swept."
                               : room.acousticsCount() > 0 ? juce::String ("Only an estimate so far (from the Room EQ pink noise). Measure with a sweep for the real numbers.")
                                                           : juce::String ("Not measured yet: press Measure with a sweep.");
            g.setColour (phase != Phase::Idle ? p.accent : fromSweeps ? p.textDim : p.warn);
            g.setFont (font (11.5f, phase != Phase::Idle));
            g.drawFittedText (t, sweepInfo, juce::Justification::topLeft, 2);
        }

        // the points: tick to sweep there, with what came back
        drawPanel (g, posCard.toFloat(), "Listening points");
        for (int i = 0; i < proc.positions.size(); ++i)
        {
            const auto row = pointRow (i);
            if (row.getBottom() > pointsList.getBottom()) break;
            const auto& name = proc.positions[i];
            const bool on = ! room.acSkipped.contains (name);
            auto r = row.reduced (6, 0);
            const auto box = r.removeFromLeft (16).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            g.setColour (on ? positionColour (i) : p.textDim);
            if (on)
            {
                g.fillRoundedRectangle (box, 3.0f);
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
                tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.5f);
                tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
                g.setColour (p.bg);
                g.strokePath (tick, juce::PathStrokeType (1.8f));
            }
            else g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.3f);
            r.removeFromLeft (8);
            const acoustics::Result* res = nullptr;
            for (const auto& w : room.sweeps) if (w.position == name && w.ac.valid) res = &w.ac;
            g.setColour (p.text);
            g.setFont (font (12.0f, true));
            g.drawText (name, r.removeFromLeft (110), juce::Justification::centredLeft, true);
            g.setFont (monoFont (11.0f));
            g.setColour (res != nullptr ? p.textDim : p.textFaint);
            g.drawText (res != nullptr ? "RT " + num (res->rtMid(), 2) + "  STI " + num (res->sti, 2) : juce::String (on ? "to sweep" : "left out"), r, juce::Justification::centredLeft, true);
        }

        // the decision tree
        drawPanel (g, treeCard.toFloat(), "What it means");
        paintTree (g, avg, rtLo, rtHi, stiMin);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (70);
        const int tw = (top.getWidth() - 5 * 8) / 6;
        for (int i = 0; i < 6; ++i) { tiles[(size_t) i] = top.removeFromLeft (tw); top.removeFromLeft (8); }
        r.removeFromTop (10);
        auto right = r.removeFromRight (420);
        r.removeFromRight (10);
        auto bar = r.removeFromBottom (32);
        r.removeFromBottom (6);
        chart.setBounds (r);
        const int bw = 64;
        for (auto& b : metricButtons) { b.setBounds (bar.removeFromLeft (bw).reduced (0, 2)); bar.removeFromLeft (4); }
        bar.removeFromLeft (12);
        useBox.setBounds (bar.removeFromRight (juce::jmin (330, bar.getWidth())).reduced (0, 2));
        sweepButton.setBounds (right.removeFromTop (34));
        right.removeFromTop (6);
        sweepInfo = right.removeFromTop (34);
        right.removeFromTop (6);
        const int listH = 36 + juce::jmax (1, proc.positions.size()) * 26 + 44;
        posCard = right.removeFromTop (juce::jmin (listH, right.getHeight() / 2));
        {
            auto c = posCard.reduced (12, 0).withTrimmedTop (34);
            planButton.setBounds (c.removeFromBottom (40).withSizeKeepingCentre (c.getWidth(), 28));
            pointsList = c;
        }
        right.removeFromTop (10);
        treeCard = right;
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < proc.positions.size(); ++i)
            if (pointRow (i).contains (e.getPosition()) && e.x < pointRow (i).getX() + 30)
            {
                const auto& name = proc.positions[i];
                if (proc.room.acSkipped.contains (name)) proc.room.acSkipped.removeString (name);
                else proc.room.acSkipped.add (name);
                changed();
                return;
            }
    }

private:
    enum class Phase { Idle, Lead, Recording, Working };
    static constexpr double kSweepSec = 4.0;
    struct Saved { int type = 0; float sweepSec = 3.0f; int output = 0; bool on = false; } saved;

    juce::Rectangle<int> pointRow (int i) const { return { pointsList.getX(), pointsList.getY() + i * 26, pointsList.getWidth(), 24 }; }

    void refreshButtons()
    {
        sweepButton.setButtonText (phase != Phase::Idle ? juce::String ("Stop")
                                   : sweptCount() >= points().size() && ! points().isEmpty() ? juce::String ("Measure again with a sweep") : juce::String ("Measure with a sweep"));
        sweepButton.setColour (juce::TextButton::buttonColourId, phase != Phase::Idle ? pal().bad : pal().accent);
        sweepButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
    }

    void changed() { refreshButtons(); repaint(); if (onChanged) onChanged(); }

    // The decision tree, one node per question, each with the numbers and a plain sentence.
    void paintTree (juce::Graphics& g, const acoustics::Result& avg, double rtLo, double rtHi, double stiMin)
    {
        const auto& p = pal();
        const auto& room = proc.room;
        auto r = treeCard.reduced (12, 0).withTrimmedTop (34);
        struct Node { int state; juce::String title, text; };   // state 0 to do, 1 fine, 2 a look, 3 a problem
        std::vector<Node> nodes;
        const bool swept = room.acousticsFromSweeps(), any = room.acousticsCount() > 0;
        const double rt = avg.rtMid();
        nodes.push_back ({ swept ? 1 : any ? 2 : 0, "1  Measured?",
                           swept ? "Sweeps at " + juce::String (sweptCount()) + " point(s)." : any ? "Only an estimate from the Room EQ pink noise: sweep for the real numbers."
                                                                                                    : "Not yet: Measure with a sweep at each ticked point." });
        if (any)
        {
            // 2 usable: enough decay above the noise
            double worst = 200.0;
            for (const auto& w : room.sweeps) if (w.ac.valid && std::isfinite (w.ac.band[2].rangeDb)) worst = std::min (worst, w.ac.band[2].rangeDb);
            const bool noisy = worst < 30.0;
            nodes.push_back ({ noisy ? 2 : 1, "2  Usable?", noisy ? "Only " + juce::String (juce::roundToInt (worst)) + " dB of decay above the noise: sweep louder (or quieter room) and measure again."
                                                                  : "Clean enough: the decay stands well above the noise." });
            // 3 reverberation against the room's use, the lows against the mids
            if (std::isfinite (rt))
            {
                const double low = 0.5 * ((std::isfinite (avg.band[0].rt()) ? avg.band[0].rt() : rt) + (std::isfinite (avg.band[1].rt()) ? avg.band[1].rt() : rt));
                const double bass = rt > 0.05 ? low / rt : 1.0;
                const double hiRt = std::isfinite (avg.band[5].rt()) ? avg.band[5].rt() : rt;
                juce::String t = "RT " + juce::String (rt, 2) + " s (" + juce::String (rtLo, 1) + " - " + juce::String (rtHi, 1) + " s is usual for "
                                 + RoomTuning::useNames()[room.use].toLowerCase() + ").";
                int st = 1;
                if (bass > 1.3) { t << " The lows ring " << juce::String (bass, 1) << "x longer than the mids: bass build-up. Bass traps, sub placement or a cardioid sub array; EQ won't shorten the decay."; st = 2; }
                if (rt > rtHi * 1.15) { t << " Long in the mids and highs: absorption on the side and back walls; keep the PA off the walls."; st = std::max (st, rt > rtHi * 1.5 ? 3 : 2); }
                else if (rt < rtLo * 0.85) t << " Drier than usual: fine for the PA.";
                if (hiRt > rt * 1.2) t << " The highs ring longer than the mids: hard, bare surfaces.";
                const auto modes = room.modes (room.count (true) > 0 && room.count (false) == 0);
                if (! modes.empty()) t << " Room modes at " << freqLabel (modes.front().freq) << " Hz" << (modes.size() > 1 ? " and more" : "") << ": cut gently; moving the subs changes them.";
                nodes.push_back ({ st, "3  Reverberation", t });
            }
            // 4 speech, and late energy
            {
                const double edt = avg.mid (&acoustics::Band::edt), sti = avg.sti;
                juce::String t;
                int st = 1;
                if (std::isfinite (sti)) { t << "STI " << juce::String (sti, 2) << " (" << acoustics::stiRating (sti) << "; aim for " << juce::String (stiMin, 2) << " or more)."; if (sti < stiMin) st = sti < stiMin - 0.1 ? 3 : 2; }
                if (std::isfinite (edt) && std::isfinite (rt) && rt > edt * 1.4)
                {
                    t << " The decay starts fast and ends slow (EDT " << juce::String (edt, 2) << " s against RT " << juce::String (rt, 2) << " s): late reflections, "
                      << "an echo from a far wall or a coupled space (a balcony, a foyer). Listen for it at the back.";
                    st = std::max (st, 2);
                }
                if (t.isEmpty()) t = "Not rated.";
                nodes.push_back ({ st, "4  Speech and echoes", t });
            }
        }
        juce::String why;
        const int verdict = room.acousticsVerdict (why);
        nodes.push_back ({ verdict == 0 ? 0 : verdict == 1 ? 1 : verdict == 2 ? 2 : 3, "5  So:", why + (verdict > 0 && ! swept ? " (from the estimate)" : juce::String()) });

        for (size_t k = 0; k < nodes.size(); ++k)
        {
            const auto& n = nodes[k];
            const int lines = n.text.length() > 150 ? 4 : n.text.length() > 90 ? 3 : 2;
            auto row = r.removeFromTop (20 + lines * 15);
            if (row.getBottom() > treeCard.getBottom() - 6) break;
            const auto badge = row.removeFromLeft (22).toFloat().withHeight (22.0f).withSizeKeepingCentre (16.0f, 16.0f);
            drawStatusBadge (g, badge, n.state == 1 ? StepStatus::Ready : n.state == 0 ? StepStatus::NotStarted : StepStatus::Attention);
            if (n.state == 3) { g.setColour (p.bad); g.fillEllipse (badge); g.setColour (p.onAccent); g.setFont (font (12.0f, true)); g.drawText ("!", badge, juce::Justification::centred); }
            row.removeFromLeft (6);
            g.setColour (p.text);
            g.setFont (font (12.5f, true));
            g.drawText (n.title, row.removeFromTop (18), juce::Justification::centredLeft, true);
            g.setColour (n.state == 0 ? p.textDim : n.state == 1 ? p.textDim : n.state == 2 ? p.warn : p.bad);
            g.setFont (font (11.5f, k + 1 == nodes.size()));
            g.drawFittedText (n.text, row, juce::Justification::topLeft, lines);
            // the line down to the next node
            if (k + 1 < nodes.size())
            {
                g.setColour (p.border);
                g.drawVerticalLine (treeCard.getX() + 12 + 11, (float) row.getBottom() - 2.0f, (float) row.getBottom() + 6.0f);
            }
            r.removeFromTop (6);
        }
        g.setColour (p.textFaint);
        g.setFont (font (10.5f));
        g.drawFittedText ("From the system's own sweeps (ISO 3382 / IEC 60268-16 methods); a full acoustic survey uses an omnidirectional source.",
                          treeCard.reduced (12, 8).removeFromBottom (28), juce::Justification::bottomLeft, 2);
    }

    void restoreGenerator()
    {
        auto& st = proc.settings;
        st.genOn = false;
        st.genType = saved.type;
        st.genSweepSec = saved.sweepSec;
        st.genOutput = saved.output;
    }

    double rate() const { return proc.getSampleRate() > 1000.0 ? proc.getSampleRate() : 48000.0; }

    void timerCallback() override
    {
        const double el = juce::Time::getMillisecondCounterHiRes() - phaseStart;
        if (phase == Phase::Lead && el > 300.0)
        {
            // two sweeps and the silence after each: the second is clean of the start-up
            const double period = kSweepSec + 0.5;
            proc.recorder.start ((int) (2.0 * period * rate() + 0.3 * rate()), proc.settings.micCount.load());
            phase = Phase::Recording;
        }
        else if (phase == Phase::Recording && proc.recorder.done())
        {
            restoreGenerator();
            phase = Phase::Working;
            status = "Working out each mic's impulse response...";
            // the deconvolution is a few large FFTs: off the message thread
            struct Job { std::vector<std::vector<float>> mics; std::vector<float> ref; double fs; int n; std::vector<juce::String> where, names; };
            auto job = std::make_shared<Job>();
            const auto& rec = proc.recorder;
            job->fs = rate();
            job->n = rec.length;
            job->ref = rec.ref;
            for (int m = 0; m < rec.numMics; ++m)
                if (proc.mics.position[(size_t) m].isNotEmpty())
                {
                    job->mics.push_back (rec.mic[(size_t) m]);
                    job->where.push_back (proc.mics.position[(size_t) m]);
                    job->names.push_back (proc.mics.nameOf (m));
                }
            juce::Component::SafePointer<AcousticsPage> safe (this);
            std::thread ([job, safe]
            {
                auto results = std::make_shared<std::vector<acoustics::Result>>();
                for (const auto& x : job->mics)
                    results->push_back (acoustics::analyse (deconv::impulse (job->ref.data(), x.data(), job->n, job->fs, 3.5), job->fs));
                juce::MessageManager::callAsync ([safe, job, results] { if (safe != nullptr) safe->sweepDone (job->where, job->names, *results); });
            }).detach();
        }
        else if (phase == Phase::Recording && el > (2.0 * (kSweepSec + 0.5) + 6.0) * 1000.0)
        {
            stopSweep ("The recording did not finish: check the audio device.");
            return;
        }
        if (phase == Phase::Lead || phase == Phase::Recording) { if (! proc.settings.genOn.load()) { stopSweep ("Stopped: the generator was turned off."); return; } }
        repaint (sweepInfo.expanded (2));
    }

public:
    void sweepDone (const std::vector<juce::String>& where, const std::vector<juce::String>& names, const std::vector<acoustics::Result>& results)
    {
        stopTimer();
        phase = Phase::Idle;
        juce::StringArray done, failed;
        auto& sw = proc.room.sweeps;
        for (size_t k = 0; k < results.size() && k < where.size(); ++k)
        {
            if (! results[k].valid || ! std::isfinite (results[k].rtMid())) { failed.add (where[k]); continue; }
            sw.erase (std::remove_if (sw.begin(), sw.end(), [&] (const SweepMeasurement& w) { return w.position == where[k]; }), sw.end());
            sw.push_back ({ where[k], names[k], juce::Time::getCurrentTime().formatted ("%H:%M"), results[k] });
            done.add (where[k] + " " + juce::String (results[k].rtMid(), 2) + " s");
        }
        status = done.isEmpty() ? juce::String ("Nothing clear came back: raise the level or reduce the noise, and check the reference.")
                                : "RT60 " + done.joinIntoString (", ") + "." + (failed.isEmpty() ? juce::String() : " Not clear at " + failed.joinIntoString (", ") + ".");
        if (! done.isEmpty()) { ++serial; redo.clear(); }
        changed();
        chart.repaint();
    }

private:
    LiveAlignProcessor& proc;
    AcousticsChart chart;
    juce::TextButton sweepButton { "Measure with a sweep" }, planButton;
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    juce::String status, redo;
    int serial = 0;
    juce::Rectangle<int> sweepInfo, pointsList;
    juce::TextButton metricButtons[4];
    juce::ComboBox useBox;
    std::array<juce::Rectangle<int>, 6> tiles;
    juce::Rectangle<int> posCard, treeCard;
};
} // namespace la::ui
