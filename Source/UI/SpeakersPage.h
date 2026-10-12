// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Step 2: Speakers. Build the system (tops, subs, fills, delays), measure each source on its own with one mic
// (or import its response), overlay them with the predicted sum, and work out the crossover for any processor:
// frequency, filter type and slope, delay, polarity and level. Ends with a settings summary for the processor.
#include <functional>
#include <thread>
#include "Graph.h"
#include "NavRail.h"
#include "../PluginProcessor.h"

namespace la::ui
{
inline juce::Colour sourceColour (int i) { return traceColour (i); }

//==============================================================================
// Magnitude (top) and phase (bottom) of every shown source, and their predicted sum.
class SpeakerGraph : public juce::Component
{
public:
    explicit SpeakerGraph (const SpeakerSystem& s) : sys (s) { setMouseCursor (juce::MouseCursor::CrosshairCursor); }

    void setWithProcessing (bool b) { withProc = b; rebuild(); }
    void setXoMarker (double f) { xoFreq = f; repaint(); }
    void setLegendRight (int px) { legendRight = (float) px; }

    // Call whenever the system changes.
    void rebuild()
    {
        if (grid.empty()) grid = xo::analysisGrid();
        curves.clear();
        sum.assign (grid.size(), cplx (0.0, 0.0));
        int used = 0;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const auto& s = sys.sources[(size_t) i];
            if (! s.hasResponse() || ! s.show) continue;
            auto r = withProc ? sys.processedResponse (i, grid) : sys.rawResponse (i, grid);
            if (r.empty()) continue;
            Curve c;
            c.index = i;
            c.db.resize (grid.size());
            c.ph.resize (grid.size());
            for (size_t k = 0; k < grid.size(); ++k)
            {
                const double m = std::abs (r.h[k]);
                c.db[k] = m < 1e-5 ? NAN : (float) (20.0 * std::log10 (m));
                c.ph[k] = m < 1e-5 ? NAN : (float) (std::arg (r.h[k]) * 180.0 / kPi);
                sum[k] += r.h[k];
            }
            curves.push_back (std::move (c));
            ++used;
        }
        sumDb.assign (grid.size(), NAN);
        if (used > 1)
            for (size_t k = 0; k < grid.size(); ++k)
                if (std::abs (sum[k]) > 1e-5) sumDb[k] = (float) (20.0 * std::log10 (std::abs (sum[k])));

        // level: the top of the loudest curve, rounded up to 6 dB
        float top = -200.0f;
        for (const auto& c : curves) for (float v : c.db) if (std::isfinite (v)) top = juce::jmax (top, v);
        for (float v : sumDb) if (std::isfinite (v)) top = juce::jmax (top, v);
        topDb = top > -199.0f ? std::ceil ((top + 3.0f) / 6.0f) * 6.0f : 6.0f;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds().toFloat();
        drawPanel (g, r, "Speaker responses");
        auto plotAll = r.reduced (12.0f).withTrimmedTop (28.0f).withTrimmedLeft (34.0f).withTrimmedBottom (16.0f);
        auto magArea = plotAll.removeFromTop (plotAll.getHeight() * 0.68f);
        plotAll.removeFromTop (8.0f);
        auto phArea = plotAll;
        ax.area = magArea;

        g.setColour (p.bg.withAlpha (0.5f));
        g.fillRect (magArea);
        g.fillRect (phArea);
        drawFreqGrid (g, ax, magArea, {});
        FreqAxis phAx = ax;
        phAx.area = phArea;
        drawFreqGrid (g, phAx, phArea, phArea.withY (phArea.getBottom()).withHeight (16.0f));

        const float range = 48.0f;
        auto yDb = [&] (float db) { return magArea.getY() + (topDb - db) / range * magArea.getHeight(); };
        g.setFont (font (10.0f));
        for (float db = topDb; db >= topDb - range; db -= 6.0f)
        {
            const float y = yDb (db);
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) y, magArea.getX(), magArea.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (magArea.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
        }
        for (int ph : { 180, 90, 0, -90, -180 })
        {
            const float y = phArea.getY() + (180.0f - (float) ph) / 360.0f * phArea.getHeight();
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) y, phArea.getX(), phArea.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String (ph), juce::Rectangle<float> (phArea.getX() - 34.0f, y - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
        }

        if (xoFreq > 0.0)
        {
            const float x = ax.x (xoFreq);
            g.setColour (p.warn.withAlpha (0.55f));
            const float dashes[] = { 4.0f, 4.0f };
            juce::Path line, dashed;
            line.startNewSubPath (x, magArea.getY());
            line.lineTo (x, phArea.getBottom());
            juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
            g.fillPath (dashed);
            g.setFont (font (10.5f, true));
            g.drawText ("XO " + freqLabel (xoFreq), juce::Rectangle<float> (x + 4.0f, magArea.getY() + 2.0f, 70.0f, 14.0f), juce::Justification::centredLeft);
        }

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (magArea.toNearestInt());
            auto xAt = [&] (size_t k) { return ax.x (grid[k]); };
            for (const auto& c : curves)
                strokeTrace (g, tracePath (grid.size(), xAt, [&] (size_t k) { return yDb (c.db[k]); }, [] (size_t) { return true; }),
                             sourceColour (c.index), 1.8f);
            if (! sumDb.empty() && curves.size() > 1)
                strokeTrace (g, tracePath (grid.size(), xAt, [&] (size_t k) { return yDb (sumDb[k]); }, [] (size_t) { return true; }),
                             p.text, 2.6f);
        }
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (phArea.toNearestInt());
            auto xAt = [&] (size_t k) { return ax.x (grid[k]); };
            for (const auto& c : curves)
                strokeTrace (g, phasePath (c.ph, xAt, [] (size_t) { return true; }, phArea), sourceColour (c.index).withAlpha (0.9f), 1.4f);
        }

        // legend
        auto legend = juce::Rectangle<float> (r.getRight() - legendRight, r.getY() + 8.0f, 0.0f, 18.0f);
        g.setFont (font (11.0f, true));
        auto addKey = [&] (const juce::String& name, juce::Colour c)
        {
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 26.0f;
            legend.setX (legend.getX() - w);
            auto k = legend.withWidth (w);
            g.setColour (c);
            g.fillRoundedRectangle (k.removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, 3.0f), 1.5f);
            g.setColour (p.textDim);
            g.drawText (name, k.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
        };
        if (curves.size() > 1) addKey ("Predicted sum", p.text);
        for (auto it = curves.rbegin(); it != curves.rend(); ++it)
            addKey (sys.sources[(size_t) it->index].name, sourceColour (it->index));

        if (curves.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (13.5f));
            g.drawFittedText ("No responses yet. Pick a speaker on the left and press Measure, or import its response file.",
                              magArea.reduced (40.0f).toNearestInt(), juce::Justification::centred, 2);
        }

        if (hover.x > magArea.getX() && hover.x < magArea.getRight() && hover.y > magArea.getY() && hover.y < magArea.getBottom())
        {
            const double f = ax.freqAt (hover.x);
            const float db = topDb - (hover.y - magArea.getY()) / magArea.getHeight() * range;
            g.setColour (p.textDim.withAlpha (0.5f));
            g.drawVerticalLine ((int) hover.x, magArea.getY(), phArea.getBottom());
            drawReadout (g, magArea, hover, freqLabel (f) + " Hz   " + juce::String (db, 1) + " dB");
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hover = e.position; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = { -1.0f, -1.0f }; repaint(); }

private:
    struct Curve { int index; std::vector<float> db, ph; };
    const SpeakerSystem& sys;
    std::vector<double> grid;
    std::vector<Curve> curves;
    std::vector<cplx> sum;
    std::vector<float> sumDb;
    float topDb = 6.0f;
    bool withProc = true;
    double xoFreq = 0.0;
    float legendRight = 12.0f;
    FreqAxis ax;
    juce::Point<float> hover { -1.0f, -1.0f };
};

//==============================================================================
// The sources of the system, one row each: colour, name, role, output, state. Click to select.
class SourceList : public juce::Component
{
public:
    std::function<void (int)> onSelect;
    std::function<void (int)> onToggleShow;

    explicit SourceList (const SpeakerSystem& s) : sys (s) {}

    void setSelected (int i) { selected = i; repaint(); }
    static constexpr int kRowH = 36;

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const auto& s = sys.sources[(size_t) i];
            auto row = juce::Rectangle<int> (0, i * kRowH, getWidth(), kRowH - 4).toFloat();
            g.setColour (i == selected ? p.panelRaised : (i == hoverRow ? p.panelRaised.withAlpha (0.5f) : p.panel));
            g.fillRoundedRectangle (row, 6.0f);
            if (i == selected)
            {
                g.setColour (p.accent);
                g.drawRoundedRectangle (row.reduced (0.5f), 6.0f, 1.2f);
            }
            auto r = row.reduced (10.0f, 0.0f);
            // eye: show / hide on the graph
            auto eye = r.removeFromRight (20.0f).withSizeKeepingCentre (16.0f, 16.0f);
            g.setColour (s.show ? sourceColour (i) : p.textFaint);
            if (s.show) g.fillEllipse (eye.reduced (3.0f));
            g.drawEllipse (eye.reduced (1.0f), 1.3f);

