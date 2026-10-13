// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Step 3: Room tuning. Measure the whole system at the listening positions - several mics at once, or one mic moved
// from position to position - and compare their average with a target curve. LiveAlign suggests parametric EQ
// within your processor's limits; once it is in the processor, measure again to check the result.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "../PluginProcessor.h"

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

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto& room = proc.room;
        auto r = getLocalBounds().toFloat();
        drawPanel (g, r, "Room average vs target");
        {
            // legend, right of the title
            struct Item { juce::String name; juce::Colour c; bool dashed; };
            const Item items[] = { { "Before", p.text, false }, { "After", p.good, false }, { "Target", p.good.withAlpha (0.9f), true }, { "With suggested EQ", p.accent, true } };
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
        const auto before = room.average (false), after = room.average (true);
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
        // EQ range shading
        if (eqHi > eqLo)
        {
            g.setColour (p.accent.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (ax.x (eqLo), plot.getY(), ax.x (eqHi), plot.getBottom()));
        }

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        auto xAt = [&] (size_t k) { return ax.x (grid[k]); };

        if (! hasBefore && ! hasAfter)
        {
            g.setColour (p.textDim);
            g.setFont (font (13.5f));
            g.drawFittedText ("No measurements yet. Put the mic(s) at the listening positions, say where each one is on the left, and press Measure.",
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
                const double lvl = m.levelDb();
                const int pi = proc.positions.indexOf (m.position);
                const bool sel = m.position == selected;
                std::vector<double> raw (grid.size());
                for (size_t i = 0; i < grid.size(); ++i)
                    raw[i] = grid[i] >= m.tf.freq.front() && grid[i] <= m.tf.freq.back() ? m.tf.dbAt (grid[i]) - lvl : NAN;
                const auto v = eq::smooth (grid, raw, 1.0 / 6.0);   // the tonal shape at each seat, not every reflection
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
        // predicted: the before average with the suggested EQ
        if (showPred && hasBefore && ! room.suggested.empty())
        {
            auto pred = eq::smooth (grid, before, 1.0 / 12.0);
            for (size_t i = 0; i < grid.size(); ++i) if (std::isfinite (pred[i])) pred[i] += eq::totalDb (room.suggested, grid[i]);
            curve (pred, p.accent, 2.0f, true);
        }
    }

private:
    LiveAlignProcessor& proc;
    juce::String selected;
    bool showPos = true, showBefore = true, showAfter = true, showPred = true;
    double eqLo = 0.0, eqHi = 0.0;
};

//==============================================================================
class RoomPage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onChanged;
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

        addPositionButton.onClick = [this]
        {
            if (askText) askText ("New position", "Row " + juce::String (proc.positions.size() + 1), [this] (const juce::String& t)
            {
                if (t.isNotEmpty() && ! proc.positions.contains (t)) proc.positions.add (t);
                refresh();
                changed();
            });
        };
        addPositionButton.setTooltip ("Add a listening position (a row, a seat, the balcony ...)");
        addAndMakeVisible (addPositionButton);
        clearButton.onClick = [this] { clearSelected(); };
        clearButton.setTooltip ("Remove the measurements at the selected position");
        addAndMakeVisible (clearButton);
        nextPosButton.onClick = [this] { planNextRound (true); };
        nextPosButton.setTooltip ("Put the mics at the next planned positions that have not been measured, and say where to move them");
        addAndMakeVisible (nextPosButton);

        // ---- 1 measure ----
        outputBox.setTooltip ("Where the pink noise plays from: all outputs (the whole system), or one output");
        outputBox.onChange = [this] { playOutput = outputBox.getSelectedItemIndex(); };
        addAndMakeVisible (outputBox);
        for (int i = 0; i < 2; ++i)
        {
            phaseButtons[i].setButtonText (i == 0 ? "Before EQ" : "After EQ");
            phaseButtons[i].setRadioGroupId (8201);
            phaseButtons[i].setClickingTogglesState (true);
            phaseButtons[i].onClick = [this, i] { if (phaseButtons[i].getToggleState()) { measuringAfter = i == 1; repaint(); } };
            addAndMakeVisible (phaseButtons[i]);
        }
        phaseButtons[0].setToggleState (true, juce::dontSendNotification);
        measureButton.onClick = [this] { if (phase == Phase::Idle) startMeasure(); else cancel ("Stopped."); };
        measureButton.setTooltip ("Plays pink noise, finds each mic's arrival and averages the response at every mic, each at its position");
        addAndMakeVisible (measureButton);

        // ---- 2 target and EQ ----
        for (const auto& t : targets::list()) targetBox.addItem (t.name, targetBox.getNumItems() + 1);
        targetBox.onChange = [this] { room.target = targetBox.getSelectedItemIndex(); room.suggested.clear(); changed(); };
        targetBox.setTooltip ("The tonal balance to tune toward");
        speakerBox.onChange = [this] { room.eqSpeaker = speakerBox.getSelectedItemIndex(); room.suggested.clear(); changed(); };
        speakerBox.setTooltip ("The speaker the EQ goes on. Speakers that share its settings (Main R) get it too.");
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
        applyButton.setTooltip ("Put these bands in the processor settings of the chosen speaker (and the speakers that share its settings)");
        for (auto* c : std::initializer_list<juce::Component*> { &targetBox, &speakerBox, &bandsBox, &boostBox, &suggestButton, &applyButton })
            addAndMakeVisible (*c);

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
        // mic position pickers
        const int n = settings.micCount.load();
        if ((int) micBoxes.size() != n)
        {
            micBoxes.clear();
            for (int m = 0; m < n; ++m)
            {
                auto b = std::make_unique<juce::ComboBox>();
                b->setEditableText (true);
                b->setTextWhenNothingSelected ("Where is it?");
                b->onChange = [this, m]
                {
                    proc.setMicPosition (m, micBoxes[(size_t) m]->getText());
                    refresh();
                    changed();
                };
                b->setTooltip ("Where this mic is now. Its aim (and calibration file) follows the position.");
                addAndMakeVisible (*b);
                micBoxes.push_back (std::move (b));
            }
        }
        for (int m = 0; m < n; ++m)
        {
            auto& b = *micBoxes[(size_t) m];
            if (b.isPopupActive() || b.hasKeyboardFocus (true)) continue;
            b.clear (juce::dontSendNotification);
            for (int i = 0; i < proc.positions.size(); ++i) b.addItem (proc.positions[i], i + 1);
            b.setText (proc.mics.position[(size_t) m], juce::dontSendNotification);
        }

        const int outs = juce::jmax (1, proc.numOutputs());
        outputBox.clear (juce::dontSendNotification);
        outputBox.addItem ("All outputs (whole system)", 1);
        for (int i = 0; i < outs; ++i) outputBox.addItem ("Output " + juce::String (i + 1), i + 2);
        outputBox.setSelectedItemIndex (juce::jlimit (0, outs, playOutput), juce::dontSendNotification);

        targetBox.setSelectedItemIndex (juce::jlimit (0, targets::count() - 1, room.target), juce::dontSendNotification);
        speakerBox.clear (juce::dontSendNotification);
        for (int i = 0; i < (int) proc.system.sources.size(); ++i)
        {
            const auto& s = proc.system.sources[(size_t) i];
            if (proc.system.master (i) != i) continue;   // EQ goes on the speaker the others follow
            speakerBox.addItem (s.name + (s.proc.eq.empty() ? juce::String() : "  (" + juce::String ((int) s.proc.eq.size()) + " bands now)"), i + 1);
        }
        if (speakerBox.getNumItems() > 0)
        {
            room.eqSpeaker = juce::jlimit (0, (int) proc.system.sources.size() - 1, room.eqSpeaker);
            if (speakerBox.indexOfItemId (room.eqSpeaker + 1) < 0) room.eqSpeaker = speakerBox.getItemId (0) - 1;
            speakerBox.setSelectedId (room.eqSpeaker + 1, juce::dontSendNotification);
        }
        bandsBox.setSelectedId (room.maxBands, juce::dontSendNotification);
        if (bandsBox.getSelectedId() == 0) bandsBox.setSelectedId (8, juce::dontSendNotification);
        boostBox.setSelectedId ((int) room.maxBoostDb + 1, juce::dontSendNotification);
        if (boostBox.getSelectedId() == 0) boostBox.setSelectedId (4, juce::dontSendNotification);
        tunedButton.setButtonText (room.tuned ? "Room tuned " + room.tunedOn + "  (undo)" : juce::String ("Mark the room as tuned"));
        nextPosButton.setVisible (true);
        nextPosButton.setButtonText (n == 1 ? "Next position" : "Next positions");

        double lo, hi;
        eqRange (lo, hi);
        graph.setRange (lo, hi);
        syncGraph();
        resized();
        repaint();
    }

    // What to do next on this page (for the header).
    juce::String nextHint() const
    {
        if (room.count (false) == 0) return "Place the mics and measure";
        if (plannedLeft (false) > 0 && room.count (true) == 0) return "Measure the next positions";
        if (room.suggested.empty() && ! eqApplied()) return "Suggest EQ";
        if (! eqApplied()) return "Copy the EQ to the speaker settings";
        if (room.count (true) == 0) return "Enter the EQ in the processor, then measure after";
        if (! room.tuned) return "Mark the room as tuned";
        return {};
    }

    // Does what nextHint() says, from the header button.
    void doNext()
    {
        if (phase != Phase::Idle) return;
        const auto h = nextHint();
        if (h.startsWith ("Place") || h.startsWith ("Enter") || h.startsWith ("Measure the next")) { if (h.startsWith ("Enter")) setMeasuringAfter (true); startMeasure(); }
        else if (h.startsWith ("Suggest")) suggest();
        else if (h.startsWith ("Copy")) applyEq();
        else if (h.startsWith ("Mark")) { room.tuned = true; room.tunedOn = LiveAlignProcessor::today(); refresh(); changed(); }
    }

    bool busy() const { return phase != Phase::Idle; }
    void measure() { startMeasure(); }
    void suggestNow() { suggest(); }
    void applyNow() { applyEq(); }
    void setMeasuringAfter (bool a) { measuringAfter = a; phaseButtons[a ? 1 : 0].setToggleState (true, juce::dontSendNotification); }
    juce::String statusText() const { return status; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, posCard.toFloat(), "Listening positions");
        drawPanel (g, micCard.toFloat(), "Where the mics are now");
        drawStep (g, step1, 1, "Measure", room.count (false) > 0);
        drawStep (g, step2, 2, "Target and EQ", eqApplied());
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
            if (room.hasAt (name, false)) st << "before";
            if (room.hasAt (name, true)) st << (st.isEmpty() ? "" : " + ") << "after";
            g.setFont (font (10.5f, st.isNotEmpty()));
            g.setColour (st.isNotEmpty() ? p.good : p.textFaint);
            g.drawText (st.isNotEmpty() ? st.toUpperCase() : juce::String ("not measured"), r.removeFromRight (104), juce::Justification::centredRight, true);
            const int aim = proc.positionAimOf (name);
            int here = 0;
            for (int m = 0; m < settings.micCount.load(); ++m) here += proc.mics.position[(size_t) m] == name ? 1 : 0;
            juce::String sub = aim < 0 ? juce::String() : aim == 1 ? "Mic up (" + calAngleName (aim) + ")" : "Mic at the speaker (" + calAngleName (aim) + ")";
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

        // mics
        for (int m = 0; m < (int) micBoxes.size(); ++m)
        {
            auto lab = micLabel (m);
            g.setColour (micColour (m));
            g.fillEllipse (lab.removeFromLeft (10).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
            g.setColour (p.text);
            g.setFont (font (12.0f, true));
            g.drawText (proc.mics.nameOf (m), lab.withTrimmedLeft (6), juce::Justification::centredLeft, true);
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawFittedText (settings.micCount.load() == 1
                              ? "One mic: measure, and LiveAlign says where to move it next. Untick a position to leave it out."
                              : "The mics measure their positions at the same time; LiveAlign then says where to move them for the rest.",
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
                                                   : juce::String (room.count (false)) + " position(s) measured before EQ, " + juce::String (room.count (true)) + " after.",
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
            juce::String t = "Range " + freqLabel (lo) + " - " + freqLabel (hi) + " Hz (from the speaker's filters).";
            txt (rangeInfo, t, p.textDim);
            auto r = bandsArea;
            g.setFont (monoFont (11.5f));
            if (room.suggested.empty())
            {
                g.setColour (p.textFaint);
                g.setFont (font (11.5f));
                g.drawFittedText (room.count (false) == 0 ? "Measure first." : "Press Suggest EQ.", r, juce::Justification::topLeft, 2);
            }
            for (size_t i = 0; i < room.suggested.size() && r.getHeight() >= 15; ++i)
            {
                const auto& b = room.suggested[i];
                g.setColour (b.gainDb < 0.0 ? p.text : p.warn);
                g.drawText (juce::String ((int) i + 1).paddedLeft (' ', 2) + "   " + (freqLabel (b.freq) + " Hz").paddedRight (' ', 9) + "  "
                                + signedText ((float) b.gainDb, 1).paddedLeft (' ', 5) + " dB   Q " + juce::String (b.q, 2),
                            r.removeFromTop (16), juce::Justification::centredLeft);
            }
        }

        {
            double lo, hi, rb, mb, ra, ma;
            eqRange (lo, hi);
            juce::String t;
            if (room.count (false) > 0)
            {
                room.deviation (room.average (false), lo, hi, rb, mb);
                t << "Before: " << juce::String (rb, 1) << " dB RMS from the target (worst " << juce::String (mb, 1) << " dB).";
                const double spread = room.levelSpread (false);
                if (std::isfinite (spread) && room.count (false) > 1) t << "\nLevel across positions: " << juce::String (spread, 1) << " dB.";
            }
            if (room.count (true) > 0)
            {
                room.deviation (room.average (true), lo, hi, ra, ma);
                t << "\nAfter: " << juce::String (ra, 1) << " dB RMS (worst " << juce::String (ma, 1) << " dB).";
            }
            if (t.isEmpty()) t = "Measure again with After EQ selected once the EQ is in the processor.";
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

        // left: positions and mics
        const int micH = 34 + (int) micBoxes.size() * 34 + 56 + (nextPosButton.isVisible() ? 36 : 0);
        micCard = left.removeFromBottom (juce::jmin (left.getHeight() / 2, micH));
        left.removeFromBottom (10);
        posCard = left;
        {
            auto c = posCard.reduced (12, 0).withTrimmedTop (36);
            auto b = c.removeFromBottom (44).withSizeKeepingCentre (c.getWidth(), 28);
            addPositionButton.setBounds (b.removeFromLeft (130));
            b.removeFromLeft (8);
            clearButton.setBounds (b.removeFromLeft (110));
            posList = c;
        }
        {
            auto c = micCard.reduced (12, 0).withTrimmedTop (36);
            micLabels.clear();
            for (auto& b : micBoxes)
            {
                auto row = c.removeFromTop (28);
                micLabels.push_back (row.removeFromLeft (100));
                b->setBounds (row);
                c.removeFromTop (6);
            }
            if (nextPosButton.isVisible()) { nextPosButton.setBounds (c.removeFromTop (28)); c.removeFromTop (8); }
            micHint = c.removeFromTop (44);
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
            put ("Target", targetBox);
            put ("EQ on", speakerBox);
            {
                auto row = c.removeFromTop (26);
                labels.push_back ({ "Limits", row.removeFromLeft (64) });
                bandsBox.setBounds (row.removeFromLeft (row.getWidth() * 42 / 100));
                row.removeFromLeft (6);
                boostBox.setBounds (row);
                c.removeFromTop (6);
            }
            rangeInfo = c.removeFromTop (18);
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
    juce::Rectangle<int> micLabel (int m) const { return m < (int) micLabels.size() ? micLabels[(size_t) m] : juce::Rectangle<int>(); }

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
        measureButton.setButtonText (b ? "Stop" : measuringAfter ? "Measure after EQ" : "Measure");
        measureButton.setColour (juce::TextButton::buttonColourId, b ? pal().bad : pal().accent);
        measureButton.setEnabled (proc.isStandalone() || proc.uiAsStandalone);
        applyButton.setEnabled (! room.suggested.empty());
        clearButton.setEnabled (selectedPos.isNotEmpty() && (room.hasAt (selectedPos, false) || room.hasAt (selectedPos, true)));
    }

    void eqRange (double& lo, double& hi) const { room.eqRange (proc.system, lo, hi); }

    bool eqApplied() const
    {
        if (room.eqSpeaker < 0 || room.eqSpeaker >= (int) proc.system.sources.size()) return false;
        if (room.suggested.empty()) return false;
        const auto& eqNow = proc.system.sources[(size_t) room.eqSpeaker].proc.eq;
        const auto want = room.combinedEq();
        if (eqNow.size() != want.size()) return false;
        for (size_t i = 0; i < eqNow.size(); ++i)
            if (std::abs (eqNow[i].freq - want[i].freq) > 0.5 || std::abs (eqNow[i].gainDb - want[i].gainDb) > 0.05) return false;
        return true;
    }

    void suggest()
    {
        if (room.count (false) == 0) { status = "Measure the positions first."; repaint(); return; }
        eq::Options o;
        eqRange (o.fLo, o.fHi);
        o.maxBands = room.maxBands;
        o.maxBoostDb = room.maxBoostDb;
        o.maxCutDb = room.maxCutDb;
        // EQ already on the speaker was in the measurement (kept in baseEq): fit what is left; applying adds to it
        const auto f = RoomTuning::grid();
        const auto r = eq::fit (f, room.average (false), room.targetCurve(), o);
        room.suggested = r.bands;
        room.offsetDb = r.offsetDb;
        room.rmsBefore = r.rmsBefore;
        room.rmsAfter = r.rmsAfter;
        status = room.suggested.empty() ? juce::String ("Already within 1 dB of the target: no EQ needed.")
                                        : juce::String ((int) room.suggested.size()) + " bands: " + juce::String (r.rmsBefore, 1) + " dB RMS from the target before, "
                                              + juce::String (r.rmsAfter, 1) + " dB predicted after.";
        changed();
    }

    void applyEq()
    {
        if (room.suggested.empty() || room.eqSpeaker < 0 || room.eqSpeaker >= (int) proc.system.sources.size()) return;
        proc.system.sources[(size_t) room.eqSpeaker].proc.eq = room.combinedEq();
        proc.system.syncLinks();
        status = "EQ copied to " + proc.system.sources[(size_t) room.eqSpeaker].name + "'s settings. Enter it in the processor, then measure After EQ.";
        setMeasuringAfter (true);
        refresh();
        changed();
    }

    void clearSelected()
    {
        if (selectedPos.isEmpty()) return;
        room.meas.erase (std::remove_if (room.meas.begin(), room.meas.end(), [this] (const RoomMeasurement& m) { return m.position == selectedPos; }), room.meas.end());
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
            if (fromButton) status = "Every planned position is measured" + juce::String (measuringAfter ? " after EQ." : ".");
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

    // One mic: move it to the next position not measured yet (in this before / after round).
    void moveToNextPosition (bool fromButton)
    {
        if (settings.micCount.load() != 1 || proc.positions.isEmpty()) return;
        const int curIdx = proc.positions.indexOf (proc.mics.position[0]);
        for (int k = 1; k <= proc.positions.size(); ++k)
        {
            const auto& cand = proc.positions[(curIdx + k + proc.positions.size()) % proc.positions.size()];
            if (! room.hasAt (cand, measuringAfter))
            {
                proc.setMicPosition (0, cand);
                if (fromButton) status = "Move " + proc.mics.nameOf (0) + " to " + cand + ", then measure.";
                refresh();
                changed();
                return;
            }
        }
        if (fromButton) { status = "Every position has been measured."; repaint(); }
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
        phaseStart = juce::Time::getMillisecondCounterHiRes();
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
        if (proc.numOutputs() + 1 != outputBox.getNumItems() || (int) micBoxes.size() != settings.micCount.load()) refresh();
        if (phase == Phase::Idle) return;
        if (! settings.genOn.load()) { cancel ("Stopped: the generator was turned off."); return; }
        const double elapsed = juce::Time::getMillisecondCounterHiRes() - phaseStart;
        if (phase == Phase::Settling)
        {
            const auto done = proc.analyzer.samplesAnalysed();
            progress = 0.2f * (float) juce::jlimit (0.0, 1.0, 1.0 - ((double) settleUntil - (double) done) / (kSettleSeconds * rate()));
            if (done >= settleUntil)
            {
                delaySerial = proc.analyzer.readDelayReport().serial;
                settings.requestFindDelay = true;
                phase = Phase::FindingTime;
                phaseStart = juce::Time::getMillisecondCounterHiRes();
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
                phaseStart = juce::Time::getMillisecondCounterHiRes();
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
            if (r.ir.size() > 1024) rm.ac = acoustics::analyse (r.ir, rate());
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
            if (! measuringAfter)
            {
                room.suggested.clear();
                if (room.count (false) == (int) done.size() && room.eqSpeaker >= 0 && room.eqSpeaker < (int) proc.system.sources.size())
                    room.baseEq = proc.system.sources[(size_t) room.eqSpeaker].proc.eq;   // first "before" round: note the EQ in place
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

    juce::ToggleButton showPositions { "Positions" }, showBefore { "Before" }, showAfter { "After" }, showPredicted { "With suggested EQ" };
    juce::TextButton addPositionButton { "+ Position" }, clearButton { "Clear position" }, nextPosButton { "Next position" };
    juce::ComboBox outputBox, targetBox, speakerBox, bandsBox, boostBox;
    juce::TextButton phaseButtons[2], measureButton { "Measure" }, suggestButton { "Suggest EQ" }, applyButton { "Copy to the speaker settings" };
    juce::TextButton tunedButton { "Mark the room as tuned" };
    std::vector<std::unique_ptr<juce::ComboBox>> micBoxes;
    std::vector<juce::Rectangle<int>> micLabels;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
    juce::Rectangle<int> posCard, micCard, posList, micHint, step1, step2, step3, measureInfo, rangeInfo, bandsArea, checkInfo;

    juce::String selectedPos, status, movePrompt;
    juce::StringArray measuredAtStart;
    juce::Rectangle<int> promptArea;
    int playOutput = 0;
    bool measuringAfter = false, sawReset = false;
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    float progress = 0.0f;
    juce::uint64 delaySerial = 0, settleUntil = 0;
};
} // namespace la::ui
