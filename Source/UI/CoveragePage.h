// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Tuning step: Aim and coverage, after Level balance and before the speakers are checked together. Each speaker alone
// at the points it covers (Level balance's readings): its level at the edge of its area against its loudest point,
// within the coverage spec (Settings, plus or minus 3 dB by default). A point that falls off gets a hint from the plan
// (tilt it down, turn it toward ...). After re-aiming, the speaker is measured again at those points; the report shows
// before and after.
#include <functional>
#include "Graph.h"
#include "GuidedMeasure.h"
#include "MeasureRunner.h"
#include "../ShowChecks.h"

namespace la::ui
{
class CoveragePage : public juce::Component, public GuidedSource, private juce::Timer
{
public:
    std::function<void()> onChanged, onOpenLevel, onOpenPlan;
    std::function<void (GuidedSource&)> onGuided;

    explicit CoveragePage (LiveAlignProcessor& p) : proc (p), runner (p)
    {
        reaimButton.setTooltip ("After re-aiming the selected speaker: measure it again at the points it covers, in the guided pop-up");
        reaimButton.onClick = [this] { startReaim(); };
        levelButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Level balance  \xe2\x86\x92")));
        levelButton.onClick = [this] { if (onOpenLevel) onOpenLevel(); };
        planButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Speakers on the plan  \xe2\x86\x92")));
        planButton.setTooltip ("The hints use where the speakers and points are on the plan, and which way each speaker faces");
        planButton.onClick = [this] { if (onOpenPlan) onOpenPlan(); };
        doneButton.onClick = [this]
        {
            proc.coverage.done = ! proc.coverage.done;
            proc.coverage.doneOn = proc.coverage.done ? LiveAlignProcessor::today() : juce::String();
            changed();
        };
        speakerBox.setTooltip ("The speaker you re-aimed");
        speakerBox.onChange = [this] { selected = speakerBox.getSelectedId() - 1; repaint(); };
        for (auto* c : std::initializer_list<juce::Component*> { &reaimButton, &levelButton, &planButton, &doneButton, &speakerBox }) addAndMakeVisible (*c);
        lookAndFeelChanged();
        refresh();
        startTimerHz (15);
    }
    ~CoveragePage() override { stopTimer(); }

    void setSnapshot (const Snapshot& s) { runner.setSnapshot (&s); }
    void lookAndFeelChanged() override
    {
        reaimButton.setColour (juce::TextButton::buttonColourId, pal().accent);
        reaimButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
    }

    // ---- the analysis ----
    double levelOf (int spk, const juce::String& point) const
    {
        const auto* r = proc.level.find (spk, point, false);
        return r != nullptr ? r->band (coverage::kLo, coverage::kHi) : NAN;
    }
    std::vector<CoverageRow> rows() const
    {
        return coverage::analyse (proc.system, proc.level.points, proc.plan, proc.pref ("coverageTol"),
                                  [this] (int k, const juce::String& p) { return levelOf (k, p); });
    }
    int problems() const { int n = 0; for (const auto& r : rows()) n += r.ok ? 0 : 1; return n; }
    bool measured() const { return ! rows().empty(); }
    bool done() const { return measured() && (problems() == 0 || proc.coverage.done); }
    juce::String nextHint() const
    {
        if (! measured()) return "Measure Level balance first (each speaker at its points)";
        if (problems() > 0 && ! proc.coverage.done) return "Re-aim " + juce::String (problems()) + (problems() == 1 ? " speaker" : " speakers") + ", or accept the coverage";
        return {};
    }

    void refresh()
    {
        speakerBox.clear (juce::dontSendNotification);
        for (int i = 0; i < (int) proc.system.sources.size(); ++i)
            if (proc.system.sources[(size_t) i].role != (int) SpeakerRole::Sub) speakerBox.addItem (proc.system.nameOf (i), i + 1);
        if (selected < 0 || selected >= (int) proc.system.sources.size())
        {
            selected = -1;
            for (const auto& r : rows()) if (! r.ok) { selected = r.spk; break; }
            if (selected < 0 && speakerBox.getNumItems() > 0) selected = speakerBox.getItemId (0) - 1;
        }
        speakerBox.setSelectedId (selected + 1, juce::dontSendNotification);
        refreshButtons();
        resized();
        repaint();
    }

