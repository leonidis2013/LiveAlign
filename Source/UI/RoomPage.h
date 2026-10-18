// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include "../Clock.h"
// Step 3: Room tuning. Measure the whole system at the listening positions - several mics at once, or one mic moved
// from position to position - and compare their average with a target curve. LiveAlign suggests parametric EQ
// within your processor's limits; once it is in the processor, measure again to check the result.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "GuidedMeasure.h"
#include "../PluginProcessor.h"
#include "../AcousticsSummary.h"

namespace la::ui
{
inline juce::Colour positionColour (int i) { return traceColour (i + 2); }

//==============================================================================
class RoomGraph : public juce::Component
{
public:
    explicit RoomGraph (LiveAlignProcessor& p) : proc (p) {}

    void setSelectedPosition (const juce::String& s) { selected = s; repaint(); }
    void setShow (bool positions, bool before, bool after, bool predicted) { showPos = positions; showBefore = before; showAfter = after; showPred = predicted; repaint(); }
    void setRange (double lo, double hi) { eqLo = lo; eqHi = hi; repaint(); }

    // The averages and each seat's smoothed curve change only when a measurement does: worked out then, not on every
    // repaint (a dozen seats at 1/6 octave was most of the page's paint time).
    static juce::String measKey (const RoomMeasurement& m)
    {
        const double mid = m.tf.db.empty() ? 0.0 : m.tf.db[m.tf.db.size() / 2];
        return m.position + "|" + m.time + "|" + (m.after ? "a" : "b") + (m.include ? "i" : "x") + "|" + juce::String ((int) m.tf.freq.size())
               + "|" + juce::String (mid, 6);
    }
    void refreshCache()
    {
        const auto& room = proc.room;
        juce::String key;
        for (const auto& m : room.meas) key << measKey (m) << ";";
        if (key == cacheKey && posCurves.size() == room.meas.size()) return;
        cacheKey = key;
        avgBefore = room.average (false);
        avgAfter = room.average (true);
        const auto grid = RoomTuning::grid();
        posCurves.assign (room.meas.size(), {});
        for (size_t k = 0; k < room.meas.size(); ++k)
        {
            const auto& m = room.meas[k];
            if (m.tf.freq.size() < 2) { posCurves[k].assign (grid.size(), NAN); continue; }
            const double lvl = m.levelDb();
            std::vector<double> raw (grid.size());
            for (size_t i = 0; i < grid.size(); ++i)
                raw[i] = grid[i] >= m.tf.freq.front() && grid[i] <= m.tf.freq.back() ? m.tf.dbAt (grid[i]) - lvl : NAN;
            posCurves[k] = eq::smooth (grid, raw, 1.0 / 6.0);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto& room = proc.room;
        auto r = getLocalBounds().toFloat();
        drawPanel (g, r, "Room average vs target");
        {
            // legend, right of the title
            struct Item { juce::String name; juce::Colour c; bool dashed; };
            const Item items[] = { { "As found", p.text, false }, { room.checkRound > 0 ? "Check " + juce::String (room.checkRound) : juce::String ("Check"), p.good, false }, { "Target", p.good.withAlpha (0.9f), true }, { "With suggested EQ", p.accent, true } };
            auto lr = juce::Rectangle<float> (r.getRight() - 12.0f, r.getY() + 8.0f, 0.0f, 18.0f);
            g.setFont (font (10.5f));
            for (int i = 3; i >= 0; --i)
            {
                const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), items[i].name) + 26.0f;
                lr.setX (lr.getX() - w - 10.0f);
                lr.setWidth (w);
                auto box = lr;
                const auto sw = box.removeFromLeft (18.0f);
                g.setColour (items[i].c);
                if (items[i].dashed) { g.fillRect (sw.getX(), sw.getCentreY() - 1.0f, 7.0f, 2.0f); g.fillRect (sw.getX() + 11.0f, sw.getCentreY() - 1.0f, 7.0f, 2.0f); }
                else g.fillRect (sw.getX(), sw.getCentreY() - 1.5f, 18.0f, 3.0f);
                g.setColour (p.textDim);
                g.drawText (items[i].name, box.withTrimmedLeft (6.0f), juce::Justification::centredLeft);
            }
        }
        auto plot = r.reduced (12.0f).withTrimmedTop (28.0f).withTrimmedLeft (34.0f).withTrimmedBottom (16.0f);
        g.setColour (p.bg.withAlpha (0.5f));
        g.fillRect (plot);
        FreqAxis ax { 20.0, 20000.0, plot };
        drawFreqGrid (g, ax, plot, plot.withY (plot.getBottom()).withHeight (16.0f));

        const auto grid = RoomTuning::grid();
        refreshCache();
        const auto& before = avgBefore;
        const auto& after = avgAfter;
        const bool hasBefore = room.count (false) > 0, hasAfter = room.count (true) > 0;
        const auto& ref = hasBefore ? before : after;