            g.setColour (sourceColour (i));
            g.fillRoundedRectangle (r.removeFromLeft (4.0f).withSizeKeepingCentre (4.0f, 20.0f), 2.0f);
            r.removeFromLeft (10.0f);
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            g.drawText (s.name.isNotEmpty() ? s.name : "Speaker " + juce::String (i + 1), r.removeFromTop (row.getHeight() * 0.55f), juce::Justification::bottomLeft, true);
            g.setFont (font (10.5f));
            juce::String info = speakerRoleNames()[s.role] + "  -  " + speakerChannelNames()[s.channel] + "  -  "
                                + (s.output == 0 ? juce::String ("all outs") : "out " + juce::String (s.output));
            g.setColour (p.textDim);
            g.drawText (info, r, juce::Justification::topLeft, true);
            g.setColour (s.hasResponse() ? p.good : p.warn);
            g.setFont (font (10.5f, true));
            g.drawText (s.hasResponse() ? (s.imported ? "IMPORTED" : "MEASURED") : "TO MEASURE", r.withTrimmedRight (6.0f), juce::Justification::topRight, true);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = e.y / kRowH < (int) sys.sources.size() ? e.y / kRowH : -1;
        if (h != hoverRow) { hoverRow = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = e.y / kRowH;
        if (i < 0 || i >= (int) sys.sources.size()) return;
        if (e.x > getWidth() - 34) { if (onToggleShow) onToggleShow (i); return; }
        if (onSelect) onSelect (i);
    }

private:
    const SpeakerSystem& sys;
    int selected = 0, hoverRow = -1;
};

//==============================================================================
// The time alignments of the system, one row each: which speaker is lined up to which, where, and the delay.
class AlignmentList : public juce::Component
{
public:
    std::function<void (int)> onSelect;

    explicit AlignmentList (const SpeakerSystem& s) : sys (s) {}
    void setSelected (int i) { selected = i; repaint(); }
    static constexpr int kRowH = 38;

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        if (sys.alignments.empty())
        {
            g.setColour (p.textFaint);
            g.setFont (font (11.5f));
            g.drawText ("No alignments yet: add one below.", getLocalBounds(), juce::Justification::centredLeft, true);
            return;
        }
        for (int i = 0; i < (int) sys.alignments.size(); ++i)
        {
            const auto& a = sys.alignments[(size_t) i];
            auto row = juce::Rectangle<int> (0, i * kRowH, getWidth(), kRowH - 4).toFloat();
            g.setColour (i == selected ? p.panelRaised : (i == hoverRow ? p.panelRaised.withAlpha (0.5f) : p.panel));
            g.fillRoundedRectangle (row, 6.0f);
            if (i == selected) { g.setColour (p.accent); g.drawRoundedRectangle (row.reduced (0.5f), 6.0f, 1.2f); }
            auto r = row.reduced (10.0f, 2.0f);
            auto top = r.removeFromTop (r.getHeight() * 0.55f);
            g.setFont (font (12.5f, true));
            g.setColour (sourceColour (a.align));
            const auto alignName = name (a.align), refName = name (a.ref);
            g.drawText (alignName, top, juce::Justification::bottomLeft, true);
            const float w1 = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), alignName);
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92  ")), top.withTrimmedLeft (w1), juce::Justification::bottomLeft, true);
            g.setFont (font (12.5f, true));
            g.setColour (sourceColour (a.ref));
            g.drawText (refName, top.withTrimmedLeft (w1 + 28.0f), juce::Justification::bottomLeft, true);

            g.setFont (font (10.5f));
            g.setColour (p.textDim);
            g.drawText ("at " + (a.spot.isNotEmpty() ? a.spot : juce::String ("the mic position")), r, juce::Justification::topLeft, true);
            if (a.measured() && a.ref < (int) sys.sources.size())
            {
                const double need = a.neededMs (sys.sources[(size_t) a.ref].proc.delayMs);
                g.setFont (font (10.5f, true));
                g.setColour (need >= -0.005 ? p.good : p.warn);
                g.drawText (need >= -0.005 ? "delay " + juce::String (need, 2) + " ms" : "swap the pair", r.withTrimmedRight (4.0f), juce::Justification::topRight, true);
            }
            else
            {
                g.setFont (font (10.5f, true));
                g.setColour (p.warn);
                g.drawText ("TO MEASURE", r.withTrimmedRight (4.0f), juce::Justification::topRight, true);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = e.y / kRowH < (int) sys.alignments.size() ? e.y / kRowH : -1;
        if (h != hoverRow) { hoverRow = h; repaint(); }
        setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
    }
    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = e.y / kRowH;
        if (i >= 0 && i < (int) sys.alignments.size() && onSelect) onSelect (i);
    }

private:
    juce::String name (int i) const
    {
        if (i < 0 || i >= (int) sys.sources.size()) return "?";
        return sys.sources[(size_t) i].name.isNotEmpty() ? sys.sources[(size_t) i].name : "Speaker " + juce::String (i + 1);
    }
    const SpeakerSystem& sys;
    int selected = 0, hoverRow = -1;
};

//==============================================================================
class SpeakersPage : public juce::Component, private juce::Timer
{
public:
    std::function<void (int)> onImport;   // the editor owns the file chooser
    std::function<void()> onChanged;