    // ---- guided: the re-aimed speaker at its points ----
    juce::String guidedTitle() const override { return "Aim and coverage: " + proc.system.nameOf (reaimSpk); }
    std::vector<GuideSpot> guidedSpots() override
    {
        std::vector<GuideSpot> out;
        const int now = currentIndex();
        for (int k = 0; k < reaimPoints.size(); ++k)
        {
            GuideSpot sp;
            sp.id = k;
            sp.label = reaimPoints[k];
            sp.marker = proc.plan.ensureMarker (reaimPoints[k]);
            sp.x = proc.plan.items[(size_t) sp.marker].x;
            sp.y = proc.plan.items[(size_t) sp.marker].y;
            sp.state = k == now ? GuideSpot::Now : remeasured.contains (reaimPoints[k]) ? GuideSpot::Done : GuideSpot::ToDo;
            if (sp.state == GuideSpot::Now) { sp.mic = micAt (reaimPoints[k]); sp.speaker = reaimSpk; sp.how = "seated ear height, " + aimHowText (proc, sp.mic, RoomAim); }
            out.push_back (sp);
        }
        return out;
    }
    bool guidedNeedsProcessor() const override { return false; }
    juce::String guidedHowTo() const override
    {
        if (currentIndex() < 0) return "Measured again at every point.";
        if (const auto m = proc.system.muteText ({ reaimSpk }); m.isNotEmpty()) return "First: " + m;
        return "Only " + proc.system.nameOf (reaimSpk) + " plays, as re-aimed.";
    }
    bool guidedStart() override
    {
        const int k = currentIndex();
        if (k < 0) { status = "Every point is measured again."; return false; }
        const auto point = reaimPoints[k];
        const int mic = micAt (point);
        proc.setMicPosition (mic, point);
        const int spk = reaimSpk;
        const bool ok = runner.start (proc.system.sources[(size_t) spk].output, mic, -1, [this, spk, point] (const MeasureRunner::Result& r)
        {
            if (! r.ok) { status = r.error; repaint(); return; }
            LevelReading rd;
            rd.spk = spk;
            rd.point = point;
            rd.when = juce::Time::getCurrentTime().formatted ("%H:%M");
            rd.tf = twelfthOctave (r.tf);
            rd.hasArrival = r.hasArrival;
            rd.arrivalMs = r.arrivalMs;
            rd.delayAtMs = proc.system.sources[(size_t) spk].proc.delayMs;
            rd.trimAtDb = proc.system.sources[(size_t) spk].trimDb;
            rd.eqAtDb = LevelBalance::eqInRange (proc.system, spk);
            proc.level.set (std::move (rd));   // Level balance sees the new aim too
            proc.level.applied = false;
            remeasured.addIfNotAlreadyThere (point);
            ++serial;
            status = proc.system.nameOf (spk) + " at " + point + ": " + juce::String (levelOf (spk, point), 1) + " dB.";
            changed();
        });
        if (! ok) status = runner.lastError();
        return ok;
    }
    void guidedStop() override { runner.stop(); }
    bool guidedBusy() const override { return runner.busy(); }
    float guidedProgress() const override { return runner.progress(); }
    juce::String guidedPhase() const override { return runner.phaseText (proc.system.nameOf (reaimSpk)); }
    int guidedSerial() const override { return serial; }
    juce::String guidedStatus() const override { return status; }
    void guidedRedo (const std::vector<GuideSpot>& was) override { for (const auto& sp : was) remeasured.removeString (sp.label); }
    void guidedClosed() override { refresh(); }
    int guidedAimKind() const override { return RoomAim; }

    // For the preview: re-aim without the pop-up (the measurements then run one by one with guidedStart).
    void reaimNow (int spk) { selected = spk; startReaim (false); guidedStart(); }
    bool reaimBusy() const { return runner.busy(); }
    bool reaimLeft() const { return currentIndex() >= 0; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, tableCard.toFloat(), "Each speaker across the points it covers (1 - 4 kHz, dB against its loudest point)");
        drawPanel (g, adviceCard.toFloat(), "What to do");
        drawPanel (g, reaimCard.toFloat(), "Re-aim, then measure again");
        paintTable (g);

        auto r = adviceArea;
        const auto list = rows();
        const double tol = proc.pref ("coverageTol");
        const auto pm = juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"));
        g.setFont (font (12.0f));
        if (list.empty())
        {
            g.setColour (p.textDim);
            g.drawFittedText ("Measure each speaker at its points on Level balance first: tick the centre and a seat at the edge of each speaker's area "
                              "(the front row, the back, the sides).", r, juce::Justification::topLeft, 6);
        }
        else
        {
            const int n = problems();
            g.setColour (n == 0 ? p.good : p.warn);
            g.setFont (font (12.5f, true));
            g.drawText (n == 0 ? "Every speaker covers its area within " + pm + juce::String (tol, 1) + " dB."
                               : juce::String (n) + (n == 1 ? " speaker falls off" : " speakers fall off") + " more than the spec (" + pm + juce::String (tol, 1) + " dB):",
                        r.removeFromTop (22), juce::Justification::centredLeft, true);
            r.removeFromTop (4);
            for (const auto& row : list)
            {
                if (row.advice.isEmpty()) continue;
                const int lines = 1 + row.advice.length() / 60;
                if (r.getHeight() < lines * 16) break;
                g.setColour (row.ok ? p.textDim : p.text);
                g.setFont (font (12.0f, ! row.ok));
                g.drawFittedText (row.advice, r.removeFromTop (lines * 16 + 2), juce::Justification::topLeft, lines, 1.0f);
                r.removeFromTop (6);
            }
            g.setColour (p.textFaint);
            g.setFont (font (11.0f));
            g.drawFittedText ("Aim before EQ: a speaker pointed at the wrong seats cannot be fixed with EQ or level. Distance alone takes about 6 dB "
                              "each time the distance doubles, so a long room needs the speaker aimed further back (or a delay).",
                              r.removeFromBottom (48), juce::Justification::topLeft, 3);
        }