        // level: the average's 500 Hz - 4 kHz level sits at 0 dB
        const float top = 12.0f, range = 36.0f;
        auto yDb = [&] (double db) { return plot.getY() + (float) (top - db) / range * plot.getHeight(); };
        g.setFont (font (10.0f));
        for (float db = top; db >= top - range; db -= 6.0f)
        {
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) yDb (db), plot.getX(), plot.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 34.0f, yDb (db) - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
        }
        // EQ range: where the system plays; outside it is greyed out (no EQ there)
        if (eqHi > eqLo)
        {
            g.setColour (p.accent.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (ax.x (eqLo), plot.getY(), ax.x (eqHi), plot.getBottom()));
            g.setColour (p.bg.withAlpha (0.55f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), plot.getY(), ax.x (eqLo), plot.getBottom()));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (ax.x (eqHi), plot.getY(), plot.getRight(), plot.getBottom()));
        }

        // what the room acoustics allowance does, where it does it
        if (room.allow.on && (hasBefore || hasAfter))
        {
            const auto& al = room.allow;
            g.setFont (font (10.0f, true));
            for (const auto& rr : al.noisy)
            {
                const auto band = juce::Rectangle<float>::leftTopRightBottom (ax.x (rr.first), plot.getY(), ax.x (rr.second), plot.getBottom());
                g.setColour (p.textFaint.withAlpha (0.12f));
                g.fillRect (band);
                g.setColour (p.textFaint);
                g.drawText ("noise", band.withHeight (14.0f).translated (0.0f, plot.getHeight() - 16.0f), juce::Justification::centred);
            }
            for (const auto& rr : al.windowed)
            {
                const auto band = juce::Rectangle<float>::leftTopRightBottom (ax.x (rr.first), plot.getY(), ax.x (rr.second), plot.getBottom());
                g.setColour (p.accent.withAlpha (0.06f));
                g.fillRect (band);
                g.setColour (p.accent.withAlpha (0.8f));
                g.drawText ("direct sound", band.withHeight (14.0f).translated (0.0f, 2.0f), juce::Justification::centred);
            }
            {
                const float x = ax.x (al.schroederHz);
                juce::Path l;
                l.startNewSubPath (x, plot.getY());
                l.lineTo (x, plot.getBottom());
                juce::Path d;
                const float dashes[] = { 3.0f, 4.0f };
                juce::PathStrokeType (1.2f).createDashedStroke (d, l, dashes, 2);
                g.setColour (p.warn.withAlpha (0.8f));
                g.fillPath (d);
                g.drawText ((al.schroederAssumed ? "~" : "") + juce::String (juce::roundToInt (al.schroederHz)) + " Hz: modes below",
                            juce::Rectangle<float> (x + 4.0f, plot.getBottom() - 30.0f, 130.0f, 13.0f), juce::Justification::centredLeft);
            }
            int nth = 0;
            for (const auto& c : al.modeCuts)
            {
                const float x = ax.x (c.freq);
                const float ly = plot.getY() + 30.0f + 13.0f * (float) (nth++ % 2);
                juce::Path tri;
                tri.addTriangle (x - 5.0f, plot.getY() + 2.0f, x + 5.0f, plot.getY() + 2.0f, x, plot.getY() + 10.0f);
                g.setColour (p.accent);
                g.fillPath (tri);
                g.drawText ("mode Q" + juce::String (c.q, 1), juce::Rectangle<float> (x - 40.0f, ly, 80.0f, 13.0f), juce::Justification::centred);
            }
            const auto& ref2 = hasBefore ? before : after;
            const auto sm2 = eq::smooth (grid, ref2, 1.0 / 12.0);
            for (const auto& n : al.nulls)
            {
                size_t k = 0;
                while (k + 1 < grid.size() && grid[k] < n.freq) ++k;
                if (! std::isfinite (sm2[k])) continue;
                const float x = ax.x (n.freq), y = yDb (sm2[k]);
                g.setColour (p.warn);
                g.drawEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f, 1.4f);
                g.drawText ("null: move, don't boost", juce::Rectangle<float> (x - 70.0f, y + 6.0f, 140.0f, 13.0f), juce::Justification::centred);
            }
            if (al.noHighBoost)
            {
                g.setColour (p.warn);
                g.drawText ("no boosts above 2 kHz", juce::Rectangle<float> (ax.x (2000.0) + 4.0f, plot.getY() + 30.0f, 140.0f, 13.0f), juce::Justification::centredLeft);
            }
        }

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        auto xAt = [&] (size_t k) { return ax.x (grid[k]); };

        if (! hasBefore && ! hasAfter)
        {
            g.setColour (p.textDim);
            g.setFont (font (13.5f));
            g.drawFittedText ("No measurements yet. Press Measure as found: the guided pop-up takes the mic to each listening point.",
                              plot.reduced (60.0f).toNearestInt(), juce::Justification::centred, 3);
            return;
        }

        // target, lined up with the average in the EQ range, with a +/-3 dB band
        {
            const auto t = room.targetCurve();
            std::vector<double> d;
            const auto sm = eq::smooth (grid, ref, 1.0 / 6.0);
            for (size_t i = 0; i < grid.size(); ++i)
                if (grid[i] >= juce::jmax (100.0, eqLo) && grid[i] <= juce::jmin (8000.0, eqHi > 0.0 ? eqHi : 8000.0) && std::isfinite (sm[i])) d.push_back (sm[i] - t[i]);
            const double off = eq::median (d);
            juce::Path line, band;
            std::vector<juce::Point<float>> lower;
            for (size_t i = 0; i < grid.size(); i += 2)
            {
                const float x = ax.x (grid[i]);
                const double v = t[i] + off;
                if (i == 0) { line.startNewSubPath (x, yDb (v)); band.startNewSubPath (x, yDb (v + 3.0)); }
                else { line.lineTo (x, yDb (v)); band.lineTo (x, yDb (v + 3.0)); }
                lower.push_back ({ x, yDb (v - 3.0) });
            }
            for (auto it = lower.rbegin(); it != lower.rend(); ++it) band.lineTo (*it);
            band.closeSubPath();
            g.setColour (p.good.withAlpha (0.08f));
            g.fillPath (band);
            juce::Path dashed;
            const float dashes[] = { 7.0f, 5.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (dashed, line, dashes, 2);
            g.setColour (p.good.withAlpha (0.9f));
            g.fillPath (dashed);
        }

        // each position (normalised the same way as the average)
        if (showPos)
        {
            for (size_t k = 0; k < room.meas.size(); ++k)
            {
                const auto& m = room.meas[k];
                if (! m.include || (m.after && ! showAfter) || (! m.after && ! showBefore)) continue;
                const int pi = proc.positions.indexOf (m.position);
                const bool sel = m.position == selected;
                const auto& v = posCurves[k];   // the tonal shape at each seat (1/6 octave), worked out once per measurement
                strokeTrace (g, tracePath (grid.size(), xAt, [&] (size_t i) { return std::isfinite (v[i]) ? yDb (v[i]) : NAN; }, [] (size_t) { return true; }),
                             positionColour (juce::jmax (0, pi)).withAlpha (sel ? 0.95f : 0.35f), sel ? 1.8f : 1.0f);
            }
        }
        auto curve = [&] (const std::vector<double>& v, juce::Colour c, float w, bool dashed)
        {
            auto path = tracePath (grid.size(), xAt, [&] (size_t i) { return yDb (v[i]); }, [] (size_t) { return true; });
            if (! dashed) { strokeTrace (g, path, c, w); return; }
            juce::Path d;
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (w).createDashedStroke (d, path, dashes, 2);
            g.setColour (c);
            g.fillPath (d);
        };
        if (hasBefore && showBefore) curve (eq::smooth (grid, before, 1.0 / 12.0), p.text, 2.6f, false);
        if (hasAfter && showAfter) curve (eq::smooth (grid, after, 1.0 / 12.0), p.good, 2.6f, false);
        // predicted: the latest measurement (the check, once there is one) with the suggested EQ on top
        if (showPred && hasBefore && ! room.suggested.empty())
        {
            auto pred = eq::smooth (grid, room.checkRound > 0 && hasAfter ? after : before, 1.0 / 12.0);
            for (size_t i = 0; i < grid.size(); ++i) if (std::isfinite (pred[i])) pred[i] += eq::totalDb (room.suggested, grid[i]);
            curve (pred, p.accent, 2.0f, true);
        }
    }

private:
    LiveAlignProcessor& proc;
    juce::String selected;
    bool showPos = true, showBefore = true, showAfter = true, showPred = true;
    double eqLo = 0.0, eqHi = 0.0;
    juce::String cacheKey;
    std::vector<double> avgBefore, avgAfter;
    std::vector<std::vector<double>> posCurves;
};