    explicit SpeakersPage (LiveAlignProcessor& p)
        : proc (p), settings (p.settings), sys (p.system), graph (p.system), list (p.system), alList (p.system)
    {
        addAndMakeVisible (graph);
        addAndMakeVisible (list);
        list.onSelect = [this] (int i) { select (i); };
        list.onToggleShow = [this] (int i) { sys.sources[(size_t) i].show = ! sys.sources[(size_t) i].show; systemChanged (false); };

        addButton.onClick = [this]
        {
            if ((int) sys.sources.size() >= SpeakerSystem::kMaxSources) return;
            SpeakerSource s;
            s.name = "Speaker " + juce::String (sys.sources.size() + 1);
            s.role = (int) SpeakerRole::Fill;
            s.output = juce::jmin ((int) sys.sources.size() + 1, juce::jmax (1, proc.numOutputs()));
            sys.sources.push_back (s);
            select ((int) sys.sources.size() - 1);
            systemChanged (true);
        };
        addButton.setTooltip ("Add a speaker or group (up to 8): a fill, a delay, another sub ...");
        removeButton.onClick = [this] { removeSelected(); };
        addAndMakeVisible (addButton);
        addAndMakeVisible (removeButton);

        // ---- selected source ----
        nameEditor.setFont (font (13.0f));
        nameEditor.onTextChange = [this] { if (auto* s = current()) { s->name = nameEditor.getText().trim(); systemChanged (false); refreshChoices(); } };
        roleBox.addItemList (speakerRoleNames(), 1);
        roleBox.onChange = [this] { if (auto* s = current()) { s->role = roleBox.getSelectedItemIndex(); systemChanged (false); } };
        channelBox.addItemList (speakerChannelNames(), 1);
        channelBox.setTooltip ("Where this speaker sits: mono, stereo left, centre, stereo right, or both sides (L+R)");
        channelBox.onChange = [this] { if (auto* s = current()) { s->channel = channelBox.getSelectedItemIndex(); systemChanged (false); } };
        outputBox.onChange = [this] { if (auto* s = current()) { s->output = outputBox.getSelectedItemIndex(); systemChanged (false); } };
        outputBox.setTooltip ("The interface output that feeds this speaker or group while it is measured. "
                              "Or play all outputs and mute the others in your processor.");
        measureButton.onClick = [this] { if (phase == Phase::Idle) startMeasure (selected); else cancelMeasure ("Stopped."); };
        measureButton.setTooltip ("Plays pink noise from this speaker's output, finds its arrival and averages its response at the measurement mic");
        importButton.onClick = [this] { if (onImport) onImport (selected); };
        importButton.setTooltip ("Use a manufacturer or measured response: FRD, CSV or TXT (frequency, dB, optional phase)");
        clearButton.onClick = [this] { if (auto* s = current()) { s->response = {}; s->imported = false; s->hasArrival = false; s->source.clear(); systemChanged (true); } };
        offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 24);
        offsetSlider.setRange (-20.0, 50.0, 0.01);
        offsetSlider.setTextValueSuffix (" ms");
        offsetSlider.setTooltip ("An imported file has no arrival time: enter how much later it arrives than the measured speakers");
        offsetSlider.onValueChange = [this] { if (auto* s = current()) { s->offsetMs = offsetSlider.getValue(); systemChanged (true); } };
        for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &roleBox, &channelBox, &outputBox, &measureButton, &importButton, &clearButton, &offsetSlider })
            addAndMakeVisible (*c);

        // ---- measurement ----
        micBox.setTooltip ("The one mic used for every speaker measurement. Keep it in the same place for all of them.");
        micBox.onChange = [this]
        {
            const int m = juce::jmax (0, micBox.getSelectedItemIndex());
            if (m != sys.measureMic) { sys.measureMic = m; sys.timingLocked = false; }
            systemChanged (false);
        };
        relockButton.setTooltip ("Find the timing again on the next measurement (after moving the mic). Measure every speaker again afterwards.");
        relockButton.onClick = [this] { sys.timingLocked = false; status = "Timing will be found again on the next measurement."; systemChanged (false); };
        addAndMakeVisible (micBox);
        addAndMakeVisible (relockButton);

        processedToggle.setToggleState (true, juce::dontSendNotification);
        processedToggle.onClick = [this] { graph.setWithProcessing (processedToggle.getToggleState()); };
        addAndMakeVisible (processedToggle);

        // ---- crossover ----
        highBox.onChange = [this] { sys.xoHigh = highBox.getSelectedItemIndex(); xoChanged(); };
        lowBox.onChange = [this] { sys.xoLow = lowBox.getSelectedItemIndex(); xoChanged(); };
        highBox.setTooltip ("The speaker that plays above the crossover (the top)");
        lowBox.setTooltip ("The speaker that plays below the crossover (the sub)");
        rangeBox.addItemList ({ "Sub to top: 40 - 250 Hz", "Sub to top: 60 - 160 Hz", "Woofer to mid: 150 Hz - 1 kHz", "Mid to high: 800 Hz - 5 kHz" }, 1);
        rangeBox.setTooltip ("Where to look for the crossover frequency");
        rangeBox.onChange = [this]
        {
            static const double lo[] = { 40.0, 60.0, 150.0, 800.0 }, hi[] = { 250.0, 160.0, 1000.0, 5000.0 };
            const int i = juce::jlimit (0, 3, rangeBox.getSelectedItemIndex());
            sys.opt.fMin = lo[i];
            sys.opt.fMax = hi[i];
            freqSlider.setRange (lo[i] * 0.5, hi[i] * 1.5, 0.1);
            xoChanged();
        };
        for (auto* c : { &highBox, &lowBox, &rangeBox }) addAndMakeVisible (*c);

        for (int t = 0; t < 3; ++t)
        {
            auto& b = typeButtons[t];
            b.setButtonText (t == 0 ? "Butterworth" : t == 1 ? "Linkwitz-Riley" : "Bessel");
            b.setClickingTogglesState (true);
            b.onClick = [this, t] { sys.caps.types[t + 1] = typeButtons[t].getToggleState(); xoChanged(); };
            addAndMakeVisible (b);
        }
        for (int i = 0; i < kSlopeButtons; ++i)
        {
            auto& b = slopeButtons[i];
            b.setButtonText (juce::String (kSlopes[i]));
            b.setClickingTogglesState (true);
            b.onClick = [this, i] { sys.caps.slopes[kSlopes[i] / 6 - 1] = slopeButtons[i].getToggleState(); xoChanged(); };
            addAndMakeVisible (b);
        }
        findButton.onClick = [this] { findBest(); };
        findButton.setTooltip ("Tries every filter your processor offers, at every frequency in the range, with every delay and both polarities");
        addAndMakeVisible (findButton);

        // manual adjustment of the pair
        freqSlider.setRange (20.0, 400.0, 0.1);
        freqSlider.setSkewFactorFromMidPoint (100.0);
        freqSlider.setTextValueSuffix (" Hz");
        delaySlider.setRange (-20.0, 20.0, 0.01);
        delaySlider.setTextValueSuffix (" ms");
        delaySlider.setTooltip ("Delay of the low speaker relative to the high one (negative: the high one is delayed)");
        gainSlider.setRange (-18.0, 18.0, 0.1);
        gainSlider.setTextValueSuffix (" dB");
        gainSlider.setTooltip ("Level of the low speaker relative to its measurement");
        for (auto* s : { &freqSlider, &delaySlider, &gainSlider })
        {
            s->setSliderStyle (juce::Slider::LinearHorizontal);
            s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 24);
            s->onValueChange = [this] { manualChanged(); };
            addAndMakeVisible (*s);
        }
        typeBox.addItemList ({ "Butterworth", "Linkwitz-Riley", "Bessel" }, 1);
        typeBox.onChange = [this] { refreshSlopeBox(); manualChanged(); };
        slopeBox.onChange = [this] { manualChanged(); };
        invertButton.setClickingTogglesState (true);
        invertButton.setTooltip ("Invert the polarity of the low speaker");
        invertButton.onClick = [this] { manualChanged(); };
        for (auto* c : std::initializer_list<juce::Component*> { &typeBox, &slopeBox, &invertButton }) addAndMakeVisible (*c);

        // ---- right column tabs: crossover / time alignment ----
        for (int t = 0; t < 2; ++t)
        {
            tabButtons[t].setButtonText (t == 0 ? "Crossover" : "Time alignment");
            tabButtons[t].setRadioGroupId (4711);
            tabButtons[t].setClickingTogglesState (true);
            tabButtons[t].onClick = [this, t] { if (tabButtons[t].getToggleState()) setRightTab (t); };
            addAndMakeVisible (tabButtons[t]);
        }
        tabButtons[0].setToggleState (true, juce::dontSendNotification);

        // ---- time alignment ----
        alList.onSelect = [this] (int i) { selectAlignment (i); };
        addChildComponent (alList);
        alAddButton.onClick = [this] { addAlignment(); };
        alAddButton.setTooltip ("Line up a fill, a delay, or one side of a left / right pair with another speaker");
        alRemoveButton.onClick = [this]
        {
            if (alSelected < 0 || alSelected >= (int) sys.alignments.size() || phase != Phase::Idle) return;
            sys.alignments.erase (sys.alignments.begin() + alSelected);
            alSelected = juce::jmax (0, alSelected - 1);
            systemChanged (false);
        };
        alAlignBox.setTooltip ("The speaker that gets the delay (a fill, a delay, or one side of a left / right pair)");
        alRefBox.setTooltip ("The speaker it is lined up to (usually the main)");
        alAlignBox.onChange = [this] { if (auto* a = currentAlignment()) { a->align = alAlignBox.getSelectedItemIndex(); a->hasAlign = false; a->hasRef = false; systemChanged (false); } };
        alRefBox.onChange = [this] { if (auto* a = currentAlignment()) { a->ref = alRefBox.getSelectedItemIndex(); a->hasAlign = false; a->hasRef = false; systemChanged (false); } };
        alSpotBox.setEditableText (true);
        alSpotBox.setTextWhenNothingSelected ("Where they meet");
        alSpotBox.setTooltip ("Where both speakers cover the seats: front rows for a front fill, under the balcony for a delay, "
                              "the centre line for left / right. Type a new name to add a position.");
        alSpotBox.onChange = [this]
        {
            auto* a = currentAlignment();
            if (a == nullptr) return;
            const auto t = alSpotBox.getText().trim();
            if (t == a->spot) return;
            a->spot = t;
            a->hasRef = a->hasAlign = false;
            if (t.isNotEmpty() && ! proc.positions.contains (t)) proc.positions.add (t);
            systemChanged (false);
        };
        alExtraSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        alExtraSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 76, 24);
        alExtraSlider.setRange (0.0, 20.0, 0.1);
        alExtraSlider.setTextValueSuffix (" ms");
        alExtraSlider.setTooltip ("Extra delay on top of the alignment so the sound still seems to come from the main "
                                  "(precedence effect). 0 for left / right; often 5 to 10 ms for delay speakers.");
        alExtraSlider.onValueChange = [this] { if (auto* a = currentAlignment()) { a->precedenceMs = alExtraSlider.getValue(); refreshApply(); changedOutside(); alList.repaint(); } };
        alMeasureButton.onClick = [this] { if (phase == Phase::Idle) startArrival (alSelected); else cancelMeasure ("Stopped."); };
        alMeasureButton.setTooltip ("Plays pink noise from each of the two speakers in turn and finds when each arrives at the mic");
        alApplyButton.onClick = [this] { applyAlignment(); };
        for (auto* c : std::initializer_list<juce::Component*> { &alAddButton, &alRemoveButton, &alAlignBox, &alRefBox, &alSpotBox, &alExtraSlider,
                                                                 &alMeasureButton, &alApplyButton })
            addChildComponent (*c);

        copyButton.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (summaryText (false)); status = "Settings copied to the clipboard."; repaint(); };
        copyButton.setTooltip ("Copy the settings as text, to paste in your notes or a show file");
        doneButton.onClick = [this]
        {
            proc.speakers.configured = true;
            proc.speakers.configuredOn = LiveAlignProcessor::today();
            proc.speakers.changed = false;
            status = "Speakers marked as set up. Enter these settings in your processor, then check the sum on Room tuning.";
            changedOutside();
        };
        doneButton.setTooltip ("The settings are in your processor: this venue's speakers are set up until something changes");
        addAndMakeVisible (copyButton);
        addAndMakeVisible (doneButton);

        lookAndFeelChanged();
        refreshChoices();
        startTimerHz (15);
    }

    ~SpeakersPage() override { stopTimer(); alive->store (false); }

    void setSnapshot (const Snapshot& s) { snap = &s; }

    void lookAndFeelChanged() override
    {
        measureButton.setColour (juce::TextButton::buttonColourId, pal().accent);
        measureButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
        findButton.setColour (juce::TextButton::buttonColourId, pal().accent);
        findButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
        nameEditor.applyColourToAllText (pal().text);
        refreshMeasureButton();
        graph.rebuild();
    }

    void refreshMeasureButton()
    {
        const bool busyNow = phase != Phase::Idle;
        const bool resp = busyNow && ! arrivalMode, arr = busyNow && arrivalMode;
        measureButton.setButtonText (resp ? "Stop" : "Measure");
        measureButton.setColour (juce::TextButton::buttonColourId, resp ? pal().bad : pal().accent);
        measureButton.setEnabled (! arr && (proc.isStandalone() || proc.uiAsStandalone));
        alMeasureButton.setButtonText (arr ? "Stop" : "Measure both");
        alMeasureButton.setColour (juce::TextButton::buttonColourId, arr ? pal().bad : pal().accent);
        alMeasureButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
        alMeasureButton.setEnabled (! resp && currentAlignment() != nullptr && (proc.isStandalone() || proc.uiAsStandalone));
    }

    // After a venue is opened, or mics or outputs changed.
    void refreshChoices()
    {
        selected = juce::jlimit (0, juce::jmax (0, (int) sys.sources.size() - 1), selected);
        list.setSelected (selected);

        micBox.clear (juce::dontSendNotification);
        for (int m = 0; m < settings.micCount.load(); ++m)
        {
            juce::String label = proc.mics.nameOf (m);
            if (proc.mics.position[(size_t) m].isNotEmpty()) label << "  -  " << proc.mics.position[(size_t) m];
            micBox.addItem (label, m + 1);
        }
        sys.measureMic = juce::jlimit (0, settings.micCount.load() - 1, sys.measureMic);
        micBox.setSelectedItemIndex (sys.measureMic, juce::dontSendNotification);

        const int outs = juce::jmax (1, proc.numOutputs());
        outputBox.clear (juce::dontSendNotification);
        outputBox.addItem ("All outputs", 1);
        for (int i = 0; i < outs; ++i) outputBox.addItem ("Output " + juce::String (i + 1), i + 2);
        nOuts = outs;

        for (auto* b : { &highBox, &lowBox })
        {
            b->clear (juce::dontSendNotification);
            for (int i = 0; i < (int) sys.sources.size(); ++i) b->addItem (nameOf (i), i + 1);
        }
        highBox.setSelectedItemIndex (sys.xoHigh, juce::dontSendNotification);
        refreshAlignment();
        lowBox.setSelectedItemIndex (sys.xoLow, juce::dontSendNotification);
        static const double lo[] = { 40.0, 60.0, 150.0, 800.0 }, hi[] = { 250.0, 160.0, 1000.0, 5000.0 };
        int ri = 0;
        for (int i = 0; i < 4; ++i) if (std::abs (lo[i] - sys.opt.fMin) < 0.5 && std::abs (hi[i] - sys.opt.fMax) < 0.5) ri = i;
        rangeBox.setSelectedItemIndex (ri, juce::dontSendNotification);
        freqSlider.setRange (lo[ri] * 0.5, hi[ri] * 1.5, 0.1);
        for (int t = 0; t < 3; ++t) typeButtons[t].setToggleState (sys.caps.types[t + 1], juce::dontSendNotification);
        for (int i = 0; i < kSlopeButtons; ++i) slopeButtons[i].setToggleState (sys.caps.slopes[kSlopes[i] / 6 - 1], juce::dontSendNotification);

        refreshSelected();
        refreshManual();
        graph.rebuild();
        resized();
        repaint();
    }

    void sourceImported() { systemChanged (true); }

    // Same as the buttons (used by the local screenshot tool).
    void measure (int i) { select (i); startMeasure (i); }
    void findCrossover() { findBest(); }
    bool busy() const { return phase != Phase::Idle || searching; }
    void newAlignment() { addAlignment(); }
    void measureAlignment (int i) { selectAlignment (i); setRightTab (1); startArrival (i); }
    void applyAlignmentNow() { applyAlignment(); }
    juce::String statusText() const { return status; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, systemCard.toFloat(), "Speakers");
        drawPanel (g, selCard.toFloat(), "Selected speaker", current() != nullptr ? sourceColour (selected) : juce::Colour());
        drawPanel (g, measureCard.toFloat(), "Measurement");
        drawPanel (g, xoCard.toFloat(), {});
        drawPanel (g, summaryCard.toFloat(), "Settings for your processor");

        auto label = [&] (juce::Rectangle<int> r, const juce::String& t)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            g.drawText (t.toUpperCase(), r, juce::Justification::centredLeft, true);
        };
        for (int i = 0; i < (int) labels.size(); ++i)
        {
            if (rightTab == 0 && i >= alLabelFrom && i < alLabelTo) continue;
            if (rightTab == 1 && i >= xoLabelFrom && i < xoLabelTo) continue;
            label (labels[(size_t) i].first, labels[(size_t) i].second);
        }

        // selected speaker info
        if (const auto* s = current())
        {
            g.setColour (s->hasResponse() ? p.textDim : p.warn);
            g.setFont (font (11.5f));
            juce::String t;
            if (s->hasResponse()) t << (s->imported ? "Imported: " : "Measured: ") << s->source;
            else t << "Not measured yet.";
            if (s->hasArrival) t << "\nArrives " << juce::String (s->arrivalMs, 2) << " ms after the reference";
            g.drawFittedText (t, selInfo, juce::Justification::topLeft, 3);
        }

        // measurement status
        {
            juce::String t;
            juce::Colour c = p.textDim;
            if (sys.timingLocked) t << "Timing locked at " << juce::String (sys.lockedDelayMs, 2) << " ms for " << proc.mics.nameOf (sys.measureMic) << ".";
            else { t << "Timing is found on the first measurement, then locked so every speaker keeps its true arrival."; }
            if (phase != Phase::Idle) { t = phaseText(); c = p.accent; }
            g.setColour (c);
            g.setFont (font (11.5f, phase != Phase::Idle));
            g.drawFittedText (t, measureInfo, juce::Justification::topLeft, 3);
            if (phase != Phase::Idle)
            {
                auto bar = measureInfo.removeFromBottom (5).toFloat();
                g.setColour (p.border);
                g.fillRoundedRectangle (bar, 2.5f);
                g.setColour (p.accent);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 2.5f);
            }
        }

        if (rightTab == 1) paintAlignment (g);

        // crossover result
        if (rightTab == 0)
        {
            g.setFont (font (11.5f));
            juce::String t;
            juce::Colour c = p.textDim;
            if (searching) { t = "Searching every filter, frequency, delay and polarity..."; c = p.accent; }
            else if (! sys.pairValid()) t = "Pick two different speakers.";
            else if (! sys.sources[(size_t) sys.xoHigh].hasResponse() || ! sys.sources[(size_t) sys.xoLow].hasResponse())
            { t = "Measure or import both speakers first."; c = p.warn; }
            else if (sys.lastResult.valid)
            {
                const auto& r = sys.lastResult;
                t << "Best: " << xo::typeName (r.filter.type) << " " << r.filter.slope << " dB/oct at " << freqLabel (r.filter.freq) << " Hz, "
                  << "low " << (r.delayLowMs >= 0 ? "delayed " : "early ") << juce::String (std::abs (r.delayLowMs), 2) << " ms"
                  << (r.invertLow ? ", low inverted" : "") << ".\n"
                  << "Overlap " << freqLabel (r.highUsableFrom) << " - " << freqLabel (r.lowUsableTo) << " Hz.";
                if (! r.note.empty()) t << " " << juce::String (r.note);
                c = p.text;
            }
            else if (! sys.lastResult.note.empty()) { t = juce::String (sys.lastResult.note); c = p.warn; }
            else t = "Pick the filters your processor offers, then find the best crossover, or set it by hand below.";
            g.setColour (c);
            g.drawFittedText (t, xoInfo, juce::Justification::topLeft, 3);

            if (liveValid)
            {
                const auto q = juce::jlimit (0.0, 1.0, liveEff);
                const juce::Colour qc = q > 0.9 ? p.good : q > 0.75 ? p.warn : p.bad;
                g.setColour (qc);
                g.setFont (font (12.0f, true));
                g.drawText ("Sum through the crossover: " + juce::String (juce::roundToInt (q * 100.0)) + "% in phase, "
                                + juce::String (liveDev, 1) + " dB from flat",
                            liveLine, juce::Justification::centredLeft, true);
            }
        }

        // processor settings summary
        {
            auto r = summaryArea;
            g.setFont (monoFont (11.5f));
            juce::StringArray lines;
            lines.addLines (summaryText (true));
            for (const auto& l : lines)
            {
                if (r.getHeight() < 15) break;
                const bool head = l.startsWith ("#");
                g.setColour (head ? p.textDim : p.text);
                g.drawText (head ? l.substring (1) : l, r.removeFromTop (17), juce::Justification::centredLeft, true);
            }
        }
        if (status.isNotEmpty())
        {
            g.setColour (p.accent);
            g.setFont (font (11.5f, true));
            g.drawText (status, statusLine, juce::Justification::centredLeft, true);
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

        // ---- left: the system and the selected speaker ----
        const int listH = juce::jmax (1, (int) sys.sources.size()) * SourceList::kRowH;
        systemCard = left.removeFromTop (34 + listH + 46);
        left.removeFromTop (10);
        {
            auto c = systemCard.reduced (12, 0).withTrimmedTop (34);
            list.setBounds (c.removeFromTop (listH));
            auto row = c.removeFromTop (44).withSizeKeepingCentre (c.getWidth(), 30);
            addButton.setBounds (row.removeFromLeft (130));
            row.removeFromLeft (8);
            removeButton.setBounds (row.removeFromLeft (100));
        }
        selCard = left;
        {
            auto c = selCard.reduced (12, 0).withTrimmedTop (36);
            auto field = [&] (const juce::String& name, juce::Component& comp, int h = 28)
            {
                auto row = c.removeFromTop (h);
                labels.push_back ({ row.removeFromLeft (64), name });
                comp.setBounds (row);
                c.removeFromTop (8);
            };
            field ("Name", nameEditor);
            field ("Role", roleBox);
            field ("Channel", channelBox);
            field ("Output", outputBox);
            c.removeFromTop (4);
            measureButton.setBounds (c.removeFromTop (34));
            c.removeFromTop (8);
            auto row = c.removeFromTop (28);
            importButton.setBounds (row.removeFromLeft (row.getWidth() * 62 / 100));
            row.removeFromLeft (8);
            clearButton.setBounds (row);
            c.removeFromTop (10);
            const auto* s = current();
            offsetSlider.setVisible (s != nullptr && s->imported);
            if (offsetSlider.isVisible()) field ("Arrival", offsetSlider);
            selInfo = c.removeFromTop (54);
        }

        // ---- right: measurement and crossover ----
        measureCard = right.removeFromTop (128);
        right.removeFromTop (10);
        {
            auto c = measureCard.reduced (12, 0).withTrimmedTop (36);
            auto row = c.removeFromTop (28);
            labels.push_back ({ row.removeFromLeft (44), "Mic" });
            relockButton.setBounds (row.removeFromRight (82));
            row.removeFromRight (8);
            micBox.setBounds (row);
            c.removeFromTop (10);
            measureInfo = c.removeFromTop (46);
        }
        xoCard = right;
        {
            auto tabs = xoCard.reduced (10, 0).withTrimmedTop (8).removeFromTop (30);
            const int tw = (tabs.getWidth() - 6) / 2;
            tabButtons[0].setBounds (tabs.removeFromLeft (tw));
            tabs.removeFromLeft (6);
            tabButtons[1].setBounds (tabs);
            alLabelFrom = (int) labels.size();
            layoutAlignment (xoCard.reduced (12, 0).withTrimmedTop (48));
            alLabelTo = xoLabelFrom = (int) labels.size();
        }
        {
            auto c = xoCard.reduced (12, 0).withTrimmedTop (48);
            auto pair = c.removeFromTop (28);
            labels.push_back ({ pair.removeFromLeft (40), "High" });
            highBox.setBounds (pair.removeFromLeft (pair.getWidth() / 2 - 26));
            pair.removeFromLeft (8);
            labels.push_back ({ pair.removeFromLeft (36), "Low" });
            lowBox.setBounds (pair);
            c.removeFromTop (8);
            auto rr = c.removeFromTop (28);
            labels.push_back ({ rr.removeFromLeft (52), "Range" });
            rangeBox.setBounds (rr);
            c.removeFromTop (10);
            labels.push_back ({ c.removeFromTop (16), "Filters your processor has" });
            c.removeFromTop (4);
            auto tr = c.removeFromTop (26);
            const int tyw = (tr.getWidth() - 12) / 3;
            for (auto& b : typeButtons) { b.setBounds (tr.removeFromLeft (tyw)); tr.removeFromLeft (6); }
            c.removeFromTop (6);
            auto sr = c.removeFromTop (26);
            labels.push_back ({ sr.removeFromLeft (58), "dB/oct" });
            const int sw = (sr.getWidth() - 5 * (kSlopeButtons - 1)) / kSlopeButtons;
            for (auto& b : slopeButtons) { b.setBounds (sr.removeFromLeft (sw)); sr.removeFromLeft (5); }
            c.removeFromTop (10);
            findButton.setBounds (c.removeFromTop (34));
            c.removeFromTop (8);
            xoInfo = c.removeFromTop (48);
            c.removeFromTop (4);
            labels.push_back ({ c.removeFromTop (16), "Adjust by hand" });
            c.removeFromTop (6);
            auto put = [&] (const juce::String& name, juce::Component& comp)
            {
                auto row = c.removeFromTop (26);
                labels.push_back ({ row.removeFromLeft (64), name });
                comp.setBounds (row);
                c.removeFromTop (6);
            };
            put ("Freq", freqSlider);
            {
                auto row = c.removeFromTop (26);
                labels.push_back ({ row.removeFromLeft (64), "Filter" });
                typeBox.setBounds (row.removeFromLeft (row.getWidth() * 60 / 100));
                row.removeFromLeft (6);
                slopeBox.setBounds (row);
                c.removeFromTop (6);
            }
            put ("Low dly", delaySlider);
            put ("Low gain", gainSlider);
            {
                auto row = c.removeFromTop (26);
                labels.push_back ({ row.removeFromLeft (64), "Polarity" });
                invertButton.setBounds (row.removeFromLeft (130));
                c.removeFromTop (6);
            }
            liveLine = c.removeFromTop (22);
            xoLabelTo = (int) labels.size();
        }
        setRightTabVisibility();

        // ---- middle: graph and the settings summary ----
        const int lines = (int) sys.sources.size() + 1 + arrivalLines();
        summaryCard = r.removeFromBottom (juce::jlimit (150, r.getHeight() / 3 + 20, 34 + lines * 17 + 12 + 44));
        r.removeFromBottom (10);
        graph.setBounds (r);
        processedToggle.setBounds (juce::Rectangle<int> (r.getRight() - 12 - 150, r.getY() + 5, 150, 24));
        graph.setLegendRight (162);
        {
            auto c = summaryCard.reduced (14, 0).withTrimmedTop (34);
            auto buttons = c.removeFromBottom (44).withSizeKeepingCentre (c.getWidth(), 30);
            doneButton.setBounds (buttons.removeFromRight (190));
            buttons.removeFromRight (8);
            copyButton.setBounds (buttons.removeFromRight (130));
            buttons.removeFromRight (12);
            statusLine = buttons;
            summaryArea = c;
        }
    }