        g.setColour (status.isNotEmpty() ? p.accent : p.textDim);
        g.setFont (font (11.5f));
        g.drawFittedText (runner.busy() ? runner.phaseText (proc.system.nameOf (reaimSpk))
                                        : status.isNotEmpty() ? status : juce::String ("Re-aim the speaker, then measure it again at the points it covers."),
                          reaimInfo, juce::Justification::topLeft, 4);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (380);
        r.removeFromRight (10);
        tableCard = r;
        tableArea = tableCard.reduced (14, 0).withTrimmedTop (36).withTrimmedBottom (10);
        reaimCard = right.removeFromBottom (220);
        right.removeFromBottom (8);
        adviceCard = right;
        {
            auto c = adviceCard.reduced (12, 0).withTrimmedTop (38).withTrimmedBottom (10);
            auto b = c.removeFromBottom (30);
            planButton.setBounds (b.removeFromLeft ((b.getWidth() - 8) / 2));
            b.removeFromLeft (8);
            levelButton.setBounds (b);
            c.removeFromBottom (8);
            adviceArea = c;
        }
        {
            auto c = reaimCard.reduced (12, 0).withTrimmedTop (38);
            speakerBox.setBounds (c.removeFromTop (28));
            c.removeFromTop (6);
            reaimButton.setBounds (c.removeFromTop (30));
            c.removeFromTop (6);
            doneButton.setBounds (c.removeFromBottom (40).withSizeKeepingCentre (c.getWidth(), 28));
            reaimInfo = c;
        }
    }

private:
    int micAt (const juce::String& point) const
    {
        for (int m = 0; m < proc.tuningMicCount(); ++m) if (proc.mics.position[(size_t) m] == point) return m;
        return 0;
    }
    int currentIndex() const
    {
        for (int k = 0; k < reaimPoints.size(); ++k) if (! remeasured.contains (reaimPoints[k])) return k;
        return -1;
    }

    void startReaim (bool openGuide = true)
    {
        if (runner.busy() || selected < 0 || selected >= (int) proc.system.sources.size()) return;
        // the points it covers (or every point measured, when it covers none yet)
        reaimSpk = selected;
        reaimPoints.clear();
        for (const auto& r : rows()) if (r.spk == reaimSpk) for (const auto& cp : r.points) reaimPoints.add (cp.point);
        if (reaimPoints.isEmpty()) reaimPoints = proc.level.points;
        if (reaimPoints.isEmpty()) { status = "Tick the points on Level balance first."; repaint(); return; }
        // keep how it was (the first time it is re-aimed: as found)
        if (proc.coverage.before.find (reaimSpk) == proc.coverage.before.end())
            for (const auto& pt : proc.level.points)
                if (const double v = levelOf (reaimSpk, pt); std::isfinite (v)) proc.coverage.before[reaimSpk][pt] = v;
        proc.coverage.reaimedOn[reaimSpk] = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        remeasured.clear();
        status.clear();
        if (openGuide && onGuided) onGuided (*this);
    }