//==============================================================================
class RoomPage : public juce::Component, public GuidedSource, private juce::Timer
{
public:
    std::function<void()> onChanged;
    std::function<void (GuidedSource&)> onGuided;   // opens the guided pop-up
    std::function<void()> onOpenPlan;                // the points are made on the venue plan
    std::function<void()> onOpenVenue;               // the target is set on Home
    std::function<void (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)>)> askText;

    explicit RoomPage (LiveAlignProcessor& p) : proc (p), settings (p.settings), room (p.room), graph (p)
    {
        addAndMakeVisible (graph);

        auto setupToggle = [this] (juce::ToggleButton& t, bool on)
        {
            t.setToggleState (on, juce::dontSendNotification);
            t.onClick = [this] { syncGraph(); };
            addAndMakeVisible (t);
        };
        setupToggle (showPositions, true);
        setupToggle (showBefore, true);
        setupToggle (showAfter, true);
        setupToggle (showPredicted, true);

        addPositionButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Points on the plan  \xe2\x86\x92")));
        addPositionButton.onClick = [this] { if (onOpenPlan) onOpenPlan(); };
        addPositionButton.setTooltip ("The listening points are made, moved and renamed on the venue plan; tick the ones Room EQ measures here");
        addAndMakeVisible (addPositionButton);

        // ---- 1 measure ----
        outputBox.setTooltip ("Where the pink noise plays from: all outputs (the whole system), or one output");
        outputBox.onChange = [this] { playOutput = outputBox.getSelectedItemIndex(); };
        addAndMakeVisible (outputBox);
        for (int i = 0; i < 2; ++i)
        {
            phaseButtons[i].setButtonText (i == 0 ? "As found" : "Check");
            phaseButtons[i].setRadioGroupId (8201);
            phaseButtons[i].setClickingTogglesState (true);
            phaseButtons[i].onClick = [this, i] { if (phaseButtons[i].getToggleState()) { measuringAfter = i == 1; repaint(); } };
            addAndMakeVisible (phaseButtons[i]);
        }
        phaseButtons[0].setToggleState (true, juce::dontSendNotification);
        measureButton.onClick = [this] { if (phase == Phase::Idle) requestMeasure(); else cancel ("Stopped."); };
        measureButton.setTooltip ("Measures every ticked listening point in the guided pop-up: pink noise from the whole system, the mic moved from point to point");
        addAndMakeVisible (measureButton);

        // ---- 2 target and EQ ----
        for (const auto& t : targets::list()) targetBox.addItem (t.name, targetBox.getNumItems() + 1);
        targetBox.onChange = [this] { room.target = targetBox.getSelectedItemIndex(); room.suggested.clear(); changed(); };
        targetBox.setTooltip ("The tonal balance to tune toward");
        speakerBox.onChange = [this]
        {
            const int id = speakerBox.getSelectedId();
            room.eqGroup = id == 1 ? RoomTuning::kWholeSystem : id == 2 ? RoomTuning::kMains : id - 3;
            room.suggested.clear();
            changed();
        };
        speakerBox.setTooltip ("Where the EQ goes: the whole system (every speaker), the mains, or one speaker. The room is "
                               "measured as a whole; the other speakers in a group follow their lead.");
        static const int bandsOpts[] = { 3, 4, 5, 6, 8, 10, 12, 16, 31 };
        for (int b : bandsOpts) bandsBox.addItem (juce::String (b) + " bands", b);
        bandsBox.onChange = [this] { room.maxBands = juce::jmax (1, bandsBox.getSelectedId()); };
        bandsBox.setTooltip ("How many parametric EQ bands your processor has free for this output");
        static const int boostOpts[] = { 0, 2, 3, 6 };
        for (int b : boostOpts) boostBox.addItem (b == 0 ? juce::String ("Cuts only") : "Boost up to " + juce::String (b) + " dB", b + 1);
        boostBox.onChange = [this] { room.maxBoostDb = juce::jmax (0, boostBox.getSelectedId() - 1); };
        boostBox.setTooltip ("Boosting eats headroom and cannot fill dips caused by reflections; small or no boost is usual");
        suggestButton.onClick = [this] { suggest(); };
        suggestButton.setTooltip ("Fits parametric EQ to bring the average toward the target, within the limits above");
        applyButton.onClick = [this] { applyEq(); };
        applyButton.setTooltip ("Once you have put these bands in your processor: LiveAlign notes them as the EQ in the processor (for the "
                                "settings sheet and the report) and the next step is to measure again to check");
        allowToggle.setButtonText ("Allow for the room acoustics");
        allowToggle.onClick = [this]
        {
            // the venue's own value; the same as the default in Settings means no override
            const bool on = allowToggle.getToggleState();
            if ((AppPrefs::get().value ("roomAllow") > 0.5) == on) proc.venuePrefs.reset ("roomAllow");
            else proc.venuePrefs.set ("roomAllow", on ? 1.0 : 0.0);
            room.suggested.clear();
            refresh();
            changed();
        };
        for (auto* c : std::initializer_list<juce::Component*> { &targetLink, &speakerBox, &bandsBox, &boostBox, &allowToggle, &suggestButton, &applyButton })
            addAndMakeVisible (*c);
        targetLink.setTooltip ("The tonal balance Room EQ tunes toward. It is set with the venue's other targets on Home.");
        targetLink.onClick = [this] { if (onOpenVenue) onOpenVenue(); };

        // ---- 3 check ----
        tunedButton.onClick = [this]
        {
            room.tuned = ! room.tuned;
            room.tunedOn = room.tuned ? LiveAlignProcessor::today() : juce::String();
            changed();
        };
        addAndMakeVisible (tunedButton);

        lookAndFeelChanged();
        refresh();
        startTimerHz (15);
    }

    ~RoomPage() override { stopTimer(); }

    void setSnapshot (const Snapshot& s) { snap = &s; }

    void lookAndFeelChanged() override
    {
        for (auto* b : { &measureButton, &suggestButton })
        {
            b->setColour (juce::TextButton::buttonColourId, pal().accent);
            b->setColour (juce::TextButton::textColourOffId, pal().onAccent);
        }
        refreshMeasureButton();
    }

    // After a venue or session is opened, or mics, positions or speakers changed.
    void refresh()
    {
        proc.syncPositionsWithPlan();
        updateRoomAllowance (proc);
        allowToggle.setToggleState (room.allow.wanted, juce::dontSendNotification);
        allowToggle.setTooltip (room.allow.wanted && ! room.allow.on ? juce::String ("Needs Room acoustics measured first: until then it has no effect.")
                                : (room.allow.on ? room.allow.notes.joinIntoString ("\n") + "\n\n" : juce::String())
                                  + "Uses the room acoustics: the Schroeder frequency, the room modes and how long they ring, long reverb, the noise and the clarity. "
                                  "Off: Room EQ fits the average as measured. The default is in Settings, Tuning defaults"
                                  + juce::String (proc.venuePrefs.has ("roomAllow") ? " (this venue has its own)." : "."));

        const int outs = juce::jmax (1, proc.numOutputs());
        outputBox.clear (juce::dontSendNotification);
        outputBox.addItem ("All outputs (whole system)", 1);
        for (int i = 0; i < outs; ++i) outputBox.addItem ("Output " + juce::String (i + 1), i + 2);
        outputBox.setSelectedItemIndex (juce::jlimit (0, outs, playOutput), juce::dontSendNotification);

        targetBox.setSelectedItemIndex (juce::jlimit (0, targets::count() - 1, room.target), juce::dontSendNotification);
        targetLink.setButtonText (juce::String (targets::list()[(size_t) juce::jlimit (0, targets::count() - 1, room.target)].name)
                                  + juce::String (juce::CharPointer_UTF8 ("   \xc2\xb7   Edit on Home \xe2\x86\x92")));
        speakerBox.clear (juce::dontSendNotification);
        speakerBox.addItem ("Whole system (every speaker)", 1);
        bool anyMain = false;
        for (int i = 0; i < (int) proc.system.sources.size(); ++i) anyMain = anyMain || proc.system.isMain (i);
        if (anyMain) speakerBox.addItem ("Mains only", 2);
        for (int i = 0; i < (int) proc.system.sources.size(); ++i)
        {
            const auto& s = proc.system.sources[(size_t) i];
            if (proc.system.master (i) != i) continue;   // EQ goes on the speaker the others follow
            speakerBox.addItem (s.name + " only", i + 3);
        }
        {
            int id = room.eqGroup == RoomTuning::kWholeSystem ? 1 : room.eqGroup == RoomTuning::kMains ? 2 : room.eqGroup + 3;
            if (speakerBox.indexOfItemId (id) < 0) { room.eqGroup = RoomTuning::kWholeSystem; id = 1; }
            speakerBox.setSelectedId (id, juce::dontSendNotification);
        }
        bandsBox.setSelectedId (room.maxBands, juce::dontSendNotification);
        if (bandsBox.getSelectedId() == 0) bandsBox.setSelectedId (8, juce::dontSendNotification);
        boostBox.setSelectedId ((int) room.maxBoostDb + 1, juce::dontSendNotification);
        if (boostBox.getSelectedId() == 0) boostBox.setSelectedId (4, juce::dontSendNotification);
        tunedButton.setButtonText (room.tuned ? "Room tuned " + room.tunedOn + "  (undo)" : juce::String ("Mark the room as tuned (as left)"));

        double lo, hi;
        eqRange (lo, hi);
        graph.setRange (lo, hi);
        syncGraph();
        resized();
        repaint();
    }

    // What to do next on this page (for the header).
    // The Room EQ loop: measure as found, suggest EQ, enter it in your processor, measure a check at the same
    // positions, refine until it is close enough, then mark the room as left (tuned).
    juce::String nextHint() const
    {
        if (room.count (false) == 0) return "Measure as found";
        if (room.checkRound == 0 && plannedLeft (false) > 0) return "Measure the next positions";
        if (! room.suggested.empty()) return "I've entered this EQ";
        if (room.checkRound > 0 && plannedLeft (true) > 0) return "Measure check " + juce::String (room.checkRound);
        if (room.tuned) return {};
        if (room.checkRound == 0) return "Suggest EQ";
        if (closeEnough() && ! proc.listening.done) return "Listen at the points (Listening check)";
        return closeEnough() ? juce::String ("Mark the room as tuned") : juce::String ("Refine the EQ");
    }

    // Does what nextHint() says, from the header button.
    void doNext()
    {
        if (phase != Phase::Idle) return;
        const auto h = nextHint();
        if (h.startsWith ("Measure")) { setMeasuringAfter (room.checkRound > 0); requestMeasure(); }
        else if (h.startsWith ("Suggest") || h.startsWith ("Refine")) suggest();
        else if (h.startsWith ("I've")) applyEq();
        else if (h.startsWith ("Mark")) { room.tuned = true; room.tunedOn = LiveAlignProcessor::today(); refresh(); changed(); }
    }

    // The latest round within 1.5 dB RMS of the target: good enough to call it done.
    bool closeEnough() const
    {
        if (room.count (room.checkRound > 0) == 0) return false;
        double lo, hi, rms, mx;
        eqRange (lo, hi);
        room.deviation (room.average (room.checkRound > 0), lo, hi, rms, mx);
        return std::isfinite (rms) && rms <= proc.pref ("eqClose");
    }

    bool busy() const { return phase != Phase::Idle; }

    // The listening check found a problem at a point: measure it again (the latest round), in the guided pop-up.
    void remeasurePoint (const juce::String& point)
    {
        if (phase != Phase::Idle || point.isEmpty()) return;
        const bool after = room.checkRound > 0;
        room.meas.erase (std::remove_if (room.meas.begin(), room.meas.end(), [&] (const RoomMeasurement& m) { return m.position == point && m.after == after; }),
                         room.meas.end());
        room.skipped.removeString (point);
        if (proc.micAtPoint (point) < 0) proc.linkMicToPoint (point, 0);
        setMeasuringAfter (after);
        status = "Measuring " + point + " again (the listening check found a problem there).";
        refresh();
        requestMeasure();
    }

    // Measure: in the guided pop-up when Guided is on, otherwise right here.
    void requestMeasure()
    {
        if (phase != Phase::Idle) return;
        if (onGuided) onGuided (*this);
        else startMeasure();
    }

    // ---- guided measuring ----
    juce::String guidedTitle() const override { return juce::String ("Room EQ, ") + (measuringAfter ? "check " + juce::String (juce::jmax (1, room.checkRound)) : juce::String ("as found")); }

    std::vector<GuideSpot> guidedSpots() override
    {
        const int n = settings.micCount.load();
        // the mics go to the next planned positions, unless one is already at a position still to measure
        bool atNew = false, unplaced = false;
        for (int m = 0; m < n; ++m)
        {
            const auto& pos = proc.mics.position[(size_t) m];
            unplaced = unplaced || pos.isEmpty();
            atNew = atNew || (pos.isNotEmpty() && ! room.skipped.contains (pos) && ! room.hasAt (pos, measuringAfter));
        }
        if ((unplaced || ! atNew) && plannedLeft (measuringAfter) > 0) planNextRound (false);

        std::vector<GuideSpot> out;
        const size_t markersBefore = proc.plan.items.size();
        const int nSpk = (int) proc.system.sources.size();
        auto isTop = [this] (int i) { return proc.system.sources[(size_t) i].role != (int) SpeakerRole::Sub; };
        for (const auto& pos : proc.positions)
        {
            if (room.skipped.contains (pos)) continue;
            GuideSpot sp;
            sp.label = pos;
            sp.marker = proc.plan.ensureMarker (pos);
            sp.x = proc.plan.items[(size_t) sp.marker].x;
            sp.y = proc.plan.items[(size_t) sp.marker].y;
            if (room.hasAt (pos, measuringAfter)) sp.state = GuideSpot::Done;
            else
            {
                for (int m = 0; m < n; ++m)
                    if (proc.mics.position[(size_t) m] == pos) { sp.mic = m; sp.state = GuideSpot::Now; break; }
            }
            const int aim = proc.roomAim;
            sp.up = aim == Cal90;
            if (! sp.up) sp.aimAt = proc.plan.nearestSpeaker (sp.x, sp.y, nSpk, isTop);
            if (sp.mic >= 0) sp.how = "seated ear height, " + aimHowText (proc, sp.mic, RoomAim);
            out.push_back (sp);
        }
        // the mic symbols sit on their positions
        for (const auto& sp : out)
            if (sp.mic >= 0 && sp.mic < kMaxMics) { proc.plan.mics[(size_t) sp.mic].x = sp.x; proc.plan.mics[(size_t) sp.mic].y = sp.y; proc.plan.mics[(size_t) sp.mic].placed = true; }
        if (proc.plan.items.size() != markersBefore && onChanged) onChanged();
        return out;
    }

    bool guidedNeedsProcessor() const override { return true; }
    juce::String guidedHowTo() const override
    {
        if (proc.positions.isEmpty()) return "Add the listening points on the venue plan first.";
        juce::StringArray idle;
        for (int m = 0; m < settings.micCount.load(); ++m)
        {
            const auto& pos = proc.mics.position[(size_t) m];
            if (pos.isEmpty() || room.skipped.contains (pos) || room.hasAt (pos, measuringAfter)) idle.add (proc.mics.nameOf (m));
        }
        juce::String t = (playOutput == 0 ? juce::String ("First: unmute every speaker of the system. ") : juce::String())
                         + "Mics at seated ear height, about 4 ft (1.2 m). Pink noise plays from " + outputBox.getText().toLowerCase()
                         + ". Drag the pulsing point if it is not where the mic really is.";
        if (! idle.isEmpty() && idle.size() < settings.micCount.load())
            t << " " << idle.joinIntoString (", ") << (idle.size() == 1 ? " sits this one out." : " sit this one out.");
        return t;
    }

    bool guidedStart() override { startMeasure(); return phase != Phase::Idle; }
    void guidedStop() override { cancel ("Stopped."); }
    bool guidedBusy() const override { return phase != Phase::Idle; }
    float guidedProgress() const override { return progress; }
    juce::String guidedPhase() const override { return phaseText(); }
    int guidedSerial() const override { return measureSerial; }
    juce::String guidedStatus() const override { return status; }

    void guidedRedo (const std::vector<GuideSpot>& was) override
    {
        for (const auto& sp : was) if (sp.mic >= 0) proc.setMicPosition (sp.mic, sp.label);
        refresh();
    }

    bool guidedCanSkip() const override { return true; }
    void guidedSkip() override
    {
        for (int m = 0; m < settings.micCount.load(); ++m)
        {
            const auto& pos = proc.mics.position[(size_t) m];
            if (pos.isNotEmpty() && ! room.hasAt (pos, measuringAfter)) room.skipped.addIfNotAlreadyThere (pos);
        }
        planNextRound (false);
        changed();
    }

    void guidedClosed() override { refresh(); }
    int guidedAimKind() const override { return RoomAim; }
    int guidedAimMic() const override
    {
        for (int m = 0; m < settings.micCount.load(); ++m)
            if (proc.mics.position[(size_t) m].isNotEmpty() && ! room.hasAt (proc.mics.position[(size_t) m], measuringAfter)) return m;
        return 0;
    }
    void measure() { startMeasure(); }
    void suggestNow() { suggest(); }
    void applyNow() { applyEq(); }
    void setMeasuringAfter (bool a) { measuringAfter = a; phaseButtons[a ? 1 : 0].setToggleState (true, juce::dontSendNotification); }
    juce::String statusText() const { return status; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, posCard.toFloat(), "Listening points");
        drawStep (g, step1, 1, "Measure", room.count (false) > 0);
        drawStep (g, step2, 2, "Target and EQ", room.checkRound > 0 && room.suggested.empty());
        drawStep (g, step3, 3, "Check", room.tuned);

        // positions list
        for (int i = 0; i < proc.positions.size(); ++i)
        {
            const auto row = posRow (i);
            if (row.getBottom() > posList.getBottom()) break;
            const auto& name = proc.positions[i];
            const bool sel = name == selectedPos;
            g.setColour (sel ? p.panelRaised : p.panel);
            g.fillRoundedRectangle (row.toFloat(), 5.0f);
            if (sel) { g.setColour (p.accent); g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 5.0f, 1.2f); }
            auto r = row.reduced (8, 0);
            {
                // in the measuring plan: a tick box in the position's colour
                const auto box = r.removeFromLeft (16).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
                const bool planned = ! room.skipped.contains (name);
                g.setColour (positionColour (i));
                if (planned)
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
            }
            r.removeFromLeft (8);
            juce::String st;
            if (room.hasAt (name, false)) st << "as found";
            if (room.hasAt (name, true)) st << (st.isEmpty() ? "" : " + ") << "check " << room.checkRound;
            g.setFont (font (10.5f, st.isNotEmpty()));
            g.setColour (st.isNotEmpty() ? p.good : p.textFaint);
            g.drawText (st.isNotEmpty() ? st.toUpperCase() : juce::String ("not measured"), r.removeFromRight (104), juce::Justification::centredRight, true);
            int here = 0;
            for (int m = 0; m < settings.micCount.load(); ++m) here += proc.mics.position[(size_t) m] == name ? 1 : 0;
            juce::String sub = room.skipped.contains (name) ? juce::String ("left out (tick to measure it)") : juce::String();
            if (here > 0) sub << (sub.isEmpty() ? "" : "  -  ") << (here == 1 ? "a mic is here" : juce::String (here) + " mics here");
            g.setColour (p.text);
            g.setFont (font (12.5f, true));
            g.drawText (name, sub.isEmpty() ? r : r.withTrimmedBottom (r.getHeight() / 2 - 2), sub.isEmpty() ? juce::Justification::centredLeft : juce::Justification::bottomLeft, true);
            if (sub.isNotEmpty())
            {
                g.setColour (here > 0 ? p.accent : p.textDim);
                g.setFont (font (10.5f));
                g.drawText (sub, r.withTrimmedTop (r.getHeight() / 2 + 1), juce::Justification::topLeft, true);
            }
        }

        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawFittedText (settings.micCount.load() == 1
                              ? "One mic: the guided pop-up says where to move it next. Untick a point to leave it out."
                              : "The mics measure their points at the same time; the pop-up then says where to move them for the rest.",
                          micHint, juce::Justification::topLeft, 3);

        // step texts
        auto txt = [&] (juce::Rectangle<int> r, const juce::String& t, juce::Colour c, bool bold = false)
        {
            g.setColour (c);
            g.setFont (font (11.5f, bold));
            g.drawFittedText (t, r, juce::Justification::topLeft, 4);
        };
        for (const auto& l : labels)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            g.drawText (l.first.toUpperCase(), l.second, juce::Justification::centredLeft, true);
        }
        if (! promptArea.isEmpty())
        {
            g.setColour (p.accent.withAlpha (0.16f));
            g.fillRoundedRectangle (promptArea.toFloat(), 6.0f);
            g.setColour (p.accent);
            g.fillRoundedRectangle (promptArea.toFloat().removeFromLeft (4.0f), 2.0f);
            g.setColour (p.text);
            g.setFont (font (12.5f, true));
            g.drawFittedText (movePrompt, promptArea.reduced (12, 4), juce::Justification::centredLeft, 3);
        }
        if (phase != Phase::Idle) txt (measureInfo, phaseText(), p.accent, true);
        else txt (measureInfo, status.isNotEmpty() ? status
                                                   : juce::String (room.count (false)) + " position(s) measured as found"
                                                         + (room.checkRound > 0 ? ", " + juce::String (room.count (true)) + " in check " + juce::String (room.checkRound) : juce::String()) + ".",
                  status.isNotEmpty() ? p.accent : p.textDim, status.isNotEmpty());
        if (phase != Phase::Idle)
        {
            auto bar = measureInfo.withTrimmedTop (measureInfo.getHeight() - 5).toFloat();
            g.setColour (p.border);
            g.fillRoundedRectangle (bar, 2.5f);
            g.setColour (p.accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 2.5f);
        }

        {
            double lo, hi;
            eqRange (lo, hi);
            juce::String t = "Range " + freqLabel (lo) + " - " + freqLabel (hi) + " Hz ("
                             + (room.sysMeasured ? "where the speakers play, from Speaker response" : room.sysHasShape ? "from the speakers' filters" : "default") + ").";
            if (room.sysNote.isNotEmpty()) t << "\n" << room.sysNote;
            {
                juce::String why;
                if (room.acousticsVerdict (why) >= 2) t << "\nRoom acoustics: " << why;
            }
            if (room.allow.wanted)
            {
                if (! room.allow.on) t << "\nAllow for the room acoustics: needs Room acoustics measured first.";
                else t << "\nAllowing for the room: " << room.allow.summary();
            }
            if (const int narrow = narrowBands(); narrow > 0)
                t << "\n" << narrow << (narrow == 1 ? " band" : " bands") << " in the processor now " << (narrow == 1 ? "is" : "are")
                  << " narrower than room EQ should be (a cut above Q " << juce::String (proc.pref ("eqCutQ"), 1) << ", or a boost above Q " << juce::String (proc.pref ("eqBoostQ"), 1) << "): widen or remove "
                  << (narrow == 1 ? "it" : "them") << " in the processor, then measure a check.";
            txt (rangeInfo, t, room.sysNote.isNotEmpty() ? p.accent : p.textDim);
            auto r = bandsArea;
            auto bandLine = [&] (int n, const eq::Band& b, juce::Colour c)
            {
                g.setColour (c);
                g.setFont (monoFont (11.5f));
                g.drawText (juce::String (n).paddedLeft (' ', 2) + "   " + (freqLabel (b.freq) + " Hz").paddedRight (' ', 9) + "  "
                                + signedText ((float) b.gainDb, 1).paddedLeft (' ', 5) + " dB   Q " + juce::String (b.q, 2),
                            r.removeFromTop (16), juce::Justification::centredLeft);
            };
            auto heading = [&] (const juce::String& h)
            {
                g.setColour (p.textDim);
                g.setFont (font (10.5f, true));
                g.drawText (h.toUpperCase(), r.removeFromTop (18), juce::Justification::bottomLeft);
            };
            if (! room.suggested.empty())
            {
                heading (room.checkRound > 0 ? "Change to enter (refinement)" : "To enter in your processor");
                for (size_t i = 0; i < room.suggested.size() && r.getHeight() >= 15; ++i)
                    bandLine ((int) i + 1, room.suggested[i], room.suggested[i].gainDb < 0.0 ? p.text : p.warn);
                r.removeFromTop (6);
            }
            if (! room.baseEq.empty() && r.getHeight() > 34)
            {
                heading ("In your processor now (" + room.eqGroupName (proc.system) + ")");
                for (size_t i = 0; i < room.baseEq.size(); ++i)
                {
                    if (r.getHeight() < 30 && i + 1 < room.baseEq.size())
                    {
                        g.setColour (p.textFaint);
                        g.setFont (font (11.0f));
                        g.drawText ("... and " + juce::String ((int) (room.baseEq.size() - i)) + " more (all in the processor settings)", r.removeFromTop (16), juce::Justification::centredLeft);
                        break;
                    }
                    bandLine ((int) i + 1, room.baseEq[i], p.textDim);
                }
            }
            if (room.suggested.empty() && room.baseEq.empty())
            {
                g.setColour (p.textFaint);
                g.setFont (font (11.5f));
                g.drawFittedText (room.count (false) == 0 ? "Measure as found first." : "Press Suggest EQ.", r, juce::Justification::topLeft, 2);
            }
        }

        {
            double lo, hi, rb, mb, ra, ma;
            eqRange (lo, hi);
            juce::String t;
            if (room.count (false) > 0)
            {
                room.deviation (room.average (false), lo, hi, rb, mb);
                t << "As found: " << juce::String (rb, 1) << " dB RMS from the target (worst " << juce::String (mb, 1) << " dB).";
                const double spread = room.levelSpread (false);
                if (std::isfinite (spread) && room.count (false) > 1) t << "\nLevel across positions: " << juce::String (spread, 1) << " dB.";
                const double v0 = seatVariance (room, false);
                if (std::isfinite (v0)) t << "\nSeat to seat: " << juce::String (v0, 1) << " dB as found" << (room.count (true) > 1 ? ", " + juce::String (seatVariance (room, true), 1) + " dB now" : juce::String())
                                         << " (the tonal difference between points; under 2 dB is very even).";
            }
            if (room.count (true) > 0)
            {
                room.deviation (room.average (true), lo, hi, ra, ma);
                t << "\nCheck " << room.checkRound << ": " << juce::String (ra, 1) << " dB RMS (worst " << juce::String (ma, 1) << " dB)"
                  << (ra <= proc.pref ("eqClose") ? (proc.listening.done ? ": close enough, mark the room as tuned." : ": close enough. Now the listening check, then mark it as tuned.")
                                                  : ": refine the EQ, or mark it as tuned if it sounds right.");
            }
            if (t.isEmpty()) t = "Once the EQ is in your processor (I've entered this EQ), measure a check at the same positions.";
            txt (checkInfo, t, room.count (true) > 0 ? p.text : p.textDim);
        }
    }

    void resized() override
    {
        labels.clear();
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (290);
        r.removeFromLeft (10);
        auto right = r.removeFromRight (350);
        r.removeFromRight (10);

        // left: the listening points
        posCard = left;
        {
            auto c = posCard.reduced (12, 0).withTrimmedTop (36);
            auto b = c.removeFromBottom (44).withSizeKeepingCentre (c.getWidth(), 28);
            addPositionButton.setBounds (b.removeFromLeft (190));
            micHint = c.removeFromBottom (44);
            c.removeFromBottom (6);
            posList = c;
        }

        // centre: graph and what it shows
        auto toggles = r.removeFromBottom (28);
        r.removeFromBottom (6);
        graph.setBounds (r);
        const int tw = toggles.getWidth() / 4;
        for (auto* t : { &showPositions, &showBefore, &showAfter, &showPredicted }) t->setBounds (toggles.removeFromLeft (tw));

        // right: three steps
        step1 = right.removeFromTop (movePrompt.isNotEmpty() ? 250 : 196);
        right.removeFromTop (8);
        step3 = right.removeFromBottom (166);
        right.removeFromBottom (8);
        step2 = right;
        {
            auto c = step1.reduced (12, 0).withTrimmedTop (40);
            auto row = c.removeFromTop (28);
            labels.push_back ({ "Play", row.removeFromLeft (50) });
            outputBox.setBounds (row);
            c.removeFromTop (6);
            auto ph = c.removeFromTop (26);
            phaseButtons[0].setBounds (ph.removeFromLeft (ph.getWidth() / 2 - 3));
            ph.removeFromLeft (6);
            phaseButtons[1].setBounds (ph);
            c.removeFromTop (8);
            promptArea = movePrompt.isNotEmpty() ? c.removeFromTop (48) : juce::Rectangle<int>();
            if (movePrompt.isNotEmpty()) c.removeFromTop (6);
            measureButton.setBounds (c.removeFromTop (32));
            c.removeFromTop (6);
            measureInfo = c.removeFromTop (40);
        }
        {
            auto c = step2.reduced (12, 0).withTrimmedTop (40);
            auto put = [&] (const juce::String& name, juce::Component& comp)
            {
                auto row = c.removeFromTop (26);
                labels.push_back ({ name, row.removeFromLeft (64) });
                comp.setBounds (row);
                c.removeFromTop (6);
            };
            put ("Target", targetLink);
            put ("EQ on", speakerBox);
            {
                auto row = c.removeFromTop (24);
                row.removeFromLeft (64);
                allowToggle.setBounds (row);
                c.removeFromTop (4);
            }
            {
                auto row = c.removeFromTop (26);
                labels.push_back ({ "Limits", row.removeFromLeft (64) });
                bandsBox.setBounds (row.removeFromLeft (row.getWidth() * 42 / 100));
                row.removeFromLeft (6);
                boostBox.setBounds (row);
                c.removeFromTop (6);
            }
            {
                juce::String why;
                const int lines = 1 + (room.sysNote.isNotEmpty() ? 1 : 0) + (room.acousticsVerdict (why) >= 2 ? 2 : 0) + (narrowBands() > 0 ? 2 : 0)
                                  + (room.allow.wanted ? 2 : 0);
                rangeInfo = c.removeFromTop (juce::jmin (lines * 16 + 2, juce::jmax (48, c.getHeight() - 160)));
            }
            c.removeFromTop (4);
            suggestButton.setBounds (c.removeFromTop (30));
            c.removeFromTop (8);
            auto bottom = c.removeFromBottom (30);
            applyButton.setBounds (bottom);
            c.removeFromBottom (6);
            bandsArea = c;
        }
        {
            auto c = step3.reduced (12, 0).withTrimmedTop (40);
            tunedButton.setBounds (c.removeFromBottom (36).withSizeKeepingCentre (c.getWidth(), 30));
            checkInfo = c;
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < proc.positions.size(); ++i)
            if (posRow (i).contains (e.getPosition()))
            {
                if (e.x < posRow (i).getX() + 30)   // the tick box: in or out of the measuring plan
                {
                    const auto& name = proc.positions[i];
                    if (room.skipped.contains (name)) room.skipped.removeString (name);
                    else room.skipped.add (name);
                    movePrompt.clear();
                    changed();
                    return;
                }
                selectedPos = proc.positions[i] == selectedPos ? juce::String() : proc.positions[i];
                graph.setSelectedPosition (selectedPos);
                repaint();
                return;
            }
    }