private:
    enum class Phase { Idle, Settling, FindingTime, Averaging };
    static constexpr int kSlopeButtons = 6;
    static constexpr double kSettleSeconds = 3.0;
    static constexpr int kSlopes[kSlopeButtons] = { 6, 12, 18, 24, 36, 48 };

    SpeakerSource* current() { return selected >= 0 && selected < (int) sys.sources.size() ? &sys.sources[(size_t) selected] : nullptr; }
    const SpeakerSource* current() const { return selected >= 0 && selected < (int) sys.sources.size() ? &sys.sources[(size_t) selected] : nullptr; }

    juce::String nameOf (int i) const
    {
        if (i < 0 || i >= (int) sys.sources.size()) return {};
        const auto& n = sys.sources[(size_t) i].name;
        return n.isNotEmpty() ? n : "Speaker " + juce::String (i + 1);
    }

    void select (int i)
    {
        selected = juce::jlimit (0, juce::jmax (0, (int) sys.sources.size() - 1), i);
        list.setSelected (selected);
        refreshSelected();
        resized();
        repaint();
    }

    void refreshSelected()
    {
        const auto* s = current();
        for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &roleBox, &channelBox, &outputBox, &measureButton, &importButton, &clearButton })
            c->setEnabled (s != nullptr);
        removeButton.setEnabled (sys.sources.size() > 1);
        addButton.setEnabled ((int) sys.sources.size() < SpeakerSystem::kMaxSources);
        if (s == nullptr) return;
        if (! nameEditor.hasKeyboardFocus (true)) nameEditor.setText (s->name, juce::dontSendNotification);
        roleBox.setSelectedItemIndex (s->role, juce::dontSendNotification);
        channelBox.setSelectedItemIndex (s->channel, juce::dontSendNotification);
        outputBox.setSelectedItemIndex (juce::jlimit (0, nOuts, s->output), juce::dontSendNotification);
        offsetSlider.setValue (s->offsetMs, juce::dontSendNotification);
        clearButton.setEnabled (s->hasResponse());
        refreshMeasureButton();
    }

    void removeSelected()
    {
        if (sys.sources.size() <= 1 || phase != Phase::Idle) return;
        const int i = selected;
        // after the click: the selection editor is rebuilt
        juce::Component::SafePointer<SpeakersPage> safe (this);
        juce::MessageManager::callAsync ([safe, i]
        {
            if (safe == nullptr || i >= (int) safe->sys.sources.size()) return;
            auto& system = safe->sys;
            system.sources.erase (system.sources.begin() + i);
            system.sourceRemoved (i);
            auto fix = [i] (int& k) { if (k == i) k = 0; else if (k > i) --k; };
            fix (system.xoHigh);
            fix (system.xoLow);
            if (system.xoHigh == system.xoLow && system.sources.size() > 1) system.xoLow = system.xoHigh == 0 ? 1 : 0;
            safe->selected = juce::jmax (0, i - 1);
            safe->systemChanged (true);
        });
    }

    // Something about the sources changed: rebuild what depends on them.
    void systemChanged (bool responses)
    {
        if (responses) { sys.lastResult = {}; }
        refreshChoices();
        changedOutside();
    }

    void changedOutside()
    {
        refreshApply();
        alList.repaint();
        if (onChanged) onChanged();
        repaint();
    }

    void xoChanged()
    {
        sys.lastResult = {};
        refreshManual();
        graph.rebuild();
        changedOutside();
    }

    // ---- manual controls: they show and edit the pair's processing ----
    void refreshSlopeBox()
    {
        const int type = typeBox.getSelectedItemIndex() + 1;
        const int keep = slopeBox.getSelectedId();
        slopeBox.clear (juce::dontSendNotification);
        for (int s = 6; s <= 48; s += 6)
            if (xo::slopeValid (type, s)) slopeBox.addItem (juce::String (s) + " dB/oct", s);
        if (keep > 0 && xo::slopeValid (type, keep)) slopeBox.setSelectedId (keep, juce::dontSendNotification);
        else slopeBox.setSelectedItemIndex (juce::jmin (slopeBox.getNumItems() - 1, type == xo::LinkwitzRiley ? 1 : 3), juce::dontSendNotification);
    }

    void refreshManual()
    {
        const bool ok = sys.pairValid();
        for (auto* c : std::initializer_list<juce::Component*> { &freqSlider, &typeBox, &slopeBox, &delaySlider, &gainSlider, &invertButton })
            c->setEnabled (ok);
        liveValid = false;
        if (! ok) { graph.setXoMarker (0.0); return; }
        const auto& hi = sys.sources[(size_t) sys.xoHigh].proc;
        const auto& lo = sys.sources[(size_t) sys.xoLow].proc;
        const auto& f = hi.hp.active() ? hi.hp : lo.lp;
        typeBox.setSelectedItemIndex (juce::jlimit (0, 2, (f.active() ? f.type : (int) xo::LinkwitzRiley) - 1), juce::dontSendNotification);
        refreshSlopeBox();
        if (f.active()) slopeBox.setSelectedId (f.slope, juce::dontSendNotification);
        freqSlider.setValue (f.active() ? f.freq : std::sqrt (sys.opt.fMin * sys.opt.fMax), juce::dontSendNotification);
        delaySlider.setValue (lo.delayMs - hi.delayMs, juce::dontSendNotification);
        gainSlider.setValue (lo.gainDb, juce::dontSendNotification);
        invertButton.setToggleState (lo.invert != hi.invert, juce::dontSendNotification);
        invertButton.setButtonText (invertButton.getToggleState() ? "Low inverted" : "Low normal");
        graph.setXoMarker (f.active() ? f.freq : 0.0);
        updateLive();
    }

    void manualChanged()
    {
        if (! sys.pairValid()) return;
        auto& hi = sys.sources[(size_t) sys.xoHigh].proc;
        auto& lo = sys.sources[(size_t) sys.xoLow].proc;
        xo::Filter f;
        f.type = typeBox.getSelectedItemIndex() + 1;
        f.slope = juce::jmax (6, slopeBox.getSelectedId());
        f.freq = freqSlider.getValue();
        hi.hp = f;
        lo.lp = f;
        const double d = delaySlider.getValue();
        hi.delayMs = d < 0.0 ? -d : 0.0;
        lo.delayMs = d > 0.0 ? d : 0.0;
        lo.gainDb = gainSlider.getValue();
        hi.invert = false;
        lo.invert = invertButton.getToggleState();
        invertButton.setButtonText (lo.invert ? "Low inverted" : "Low normal");
        graph.setXoMarker (f.freq);
        graph.rebuild();
        updateLive();
        changedOutside();
    }

    void updateLive()
    {
        liveValid = false;
        if (! sys.pairValid()) return;
        const auto grid = xo::analysisGrid();
        const auto h = sys.rawResponse (sys.xoHigh, grid), l = sys.rawResponse (sys.xoLow, grid);
        if (h.empty() || l.empty()) return;
        const auto& ph = sys.sources[(size_t) sys.xoHigh].proc;
        const auto& pl = sys.sources[(size_t) sys.xoLow].proc;
        if (! ph.hp.active()) return;
        double eff = 0.0;
        const double cost = xo::assess (h, l, ph, pl, ph.hp.freq, eff);
        liveEff = eff;
        liveDev = juce::jmax (0.0, cost - 1.5 * (1.0 - eff));
        liveValid = true;
        repaint();
    }

    // ---- automatic crossover search (on a worker thread) ----
    void findBest()
    {
        if (searching || ! sys.pairValid()) return;
        const auto grid = xo::analysisGrid();
        auto h = sys.rawResponse (sys.xoHigh, grid), l = sys.rawResponse (sys.xoLow, grid);
        if (h.empty() || l.empty()) { repaint(); return; }
        searching = true;
        findButton.setEnabled (false);
        repaint();
        const auto caps = sys.caps;
        const auto opt = sys.opt;
        juce::Component::SafePointer<SpeakersPage> safe (this);
        auto flag = alive;
        std::thread ([safe, flag, h = std::move (h), l = std::move (l), caps, opt]
        {
            auto result = xo::optimise (h, l, caps, opt);
            if (! flag->load()) return;
            juce::MessageManager::callAsync ([safe, result] { if (safe != nullptr) safe->applyResult (result); });
        }).detach();
    }

    void applyResult (const xo::Result& r)
    {
        searching = false;
        findButton.setEnabled (true);
        sys.lastResult = r;
        if (r.valid && sys.pairValid())
        {
            auto& hi = sys.sources[(size_t) sys.xoHigh].proc;
            auto& lo = sys.sources[(size_t) sys.xoLow].proc;
            hi.hp = r.filter;
            lo.lp = r.filter;
            hi.delayMs = r.delayLowMs < 0.0 ? -r.delayLowMs : 0.0;
            lo.delayMs = r.delayLowMs > 0.0 ? r.delayLowMs : 0.0;
            hi.invert = false;
            lo.invert = r.invertLow;
            hi.gainDb = 0.0;
            lo.gainDb = r.gainLowDb;
        }
        refreshManual();
        graph.rebuild();
        changedOutside();
    }

    // ---- measuring one source ----
    void startMeasure (int i)
    {
        if (i < 0 || i >= (int) sys.sources.size()) return;
        if (settings.refSource.load() == 0)
        {
            status = "Set a reference on Mic setup (the loopback, or the generator) to measure speakers.";
            repaint();
            return;
        }
        measuring = i;
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        settings.genOutput = sys.sources[(size_t) i].output;
        settings.genType = 0;   // pink noise
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

    void cancelMeasure (const juce::String& why)
    {
        settings.genOn = false;
        if (phase == Phase::FindingTime || phase == Phase::Averaging)
            for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        phase = Phase::Idle;
        measuring = -1;
        arrivalMode = false;
        arrivalQueue.clear();
        status = why;
        refreshMeasureButton();
        repaint();
    }

    double rate() const
    {
        if (snap != nullptr && snap->sampleRate > 1000.0) return snap->sampleRate;
        return proc.getSampleRate() > 1000.0 ? proc.getSampleRate() : 48000.0;
    }

    juce::String phaseText() const
    {
        const auto who = nameOf (measuring);
        switch (phase)
        {
            case Phase::Settling: return arrivalMode ? "Playing pink noise through " + who + " (arrival at " + spotName() + ")..."
                                                     : "Playing pink noise through " + who + "...";
            case Phase::FindingTime: return arrivalMode ? "Finding when " + who + " arrives at " + spotName() + "..." : sys.timingLocked ? "Finding the arrival of " + who + "..." : "Finding the timing (first measurement)...";
            case Phase::Averaging: return "Averaging " + who + " at " + proc.mics.nameOf (sys.measureMic) + "...";
            case Phase::Idle:
            default: return {};
        }
    }

    void timerCallback() override
    {
        if (proc.numOutputs() != nOuts) refreshChoices();
        if (phase == Phase::Idle) return;
        const double elapsed = juce::Time::getMillisecondCounterHiRes() - phaseStart;
        const int mic = juce::jlimit (0, settings.micCount.load() - 1, sys.measureMic);

        if (! settings.genOn.load()) { cancelMeasure ("Stopped: the generator was turned off."); return; }

        if (phase == Phase::Settling)
        {
            // wait until the analysis has heard only this speaker for its whole timing window (it can be behind real time)
            const auto done = proc.analyzer.samplesAnalysed();
            const double total = kSettleSeconds * rate();
            progress = 0.15f * (float) juce::jlimit (0.0, 1.0, 1.0 - ((double) settleUntil - (double) done) / total);
            if (done >= settleUntil)
            {
                if (arrivalMode && snap != nullptr && mic < (int) snap->mics.size())
                    if (auto* a = alignmentAt (arrivalAlignment))
                        (arrivalWhich == 0 ? a->refLevelDb : a->alignLevelDb) = snap->mics[(size_t) mic].rawZSlowDbfs;
                delaySerial = proc.analyzer.readDelayReport().serial;
                settings.requestFindDelay = true;
                phase = Phase::FindingTime;
                phaseStart = juce::Time::getMillisecondCounterHiRes();
            }
        }
        else if (phase == Phase::FindingTime)
        {
            progress = 0.15f + (float) juce::jmin (1.0, elapsed / 2000.0) * 0.1f;
            const auto rep = proc.analyzer.readDelayReport();
            if (rep.serial != delaySerial && arrivalMode)
            {
                for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
                const bool valid = mic < rep.micCount && rep.valid[(size_t) mic];
                auto* a = alignmentAt (arrivalAlignment);
                if (a == nullptr) { cancelMeasure ("The alignment was removed."); return; }
                if (! valid)
                {
                    cancelMeasure ("No clear arrival of " + nameOf (measuring) + " at " + proc.mics.nameOf (mic)
                                   + ". Check that it is playing and the mic hears it, then measure again.");
                    return;
                }
                if (arrivalWhich == 0) { a->hasRef = true; a->refArrivalMs = rep.ms[(size_t) mic]; }
                else { a->hasAlign = true; a->alignArrivalMs = rep.ms[(size_t) mic]; }
                if (! arrivalQueue.empty()) nextArrival();
                else finishArrival();
                return;
            }
            if (rep.serial != delaySerial)
            {
                auto& s = sys.sources[(size_t) measuring];
                const bool valid = mic < rep.micCount && rep.valid[(size_t) mic];
                if (valid) { s.hasArrival = true; s.arrivalMs = rep.ms[(size_t) mic]; }
                else s.hasArrival = false;
                if (! sys.timingLocked)
                {
                    if (! valid)
                    {
                        for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
                        cancelMeasure ("No clear arrival at " + proc.mics.nameOf (mic) + ". Check that " + nameOf (measuring)
                                       + " is playing and the mic hears it, then measure again.");
                        return;
                    }
                    sys.timingLocked = true;
                    sys.lockedDelayMs = rep.ms[(size_t) mic];
                }
                // keep the other mics' delays; the measurement mic uses the locked delay
                for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
                settings.micDelayMs[mic] = (float) sys.lockedDelayMs;
                settings.requestResetAverages = true;
                sawReset = false;
                phase = Phase::Averaging;
                phaseStart = juce::Time::getMillisecondCounterHiRes();
            }
            else if (elapsed > 10000.0)
            {
                cancelMeasure ("The timing search did not answer. Check the audio device and try again.");
                return;
            }
        }
        else if (phase == Phase::Averaging)
        {
            const int want = juce::jmax (4, settings.averages.load());
            const int frames = snap != nullptr && mic < (int) snap->mics.size() ? snap->mics[(size_t) mic].tfFrames : 0;
            // only frames averaged after the restart count (the analysis can be behind real time)
            if (! sawReset && frames < want) sawReset = true;
            progress = 0.25f + 0.75f * (sawReset ? juce::jlimit (0.0f, 1.0f, (float) frames / (float) want) : 0.0f);
            if (sawReset && frames >= want && elapsed > 1500.0) finishMeasure (mic);
            else if (elapsed > 30000.0)
            {
                if (sawReset && frames > 0) finishMeasure (mic);
                else cancelMeasure ("No transfer function: check the reference and that the speaker is playing.");
                return;
            }
        }
        repaint (measureInfo.expanded (2));
    }

    void finishMeasure (int mic)
    {
        if (snap == nullptr || mic >= (int) snap->mics.size()) { cancelMeasure ("No data from the mic."); return; }
        const auto& r = snap->mics[(size_t) mic];
        FreqTable t;
        for (size_t i = 0; i < snap->tfFreq.size() && i < r.tfMag.size(); ++i)
        {
            if (! std::isfinite (r.tfMag[i])) continue;
            t.freq.push_back (snap->tfFreq[i]);
            t.db.push_back (r.tfMag[i]);
            t.phase.push_back (i < r.tfPhase.size() && std::isfinite (r.tfPhase[i]) ? r.tfPhase[i] : 0.0);
            t.extra.push_back (i < r.tfCoh.size() ? r.tfCoh[i] : 0.0);
        }
        if (t.freq.size() < 16) { cancelMeasure ("Not enough of the response was clear enough to keep. Raise the level or reduce the noise."); return; }
        auto& s = sys.sources[(size_t) measuring];
        s.response = std::move (t);
        s.imported = false;
        s.delayUsedMs = sys.lockedDelayMs;
        s.source = proc.mics.nameOf (mic) + ", " + LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        const auto who = nameOf (measuring);
        settings.genOn = false;
        phase = Phase::Idle;
        measuring = -1;
        refreshMeasureButton();
        // next speaker that still needs measuring
        for (int k = 0; k < (int) sys.sources.size(); ++k)
            if (! sys.sources[(size_t) k].hasResponse()) { selected = k; break; }
        status = who + " measured.";
        systemChanged (true);
    }

    // ---- right column tabs ----
    void setRightTab (int t)
    {
        rightTab = juce::jlimit (0, 1, t);
        for (int i = 0; i < 2; ++i) tabButtons[i].setToggleState (i == rightTab, juce::dontSendNotification);
        setRightTabVisibility();
        repaint();
    }

    void setRightTabVisibility()
    {
        const bool xoTab = rightTab == 0;
        for (auto* c : std::initializer_list<juce::Component*> { &highBox, &lowBox, &rangeBox, &findButton, &freqSlider, &typeBox, &slopeBox,
                                                                 &delaySlider, &gainSlider, &invertButton })
            c->setVisible (xoTab);
        for (auto& b : typeButtons) b.setVisible (xoTab);
        for (auto& b : slopeButtons) b.setVisible (xoTab);
        for (auto* c : std::initializer_list<juce::Component*> { &alList, &alAddButton, &alRemoveButton, &alAlignBox, &alRefBox, &alSpotBox,
                                                                 &alExtraSlider, &alMeasureButton, &alApplyButton })
            c->setVisible (! xoTab);
    }

    // ---- time alignment ----
    Alignment* alignmentAt (int i) { return i >= 0 && i < (int) sys.alignments.size() ? &sys.alignments[(size_t) i] : nullptr; }
    Alignment* currentAlignment() { return alignmentAt (alSelected); }
    const Alignment* currentAlignment() const { return alSelected >= 0 && alSelected < (int) sys.alignments.size() ? &sys.alignments[(size_t) alSelected] : nullptr; }

    static juce::String spotOf (const Alignment& a) { return a.spot.isNotEmpty() ? a.spot : juce::String ("the mic position"); }

    juce::String spotName() const
    {
        if (arrivalAlignment >= 0 && arrivalAlignment < (int) sys.alignments.size() && sys.alignments[(size_t) arrivalAlignment].spot.isNotEmpty())
            return sys.alignments[(size_t) arrivalAlignment].spot;
        return "the mic";
    }

    void selectAlignment (int i)
    {
        alSelected = juce::jlimit (0, juce::jmax (0, (int) sys.alignments.size() - 1), i);
        refreshAlignment();
        repaint();
    }

    // A sensible first pair: left / right mains, else a fill or delay to the main.
    void addAlignment()
    {
        if ((int) sys.alignments.size() >= SpeakerSystem::kMaxAlignments || sys.sources.size() < 2) return;
        const auto& src = sys.sources;
        auto find = [&src] (auto pred) { for (int i = 0; i < (int) src.size(); ++i) if (pred (src[(size_t) i])) return i; return -1; };
        auto used = [this] (int ref, int al) { for (const auto& a : sys.alignments) if (a.ref == ref && a.align == al) return true; return false; };
        Alignment a;
        const int left = find ([] (const SpeakerSource& s) { return s.role == (int) SpeakerRole::Main && s.channel == (int) SpeakerChannel::Left; });
        const int right = find ([] (const SpeakerSource& s) { return s.role == (int) SpeakerRole::Main && s.channel == (int) SpeakerChannel::Right; });
        int mainIdx = find ([] (const SpeakerSource& s) { return s.role == (int) SpeakerRole::Main; });
        if (mainIdx < 0) mainIdx = 0;
        a.ref = mainIdx;
        a.align = -1;
        for (int i = 0; i < (int) src.size() && a.align < 0; ++i)
            if ((src[(size_t) i].role == (int) SpeakerRole::Fill || src[(size_t) i].role == (int) SpeakerRole::Delay) && ! used (mainIdx, i)) a.align = i;
        if (a.align < 0 && left >= 0 && right >= 0 && ! used (left, right)) { a.ref = left; a.align = right; }
        if (a.align < 0) a.align = mainIdx == 0 ? 1 : 0;
        const auto& al = src[(size_t) a.align];
        const bool pair = al.role == (int) SpeakerRole::Main;
        a.spot = pair ? juce::String ("Centre") : al.role == (int) SpeakerRole::Fill ? juce::String ("Front rows")
                                                : al.role == (int) SpeakerRole::Delay ? juce::String ("Delay area") : juce::String ("Overlap");
        if (a.spot.isNotEmpty() && ! proc.positions.contains (a.spot)) proc.positions.add (a.spot);
        a.precedenceMs = al.role == (int) SpeakerRole::Delay ? 5.0 : 0.0;
        sys.alignments.push_back (a);
        alSelected = (int) sys.alignments.size() - 1;
        setRightTab (1);
        systemChanged (false);
    }

    void refreshAlignment()
    {
        alSelected = juce::jlimit (0, juce::jmax (0, (int) sys.alignments.size() - 1), alSelected);
        alList.setSelected (alSelected);
        for (auto* b : { &alAlignBox, &alRefBox })
        {
            b->clear (juce::dontSendNotification);
            for (int i = 0; i < (int) sys.sources.size(); ++i) b->addItem (nameOf (i), i + 1);
        }
        alSpotBox.clear (juce::dontSendNotification);
        for (int i = 0; i < proc.positions.size(); ++i) alSpotBox.addItem (proc.positions[i], i + 1);
        const auto* a = currentAlignment();
        for (auto* c : std::initializer_list<juce::Component*> { &alAlignBox, &alRefBox, &alSpotBox, &alExtraSlider, &alRemoveButton })
            c->setEnabled (a != nullptr);
        alAddButton.setEnabled ((int) sys.alignments.size() < SpeakerSystem::kMaxAlignments && sys.sources.size() > 1);
        if (a != nullptr)
        {
            alAlignBox.setSelectedItemIndex (a->align, juce::dontSendNotification);
            alRefBox.setSelectedItemIndex (a->ref, juce::dontSendNotification);
            alSpotBox.setText (a->spot, juce::dontSendNotification);
            alExtraSlider.setValue (a->precedenceMs, juce::dontSendNotification);
        }
        refreshApply();
        refreshMeasureButton();
    }

    void refreshApply()
    {
        const auto* a = currentAlignment();
        const bool ok = a != nullptr && a->measured() && a->ref != a->align && a->ref < (int) sys.sources.size() && a->align < (int) sys.sources.size();
        alApplyButton.setEnabled (ok);
        if (! ok) { alApplyButton.setButtonText ("Apply delay"); return; }
        const double need = a->neededMs (sys.sources[(size_t) a->ref].proc.delayMs);
        const bool applied = need >= -0.005 && std::abs (sys.sources[(size_t) a->align].proc.delayMs - need) < 0.01;
        alApplyButton.setEnabled (! applied);
        alApplyButton.setButtonText (need < -0.005 ? "Swap the pair" : applied ? "Applied: " + juce::String (need, 2) + " ms on " + nameOf (a->align)
                                                                               : "Apply " + juce::String (need, 2) + " ms to " + nameOf (a->align));
    }

    void applyAlignment()
    {
        auto* a = currentAlignment();
        if (a == nullptr || ! a->measured()) return;
        const double need = a->neededMs (sys.sources[(size_t) a->ref].proc.delayMs);
        if (need < -0.005)
        {
            // the aligned speaker is already the later one: line the other one up to it instead
            std::swap (a->ref, a->align);
            std::swap (a->refArrivalMs, a->alignArrivalMs);
            std::swap (a->refLevelDb, a->alignLevelDb);
            status = "Swapped: " + nameOf (a->align) + " is now lined up to " + nameOf (a->ref) + ".";
        }
        else
        {
            sys.sources[(size_t) a->align].proc.delayMs = need;
            status = nameOf (a->align) + " delay set to " + juce::String (need, 2) + " ms.";
            graph.rebuild();
        }
        systemChanged (false);
    }

    void layoutAlignment (juce::Rectangle<int> c)
    {
        alHint = c.removeFromTop (46);
        c.removeFromTop (4);
        const int rows = juce::jlimit (1, 4, (int) sys.alignments.size());
        alList.setBounds (c.removeFromTop (rows * AlignmentList::kRowH));
        c.removeFromTop (6);
        auto br = c.removeFromTop (28);
        alAddButton.setBounds (br.removeFromLeft (140));
        br.removeFromLeft (8);
        alRemoveButton.setBounds (br.removeFromLeft (100));
        c.removeFromTop (14);
        auto put = [&] (const juce::String& name, juce::Component& comp)
        {
            auto row = c.removeFromTop (28);
            labels.push_back ({ row.removeFromLeft (64), name });
            comp.setBounds (row);
            c.removeFromTop (6);
        };
        put ("Delay", alAlignBox);
        put ("To", alRefBox);
        put ("At", alSpotBox);
        put ("Extra", alExtraSlider);
        c.removeFromTop (4);
        alMeasureButton.setBounds (c.removeFromTop (34));
        c.removeFromTop (8);
        alInfo = c.removeFromTop (juce::jmax (60, c.getHeight() - 42));
        alApplyButton.setBounds (c.removeFromTop (32));
    }

    void paintAlignment (juce::Graphics& g)
    {
        const auto& p = pal();
        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        g.drawFittedText ("Put " + proc.mics.nameOf (sys.measureMic) + " where both speakers cover the seats (centre line for left / right). "
                          "Set both speakers' delays in the processor to 0 while measuring.",
                          alHint, juce::Justification::topLeft, 3);

        const auto* a = currentAlignment();
        juce::String t;
        juce::Colour c = p.textDim;
        if (a == nullptr) t = "Add an alignment: a fill or delay to the main, or the right main to the left.";
        else if (a->ref == a->align) { t = "Pick two different speakers."; c = p.warn; }
        else if (! a->measured()) { t = "Not measured at " + spotOf (*a) + " yet."; c = p.warn; }
        else
        {
            const auto refName = nameOf (a->ref), alName = nameOf (a->align);
            const double diff = a->alignArrivalMs - a->refArrivalMs;
            const double refDelay = sys.sources[(size_t) a->ref].proc.delayMs;
            const double need = a->neededMs (refDelay);
            t << alName << " arrives " << juce::String (std::abs (diff), 2) << " ms " << (diff < 0.0 ? "before " : "after ") << refName
              << " at " << spotOf (*a) << ".";
            if (refDelay > 0.0) t << " " << refName << " has " << juce::String (refDelay, 2) << " ms of its own delay.";
            if (need < -0.005)
                t << "\n" << alName << " is already the later one: swap the pair to delay " << refName << " instead.";
            else
            {
                t << "\nDelay " << alName << " " << juce::String (need, 2) << " ms";
                if (a->precedenceMs > 0.0) t << " (" << juce::String (a->precedenceMs, 1) << " ms extra so " << refName << " leads)";
                t << ".";
                if (std::abs (sys.sources[(size_t) a->align].proc.delayMs - need) < 0.01) t << " Applied.";
            }
            if (a->refLevelDb > -150.0f && a->alignLevelDb > -150.0f)
            {
                const float d = a->alignLevelDb - a->refLevelDb;
                t << "\nLevel here: " << alName << " " << signedText (d, 1) << " dB against " << refName;
                const bool lr = sys.sources[(size_t) a->align].role == (int) SpeakerRole::Main && sys.sources[(size_t) a->ref].role == (int) SpeakerRole::Main;
                if (lr && std::abs (d) > 1.0f) t << " (a stereo pair should match within 1 dB)";
                t << ".";
            }
            if (a->measuredOn.isNotEmpty()) t << "\nMeasured " << a->measuredOn << ".";
            c = p.text;
        }
        g.setColour (c);
        g.setFont (font (11.5f));
        g.drawFittedText (t, alInfo, juce::Justification::topLeft, 7);
    }

    void startArrival (int index)
    {
        auto* a = alignmentAt (index);
        if (a == nullptr || a->ref == a->align) return;
        if (settings.refSource.load() == 0)
        {
            status = "Set a reference on Mic setup (the loopback, or the generator) to measure arrivals.";
            repaint();
            return;
        }
        a->hasRef = a->hasAlign = false;
        arrivalMode = true;
        arrivalAlignment = index;
        arrivalQueue = { { index, 0 }, { index, 1 } };
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        status.clear();
        nextArrival();
    }

    void nextArrival()
    {
        const auto next = arrivalQueue.front();
        arrivalQueue.erase (arrivalQueue.begin());
        const auto* a = alignmentAt (next.first);
        if (a == nullptr) { cancelMeasure ("The alignment was removed."); return; }
        arrivalWhich = next.second;
        measuring = arrivalWhich == 0 ? a->ref : a->align;
        settings.genOutput = sys.sources[(size_t) measuring].output;
        settings.genType = 0;
        settings.genOn = true;
        settings.running = true;
        phase = Phase::Settling;
        phaseStart = juce::Time::getMillisecondCounterHiRes();
        settleUntil = proc.analyzer.samplesWritten() + (juce::uint64) (kSettleSeconds * rate());
        progress = 0.0f;
        refreshMeasureButton();
        repaint();
    }

    void finishArrival()
    {
        settings.genOn = false;
        phase = Phase::Idle;
        measuring = -1;
        arrivalMode = false;
        if (auto* a = alignmentAt (arrivalAlignment))
        {
            a->measuredOn = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
            status = "Measured " + nameOf (a->align) + " and " + nameOf (a->ref) + " at " + spotName() + ".";
        }
        arrivalAlignment = -1;
        refreshMeasureButton();
        systemChanged (false);
    }

    int arrivalLines() const
    {
        juce::StringArray l;
        l.addLines (summaryText (true));
        return juce::jmax (0, l.size() - (int) sys.sources.size() - 1);
    }

    // ---- the summary for the processor ----
    static juce::String filterText (const xo::Filter& f, const char* kind)
    {
        if (! f.active()) return {};
        return juce::String (kind) + " " + xo::typeShort (f.type) + juce::String (f.slope) + " @ " + juce::String (juce::roundToInt (f.freq)) + " Hz";
    }

    juce::String summaryText (bool forScreen) const
    {
        juce::String out;
        if (! forScreen) out << "LiveAlign speaker settings - " << proc.venueName << " - " << LiveAlignProcessor::today() << "\n\n";
        if (forScreen) out << "#" << juce::String ("SPEAKER").paddedRight (' ', 14) << juce::String ("CH").paddedRight (' ', 5)
                           << juce::String ("OUT").paddedRight (' ', 5) << juce::String ("FILTERS").paddedRight (' ', 30)
                           << juce::String ("DELAY").paddedRight (' ', 11) << juce::String ("POLARITY").paddedRight (' ', 10) << "GAIN\n";
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const auto& s = sys.sources[(size_t) i];
            juce::StringArray f;
            if (s.proc.hp.active()) f.add (filterText (s.proc.hp, "HPF"));
            if (s.proc.lp.active()) f.add (filterText (s.proc.lp, "LPF"));
            const auto filters = f.isEmpty() ? juce::String ("none") : f.joinIntoString (", ");
            const auto out1 = s.output == 0 ? juce::String ("all") : juce::String (s.output);
            const auto dly = juce::String (s.proc.delayMs, 2) + " ms";
            const auto pol = juce::String (s.proc.invert ? "inverted" : "normal");
            const auto gain = signedText ((float) s.proc.gainDb, 1) + " dB";
            if (forScreen)
                out << nameOf (i).substring (0, 13).paddedRight (' ', 14) << speakerChannelShort (s.channel).paddedRight (' ', 5)
                    << out1.paddedRight (' ', 5) << filters.substring (0, 29).paddedRight (' ', 30)
                    << dly.paddedRight (' ', 11) << pol.paddedRight (' ', 10) << gain << "\n";
            else
                out << nameOf (i) << " (" << speakerRoleNames()[s.role] << ", " << speakerChannelNames()[s.channel] << ", output " << out1 << "): "
                    << filters << "; delay " << dly << "; polarity " << pol << "; gain " << gain << "\n";
        }

        // time alignment: where each pair was lined up, and whether the delay is in the table above
        bool head = false;
        for (const auto& a : sys.alignments)
        {
            if (! a.measured() || a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) continue;
            if (! head) { out << (forScreen ? "#TIME ALIGNMENT\n" : "\nTime alignment:\n"); head = true; }
            const double need = a.neededMs (sys.sources[(size_t) a.ref].proc.delayMs);
            out << nameOf (a.align) << " to " << nameOf (a.ref) << " at " << spotOf (a) << ": ";
            if (need < -0.005) out << "arrives " << juce::String (-need, 2) << " ms late; swap the pair";
            else
            {
                out << "delay " << juce::String (need, 2) << " ms";
                if (a.precedenceMs > 0.0) out << " (incl. " << juce::String (a.precedenceMs, 1) << " ms extra)";
                const bool applied = std::abs (sys.sources[(size_t) a.align].proc.delayMs - need) < 0.01;
                out << (applied ? ", applied" : ", not applied yet");
            }
            if (a.refLevelDb > -150.0f && a.alignLevelDb > -150.0f)
                out << "; level " << signedText (a.alignLevelDb - a.refLevelDb, 1) << " dB";
            out << "\n";
        }
        if (! forScreen) out << "\nFilters: HPF high-pass, LPF low-pass; BW Butterworth, LR Linkwitz-Riley; slopes in dB/octave.\n";
        return out;
    }

    LiveAlignProcessor& proc;
    Settings& settings;
    SpeakerSystem& sys;
    const Snapshot* snap = nullptr;

    SpeakerGraph graph;
    SourceList list;
    juce::TextButton addButton { "Add speaker" }, removeButton { "Remove" };
    juce::TextEditor nameEditor;
    juce::ComboBox roleBox, channelBox, outputBox, micBox, highBox, lowBox, rangeBox, typeBox, slopeBox;
    juce::TextButton measureButton { "Measure" }, importButton { "Import response..." }, clearButton { "Clear" }, relockButton { "Re-time" };
    juce::Slider offsetSlider, freqSlider, delaySlider, gainSlider;
    juce::ToggleButton processedToggle { "With processing" };
    juce::TextButton typeButtons[3], slopeButtons[kSlopeButtons];
    juce::TextButton findButton { "Find best crossover" }, invertButton { "Low normal" };
    juce::TextButton copyButton { "Copy settings" }, doneButton { "Mark speakers set up" };
    juce::TextButton tabButtons[2];
    AlignmentList alList;
    juce::TextButton alAddButton { "Add alignment" }, alRemoveButton { "Remove" }, alMeasureButton { "Measure both" }, alApplyButton { "Apply delay" };
    juce::ComboBox alAlignBox, alRefBox, alSpotBox;
    juce::Slider alExtraSlider;
    juce::Rectangle<int> alHint, alInfo;
    int alLabelFrom = 0, alLabelTo = 0, xoLabelFrom = 0, xoLabelTo = 0;
    int rightTab = 0, alSelected = 0, arrivalAlignment = -1, arrivalWhich = 0;
    bool arrivalMode = false;
    std::vector<std::pair<int, int>> arrivalQueue;   // (alignment, 0 = reference speaker / 1 = aligned speaker)

    juce::Rectangle<int> systemCard, selCard, measureCard, xoCard, summaryCard, selInfo, measureInfo, xoInfo, liveLine, summaryArea, statusLine;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;

    int selected = 0, nOuts = -1, measuring = -1;
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    float progress = 0.0f;
    juce::uint64 delaySerial = 0, settleUntil = 0;
    std::array<float, kMaxMics> savedDelays {};
    bool searching = false, liveValid = false, sawReset = false;
    double liveEff = 0.0, liveDev = 0.0;
    juce::String status;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace la::ui