    void paintTable (juce::Graphics& g)
    {
        const auto& p = pal();
        auto r = tableArea;
        const auto& pts = proc.level.points;
        const auto list = rows();
        if (pts.isEmpty() || list.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("Nothing to show yet: Level balance measures each speaker alone at the ticked points.", r, juce::Justification::centred, 2);
            return;
        }
        const double tol = proc.pref ("coverageTol");
        const int nameW = 170, colW = juce::jmin (130, (r.getWidth() - nameW - 90) / juce::jmax (1, pts.size()));
        auto head = r.removeFromTop (24);
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("SPEAKER", head.removeFromLeft (nameW), juce::Justification::centredLeft);
        for (const auto& pt : pts) g.drawText (pt.toUpperCase(), head.removeFromLeft (colW), juce::Justification::centredLeft, true);
        g.drawText ("SPREAD", head, juce::Justification::centredLeft);
        r.removeFromTop (4);
        for (const auto& row : list)
        {
            auto line = r.removeFromTop (40);
            if (line.getBottom() > tableArea.getBottom() - 70) break;
            g.setColour (p.panelRaised.withAlpha (row.spk == selected ? 0.9f : 0.4f));
            g.fillRoundedRectangle (line.toFloat(), 5.0f);
            auto nm = line.removeFromLeft (nameW).reduced (8, 0);
            g.setColour (traceColour (row.spk));
            g.fillRoundedRectangle (nm.removeFromLeft (4).toFloat().withSizeKeepingCentre (4.0f, 20.0f), 2.0f);
            nm.removeFromLeft (8);
            g.setColour (p.text);
            g.setFont (font (12.5f, true));
            g.drawText (proc.system.nameOf (row.spk), nm.removeFromTop (nm.getHeight() / 2 + 2), juce::Justification::bottomLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (10.5f));
            const auto bIt = proc.coverage.before.find (row.spk);
            const auto rIt = proc.coverage.reaimedOn.find (row.spk);
            g.drawText (rIt != proc.coverage.reaimedOn.end() ? "re-aimed " + rIt->second : "loudest at " + row.onAxis, nm, juce::Justification::topLeft, true);
            for (const auto& pt : pts)
            {
                auto cell = line.removeFromLeft (colW).reduced (4, 3);
                const CoveragePoint* cp = nullptr;
                for (const auto& c : row.points) if (c.point == pt) cp = &c;
                if (cp == nullptr)
                {
                    g.setColour (p.textFaint);
                    g.setFont (font (11.0f));
                    g.drawText (std::isfinite (levelOf (row.spk, pt)) ? "not its area" : "-", cell, juce::Justification::centredLeft, true);
                    continue;
                }
                const bool bad = -cp->db > 2.0 * tol;
                g.setColour (bad ? p.warn : (cp->point == row.onAxis ? p.accent : p.text));
                g.setFont (monoFont (13.0f, true));
                g.drawText (cp->point == row.onAxis ? juce::String ("0.0") : juce::String (cp->db, 1), cell.removeFromTop (cell.getHeight() / 2 + 3), juce::Justification::bottomLeft);
                if (bIt != proc.coverage.before.end())
                {
                    // before: against its loudest point then
                    double bestB = -1e9;
                    for (const auto& [k, v] : bIt->second) bestB = std::max (bestB, v);
                    if (auto f = bIt->second.find (pt); f != bIt->second.end())
                    {
                        g.setColour (p.textDim);
                        g.setFont (font (10.5f));
                        g.drawText ("was " + juce::String (f->second - bestB, 1), cell, juce::Justification::topLeft, true);
                    }
                }
            }
            auto sp = line.reduced (4, 3);
            g.setColour (row.ok ? p.good : p.warn);
            g.setFont (font (12.0f, true));
            g.drawText (juce::String (-row.worstDb, 1) + " dB", sp.removeFromTop (sp.getHeight() / 2 + 3), juce::Justification::bottomLeft, true);
            g.setFont (font (10.5f));
            g.drawText (row.ok ? (row.enough ? juce::String ("within spec") : juce::String ("one point")) : juce::String ("re-aim"), sp, juce::Justification::topLeft, true);
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        const auto pm = juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"));
        g.drawFittedText ("Each speaker alone, at the points where it is within 6 dB of the loudest top (its area), against its own loudest point. "
                          "The spec is " + pm + juce::String (tol, 1) + " dB, so the spread from the loudest to the quietest point should stay within "
                          + juce::String (2.0 * tol, 1) + " dB (Venue, Details, or Settings), the way AVIXA A102.01 rates coverage uniformity. Subs are left out: they cover all round.",
                          tableArea.removeFromBottom (60), juce::Justification::topLeft, 4);
    }

    void refreshButtons()
    {
        reaimButton.setButtonText (runner.busy() ? "Measuring..." : "Re-aimed: measure it again");
        reaimButton.setEnabled (! runner.busy() && measured() && selected >= 0 && (proc.isStandalone() || proc.uiAsStandalone));
        doneButton.setButtonText (proc.coverage.done ? "Coverage accepted: undo" : (problems() > 0 ? "Accept the coverage as it is" : "Coverage is good"));
        doneButton.setEnabled (measured());
    }

    void changed() { refreshButtons(); repaint(); if (onChanged) onChanged(); }

    void timerCallback() override
    {
        const bool was = runner.busy();
        runner.tick();
        if (was || runner.busy()) { refreshButtons(); repaint (reaimCard); }
    }

    LiveAlignProcessor& proc;
    MeasureRunner runner;
    juce::TextButton reaimButton, levelButton, planButton, doneButton;
    juce::ComboBox speakerBox;
    juce::Rectangle<int> tableCard, tableArea, adviceCard, adviceArea, reaimCard, reaimInfo;
    juce::String status;
    juce::StringArray reaimPoints, remeasured;
    int selected = -1, reaimSpk = 0, serial = 0;
};
} // namespace la::ui