private:
    enum class Phase { Idle, Settling, FindingTime, Averaging };
    static constexpr double kSettleSeconds = 3.0;

    void drawStep (juce::Graphics& g, juce::Rectangle<int> area, int num, const juce::String& title, bool done) const
    {
        const auto& p = pal();
        const auto r = area.toFloat();
        g.setColour (p.panel);
        g.fillRoundedRectangle (r, 8.0f);
        const bool required = num < 3 || room.count (false) > 0;
        g.setColour (done ? p.good.withAlpha (0.5f) : required ? p.warn.withAlpha (0.6f) : p.border);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, done || required ? 1.4f : 1.0f);
        const auto badge = juce::Rectangle<float> (22.0f, 22.0f).withPosition (r.getX() + 12.0f, r.getY() + 10.0f);
        if (done) drawStatusBadge (g, badge, StepStatus::Ready);
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

    juce::Rectangle<int> posRow (int i) const { return { posList.getX(), posList.getY() + i * 42, posList.getWidth(), 38 }; }

    void changed()
    {
        if (onChanged) onChanged();
        refreshMeasureButton();
        graph.repaint();
        repaint();
    }

    void syncGraph()
    {
        graph.setShow (showPositions.getToggleState(), showBefore.getToggleState(), showAfter.getToggleState(), showPredicted.getToggleState());
    }

    void refreshMeasureButton()
    {
        const bool b = phase != Phase::Idle;
        measureButton.setButtonText (b ? "Stop" : measuringAfter ? "Measure check " + juce::String (juce::jmax (1, room.checkRound)) : juce::String ("Measure as found"));
        phaseButtons[1].setButtonText (room.checkRound > 0 ? "Check " + juce::String (room.checkRound) : juce::String ("Check"));
        phaseButtons[1].setEnabled (room.checkRound > 0);
        measureButton.setColour (juce::TextButton::buttonColourId, b ? pal().bad : pal().accent);
        measureButton.setEnabled (proc.isStandalone() || proc.uiAsStandalone);
        applyButton.setEnabled (! room.suggested.empty());
        suggestButton.setButtonText (room.checkRound > 0 && room.count (true) > 0 ? "Refine the EQ" : "Suggest EQ");
        suggestButton.setEnabled (room.count (false) > 0 && phase == Phase::Idle);

    }

    void eqRange (double& lo, double& hi) const { room.eqRange (proc.system, lo, hi); }

    // Bands in the processor now that are narrower than room EQ allows (entered before the limits were set).
    int narrowBands() const
    {
        int n = 0;
        const double cutQ = proc.pref ("eqCutQ"), boostQ = proc.pref ("eqBoostQ");
        for (const auto& b : room.baseEq) n += (b.gainDb < 0.0 && b.q > cutQ + 0.05) || (b.gainDb > 0.0 && b.q > boostQ + 0.05) ? 1 : 0;
        return n;
    }

    void suggest()
    {
        room.updateSystem (proc.system);
        const bool fromCheck = room.checkRound > 0 && room.count (true) > 0;
        if (room.count (false) == 0) { status = "Measure the positions as found first."; repaint(); return; }
        updateRoomAllowance (proc);
        const auto& al = room.allow;
        eq::Options o;
        eqRange (o.fLo, o.fHi);
        // the room modes most seats share are cut first (with their own width); the rest is fitted
        std::vector<eq::Band> modeBands;
        if (al.on)
            for (const auto& c : al.modeCuts)
                if (c.freq >= o.fLo && (int) modeBands.size() < room.maxBands - 1)
                    modeBands.push_back ({ c.freq, al.capCuts ? std::max (c.gainDb, -al.cutCap) : c.gainDb, c.q });
        o.maxBands = juce::jmax (1, room.maxBands - (int) modeBands.size());
        o.maxBoostDb = room.maxBoostDb;
        o.maxCutDb = al.on && al.capCuts ? std::min (proc.pref ("eqMaxCut"), al.cutCap) : proc.pref ("eqMaxCut");
        // room EQ is broad: gentle bands only (a narrow one chases a reflection at one seat), fitted to a 1/3 octave average
        o.minQ = 0.7;
        o.maxQ = proc.pref ("eqCutQ");
        o.maxBoostQ = proc.pref ("eqBoostQ");
        o.narrowQ = 3.0;
        o.smoothOct = 1.0 / 3.0;
        // fit what is left: from the latest check (the EQ entered so far is already in that measurement)
        const auto f = RoomTuning::grid();
        const auto tgt = room.targetCurve();
        const auto input = al.on ? room.fitInput (fromCheck, tgt, modeBands) : room.average (fromCheck);
        const auto r = eq::fit (f, input, tgt, o);
        room.suggested = modeBands;
        room.suggested.insert (room.suggested.end(), r.bands.begin(), r.bands.end());
        std::sort (room.suggested.begin(), room.suggested.end(), [] (const eq::Band& a, const eq::Band& b) { return a.freq < b.freq; });
        room.offsetDb = r.offsetDb;
        {
            // before and after against the target, from the measurement itself
            double lo, hi, mx;
            eqRange (lo, hi);
            const auto avg = room.average (fromCheck);
            room.deviation (avg, lo, hi, room.rmsBefore, mx);
            auto pred = avg;
            for (size_t i = 0; i < f.size(); ++i) if (std::isfinite (pred[i])) pred[i] += eq::totalDb (room.suggested, f[i]);
            room.deviation (pred, lo, hi, room.rmsAfter, mx);
        }
        const int positions = room.count (fromCheck);
        if (room.suggested.empty())
            status = fromCheck ? juce::String ("Within 1 dB of the target: nothing to refine. Mark the room as tuned.")
                               : juce::String ("Already within 1 dB of the target: no EQ needed.");
        else
        {
            status = juce::String (fromCheck ? "Refinement: " : "") + juce::String ((int) room.suggested.size()) + (room.suggested.size() == 1 ? " band: " : " bands: ")
                     + juce::String (room.rmsBefore, 1) + " dB RMS from the target now, " + juce::String (room.rmsAfter, 1) + " dB predicted"
                     + (al.on ? juce::String (" (allowing for the room acoustics)") : juce::String()) + ". Enter "
                     + (fromCheck ? "the changes" : "them") + " in your processor on " + room.eqGroupName (proc.system) + ", then press I've entered this EQ.";
            if (positions == 1) status << " Only one position is measured: this EQ is provisional (it may be fixing that one seat); measure more positions.";
        }
        changed();
    }

    // The bands are in the processor now: note them (for the settings sheet and the report) on every speaker in the
    // group, and measure a check at the same positions next.
    void applyEq()
    {
        if (room.suggested.empty()) return;
        const auto entered = eq::merge (room.baseEq, room.suggested, room.maxBands);
        for (int i : room.eqMembers (proc.system)) proc.system.sources[(size_t) i].proc.eq = entered;
        proc.system.syncLinks();
        room.baseEq = entered;
        room.suggested.clear();
        proc.noteTunedConditions();
        // the previous check is replaced by the next one
        room.meas.erase (std::remove_if (room.meas.begin(), room.meas.end(), [] (const RoomMeasurement& m) { return m.after; }), room.meas.end());
        ++room.checkRound;
        room.tuned = false;
        status = "Noted as entered on " + room.eqGroupName (proc.system) + " (" + juce::String ((int) entered.size()) + " bands in all). Now measure check "
                 + juce::String (room.checkRound) + " at the same positions.";
        setMeasuringAfter (true);
        movePrompt.clear();
        refresh();
        changed();
    }

    int plannedLeft (bool after) const
    {
        int n = 0;
        for (const auto& pos : proc.positions) n += ! room.skipped.contains (pos) && ! room.hasAt (pos, after) ? 1 : 0;
        return n;
    }

    // The next round: each mic to a planned position not measured yet in this round (before or after EQ), in order.
    // One mic visits them one by one; several mics cover several at a time. Says where to move them.
    void planNextRound (bool fromButton)
    {
        juce::StringArray todo;
        for (const auto& pos : proc.positions)
            if (! room.skipped.contains (pos) && ! room.hasAt (pos, measuringAfter)) todo.add (pos);
        const int n = settings.micCount.load();
        if (todo.isEmpty())
        {
            movePrompt.clear();
            if (fromButton) status = "Every planned position is measured" + juce::String (measuringAfter ? " in this check." : ".");
            resized();
            repaint();
            return;
        }
        juce::StringArray moves;
        for (int m = 0; m < n && m < todo.size(); ++m)
        {
            if (proc.mics.position[(size_t) m] != todo[m]) moves.add (proc.mics.nameOf (m) + " to " + todo[m]);
            proc.setMicPosition (m, todo[m]);
        }
        const int left = juce::jmax (0, todo.size() - n);
        movePrompt = moves.isEmpty() ? "Press Measure for " + todo.joinIntoString (", ", 0, n) + "."
                                     : "Move " + moves.joinIntoString (", ") + ", then press Measure.";
        if (left > 0) movePrompt << " " << left << (left == 1 ? " more position after this." : " more positions after this.");
        refresh();
        changed();
    }

    // ---- measuring ----
    double rate() const
    {
        if (snap != nullptr && snap->sampleRate > 1000.0) return snap->sampleRate;
        return proc.getSampleRate() > 1000.0 ? proc.getSampleRate() : 48000.0;
    }

    void startMeasure()
    {
        if (settings.refSource.load() == 0) { status = "Set a reference on Mic setup to measure."; repaint(); return; }
        bool unplaced = false;
        for (int m = 0; m < settings.micCount.load(); ++m) unplaced = unplaced || proc.mics.position[(size_t) m].isEmpty();
        if (unplaced) planNextRound (false);   // put them at the first planned positions
        for (int m = 0; m < settings.micCount.load(); ++m)
            if (proc.mics.position[(size_t) m].isEmpty()) { status = "Say where " + proc.mics.nameOf (m) + " is first."; repaint(); return; }
        movePrompt.clear();
        // mics left at a position already measured in this round sit this one out (unless every mic is)
        measuredAtStart.clear();
        bool anyNew = false;
        for (int m = 0; m < settings.micCount.load(); ++m) anyNew = anyNew || ! room.hasAt (proc.mics.position[(size_t) m], measuringAfter);
        if (anyNew)
            for (int m = 0; m < settings.micCount.load(); ++m)
                if (room.hasAt (proc.mics.position[(size_t) m], measuringAfter)) measuredAtStart.add (proc.mics.position[(size_t) m]);
        resized();
        proc.noteAsFound();
        settings.captureAllIr = true;   // each mic's impulse response, for the room acoustics
        settings.genOutput = playOutput;
        settings.genType = 0;
        settings.genOn = true;
        settings.running = true;
        phase = Phase::Settling;
        phaseStart = la::clock::ms();
        settleUntil = proc.analyzer.samplesWritten() + (juce::uint64) (kSettleSeconds * rate());
        progress = 0.0f;
        status.clear();
        refreshMeasureButton();
        repaint();
    }

    void cancel (const juce::String& why)
    {
        settings.genOn = false;
        settings.captureAllIr = false;
        phase = Phase::Idle;
        status = why;
        refreshMeasureButton();
        repaint();
    }

    juce::String phaseText() const
    {
        switch (phase)
        {
            case Phase::Settling: return "Playing pink noise...";
            case Phase::FindingTime: return "Finding each mic's arrival...";
            case Phase::Averaging: return "Averaging at " + juce::String (settings.micCount.load()) + (settings.micCount.load() == 1 ? " position..." : " positions...");
            case Phase::Idle:
            default: return {};
        }
    }

    void timerCallback() override
    {
        if (juce::jmax (1, proc.numOutputs()) + 1 != outputBox.getNumItems()) refresh();   // the same count refresh() builds with
        if (phase == Phase::Idle) return;
        if (! settings.genOn.load()) { cancel ("Stopped: the generator was turned off."); return; }
        const double elapsed = la::clock::ms() - phaseStart;
        if (phase == Phase::Settling)
        {
            const auto done = proc.analyzer.samplesAnalysed();
            progress = 0.2f * (float) juce::jlimit (0.0, 1.0, 1.0 - ((double) settleUntil - (double) done) / (kSettleSeconds * rate()));
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
            progress = 0.2f + (float) juce::jmin (1.0, elapsed / 2000.0) * 0.1f;
            if (proc.analyzer.readDelayReport().serial != delaySerial)
            {
                settings.requestResetAverages = true;
                sawReset = false;
                phase = Phase::Averaging;
                phaseStart = la::clock::ms();
            }
            else if (elapsed > 10000.0) { cancel ("The timing search did not answer. Check the audio device."); return; }
        }
        else if (phase == Phase::Averaging)
        {
            const int want = juce::jmax (4, settings.averages.load());
            int frames = 1 << 30;
            for (int m = 0; m < settings.micCount.load(); ++m)
                frames = juce::jmin (frames, snap != nullptr && m < (int) snap->mics.size() ? snap->mics[(size_t) m].tfFrames : 0);
            if (! sawReset && frames < want) sawReset = true;
            progress = 0.3f + 0.7f * (sawReset ? juce::jlimit (0.0f, 1.0f, (float) frames / (float) want) : 0.0f);
            if (sawReset && frames >= want && elapsed > 1500.0) capture();
            else if (elapsed > 30000.0) { if (sawReset && frames > 0) capture(); else cancel ("No transfer function: check the reference."); return; }
        }
        repaint (step1);
    }

    void capture()
    {
        settings.genOn = false;
        settings.captureAllIr = false;
        phase = Phase::Idle;
        if (snap == nullptr) { cancel ("No data."); return; }
        const float minCoh = juce::jmax (0.2f, settings.coherenceBlank.load());
        juce::StringArray done;
        for (int m = 0; m < settings.micCount.load() && m < (int) snap->mics.size(); ++m)
        {
            const auto& r = snap->mics[(size_t) m];
            RoomMeasurement rm;
            rm.position = proc.mics.position[(size_t) m];
            if (measuredAtStart.contains (rm.position)) continue;   // done in an earlier round
            rm.mic = m;
            rm.micName = proc.mics.nameOf (m);
            const int a = proc.activeCalAngle (m);
            rm.calUsed = a >= 0 ? calAngleName (a) + " " + proc.calibrations (m).name[a] : juce::String ("no cal file");
            rm.time = juce::Time::getCurrentTime().formatted ("%H:%M");
            rm.after = measuringAfter;
            for (size_t i = 0; i < snap->tfFreq.size() && i < r.tfMag.size(); ++i)
            {
                if (! std::isfinite (r.tfMag[i]) || (i < r.tfCoh.size() && r.tfCoh[i] < minCoh)) continue;
                rm.tf.freq.push_back (snap->tfFreq[i]);
                rm.tf.db.push_back (r.tfMag[i]);
                rm.tf.phase.push_back (i < r.tfPhase.size() && std::isfinite (r.tfPhase[i]) ? r.tfPhase[i] : 0.0);
                rm.tf.extra.push_back (i < r.tfCoh.size() ? r.tfCoh[i] : 0.0);
            }
            if (rm.tf.freq.size() < 16) continue;
            if (r.ir.size() > 1024)
            {
                rm.ac = acoustics::analyse (r.ir, rate());
                rm.fdw = windowedDelta (r.ir, rate(), RoomTuning::grid());
            }
            // a new measurement at a position replaces the old one of the same round
            room.meas.erase (std::remove_if (room.meas.begin(), room.meas.end(),
                                             [&rm] (const RoomMeasurement& x) { return x.position == rm.position && x.after == rm.after; }),
                             room.meas.end());
            done.add (rm.micName + " at " + rm.position);
            room.meas.push_back (std::move (rm));
        }
        if (done.isEmpty()) status = "Nothing reliable was measured: raise the level or reduce the noise.";
        else
        {
            status = "Measured " + done.joinIntoString (", ") + ".";
            ++measureSerial;
            if (! measuringAfter)
            {
                room.suggested.clear();
                if (room.count (false) == (int) done.size() && room.checkRound == 0)
                {
                    const auto members = room.eqMembers (proc.system);
                    room.baseEq = members.empty() ? std::vector<eq::Band>() : proc.system.sources[(size_t) members.front()].proc.eq;   // the EQ in place as found
                }
            }
            planNextRound (false);
            if (movePrompt.isEmpty()) status << " Every planned position is measured.";
        }
        refreshMeasureButton();
        changed();
    }

    LiveAlignProcessor& proc;
    Settings& settings;
    RoomTuning& room;
    const Snapshot* snap = nullptr;
    RoomGraph graph;

    juce::ToggleButton showPositions { "Positions" }, showBefore { "As found" }, showAfter { "Check" }, showPredicted { "With suggested EQ" };
    juce::TextButton addPositionButton;
    juce::ComboBox outputBox, targetBox, speakerBox, bandsBox, boostBox;   // targetBox: kept for the preview, not shown
    juce::TextButton targetLink;
    juce::TextButton phaseButtons[2], measureButton { "Measure" }, suggestButton { "Suggest EQ" }, applyButton { "I've entered this EQ" };
    juce::ToggleButton allowToggle;
    juce::TextButton tunedButton { "Mark the room as tuned" };

    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
    juce::Rectangle<int> posCard, posList, micHint, step1, step2, step3, measureInfo, rangeInfo, bandsArea, checkInfo;

    juce::String selectedPos, status, movePrompt;
    juce::StringArray measuredAtStart;
    juce::Rectangle<int> promptArea;
    int playOutput = 0, measureSerial = 0;
    bool measuringAfter = false, sawReset = false;
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    float progress = 0.0f;
    juce::uint64 delaySerial = 0, settleUntil = 0;
};
} // namespace la::ui
