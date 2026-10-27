// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Tuning step 3: Level balance, before time alignment. Each speaker on its own, measured at the listening points,
// so the speakers play at the right level against each other where the audience hears them: left against right at
// the centre, a fill against the mains at the seats it covers, the sub against the tops for the target's low end.
// The trims go on the processor sheet; measuring again checks they took.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "GuidedMeasure.h"
#include "MeasureRunner.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class LevelPage : public juce::Component, public GuidedSource, private juce::Timer
{
public:
    std::function<void()> onChanged, onOpenPlan, onOpenSheet;
    std::function<void (GuidedSource&)> onGuided;

    explicit LevelPage (LiveAlignProcessor& p) : proc (p), lv (p.level), runner (p)
    {
        measureButton.onClick = [this] { if (runner.busy()) { runner.stop(); status = "Stopped."; repaint(); } else startGuided (false); };
        measureButton.setTooltip ("Each speaker on its own at each ticked point, in the guided pop-up (mute the others when they share an output)");
        recheckButton.onClick = [this] { startGuided (true); };
        recheckButton.setTooltip ("After the trims are in the processor: measure each speaker again to see that they took");
        applyButton.onClick = [this] { applyTrims(); };
        applyButton.setTooltip ("Put the suggested trims on the processor sheet (each speaker's level), to enter in your processor");
        planButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Points on the plan  \xe2\x86\x92")));
        planButton.onClick = [this] { if (onOpenPlan) onOpenPlan(); };
        sheetButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Processor sheet  \xe2\x86\x92")));
        sheetButton.onClick = [this] { if (onOpenSheet) onOpenSheet(); };
        for (auto* b : { &measureButton, &recheckButton, &applyButton, &planButton, &sheetButton }) addAndMakeVisible (*b);
        lookAndFeelChanged();
        refresh();
        startTimerHz (15);
    }

    ~LevelPage() override { stopTimer(); }

    void setSnapshot (const Snapshot& s) { runner.setSnapshot (&s); }

    void lookAndFeelChanged() override
    {
        for (auto* b : { &measureButton, &applyButton })
        {
            b->setColour (juce::TextButton::buttonColourId, pal().accent);
            b->setColour (juce::TextButton::textColourOffId, pal().onAccent);
        }
    }

    // After a venue is opened, or the points or speakers changed.
    void refresh()
    {
        proc.syncPositionsWithPlan();
        // the points measured at: kept in plan order; the first time, the centre / FOH
        juce::StringArray keep;
        for (const auto& pt : proc.positions) if (lv.points.contains (pt)) keep.add (pt);
        lv.points = keep;
        if (lv.points.isEmpty() && ! proc.positions.isEmpty())
            lv.points.add (proc.positions.contains ("FOH") ? juce::String ("FOH") : proc.positions[0]);
        refreshButtons();
        resized();
        repaint();
    }

    // ---- what the navigation shows ----
    int readingsCount (bool recheck) const { int n = 0; for (const auto& r : lv.readings) n += r.recheck == recheck ? 1 : 0; return n; }
    int tasksCount() const { return lv.points.size() * (int) proc.system.sources.size(); }
    std::vector<LevelTrim> trims (bool recheck) const
    {
        auto t = lv.suggest (proc.system, proc.room.target, recheck, proc.levelAims());
        for (auto& c : LevelBalance::cabinetNotes (proc.system)) t.push_back (c);
        return t;
    }
    int attentionCount (bool recheck) const { int n = 0; for (const auto& t : trims (recheck)) n += t.attention ? 1 : 0; return n; }
    bool done() const
    {
        if (readingsCount (false) < tasksCount() || tasksCount() == 0) return false;
        if (attentionCount (false) == 0) return true;
        return lv.applied && readingsCount (true) >= (int) proc.system.sources.size() && attentionCount (true) == 0;
    }
    juce::String nextHint() const
    {
        if (tasksCount() == 0) return "Add a listening point on the plan";
        if (readingsCount (false) < tasksCount()) return "Measure each speaker at " + lv.points.joinIntoString (", ");
        if (attentionCount (false) > 0 && ! lv.applied) return "Put the trims on the processor sheet";
        if (lv.applied && readingsCount (true) < (int) proc.system.sources.size()) return "Measure again to check the trims";
        if (lv.applied && attentionCount (true) > 0) return "Enter the trims still out, then measure again";
        return {};
    }
    bool busy() const { return runner.busy(); }
    juce::String statusText() const { return status; }

    // ---- guided: each speaker alone, every placed mic at once ----
    // A task is one speaker at one point, but a pass measures the speaker at every ticked point that has a mic in
    // it, so with the mics spread over the points the rounds are per speaker, not per point. A point with no mic
    // gets the roaming mic (mic 1) on its own, as before.
    juce::String guidedTitle() const override { return recheckRun ? "Level balance: check" : "Level balance"; }

    struct Task { int point, spk; int id() const { return point * 100 + spk; } };

    // The ticked points with a mic in them now (mic, point), the lead point's mic first.
    std::vector<std::pair<int, juce::String>> passMics (const juce::String& lead) const
    {
        std::vector<std::pair<int, juce::String>> out;
        const int leadMic = micAt (lead);
        out.push_back ({ leadMic, lead });
        for (int m = 0; m < proc.tuningMicCount(); ++m)
        {
            const auto& pos = proc.mics.position[(size_t) m];
            if (m != leadMic && pos.isNotEmpty() && lv.points.contains (pos)) out.push_back ({ m, pos });
        }
        return out;
    }
    bool inPass (const Task& t, const Task& now) const
    {
        if (t.spk != now.spk) return false;
        for (const auto& [m, p] : passMics (lv.points[now.point])) if (p == lv.points[t.point]) return true;
        return false;
    }
    std::vector<Task> tasks() const
    {
        std::vector<Task> out;
        const int n = (int) proc.system.sources.size();
        for (int pi = 0; pi < lv.points.size(); ++pi)
        {
            if (recheckRun && pi > 0) break;   // the check is at the centre
            for (int i = 0; i < n; ++i) out.push_back ({ pi, i });
        }
        return out;
    }
    bool taskDone (const Task& t) const { return lv.find (t.spk, lv.points[t.point], recheckRun) != nullptr && redo != t.id(); }
    int currentIndex() const
    {
        const auto ts = tasks();
        for (size_t k = 0; k < ts.size(); ++k) if (ts[k].id() == redo) return (int) k;
        for (size_t k = 0; k < ts.size(); ++k) if (! taskDone (ts[k])) return (int) k;
        return -1;
    }
    int micAt (const juce::String& point) const
    {
        for (int m = 0; m < proc.tuningMicCount(); ++m) if (proc.mics.position[(size_t) m] == point) return m;
        return 0;   // the roaming mic
    }

    std::vector<GuideSpot> guidedSpots() override
    {
        std::vector<GuideSpot> out;
        const auto ts = tasks();
        const int now = currentIndex();
        for (int k = 0; k < (int) ts.size(); ++k)
        {
            const auto& t = ts[(size_t) k];
            const auto& point = lv.points[t.point];
            GuideSpot sp;
            sp.id = t.id();
            sp.label = proc.system.nameOf (t.spk) + " at " + point;
            sp.marker = proc.plan.ensureMarker (point);
            sp.x = proc.plan.items[(size_t) sp.marker].x;
            sp.y = proc.plan.items[(size_t) sp.marker].y;
            // the pass measures every point with a mic in it: those are "now" too
            const bool inNow = now >= 0 && (k == now || (! taskDone (t) && inPass (t, ts[(size_t) now])));
            sp.state = inNow ? GuideSpot::Now : taskDone (t) ? GuideSpot::Done : GuideSpot::ToDo;
            if (sp.state == GuideSpot::Now)
            {
                sp.mic = micAt (point);
                sp.speaker = t.spk;
                sp.how = "seated ear height, " + aimHowText (proc, sp.mic, RoomAim);
            }
            out.push_back (sp);
        }
        return out;
    }
    bool guidedNeedsProcessor() const override { return true; }
    juce::String guidedHowTo() const override
    {
        const auto ts = tasks();
        const int k = currentIndex();
        if (k < 0) return "Every speaker is measured at every point.";
        const auto& t = ts[(size_t) k];
        if (const auto m = proc.system.muteText ({ t.spk }); m.isNotEmpty()) return "First: " + m;
        const auto pm = passMics (lv.points[t.point]);
        return "Only " + proc.system.nameOf (t.spk) + " plays, with its processing in place (filters, polarity, level)."
               + (pm.size() > 1 ? " Measured at " + juce::String ((int) pm.size()) + " points at once (every ticked point with a mic in it)." : juce::String());
    }
    bool guidedStart() override
    {
        const auto ts = tasks();
        const int k = currentIndex();
        if (k < 0) { status = "Every speaker is measured."; return false; }
        const auto t = ts[(size_t) k];
        const auto point = lv.points[t.point];
        const int mic = micAt (point);
        proc.setMicPosition (mic, point);
        nowTask = t;
        const bool again = recheckRun;
        const auto& src = proc.system.sources[(size_t) t.spk];
        const auto pm = passMics (point);
        std::vector<int> mics;
        for (const auto& [m, p] : pm)
        {
            mics.push_back (m);
            // the check after the trims: the arrival is known from the as-found reading, so the averaging starts early
            if (const auto* was = lv.find (t.spk, p, false); again && was != nullptr && was->hasArrival)
                runner.hintDelay (m, src.output, p, was->arrivalMs - was->delayAtMs + src.proc.delayMs);
        }
        double lo, hi;
        proc.system.playRange (t.spk, lo, hi);
        const bool ok = runner.start (src.output, mics, -1, [this, t, again] (const std::vector<MeasureRunner::Result>& rs)
        {
            if (rs.empty() || ! rs.front().ok) { status = rs.empty() ? juce::String ("No result.") : rs.front().error; repaint(); return; }
            juce::StringArray got;
            double lo2, hi2;
            proc.system.playRange (t.spk, lo2, hi2);
            for (const auto& r : rs)
            {
                if (! r.ok || ! lv.points.contains (r.point)) continue;
                LevelReading rd;
                rd.spk = t.spk;
                rd.point = r.point;
                rd.mic = r.mic;
                rd.when = juce::Time::getCurrentTime().formatted ("%H:%M");
                rd.tf = twelfthOctave (r.tf);
                rd.hasArrival = r.hasArrival;
                rd.arrivalMs = r.arrivalMs;
                rd.delayAtMs = proc.system.sources[(size_t) t.spk].proc.delayMs;
                rd.trimAtDb = proc.system.sources[(size_t) t.spk].trimDb;
                rd.eqAtDb = LevelBalance::eqInRange (proc.system, t.spk);
                rd.genDb = r.genDb;
                rd.splDbA = r.splDbA;
                rd.recheck = again;
                lv.set (std::move (rd));
                got.add (r.point + " " + juce::String (lv.find (t.spk, r.point, again)->band (lo2, hi2), 1) + " dB");
            }
            redo = -1;
            ++serial;
            status = proc.system.nameOf (t.spk) + " at " + got.joinIntoString (", ") + ".";
            changed();
        }, lo, hi);
        if (! ok) status = runner.lastError();
        return ok;
    }
    void guidedStop() override { runner.stop(); }
    bool guidedBusy() const override { return runner.busy(); }
    float guidedProgress() const override { return runner.progress(); }
    juce::String guidedPhase() const override { return runner.phaseText (proc.system.nameOf (nowTask.spk)); }
    int guidedSerial() const override { return serial; }
    juce::String guidedStatus() const override { return status; }
    void guidedRedo (const std::vector<GuideSpot>& was) override { if (! was.empty()) redo = was.front().id; }
    void guidedClosed() override { redo = -1; refresh(); }
    int guidedAimKind() const override { return RoomAim; }
    int guidedAimMic() const override
    {
        const auto ts = tasks();
        const int k = currentIndex();
        return k >= 0 ? micAt (lv.points[ts[(size_t) k].point]) : 0;
    }

    void applyNow() { applyTrims(); }

    // For the preview: measure without the pop-up.
    void measureNow (int pointIndex, int spk, bool again)
    {
        recheckRun = again;
        redo = pointIndex * 100 + spk;
        guidedStart();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, pointsCard.toFloat(), "Listening points");
        drawPanel (g, tableCard.toFloat(), "Each speaker alone, at each point (dB)");
        drawStep (g, step1, 1, "Measure", readingsCount (false) >= tasksCount() && tasksCount() > 0);
        drawStep (g, step2, 2, "Trims", readingsCount (false) >= tasksCount() && (attentionCount (false) == 0 || lv.applied));
        drawStep (g, step3, 3, "Check", done());

        // points: tick = measured here; the first is the centre
        for (int i = 0; i < proc.positions.size(); ++i)
        {
            const auto row = pointRow (i);
            if (row.getBottom() > pointsList.getBottom()) break;
            const auto& name = proc.positions[i];
            const bool on = lv.points.contains (name);
            auto r = row.reduced (8, 0);
            const auto box = r.removeFromLeft (16).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            g.setColour (on ? p.accent : p.textDim);
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
            g.setColour (p.text);
            g.setFont (font (12.5f, true));
            g.drawText (name, r, juce::Justification::centredLeft, true);
            if (on && lv.points[0] == name)
            {
                g.setColour (p.accent);
                g.setFont (font (10.5f, true));
                g.drawText ("CENTRE", r, juce::Justification::centredRight, true);
            }
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawFittedText ("Tick where to measure: the centre (FOH) first, then a seat in each fill's or delay's area. The first ticked point "
                          "is where left and right, and the sub and the tops, are compared.", pointsHint, juce::Justification::topLeft, 4);

        paintTable (g);

        // steps
        auto txt = [&] (juce::Rectangle<int> r, const juce::String& t, juce::Colour c, bool bold = false)
        {
            g.setColour (c);
            g.setFont (font (11.5f, bold));
            g.drawFittedText (t, r, juce::Justification::topLeft, 8, 1.0f);
        };
        if (runner.busy()) txt (measureInfo, runner.phaseText (proc.system.nameOf (nowTask.spk)), p.accent, true);
        else txt (measureInfo, status.isNotEmpty() ? status
                                                   : juce::String (readingsCount (false)) + " of " + juce::String (tasksCount()) + " measured. Each speaker plays alone, "
                                                     "with its processing in place.", status.isNotEmpty() ? p.accent : p.textDim);
        {
            auto r = trimsArea;
            const auto list = trims (false);
            if (readingsCount (false) == 0) txt (r, "Measure first.", p.textFaint);
            for (const auto& t : list)
            {
                const int lines = 1 + t.why.length() / 52;
                if (r.getHeight() < lines * 15) break;
                g.setColour (t.attention ? (lv.applied ? p.text : p.warn) : p.good);
                g.setFont (font (11.5f, t.attention && ! lv.applied));
                g.drawFittedText (t.why, r.removeFromTop (lines * 15 + 2), juce::Justification::topLeft, lines, 1.0f);
                r.removeFromTop (6);
            }
            if (! list.empty() && attentionCount (false) == 0) txt (r, "Every speaker is within its tolerance: nothing to trim.", p.good, true);
            if (lv.applied) txt (r, "Put on the processor sheet " + lv.appliedOn + ": enter the trims in the processor, then measure again to check.", p.accent, true);
        }
        {
            juce::String t;
            juce::Colour c = p.textDim;
            if (readingsCount (true) == 0) t = lv.applied ? "Once the trims are in your processor, measure each speaker again at " + (lv.points.isEmpty() ? juce::String ("the centre") : lv.points[0]) + "."
                                                          : "After trimming, measure again to check.";
            else
            {
                const int left = attentionCount (true);
                juce::StringArray lines;
                for (const auto& tr : trims (true)) if (tr.spk >= 0) lines.add (tr.why);
                t = (left == 0 ? "Checked: every level is within its tolerance.\n" : "Still out: " + juce::String (left) + ".\n") + lines.joinIntoString ("\n");
                c = left == 0 ? p.good : p.warn;
            }
            txt (checkInfo, t, c);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        pointsCard = r.removeFromLeft (260);
        r.removeFromLeft (10);
        auto right = r.removeFromRight (360);
        r.removeFromRight (10);
        tableCard = r;
        {
            auto c = pointsCard.reduced (12, 0).withTrimmedTop (36);
            planButton.setBounds (c.removeFromBottom (44).withSizeKeepingCentre (c.getWidth(), 28));
            pointsHint = c.removeFromBottom (70);
            pointsList = c;
        }
        step1 = right.removeFromTop (150);
        right.removeFromTop (8);
        step3 = right.removeFromBottom (190);
        right.removeFromBottom (8);
        step2 = right;
        {
            auto c = step1.reduced (12, 0).withTrimmedTop (40);
            measureButton.setBounds (c.removeFromTop (32));
            c.removeFromTop (6);
            measureInfo = c;
        }
        {
            auto c = step2.reduced (12, 0).withTrimmedTop (40);
            auto b = c.removeFromBottom (38).withSizeKeepingCentre (c.getWidth(), 30);
            sheetButton.setBounds (b.removeFromRight (170));
            b.removeFromRight (8);
            applyButton.setBounds (b);
            c.removeFromBottom (6);
            trimsArea = c;
        }
        {
            auto c = step3.reduced (12, 0).withTrimmedTop (40);
            recheckButton.setBounds (c.removeFromTop (30));
            c.removeFromTop (6);
            checkInfo = c;
        }
        tableArea = tableCard.reduced (14, 0).withTrimmedTop (36).withTrimmedBottom (10);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < proc.positions.size(); ++i)
            if (pointRow (i).contains (e.getPosition()))
            {
                const auto& name = proc.positions[i];
                if (lv.points.contains (name)) lv.points.removeString (name);
                else
                {
                    // keep plan order
                    juce::StringArray next;
                    for (const auto& pt : proc.positions) if (lv.points.contains (pt) || pt == name) next.add (pt);
                    lv.points = next;
                }
                changed();
                return;
            }
    }

private:
    void startGuided (bool again)
    {
        if (runner.busy()) return;
        if (lv.points.isEmpty()) { status = "Tick a listening point first (the centre / FOH)."; repaint(); return; }
        recheckRun = again;
        redo = -1;
        if (again)
        {
            // a new check: the earlier check readings are replaced
            lv.readings.erase (std::remove_if (lv.readings.begin(), lv.readings.end(), [] (const LevelReading& x) { return x.recheck; }), lv.readings.end());
        }
        else if (readingsCount (false) >= tasksCount())
        {
            // every one measured: measure them all again (a fresh as-found)
            lv.readings.erase (std::remove_if (lv.readings.begin(), lv.readings.end(), [] (const LevelReading& x) { return ! x.recheck; }), lv.readings.end());
            lv.applied = false;
        }
        if (onGuided) onGuided (*this);
    }

    void applyTrims()
    {
        int n = 0;
        for (const auto& t : trims (false))
        {
            if (! t.attention || t.spk < 0 || std::abs (t.db) < 0.05) continue;
            auto& s = proc.system.sources[(size_t) t.spk];
            s.trimDb = juce::jlimit (-24.0, 24.0, s.trimDb + t.db);
            ++n;
        }
        lv.applied = true;
        lv.appliedOn = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        status = n == 0 ? juce::String ("Nothing to trim.") : juce::String (n) + (n == 1 ? " trim is" : " trims are") + " on the processor sheet: enter them, then measure again to check.";
        changed();
    }

    void paintTable (juce::Graphics& g)
    {
        const auto& p = pal();
        auto r = tableArea;
        const int n = (int) proc.system.sources.size();
        const int cols = juce::jmax (1, lv.points.size());
        if (lv.points.isEmpty())
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("Tick a listening point on the left (the centre / FOH first).", r, juce::Justification::centred, 2);
            return;
        }
        const int nameW = 170, colW = juce::jmin (140, (r.getWidth() - nameW) / cols);
        auto head = r.removeFromTop (24);
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("SPEAKER", head.removeFromLeft (nameW), juce::Justification::centredLeft);
        for (int c = 0; c < cols; ++c) g.drawText (lv.points[c].toUpperCase(), head.removeFromLeft (colW), juce::Justification::centredLeft, true);
        r.removeFromTop (4);
        for (int i = 0; i < n; ++i)
        {
            auto row = r.removeFromTop (34);
            if (row.getBottom() > tableArea.getBottom()) break;
            g.setColour (p.panelRaised.withAlpha (i % 2 == 0 ? 0.6f : 0.25f));
            g.fillRoundedRectangle (row.toFloat(), 5.0f);
            auto nm = row.removeFromLeft (nameW).reduced (8, 0);
            g.setColour (traceColour (i));
            g.fillRoundedRectangle (nm.removeFromLeft (4).toFloat().withSizeKeepingCentre (4.0f, 18.0f), 2.0f);
            nm.removeFromLeft (8);
            g.setColour (p.text);
            g.setFont (font (12.5f, true));
            const auto& s = proc.system.sources[(size_t) i];
            g.drawText (proc.system.nameOf (i), nm.removeFromTop (nm.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (10.5f));
            double lo, hi;
            proc.system.playRange (i, lo, hi);
            g.drawText (freqLabel (lo) + " - " + freqLabel (hi) + " Hz" + (std::abs (s.trimDb) > 0.04 ? "   trim " + signedText ((float) s.trimDb, 1) + " dB" : juce::String()),
                        nm, juce::Justification::topLeft, true);
            for (int c = 0; c < cols; ++c)
            {
                auto cell = row.removeFromLeft (colW).reduced (4, 2);
                const auto* first = lv.find (i, lv.points[c], false);
                const auto* again = lv.find (i, lv.points[c], true);
                if (first == nullptr && again == nullptr)
                {
                    g.setColour (p.textFaint);
                    g.setFont (font (11.5f));
                    g.drawText ("-", cell, juce::Justification::centredLeft);
                    continue;
                }
                const auto* now = again != nullptr ? again : first;
                g.setColour (p.text);
                g.setFont (monoFont (13.0f, true));
                g.drawText (juce::String (now->band (lo, hi), 1), cell.removeFromLeft (64), juce::Justification::centredLeft);
                if (again != nullptr && first != nullptr)
                {
                    // as found -> as left
                    g.setColour (p.textDim);
                    g.setFont (font (10.5f));
                    g.drawText ("was " + juce::String (first->band (lo, hi), 1), cell, juce::Justification::centredLeft, true);
                }
            }
        }
        r.removeFromTop (8);
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawFittedText ("Each speaker's level through its own range at each point, measured alone with its processing in place (dB against the "
                          "reference). Left and right should match at the centre (within " + juce::String (proc.pref ("lrTol"), 1) + " dB); a fill should be "
                          + juce::String (proc.pref ("fillHi"), 1) + " to " + juce::String (proc.pref ("fillLo"), 1) + " dB against the mains at "
                          "the seats it covers; the sub sits against the tops as the Room EQ target's low end asks. The aims are in Settings, Tuning defaults.",
                          r.removeFromTop (60), juce::Justification::topLeft, 4);
    }

    void drawStep (juce::Graphics& g, juce::Rectangle<int> area, int num, const juce::String& title, bool isDone) const
    {
        const auto& p = pal();
        const auto r = area.toFloat();
        g.setColour (p.panel);
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (isDone ? p.good.withAlpha (0.5f) : p.warn.withAlpha (0.6f));
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.4f);
        const auto badge = juce::Rectangle<float> (22.0f, 22.0f).withPosition (r.getX() + 12.0f, r.getY() + 10.0f);
        if (isDone) drawStatusBadge (g, badge, StepStatus::Ready);
        else
        {
            g.setColour (p.warn);
            g.drawEllipse (badge.reduced (1.0f), 1.4f);
            g.setFont (font (11.5f, true));
            g.drawText (juce::String (num), badge, juce::Justification::centred);
        }
        g.setColour (p.text);
        g.setFont (font (14.0f, true));
        g.drawText (title, juce::Rectangle<float> (r.getX() + 44.0f, r.getY() + 9.0f, r.getWidth() - 56.0f, 24.0f), juce::Justification::centredLeft);
    }

    juce::Rectangle<int> pointRow (int i) const { return { pointsList.getX(), pointsList.getY() + i * 34, pointsList.getWidth(), 30 }; }

    void refreshButtons()
    {
        measureButton.setButtonText (runner.busy() ? "Stop" : readingsCount (false) >= tasksCount() && tasksCount() > 0 ? juce::String ("Measure them all again")
                                                                                                                      : juce::String ("Measure each speaker"));
        measureButton.setEnabled (proc.isStandalone() || proc.uiAsStandalone);
        applyButton.setEnabled (attentionCount (false) > 0 && readingsCount (false) > 0 && ! lv.applied);
        applyButton.setButtonText (lv.applied ? "Trims on the sheet" : "Put the trims on the sheet");
        recheckButton.setEnabled (readingsCount (false) > 0 && (proc.isStandalone() || proc.uiAsStandalone));
    }

    void changed()
    {
        refreshButtons();
        repaint();
        if (onChanged) onChanged();
    }

    void timerCallback() override
    {
        const bool was = runner.busy();
        runner.tick();
        if (was || runner.busy()) { refreshButtons(); repaint (step1); }
    }

    LiveAlignProcessor& proc;
    LevelBalance& lv;
    MeasureRunner runner;
    juce::TextButton measureButton { "Measure each speaker" }, recheckButton { "Measure again to check" }, applyButton { "Put the trims on the sheet" };
    juce::TextButton planButton, sheetButton;
    juce::Rectangle<int> pointsCard, tableCard, tableArea, pointsList, pointsHint, step1, step2, step3, measureInfo, trimsArea, checkInfo;
    juce::String status;
    Task nowTask { 0, 0 };
    int redo = -1, serial = 0;
    bool recheckRun = false;
};
} // namespace la::ui
