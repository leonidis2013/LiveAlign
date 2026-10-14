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
#include "GuidedMeasure.h"
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
    // Faded where a measurement is below the minimum coherence; the selected speaker's coherence across the top.
    void setReliabilityView (int selectedSource, float minCoherence) { selectedIdx = selectedSource; minCoh = minCoherence; }

    // Call whenever the system changes.
    void rebuild()
    {
        if (grid.empty()) grid = xo::analysisGrid();
        curves.clear();
        sum.assign (grid.size(), cplx (0.0, 0.0));
        std::vector<cplx> pairSum (grid.size(), cplx (0.0, 0.0));
        int used = 0, inPair = 0;
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
            c.coh.assign (grid.size(), 1.0f);
            for (size_t k = 0; k < grid.size() && k < r.coh.size(); ++k) c.coh[k] = (float) r.coh[k];
            for (size_t k = 0; k < grid.size(); ++k)
            {
                const double m = std::abs (r.h[k]);
                c.db[k] = m < 1e-5 ? NAN : (float) (20.0 * std::log10 (m));
                c.ph[k] = m < 1e-5 ? NAN : (float) (std::arg (r.h[k]) * 180.0 / kPi);
                sum[k] += r.h[k];
            }
            curves.push_back (std::move (c));
            if (sys.realPair() && (i == sys.xoHigh || i == sys.xoLow || sys.master (i) == sys.xoHigh || sys.master (i) == sys.xoLow))
            {
                // the sum only means something for a top and its sub: left and right are separate channels
                if (sys.master (i) == i || ! sys.sources[(size_t) sys.master (i)].show) { for (size_t k = 0; k < grid.size(); ++k) pairSum[k] += r.h[k]; ++inPair; }
            }
            ++used;
        }
        sumDb.assign (grid.size(), NAN);
        hasSum = inPair > 1;
        if (hasSum)
            for (size_t k = 0; k < grid.size(); ++k)
                if (std::abs (pairSum[k]) > 1e-5) sumDb[k] = (float) (20.0 * std::log10 (std::abs (pairSum[k])));

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
            // coherence of the selected speaker, in the top quarter (as on the Monitoring transfer function)
            for (const auto& c : curves)
            {
                if (c.index != selectedIdx) continue;
                const auto cohArea = magArea.withHeight (magArea.getHeight() * 0.25f);
                auto yC = [&] (float v) { return cohArea.getBottom() - juce::jlimit (0.0f, 1.0f, v) * cohArea.getHeight(); };
                if (minCoh > 0.0f)
                {
                    const float dashes[] = { 3.0f, 4.0f };
                    juce::Path th, dashed;
                    th.startNewSubPath (magArea.getX(), yC (minCoh));
                    th.lineTo (magArea.getRight(), yC (minCoh));
                    juce::PathStrokeType (1.0f).createDashedStroke (dashed, th, dashes, 2);
                    g.setColour (p.reduction.withAlpha (0.5f));
                    g.fillPath (dashed);
                }
                strokeTrace (g, tracePath (grid.size(), xAt, [&] (size_t k) { return yC (c.coh[k]); },
                                           [&] (size_t k) { return std::isfinite (c.db[k]); }),
                             p.reduction.withAlpha (0.85f), 1.3f);
                g.setColour (p.reduction);
                g.setFont (font (10.0f, true));
                g.drawText ("COHERENCE: " + sys.sources[(size_t) c.index].name.toUpperCase(),
                            cohArea.reduced (6.0f, 2.0f).removeFromTop (12.0f), juce::Justification::topLeft);
            }
            // each speaker: faded where its measurement is not reliable
            for (const auto& c : curves)
            {
                const auto col = sourceColour (c.index);
                strokeTrace (g, tracePath (grid.size(), xAt, [&] (size_t k) { return yDb (c.db[k]); }, [] (size_t) { return true; }),
                             col.withAlpha (minCoh > 0.0f ? 0.3f : 1.0f), 1.6f);
                if (minCoh > 0.0f)
                    strokeTrace (g, tracePath (grid.size(), xAt, [&] (size_t k) { return yDb (c.db[k]); },
                                               [&] (size_t k) { return c.coh[k] >= minCoh; }),
                                 col, 1.8f);
            }
            if (! sumDb.empty() && hasSum)
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
        if (hasSum) addKey ("Top + sub sum", p.text);
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
    struct Curve { int index; std::vector<float> db, ph, coh; };
    const SpeakerSystem& sys;
    std::vector<double> grid;
    std::vector<Curve> curves;
    std::vector<cplx> sum;
    std::vector<float> sumDb;
    bool hasSum = false;
    float topDb = 6.0f;
    bool withProc = true;
    double xoFreq = 0.0;
    float legendRight = 12.0f, minCoh = 0.0f;
    int selectedIdx = -1;
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
    void setReliability (const std::vector<double>& r) { rel = r; repaint(); }
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
            // polarity: + normal, - inverted, ? not clear, an empty ring when not checked yet
            {
                r.removeFromRight (6.0f);
                auto pb = r.removeFromRight (18.0f).withSizeKeepingCentre (18.0f, 18.0f);
                const int v = s.polaritySummary();
                const int checked = s.polarityChecked();
                const auto pc = v == polarity::Normal ? p.good : v == polarity::Inverted ? p.bad : v == polarity::Unsure ? p.warn : p.textFaint;
                g.setColour (pc.withAlpha (v == polarity::Unknown ? 0.0f : 0.2f));
                g.fillEllipse (pb);
                g.setColour (pc);
                g.drawEllipse (pb.reduced (0.6f), 1.2f);
                g.setFont (monoFont (12.0f, true));
                const char* sym = v == polarity::Normal ? "+" : v == polarity::Inverted ? "-" : v == polarity::Unsure ? "?" : (checked > 0 ? "." : "");
                g.drawText (sym, pb.translated (0.0f, -0.5f), juce::Justification::centred);
            }

            g.setColour (sourceColour (i));
            g.fillRoundedRectangle (r.removeFromLeft (4.0f).withSizeKeepingCentre (4.0f, 20.0f), 2.0f);
            r.removeFromLeft (10.0f);
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            g.drawText (s.name.isNotEmpty() ? s.name : "Speaker " + juce::String (i + 1), r.removeFromTop (row.getHeight() * 0.55f), juce::Justification::bottomLeft, true);
            // measured: how reliable it is (share of its range above the minimum coherence)
            const double rv = i < (int) rel.size() ? rel[(size_t) i] : -1.0;
            const int master = sys.master (i);
            juce::Colour sc = s.hasResponse() ? p.good : master != i && sys.covered (i) ? p.textDim : p.warn;
            juce::String st = s.hasResponse() ? (s.imported ? "IMPORTED" : "MEASURED")
                            : master != i ? "AS " + sys.sources[(size_t) master].name.toUpperCase() : juce::String ("TO MEASURE");
            if (s.hasResponse() && rv >= 0.0)
            {
                st = juce::String (juce::roundToInt (rv * 100.0)) + "% RELIABLE";
                sc = rv >= 0.8 ? p.good : rv >= 0.6 ? p.warn : p.bad;
            }
            g.setColour (sc);
            g.setFont (font (10.5f, true));
            g.drawText (st, r.withTrimmedRight (6.0f), juce::Justification::topRight, true);
            const float stw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), st) + 14.0f;
            g.setFont (font (10.5f));
            juce::String info = speakerRoleNames()[s.role] + "  -  " + (s.channel == 0 ? juce::String ("Mono") : speakerChannelShort (s.channel))
                                + "  -  " + (s.output == 0 ? juce::String ("all outs") : "out " + juce::String (s.output));
            if (s.count > 1) info << "  -  x" << s.count;
            if (s.model.isNotEmpty()) info << "  -  " << s.model;
            g.setColour (p.textDim);
            g.drawText (info, r.withTrimmedRight (stw), juce::Justification::topLeft, true);

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
    std::vector<double> rel;
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
                g.drawText (need >= -0.005 ? "delay " + juce::String (need, 2) + " ms" : "delay the other " + juce::String (-need, 2) + " ms", r.withTrimmedRight (4.0f), juce::Justification::topRight, true);
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
class SpeakersPage : public juce::Component, public GuidedSource, private juce::Timer
{
public:
    std::function<void (int)> onImport;   // the editor owns the file chooser
    std::function<void (GuidedSource&)> onGuided;   // opens the guided pop-up
    std::function<void()> onChanged;

    explicit SpeakersPage (LiveAlignProcessor& p)
        : proc (p), settings (p.settings), sys (p.system), graph (p.system), list (p.system), alList (p.system)
    {
        addAndMakeVisible (graph);
        addAndMakeVisible (list);
        list.onSelect = [this] (int i) { select (i); };
        list.onToggleShow = [this] (int i) { sys.sources[(size_t) i].show = ! sys.sources[(size_t) i].show; systemChanged (false); };

        addButton.onClick = [this] { showAddMenu(); };
        addButton.setTooltip ("Add speakers (up to 8): a left / right pair, a sub, fills, delays ...");
        removeButton.onClick = [this] { removeSelected(); };
        addAndMakeVisible (addButton);
        addAndMakeVisible (removeButton);

        // ---- selected source ----
        nameEditor.setFont (font (13.0f));
        nameEditor.onTextChange = [this] { if (auto* s = current()) { s->name = nameEditor.getText().trim(); systemChanged (false); refreshChoices(); } };
        roleBox.addItemList (speakerRoleNames(), 1);
        roleBox.onChange = [this] { if (auto* s = current()) { s->role = roleBox.getSelectedItemIndex(); sys.ensurePair(); systemChanged (false); } };
        channelBox.addItemList (speakerChannelNames(), 1);
        channelBox.setTooltip ("Where this speaker sits: mono, stereo left, centre, stereo right, or both sides (L+R)");
        channelBox.onChange = [this] { if (auto* s = current()) { s->channel = channelBox.getSelectedItemIndex(); systemChanged (false); } };
        modelEditor.setFont (font (13.0f));
        modelEditor.setTextToShowWhenEmpty ("Make and model", pal().textFaint);
        modelEditor.onTextChange = [this] { if (auto* s = current()) { s->model = modelEditor.getText().trim(); changedOutside(); list.repaint(); } };
        for (int n = 1; n <= 8; ++n) countBox.addItem (n == 1 ? juce::String ("1 cabinet") : juce::String (n) + " cabinets", n);
        countBox.setTooltip ("How many cabinets this output feeds (two subs side by side, a column of two ...)");
        countBox.onChange = [this] { if (auto* s = current()) { s->count = juce::jmax (1, countBox.getSelectedId()); systemChanged (false); } };
        linkBox.setTooltip ("Share the crossover, level and polarity with another speaker, e.g. Main R with Main L. "
                            "A shared speaker needs no measurement of its own.");
        linkBox.onChange = [this]
        {
            if (auto* s = current())
            {
                const int id = linkBox.getSelectedId();
                s->linkTo = id <= 1 ? -1 : id - 2;
                if (s->linkTo == selected) s->linkTo = -1;
                systemChanged (false);
            }
        };
        outputBox.onChange = [this] { if (auto* s = current()) { s->output = outputBox.getSelectedItemIndex(); systemChanged (false); } };
        outputBox.setTooltip ("The interface output that feeds this speaker or group while it is measured. "
                              "Or play all outputs and mute the others in your processor.");
        measureButton.onClick = [this] { if (phase == Phase::Idle) requestMeasure(); else cancelMeasure ("Stopped."); };
        polarityButton.setTooltip ("A quick measurement close to the speaker that only checks its polarity: for speakers set \"Same as\" "
                                   "another (Main R), and each cabinet when several share an output. Speakers measured on their own "
                                   "have theirs checked with the measurement.");
        polarityButton.onClick = [this] { checkPolarityOfSelected(); };
        addChildComponent (polarityButton);
        guidedToggle.setButtonText ("Guided");
        guidedToggle.setTooltip ("Step by step in a pop-up: the venue plan shows which speaker plays and where the mic goes, then Measure and Next point");
        guidedToggle.setToggleState (GuidedSource::enabled(), juce::dontSendNotification);
        guidedToggle.onClick = [this] { GuidedSource::setEnabled (guidedToggle.getToggleState()); };
        addChildComponent (guidedToggle);
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
        for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &roleBox, &channelBox, &outputBox, &measureButton, &importButton, &clearButton, &offsetSlider,
                                                                 &modelEditor, &countBox, &linkBox })
            addAndMakeVisible (*c);

        // ---- measurement ----
        micBox.onChange = [this]
        {
            const int m = juce::jmax (0, micBox.getSelectedItemIndex());
            if (sys.placement == 0)
            {
                // close up: this speaker's own mic, and the one the next speakers start with
                if (selected >= 0 && selected < (int) sys.sources.size()) sys.sources[(size_t) selected].measureMic = m;
                sys.measureMic = m;
            }
            else if (m != sys.measureMic) { sys.measureMic = m; sys.timingLocked = false; }
            systemChanged (false);
        };
        placementBox.addItemList ({ "Close to each speaker (12 in / 30 cm)", "One spot for every speaker" }, 1);
        placementBox.setTooltip ("Close up gives each speaker's own response without the room; the timing between them then "
                                 "comes from Time alignment. One spot keeps the timing in the measurements but includes the room.");
        placementBox.onChange = [this] { sys.placement = placementBox.getSelectedItemIndex(); systemChanged (false); };
        addAndMakeVisible (placementBox);
        relockButton.setTooltip ("Find the timing again on the next measurement (after moving the mic). Measure every speaker again afterwards.");
        relockButton.onClick = [this] { sys.timingLocked = false; status = "Timing will be found again on the next measurement."; systemChanged (false); };
        addAndMakeVisible (micBox);
        addAndMakeVisible (relockButton);

        processedToggle.setToggleState (true, juce::dontSendNotification);
        processedToggle.onClick = [this] { graph.setWithProcessing (processedToggle.getToggleState()); };
        addAndMakeVisible (processedToggle);

        // ---- crossover ----
        highBox.onChange = [this] { sys.xoHigh = highBox.getSelectedItemIndex(); sys.xoChosen = true; xoChanged(); };
        lowBox.onChange = [this] { sys.xoLow = lowBox.getSelectedItemIndex(); sys.xoChosen = true; xoChanged(); };
        swapPairButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x87\x84")));   // left-right arrows
        swapPairButton.setTooltip ("Swap the high and low speakers");
        swapPairButton.onClick = [this] { swapPair(); };
        invertedSwapButton.onClick = [this] { swapPair(); };
        invertedSwapButton.setTooltip ("Put the top above the crossover and the sub below it");
        addAndMakeVisible (swapPairButton);
        addChildComponent (invertedSwapButton);
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
        for (auto* b : { &autoButton, &manualButton })
        {
            b->setRadioGroupId (8301);
            b->setClickingTogglesState (true);
            addChildComponent (*b);
        }
        autoButton.setConnectedEdges (juce::Button::ConnectedOnRight);
        manualButton.setConnectedEdges (juce::Button::ConnectedOnLeft);
        autoButton.onClick = [this] { if (autoButton.getToggleState() && xoManual) { xoManual = false; resized(); repaint(); } };
        manualButton.onClick = [this] { if (manualButton.getToggleState() && ! xoManual) { xoManual = true; refreshManual(); resized(); repaint(); } };
        autoButton.setTooltip ("Find the best crossover your processor can do");
        manualButton.setTooltip ("Set the frequency, filter, delay, level and polarity yourself");
        removeXoButton.setTooltip ("Clear the high-pass and low-pass filters (and the low speaker's level and polarity) set by an earlier crossover");
        removeXoButton.onClick = [this] { sys.removeCrossoverFilters(); refreshManual(); graph.rebuild(); systemChanged (false); resized(); repaint(); };
        addChildComponent (removeXoButton);
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

        // ---- signal reliability: the same minimum coherence as Settings > Transfer function ----
        minCohBox.addItemList ({ "Off", "20 %", "40 % (default)", "60 %", "80 % (strict)" }, 1);
        minCohBox.setTooltip ("How reliable a measurement point must be to count. Points with less coherence are faded on the "
                              "graph, hidden on the Monitoring transfer function, and not trusted by the crossover search. "
                              "Raise it in a noisy room to be stricter.");
        minCohBox.onChange = [this]
        {
            settings.coherenceBlank = (float) minCohBox.getSelectedItemIndex() / 5.0f;
            sys.lastResult = {};
            systemChanged (false);
        };
        addAndMakeVisible (minCohBox);

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
        alSignalBox.addItemList ({ "Pink noise", "Sweep", "Music from the console" }, 1);
        alSignal = juce::jlimit (0, 2, UserPrefs::get ("alignSignal", "0").getIntValue());
        alSignalBox.setSelectedItemIndex (alSignal, juce::dontSendNotification);
        alSignalBox.setTooltip ("What plays while each speaker is measured. Pink noise (the default) or a sweep from the generator; or music "
                                "from the console when noise is not possible: route it to one speaker at a time on the console, with the "
                                "console's feed on an input as the reference (Mics, Reference).");
        alSignalBox.onChange = [this]
        {
            alSignal = alSignalBox.getSelectedItemIndex();
            UserPrefs::set ("alignSignal", juce::String (alSignal));
            repaint();
        };
        addChildComponent (alSignalBox);
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
        alMeasureButton.onClick = [this] { if (phase == Phase::Idle) alignmentAction(); else cancelMeasure ("Stopped."); };
        alMeasureButton.setTooltip ("The next step of this alignment: play each speaker on its own, apply the delay, then check them together");
        alApplyButton.onClick = [this] { applyAlignment(); };
        alRedoButton.onClick = [this]
        {
            if (auto* a = currentAlignment()) { a->hasRef = a->hasAlign = false; a->sumBandDb = a->refBandDb = a->alignBandDb = -200.0f; a->checkedOn.clear(); a->phaseValid = false; a->refTf = a->alignTf = {}; }
            status = "Starting this alignment again.";
            systemChanged (false);
        };
        alRedoButton.setTooltip ("Measure this pair again from the first step");
        for (auto* c : std::initializer_list<juce::Component*> { &alAddButton, &alRemoveButton, &alAlignBox, &alRefBox, &alSpotBox, &alExtraSlider,
                                                                 &alMeasureButton, &alRedoButton })
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
        chooseStep();
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
        measureButton.setButtonText (resp ? "Stop" : "Measure " + nameOf (selected));
        measureButton.setColour (juce::TextButton::buttonColourId, resp ? pal().bad : pal().accent);
        measureButton.setEnabled (! arr && (proc.isStandalone() || proc.uiAsStandalone));
        polarityButton.setButtonText ("Check polarity of " + nameOf (selected));
        polarityButton.setEnabled (! busyNow && (proc.isStandalone() || proc.uiAsStandalone));
        alMeasureButton.setButtonText (arr ? "Stop" : alignmentActionText());
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
        micBox.setSelectedItemIndex (sys.micFor (selected, settings.micCount.load()), juce::dontSendNotification);
        micBox.setTooltip (sys.placement == 0 ? "The mic that measures this speaker, close up. Each speaker can have its own."
                                              : "The one mic used for every speaker measurement. Keep it in the same place for all of them.");

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

        minCohBox.setSelectedItemIndex (juce::jlimit (0, 4, (int) std::lround (settings.coherenceBlank.load() * 5.0f)), juce::dontSendNotification);
        placementBox.setSelectedItemIndex (sys.placement, juce::dontSendNotification);
        refreshReliability();
        graph.setReliabilityView (selected, settings.coherenceBlank.load());
        refreshSelected();
        refreshManual();
        graph.rebuild();
        resized();
        repaint();
    }

    // Opens the first step that is not done yet (after a venue is opened).
    // What is left on this page (empty when the speakers are set up).
    juce::String nextHint() const
    {
        if (! allMeasured()) return "Measure each speaker";
        if (sys.pairValid() && ! crossoverSet()) return "Find the crossover";
        if (alignmentNeeded() && ! alignmentDone()) return "Time-align the fills";
        if (! proc.speakers.configured) return "Mark the speakers as set up";
        return {};
    }
    void chooseStep() { openStepAt (! allMeasured() ? 0 : ! crossoverSet() ? 1 : ! alignmentDone() ? 2 : openStep); }

    void sourceImported() { systemChanged (true); }

    // Which part this page shows: 0 the system (speakers and their settings), 1 speaker response, 2 crossover,
    // 3 time alignment. Each is its own entry in the navigation pane.
    void setMode (int m)
    {
        mode = juce::jlimit (0, 3, m);
        if (mode > 0) openStep = mode - 1;
        refreshChoices();
    }
    int getMode() const { return mode; }
    bool responsesDone() const { return allMeasured(); }
    bool crossoverDone() const { return ! sys.xoNeeded() || (crossoverSet() && ! sys.pairInverted() && sys.realPair()); }
    bool alignmentRequired() const { return alignmentNeeded(); }
    bool alignmentsDone() const { return alignmentDone(); }
    juce::String stepSummaryText (int st) const { return stepSummary (st); }

    // Same as the buttons (used by the local screenshot tool).
    void measure (int i) { select (i); startMeasure (i); }
    void checkPolarity (int i, int cab) { select (i); startPolarity (i, cab); }
    void setAlignmentSignal (int sig) { alSignal = juce::jlimit (0, 2, sig); alSignalBox.setSelectedItemIndex (alSignal, juce::dontSendNotification); }
    void measureAlignmentOne (int i, int which) { selectAlignment (i); startArrivalOne (i, which); }

    // Polarity checks still to do (speakers set "Same as" another, other cabinets), and the first speaker that looks inverted.
    int polarityChecksLeft() const
    {
        int n = 0;
        for (const auto& t : guidedTasks()) n += ! t.response && ! taskDone (t) ? 1 : 0;
        return n;
    }
    juce::String invertedSpeaker() const
    {
        for (int i = 0; i < (int) sys.sources.size(); ++i)
            if (sys.sources[(size_t) i].polaritySummary() == polarity::Inverted) return nameOf (i);
        return {};
    }
    // The next polarity check: in the guided pop-up when Guided is on.
    void nextPolarityCheck()
    {
        if (phase != Phase::Idle) return;
        guidedNow = -1;
        // an inverted one first: check it again once the wiring is fixed
        for (const auto& t : guidedTasks())
            if (const auto* pc = sys.sources[(size_t) t.spk].polarityOf (t.cab); pc != nullptr && pc->verdict == polarity::Inverted) { guidedNow = t.id(); break; }
        if (GuidedSource::enabled() && onGuided) { onGuided (*this); return; }
        const auto tasks = guidedTasks();
        const int k = guidedCurrentIndex();
        guidedNow = -1;
        if (k < 0) return;
        select (tasks[(size_t) k].spk);
        if (tasks[(size_t) k].response) startMeasure (tasks[(size_t) k].spk);
        else startPolarity (tasks[(size_t) k].spk, tasks[(size_t) k].cab);
    }

    // Measure the selected speaker: in the guided pop-up when Guided is on, otherwise right here.
    void requestMeasure()
    {
        if (phase != Phase::Idle) return;
        guidedToggle.setToggleState (GuidedSource::enabled(), juce::dontSendNotification);
        if (GuidedSource::enabled() && onGuided)
        {
            const bool own = sys.master (selected) == selected && ! sys.sources[(size_t) selected].imported;
            guidedNow = SpkTask { own, selected, 0 }.id();
            onGuided (*this);
        }
        else startMeasure (selected);
    }

    // ---- guided measuring: one point per speaker that needs its own measurement ----
    juce::String guidedTitle() const override { return "Speaker response"; }

    // The guided points: each speaker's response (the ones measured on their own), then a polarity check of every
    // cabinet the responses do not cover (speakers set "Same as" another, and the other cabinets on one output).
    struct SpkTask { bool response = true; int spk = 0, cab = 0; int id() const { return (response ? 0 : 10000) + spk * 100 + cab; } };

    std::vector<SpkTask> guidedTasks() const
    {
        std::vector<SpkTask> out;
        const int n = juce::jmin ((int) sys.sources.size(), VenuePlan::kMaxSpeakers);
        for (int i = 0; i < n; ++i) if (sys.master (i) == i && ! sys.sources[(size_t) i].imported) out.push_back ({ true, i, 0 });
        for (int i = 0; i < n; ++i)
            for (int c = 0; c < sys.sources[(size_t) i].count; ++c)
                if (sys.master (i) != i || c > 0) out.push_back ({ false, i, c });
        return out;
    }

    bool taskDone (const SpkTask& t) const
    {
        const auto& s = sys.sources[(size_t) t.spk];
        if (t.response) return s.hasResponse();
        const auto* pc = s.polarityOf (t.cab);
        return pc != nullptr && pc->done();
    }

    bool taskProblem (const SpkTask& t) const
    {
        const auto* pc = sys.sources[(size_t) t.spk].polarityOf (t.cab);
        return pc != nullptr && (pc->verdict == polarity::Inverted || pc->verdict == polarity::Unsure);
    }

    // The task now: the one picked (Measure again, or the selected speaker), otherwise the first not done.
    int guidedCurrentIndex() const
    {
        const auto tasks = guidedTasks();
        for (size_t k = 0; k < tasks.size(); ++k) if (tasks[k].id() == guidedNow) return (int) k;
        for (size_t k = 0; k < tasks.size(); ++k) if (! taskDone (tasks[k])) return (int) k;
        return -1;
    }
    int guidedCurrent() const   // the speaker of the task now
    {
        const auto tasks = guidedTasks();
        const int k = guidedCurrentIndex();
        return k >= 0 ? tasks[(size_t) k].spk : -1;
    }

    std::vector<GuideSpot> guidedSpots() override
    {
        std::vector<GuideSpot> out;
        const auto tasks = guidedTasks();
        const int now = guidedCurrentIndex();
        const int micCount = proc.tuningMicCount();
        for (int k = 0; k < (int) tasks.size(); ++k)
        {
            const auto& t = tasks[(size_t) k];
            const int i = t.spk;
            const auto& sym = proc.plan.speakers[(size_t) i];
            GuideSpot sp;
            sp.id = t.id();
            sp.label = t.response ? nameOf (i) : cabinetName (i, t.cab) + " polarity";
            const float a = juce::degreesToRadians (sym.angle);
            const float side = 0.035f * (float) t.cab;   // the other cabinets beside the first
            sp.x = juce::jlimit (0.02f, 0.98f, sym.x + std::cos (a) * 0.06f - std::sin (a) * side);
            sp.y = juce::jlimit (0.02f, 0.98f, sym.y + std::sin (a) * 0.09f + std::cos (a) * side);
            sp.up = false;
            sp.aimAt = i;
            sp.state = k == now ? GuideSpot::Now : taskDone (t) ? GuideSpot::Done : GuideSpot::ToDo;
            sp.problem = taskProblem (t);
            if (sp.state == GuideSpot::Now)
            {
                sp.mic = sys.micFor (i, micCount);
                sp.speaker = i;
                if (t.response && sys.placement == 1)
                {
                    // one spot: the mic stays where it is for every speaker
                    sp.x = proc.plan.mics[(size_t) sp.mic].x;
                    sp.y = proc.plan.mics[(size_t) sp.mic].y;
                    sp.how = "stays in the same place for every speaker, aimed at " + nameOf (i);
                }
                else if (! t.response && sys.sources[(size_t) i].role == (int) SpeakerRole::Sub && sys.sources[(size_t) i].count > 1)
                    sp.how = "a few inches from this cabinet's cone, pointing at it (the others play too)";
                else sp.how = t.response ? "about 12 in (30 cm) from the cone, on its axis, pointing at it"
                                         : "about 12 in (30 cm) from the cone, pointing at it: a quick polarity check";
            }
            out.push_back (sp);
        }
        return out;
    }

    juce::String guidedHowTo() const override
    {
        const auto tasks = guidedTasks();
        const int k = guidedCurrentIndex();
        if (k < 0) return "Every speaker is measured and its polarity checked.";
        const auto& t = tasks[(size_t) k];
        const auto who = nameOf (t.spk);
        if (! t.response)
            return "Only " + who + "'s output plays. Polarity is compared with " + (sys.master (t.spk) != t.spk ? nameOf (sys.master (t.spk)) : who + "'s first cabinet")
                   + "; leave the processor's polarity for this output at normal.";
        if (sys.placement == 1)
            return "Only " + who + " plays. Bypass its crossover filters in the processor while it is measured. Keep the mic where it is.";
        return "Only " + who + " plays. Bypass its crossover filters, and leave its polarity at normal, while it is measured. Midway between "
               "the woofer and the horn on a two-way cabinet.";
    }

    bool guidedStart() override
    {
        const auto tasks = guidedTasks();
        const int k = guidedCurrentIndex();
        if (k < 0) { status = "Every speaker is measured and checked."; return false; }
        const auto t = tasks[(size_t) k];
        guidedNow = t.id();
        select (t.spk);
        if (t.response) startMeasure (t.spk);
        else startPolarity (t.spk, t.cab);
        return phase != Phase::Idle;
    }

    bool guidedCanSkip() const override
    {
        const auto tasks = guidedTasks();
        const int k = guidedCurrentIndex();
        return k >= 0 && ! tasks[(size_t) k].response;
    }
    void guidedSkip() override
    {
        const auto tasks = guidedTasks();
        const int k = guidedCurrentIndex();
        if (k < 0 || tasks[(size_t) k].response) return;
        if (auto* pc = sys.sources[(size_t) tasks[(size_t) k].spk].polarityOf (tasks[(size_t) k].cab))
        {
            *pc = {};
            pc->verdict = polarity::Skipped;
        }
        guidedNow = -1;
        systemChanged (false);
    }
    void guidedStop() override { cancelMeasure ("Stopped."); }
    bool guidedBusy() const override { return phase != Phase::Idle; }
    float guidedProgress() const override { return progress; }
    juce::String guidedPhase() const override { return phaseText(); }
    int guidedSerial() const override { return measureSerial; }
    juce::String guidedStatus() const override { return status; }
    void guidedRedo (const std::vector<GuideSpot>& was) override { if (! was.empty()) guidedNow = was.front().id; }
    void guidedClosed() override { guidedNow = -1; resized(); repaint(); }

    // Close up, each speaker can be measured with its own mic: picked in the pop-up for the speaker now.
    juce::StringArray guidedMicChoices() const override
    {
        juce::StringArray out;
        if (sys.placement != 0) return out;
        for (int m = 0; m < proc.tuningMicCount(); ++m) out.add (proc.mics.nameOf (m));
        return out;
    }
    int guidedMic() const override { return sys.micFor (guidedCurrent(), settings.micCount.load()); }
    void setGuidedMic (int m) override
    {
        const int i = guidedCurrent();
        if (i < 0 || m < 0 || sys.placement != 0) return;
        sys.sources[(size_t) i].measureMic = m;
        sys.measureMic = m;   // the next speakers start with it
        if (i == selected) micBox.setSelectedItemIndex (m, juce::dontSendNotification);
        systemChanged (false);
    }
    void findCrossover() { findBest(); }
    bool busy() const { return phase != Phase::Idle || searching; }
    void newAlignment() { addAlignment(); }
    void measureAlignment (int i) { selectAlignment (i); startArrival (i); }
    void applyAlignmentNow() { applyAlignment(); }
    void checkAlignment (int i) { selectAlignment (i); startCheck (i); }
    juce::String statusText() const { return status; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, systemCard.toFloat(), "Speakers");
        drawPanel (g, selCard.toFloat(), "Selected speaker", current() != nullptr ? sourceColour (selected) : juce::Colour());
        paintStepCards (g);
        drawPanel (g, summaryCard.toFloat(), "Settings for your processor");

        auto label = [&] (juce::Rectangle<int> r, const juce::String& t)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            g.drawText (t.toUpperCase(), r, juce::Justification::centredLeft, true);
        };
        for (int i = 0; i < (int) labels.size(); ++i)
        {
            bool hidden = false;
            for (int st = 0; st < 3; ++st)
                hidden = hidden || ((mode == 0 || st != openStep) && i >= labelFrom[st] && i < labelTo[st]);
            if (hidden) continue;
            label (labels[(size_t) i].first, labels[(size_t) i].second);
        }

        // selected speaker info
        if (const auto* s = current())
        {
            g.setColour (s->hasResponse() ? p.textDim : p.warn);
            g.setFont (font (11.5f));
            juce::String t;
            if (s->hasResponse()) t << (s->imported ? "Imported: " : "Measured: ") << s->source;
            else if (sys.master (selected) != selected)
                t << "Follows " << nameOf (sys.master (selected)) << ": same settings, no measurement of its own needed.";
            else t << "Not measured yet.";
            if (s->hasArrival) t << "\nArrives " << juce::String (s->arrivalMs, 2) << " ms after the reference";
            g.drawFittedText (t, selInfo, juce::Justification::topLeft, 3);
            const auto pol = polarityLine (selected, true);
            if (pol.first.isNotEmpty())
            {
                g.setColour (pol.second);
                g.setFont (font (11.5f, true));
                g.drawFittedText (pol.first, selInfo.withTrimmedTop (selInfo.getHeight() - 16), juce::Justification::topLeft, 1);
            }
        }

        // measurement guidance and status
        if (mode == 1)
        {
            paintGuide (g);
            juce::String t;
            juce::Colour c = p.textDim;
            if (sys.placement == 1)
            {
                if (sys.timingLocked) t << "Timing locked at " << juce::String (sys.lockedDelayMs, 2) << " ms for " << proc.mics.nameOf (sys.measureMic) << ".";
                else t << "Timing is found on the first measurement, then locked so every speaker keeps its true arrival.";
            }
            else t << "Each speaker is timed to its own arrival; line them up afterwards in Time alignment.";
            const auto pol = polarityLine (selected, false);
            if (pol.first.isNotEmpty()) { t = pol.first; c = pol.second; }
            if (phase != Phase::Idle) { t = phaseText(); c = p.accent; }
            g.setColour (c);
            g.setFont (font (11.5f, phase != Phase::Idle));
            g.drawFittedText (t, measureInfo, juce::Justification::topLeft, 4);
            if (phase != Phase::Idle)
            {
                auto bar = measureInfo.removeFromBottom (5).toFloat();
                g.setColour (p.border);
                g.fillRoundedRectangle (bar, 2.5f);
                g.setColour (p.accent);
                g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, progress)), 2.5f);
            }
        }

        if (mode == 1) paintQuality (g);
        if (mode == 3) paintAlignment (g);
        if (mode == 0)
        {
            g.setColour (p.textFaint);
            g.setFont (font (11.5f));
            g.drawFittedText ("Build the system here: each speaker, its role, channel and output. Measure them on 1 Speaker response.",
                              modeHint, juce::Justification::topLeft, 3);
        }

        // crossover result
        if (mode == 2 && ! invertedArea.isEmpty())
        {
            g.setColour (p.warn.withAlpha (0.14f));
            g.fillRoundedRectangle (invertedArea.toFloat().expanded (4.0f, 0.0f), 5.0f);
            g.setColour (p.warn);
            g.setFont (font (11.5f, true));
            g.drawFittedText (invertedText(), invertedArea, juce::Justification::centredLeft, 2);
        }
        if (mode == 2)
        {
            g.setFont (font (11.5f));
            juce::String t;
            juce::Colour c = p.textDim;
            const auto stray = sys.strayCrossoverFilters();
            if (! sys.xoNeeded())
            {
                t = sys.hasSub() ? juce::String ("Not needed: the system has only subs. A crossover goes between a top and a sub.")
                                 : juce::String ("Not needed: there is no sub in the system. A crossover goes between a top and a sub; left and right "
                                                 "mains are two channels, not two bands. Add a sub on the Speakers page if you have one.");
                if (! stray.isEmpty()) t << "\n" << stray.joinIntoString (", ") << " still " << (stray.size() == 1 ? "has" : "have")
                                         << " crossover filters from an earlier pair: remove them below.";
                c = stray.isEmpty() ? p.textDim : p.warn;
            }
            else if (searching) { t = "Searching every filter, frequency, delay and polarity..."; c = p.accent; }
            else if (! sys.pairValid()) t = "Pick two different speakers.";
            else if (sys.pairInverted()) { t = "The pair is upside down: swap it, then find the crossover."; c = p.warn; }
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
                if (sys.pairNearField()) t << "\nMeasured close up: the delay between them is set in Time alignment.";
                c = p.text;
            }
            else if (! sys.lastResult.note.empty()) { t = juce::String (sys.lastResult.note); c = p.warn; }
            else t = xoManual ? juce::String ("Set the crossover by hand. Automatic finds the best one for you.")
                              : juce::String ("Pick the filters your processor offers, then find the best crossover.");
            if (xoManual && sys.lastResult.valid && sys.xoNeeded())
            {
                const auto& lo = sys.sources[(size_t) sys.xoLow].proc;
                t = "By hand, starting from the automatic result. Low: " + juce::String (lo.delayMs, 2) + " ms, "
                    + signedText ((float) lo.gainDb, 1) + " dB" + (lo.invert ? ", inverted." : ".");
                c = p.text;
            }
            g.setColour (c);
            g.drawFittedText (t, xoInfo, juce::Justification::topLeft, 3);

            if (liveValid)
            {
                const auto q = juce::jlimit (0.0, 1.0, liveEff);
                const juce::Colour qc = q > 0.9 ? p.good : q > 0.75 ? p.warn : p.bad;
                g.setColour (qc);
                g.setFont (font (12.0f, true));
                g.drawText ("Sum through the crossover: " + juce::String (juce::roundToInt (q * 100.0)) + "% in phase, "
                                + juce::String (liveDev, 1) + " dB from flat"
                                + (liveReliable < 0.8 ? juce::String (" (") + juce::String (juce::roundToInt (liveReliable * 100.0)) + "% reliable)" : juce::String()),
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
        juce::Rectangle<int> right;
        if (mode > 0)
        {
            right = r.removeFromRight (360);
            r.removeFromRight (10);
        }

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
            field ("Name", nameEditor, 26);
            field ("Model", modelEditor, 26);
            field ("Role", roleBox, 26);
            field ("Channel", channelBox, 26);
            field ("Output", outputBox, 26);
            {
                auto row = c.removeFromTop (26);
                labels.push_back ({ row.removeFromLeft (64), "Count" });
                countBox.setBounds (row.removeFromLeft (row.getWidth() * 45 / 100));
                row.removeFromLeft (6);
                linkBox.setBounds (row);
                c.removeFromTop (8);
            }
            c.removeFromTop (4);
            auto row = c.removeFromTop (28);
            importButton.setBounds (row.removeFromLeft (row.getWidth() * 62 / 100));
            row.removeFromLeft (8);
            clearButton.setBounds (row);
            c.removeFromTop (10);
            const auto* s = current();
            offsetSlider.setVisible (s != nullptr && s->imported);
            if (offsetSlider.isVisible()) field ("Arrival", offsetSlider);
            selInfo = c.removeFromTop (54);
            modeHint = mode == 0 ? c.removeFromTop (48) : juce::Rectangle<int>();
        }

        // ---- right: this step (speaker response, crossover or time alignment) ----
        for (int i = 0; i < 3; ++i)
        {
            stepCard[i] = mode > 0 && i == openStep ? right : juce::Rectangle<int>();
            stepHeader[i] = stepCard[i].withHeight (kHeaderH);
        }
        const auto content = right.isEmpty() ? juce::Rectangle<int> (0, 0, 300, 600) : right.reduced (12, 0).withTrimmedTop (kHeaderH + 6);
        labelFrom[0] = (int) labels.size();
        {
            auto c = content;
            auto row = c.removeFromTop (28);
            labels.push_back ({ row.removeFromLeft (44), "Mic" });
            if (sys.placement == 1) { relockButton.setBounds (row.removeFromRight (82)); row.removeFromRight (8); }
            micBox.setBounds (row);
            c.removeFromTop (8);
            auto pr = c.removeFromTop (28);
            labels.push_back ({ pr.removeFromLeft (44), "Where" });
            placementBox.setBounds (pr);
            c.removeFromTop (10);
            guideArea = c.removeFromTop (118);
            c.removeFromTop (8);
            {
                auto mrow = c.removeFromTop (36);
                guidedToggle.setBounds (mrow.removeFromRight (92));
                mrow.removeFromRight (6);
                measureButton.setBounds (mrow);
            }
            c.removeFromTop (6);
            polarityButton.setBounds (c.removeFromTop (28));
            c.removeFromTop (8);
            measureInfo = c.removeFromTop (40);
            c.removeFromTop (8);
            labels.push_back ({ c.removeFromTop (16), "Signal quality" });
            c.removeFromTop (4);
            qualityArea = c.removeFromTop (40);
            c.removeFromTop (6);
            auto mr = c.removeFromTop (28);
            labels.push_back ({ mr.removeFromLeft (130), "Minimum coherence" });
            minCohBox.setBounds (mr);
            c.removeFromTop (12);
            reliabilityArea = c;
        }
        labelTo[0] = labelFrom[2] = (int) labels.size();
        layoutAlignment (content);
        labelTo[2] = labelFrom[1] = (int) labels.size();
        {
            // Crossover: the pair, then either the automatic search or the settings by hand. Without a sub there is
            // nothing to cross over: only a note (and the button to remove filters left from an earlier pair).
            auto c = content;
            const bool needed = sys.xoNeeded();
            liveLine = {};
            if (! needed)
            {
                xoInfo = c.removeFromTop (70);
                c.removeFromTop (6);
                removeXoButton.setBounds (c.removeFromTop (32));
                invertedArea = {};
            }
            else
            {
                auto pair = c.removeFromTop (28);
                labels.push_back ({ pair.removeFromLeft (40), "High" });
                const int boxW = (pair.getWidth() - 40 - 36) / 2;
                highBox.setBounds (pair.removeFromLeft (boxW));
                swapPairButton.setBounds (pair.removeFromLeft (40).reduced (5, 0));
                labels.push_back ({ pair.removeFromLeft (36), "Low" });
                lowBox.setBounds (pair);
                c.removeFromTop (8);
                if (sys.pairInverted())
                {
                    auto w = c.removeFromTop (30);
                    invertedSwapButton.setBounds (w.removeFromRight (70).withSizeKeepingCentre (70, 26));
                    w.removeFromRight (8);
                    invertedArea = w;
                    c.removeFromTop (8);
                }
                else invertedArea = {};
                {
                    auto sw = c.removeFromTop (30);
                    autoButton.setBounds (sw.removeFromLeft (sw.getWidth() / 2));
                    manualButton.setBounds (sw);
                }
                c.removeFromTop (10);
                if (! xoManual)
                {
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
                    xoInfo = c.removeFromTop (64);
                    liveLine = c.removeFromTop (22);
                }
                else
                {
                    xoInfo = c.removeFromTop (40);
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
                }
                if (! sys.strayCrossoverFilters().isEmpty()) { c.removeFromTop (6); removeXoButton.setBounds (c.removeFromTop (30)); }
            }
            labelTo[1] = (int) labels.size();
        }
        setStepVisibility();

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
        micBox.setSelectedItemIndex (sys.micFor (selected, settings.micCount.load()), juce::dontSendNotification);
        graph.setReliabilityView (selected, settings.coherenceBlank.load());
        graph.rebuild();
        refreshSelected();
        resized();
        repaint();
    }

    void refreshSelected()
    {
        const auto* s = current();
        for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &roleBox, &channelBox, &outputBox, &measureButton, &importButton, &clearButton,
                                                                 &modelEditor, &countBox, &linkBox })
            c->setEnabled (s != nullptr);
        removeButton.setEnabled (sys.sources.size() > 1);
        addButton.setEnabled ((int) sys.sources.size() < SpeakerSystem::kMaxSources);
        if (s == nullptr) return;
        if (! nameEditor.hasKeyboardFocus (true)) nameEditor.setText (s->name, juce::dontSendNotification);
        roleBox.setSelectedItemIndex (s->role, juce::dontSendNotification);
        channelBox.setSelectedItemIndex (s->channel, juce::dontSendNotification);
        if (! modelEditor.hasKeyboardFocus (true)) modelEditor.setText (s->model, juce::dontSendNotification);
        modelEditor.applyColourToAllText (pal().text);
        countBox.setSelectedId (juce::jlimit (1, 8, s->count), juce::dontSendNotification);
        linkBox.clear (juce::dontSendNotification);
        linkBox.addItem ("Own settings", 1);
        for (int i = 0; i < (int) sys.sources.size(); ++i)
            if (i != selected && sys.sources[(size_t) i].linkTo < 0) linkBox.addItem ("Same as " + nameOf (i), i + 2);
        linkBox.setSelectedId (s->linkTo >= 0 ? s->linkTo + 2 : 1, juce::dontSendNotification);
        outputBox.setSelectedItemIndex (juce::jlimit (0, nOuts, s->output), juce::dontSendNotification);
        offsetSlider.setValue (s->offsetMs, juce::dontSendNotification);
        clearButton.setEnabled (s->hasResponse());
        refreshMeasureButton();
    }

    // Speakers come in the usual shapes: a left / right pair shares its settings, subs, fills, delays.
    void showAddMenu()
    {
        const int room = SpeakerSystem::kMaxSources - (int) sys.sources.size();
        juce::PopupMenu m;
        m.addItem (1, "Left / right pair (mains, sharing settings)", room >= 2);
        m.addItem (2, "Sub", room >= 1);
        m.addItem (3, "Front fill", room >= 1);
        m.addItem (4, "Delay speaker", room >= 1);
        m.addItem (5, "Centre", room >= 1);
        m.addItem (6, "Other speaker", room >= 1);
        juce::Component::SafePointer<SpeakersPage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addButton), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& system = safe->sys;
            const int outs = juce::jmax (1, safe->proc.numOutputs());
            auto nextOut = [&system, outs]
            {
                for (int o = 1; o <= outs; ++o)
                {
                    bool used = false;
                    for (const auto& s : system.sources) used = used || s.output == o;
                    if (! used) return o;
                }
                return outs;
            };
            auto add = [&] (const juce::String& name, SpeakerRole role, SpeakerChannel ch, int link = -1)
            {
                SpeakerSource s;
                s.name = name;
                s.role = (int) role;
                s.channel = (int) ch;
                s.output = nextOut();
                s.linkTo = link;
                system.sources.push_back (s);
                return (int) system.sources.size() - 1;
            };
            switch (r)
            {
                case 1: { const int l = add ("Main L", SpeakerRole::Main, SpeakerChannel::Left); add ("Main R", SpeakerRole::Main, SpeakerChannel::Right, l); break; }
                case 2: add ("Sub", SpeakerRole::Sub, SpeakerChannel::Mono); break;
                case 3: add ("Front fill", SpeakerRole::Fill, SpeakerChannel::Mono); break;
                case 4: add ("Delay", SpeakerRole::Delay, SpeakerChannel::Mono); break;
                case 5: add ("Centre", SpeakerRole::Main, SpeakerChannel::Centre); break;
                default: add ("Speaker " + juce::String (system.sources.size() + 1), SpeakerRole::Other, SpeakerChannel::Mono); break;
            }
            system.ensurePair();
            safe->select ((int) system.sources.size() - (r == 1 ? 2 : 1));
            safe->systemChanged (true);
        });
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
            safe->proc.plan.speakerRemoved (i);
            safe->selected = juce::jmax (0, i - 1);
            safe->systemChanged (true);
        });
    }

    // Something about the sources changed: rebuild what depends on them.
    void systemChanged (bool responses)
    {
        if (responses) { sys.lastResult = {}; }
        sys.syncLinks();
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
        resized();   // the "swap them?" warning comes and goes
        refreshManual();
        graph.rebuild();
        changedOutside();
    }

    juce::String invertedText() const
    {
        auto word = [] (int role)
        {
            static const char* w[] = { "top", "Sub", "fill", "delay", "speaker of another kind" };
            return juce::String (w[juce::jlimit (0, 4, role)]);
        };
        return "HIGH is a " + word (sys.sources[(size_t) sys.xoHigh].role) + " and LOW is a " + word (sys.sources[(size_t) sys.xoLow].role)
               + " - swap them?";
    }

    void swapPair()
    {
        if (! sys.pairValid()) return;
        sys.swapPair();
        status = nameOf (sys.xoHigh) + " is now above the crossover and " + nameOf (sys.xoLow) + " below it.";
        refreshChoices();
        xoChanged();
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
        proc.noteAsFound();
        auto& hi = sys.sources[(size_t) sys.xoHigh].proc;
        auto& lo = sys.sources[(size_t) sys.xoLow].proc;
        xo::Filter f;
        f.type = typeBox.getSelectedItemIndex() + 1;
        f.slope = juce::jmax (6, slopeBox.getSelectedId());
        f.freq = freqSlider.getValue();
        hi.hp = f;
        lo.lp = f;
        const double d = delaySlider.getValue();
        sys.xoRelDelayMs = d;
        hi.delayMs = d < 0.0 ? -d : 0.0;
        lo.delayMs = d > 0.0 ? d : 0.0;
        lo.gainDb = gainSlider.getValue();
        hi.invert = false;
        lo.invert = invertButton.getToggleState();
        invertButton.setButtonText (lo.invert ? "Low inverted" : "Low normal");
        sys.syncLinks();
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
        const double cost = xo::assess (h, l, ph, pl, ph.hp.freq, eff, settings.coherenceBlank.load(), &liveReliable);
        liveEff = eff;
        liveDev = juce::jmax (0.0, cost - 1.5 * (1.0 - eff));
        liveValid = true;
        repaint();
    }

    // ---- automatic crossover search (on a worker thread) ----
    void findBest()
    {
        if (searching || ! sys.pairValid()) return;
        if (! sys.realPair())
        {
            status = ! sys.xoNeeded() ? juce::String ("No crossover: there is no sub in the system.")
                                      : juce::String ("A crossover goes between a top and a sub: pick one of each.");
            repaint();
            return;
        }
        if (sys.pairInverted())
        {
            status = invertedText();
            repaint();
            return;   // no settings from an upside-down pair
        }
        const auto grid = xo::analysisGrid();
        auto h = sys.rawResponse (sys.xoHigh, grid), l = sys.rawResponse (sys.xoLow, grid);
        if (h.empty() || l.empty()) { repaint(); return; }
        searching = true;
        findButton.setEnabled (false);
        repaint();
        const auto caps = sys.caps;
        auto opt = sys.opt;
        opt.minCoherence = settings.coherenceBlank.load();
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
        proc.noteAsFound();
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
            sys.xoRelDelayMs = r.delayLowMs;
        }
        sys.syncLinks();
        refreshManual();
        graph.rebuild();
        changedOutside();
        if (r.valid && alignmentNeeded() && ! alignmentDone())
            status = "Crossover set. Next: line the speakers up in Time alignment.";
    }

    // ---- measuring one source ----
    // A polarity check of one cabinet: measured like a close-up response (timed to its own arrival), but only its
    // polarity is kept. For speakers set "Same as" another, and for the other cabinets on one output.
    void startPolarity (int i, int cab)
    {
        if (i < 0 || i >= (int) sys.sources.size()) return;
        startMeasure (i);
        if (phase == Phase::Idle) return;
        polarityMode = true;
        polarityCab = juce::jlimit (0, juce::jmax (0, sys.sources[(size_t) i].count - 1), cab);
        measureNear = true;
    }

    // A speaker can only be measured on its own when nothing else plays from its output: "All outputs" plays every
    // speaker, and two speakers on one output play together. Says what to change, or empty when it is fine.
    juce::String outputProblem (int i) const
    {
        if (i < 0 || i >= (int) sys.sources.size() || sys.sources.size() < 2) return {};
        const auto& s = sys.sources[(size_t) i];
        if (s.output == 0)
            return nameOf (i) + " plays from All outputs, so every speaker plays while it is measured. Pick the output that feeds "
                   + nameOf (i) + " (Speakers page, Output), then measure.";
        for (int k = 0; k < (int) sys.sources.size(); ++k)
            if (k != i && sys.sources[(size_t) k].output == s.output)
                return nameOf (i) + " and " + nameOf (k) + " are both on output " + juce::String (s.output)
                       + ", so they play together. Give each its own output (Speakers page, Output), then measure.";
        return {};
    }

    void startMeasure (int i)
    {
        if (i < 0 || i >= (int) sys.sources.size()) return;
        if (const auto why = outputProblem (i); why.isNotEmpty()) { status = why; repaint(); return; }
        polarityMode = false;
        peakKnown = false;
        if (settings.refSource.load() == 0)
        {
            status = "Set a reference on Mic setup (the loopback, or the generator) to measure speakers.";
            repaint();
            return;
        }
        proc.noteAsFound();
        measuring = i;
        measureNear = sys.placement == 0;
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        proc.setAimOverride (Cal0);   // the mic is pointed at the speaker: use its 0 degree file
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
        proc.setAimOverride (-1);
        if (phase == Phase::FindingTime || phase == Phase::Averaging)
            for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        phase = Phase::Idle;
        measuring = -1;
        arrivalMode = false;
        polarityMode = false;
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
        if (polarityMode && phase != Phase::Idle && measuring >= 0)
            return phase == Phase::Settling ? "Playing pink noise from " + nameOf (measuring) + "..." : "Checking the polarity of " + cabinetName (measuring, polarityCab) + "...";
        const auto who = nameOf (measuring);
        switch (phase)
        {
            case Phase::Settling: return arrivalMode ? "Playing pink noise through " + who + " (arrival at " + spotName() + ")..."
                                                     : "Playing pink noise through " + who + "...";
            case Phase::FindingTime: return arrivalMode ? "Finding when " + who + " arrives at " + spotName() + "..." : sys.timingLocked ? "Finding the arrival of " + who + "..." : "Finding the timing (first measurement)...";
            case Phase::Averaging: return "Averaging " + who + " at " + proc.mics.nameOf (arrivalMode ? sys.measureMic : sys.micFor (measuring, settings.micCount.load())) + "...";
            case Phase::Idle:
            default: return {};
        }
    }

    void timerCallback() override
    {
        if (proc.numOutputs() != nOuts) refreshChoices();
        if (++qualityTick % 4 == 0)
        {
            const double before = liveQuality;
            updateLiveQuality();
            if (openStep == 0 && (liveQuality >= 0.0 || std::abs (before - liveQuality) > 1e-9)) repaint (stepCard[0]);
        }
        if (std::abs (settings.coherenceBlank.load() - shownMinCoh) > 0.001)   // changed in Settings
        {
            shownMinCoh = settings.coherenceBlank.load();
            refreshChoices();
        }
        if (phase == Phase::Idle) return;
        const double elapsed = juce::Time::getMillisecondCounterHiRes() - phaseStart;
        const int mic = arrivalMode ? juce::jlimit (0, settings.micCount.load() - 1, sys.measureMic)
                                    : sys.micFor (measuring, settings.micCount.load());

        if (! settings.genOn.load() && ! musicNow()) { cancelMeasure ("Stopped: the generator was turned off."); return; }

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
                    {
                        a->checkFreq = sys.overlapFreq (a->ref, a->align);
                        if (checkMode)
                        {
                            if (isXoPair (*a) && a->refTf.freq.size() > 8 && a->alignTf.freq.size() > 8)
                            {
                                // the crossover pair: compare transfer functions (the room's reverberation does not blur them)
                                settings.micDelayMs[mic] = (float) a->lockMs;
                                settings.requestResetAverages = true;
                                sawReset = false;
                                overflowsSeen = proc.analyzer.overflowCount();
                                restarts = 0;
                                phase = Phase::Averaging;
                                phaseStart = juce::Time::getMillisecondCounterHiRes();
                                return;
                            }
                            a->sumBandDb = bandLevel (mic, a->checkFreq);
                            finishCheck();
                            return;
                        }
                        (arrivalWhich == 0 ? a->refLevelDb : a->alignLevelDb) = snap->mics[(size_t) mic].rawZSlowDbfs;
                        (arrivalWhich == 0 ? a->refBandDb : a->alignBandDb) = bandLevel (mic, a->checkFreq);
                    }
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
                if (arrivalWhich == 0) { a->hasRef = true; a->refArrivalMs = rep.ms[(size_t) mic]; a->refDelayAtMeasure = sys.sources[(size_t) a->ref].proc.delayMs; }
                else { a->hasAlign = true; a->alignArrivalMs = rep.ms[(size_t) mic]; a->alignDelayAtMeasure = sys.sources[(size_t) a->align].proc.delayMs; }
                if (isXoPair (*a))
                {
                    // the crossover pair: also its transfer function, timed the same way for both, to match their phase
                    if (arrivalWhich == 0 || a->lockMs <= 0.0) a->lockMs = rep.ms[(size_t) mic];
                    if (arrivalWhich == 0) a->alignTf = {};
                    settings.micDelayMs[mic] = (float) a->lockMs;
                    settings.requestResetAverages = true;
                    sawReset = false;
                    overflowsSeen = proc.analyzer.overflowCount();
                    restarts = 0;
                    phase = Phase::Averaging;
                    phaseStart = juce::Time::getMillisecondCounterHiRes();
                    return;
                }
                if (! arrivalQueue.empty()) nextArrival();
                else finishArrival();
                return;
            }
            if (rep.serial != delaySerial)
            {
                auto& s = sys.sources[(size_t) measuring];
                const bool valid = mic < rep.micCount && rep.valid[(size_t) mic];
                peakKnown = valid && rep.confidenceDb[(size_t) mic] >= 12.0;
                peakInverted = valid && rep.inverted[(size_t) mic];
                if (polarityMode) { /* a polarity check does not change the speaker's timing */ }
                else if (valid) { s.hasArrival = true; s.arrivalMs = rep.ms[(size_t) mic]; }
                else s.hasArrival = false;
                if (measureNear)
                {
                    // close up: timed to its own arrival
                    if (! valid)
                    {
                        for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
                        cancelMeasure ("No clear arrival at " + proc.mics.nameOf (mic) + ". Check that " + nameOf (measuring)
                                       + " is playing and the mic is close to it, then measure again.");
                        return;
                    }
                    nearDelayMs = rep.ms[(size_t) mic];
                    for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
                    settings.micDelayMs[mic] = (float) nearDelayMs;
                    settings.requestResetAverages = true;
                    sawReset = false;
                    overflowsSeen = proc.analyzer.overflowCount();
                    restarts = 0;
                    phase = Phase::Averaging;
                    phaseStart = juce::Time::getMillisecondCounterHiRes();
                    return;
                }
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
                overflowsSeen = proc.analyzer.overflowCount();
                restarts = 0;
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
            const int want = juce::jmax (4, settings.averages.load()) * (musicNow() ? 3 : 1);   // music is uneven: average longer
            const int frames = snap != nullptr && mic < (int) snap->mics.size() ? snap->mics[(size_t) mic].tfFrames : 0;
            // audio was dropped (the computer could not keep up): the average is not continuous, start it again
            if (proc.analyzer.overflowCount() != overflowsSeen)
            {
                overflowsSeen = proc.analyzer.overflowCount();
                if (++restarts > 3) { cancelMeasure ("The computer could not keep up with the analysis. Close other programs or use a smaller FFT size, then measure again."); return; }
                settings.requestResetAverages = true;
                sawReset = false;
                phaseStart = juce::Time::getMillisecondCounterHiRes();
            }
            // only frames averaged after the restart count (the analysis can be behind real time)
            if (! sawReset && frames < want) sawReset = true;
            progress = 0.25f + 0.75f * (sawReset ? juce::jlimit (0.0f, 1.0f, (float) frames / (float) want) : 0.0f);
            if (sawReset && frames >= want && elapsed > 1500.0) { if (arrivalMode) finishArrivalTf (mic); else finishMeasure (mic); }
            else if (elapsed > 30000.0)
            {
                if (sawReset && frames > 0) { if (arrivalMode) finishArrivalTf (mic); else finishMeasure (mic); }
                else cancelMeasure ("No transfer function: check the reference and that the speaker is playing.");
                return;
            }
        }
        repaint (stepCard[0]);
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
        if (polarityMode) { finishPolarity (mic, t); return; }
        auto& s = sys.sources[(size_t) measuring];
        s.response = std::move (t);
        s.imported = false;
        s.nearField = measureNear;
        if (measureNear) s.measureMic = mic;   // remember which mic measured it
        notePolarity (measuring, 0, mic, s.response);
        s.delayUsedMs = measureNear ? nearDelayMs : sys.lockedDelayMs;
        if (measureNear) for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        const int calAngle = proc.activeCalAngle (mic);
        s.source = proc.mics.nameOf (mic) + (calAngle >= 0 ? " (" + calAngleName (calAngle) + " cal)" : juce::String()) + ", "
                   + LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        proc.setAimOverride (-1);
        const auto who = nameOf (measuring);
        settings.genOn = false;
        phase = Phase::Idle;
        measuring = -1;
        refreshMeasureButton();
        // next speaker that still needs measuring
        for (int k = 0; k < (int) sys.sources.size(); ++k)
            if (! sys.covered (k)) { selected = k; break; }
        status = who + " measured.";
        ++measureSerial;
        guidedNow = -1;
        systemChanged (true);
        {
            const int idx = (int) (&s - sys.sources.data());
            const double rel = idx < (int) reliability.size() ? reliability[(size_t) idx] : -1.0;
            if (rel >= 0.0 && rel < 0.8)
                status = who + " measured, but only " + juce::String (juce::roundToInt (rel * 100.0))
                         + "% of it is reliable: raise the level or reduce the noise and measure again.";
        }
        if (allMeasured()) status = "Every speaker is measured. Next: Crossover.";
        status << " " << polaritySentence (idxOf (s), 0);
    }

    // The button: the next cabinet of the selected speaker not checked yet (or the first again). A speaker measured on
    // its own with one cabinet was checked with its measurement: measure it again to check again.
    void checkPolarityOfSelected()
    {
        if (phase != Phase::Idle || selected < 0 || selected >= (int) sys.sources.size()) return;
        const auto& s = sys.sources[(size_t) selected];
        const bool own = sys.master (selected) == selected;
        if (own && ! s.hasResponse()) { status = "Measure " + nameOf (selected) + " first: its polarity is checked with the measurement."; repaint(); return; }
        if (own && s.count == 1)
        {
            const auto t = polaritySentence (selected, 0);
            status = t.isNotEmpty() ? t + " (from its measurement; measure it again to check again)" : "Measure " + nameOf (selected) + " again to check its polarity.";
            repaint();
            return;
        }
        int cab = -1;
        for (int c = own ? 1 : 0; c < s.count && cab < 0; ++c)
            if (const auto* pc = s.polarityOf (c); pc == nullptr || ! pc->done()) cab = c;
        if (cab < 0) cab = own ? 1 : 0;
        startPolarity (selected, cab);
    }

    // The selected speaker's polarity in a line (short: for the speaker card), and its colour.
    std::pair<juce::String, juce::Colour> polarityLine (int i, bool shortForm) const
    {
        const auto& p = pal();
        if (i < 0 || i >= (int) sys.sources.size()) return {};
        const auto& s = sys.sources[(size_t) i];
        const int v = s.polaritySummary(), n = s.polarityChecked();
        if (n == 0)
        {
            if (sys.master (i) != i || s.count > 1)
                return { shortForm ? juce::String ("Polarity: not checked") : "Polarity not checked yet: press Check polarity of " + nameOf (i) + ".", p.textDim };
            return {};
        }
        if (s.count == 1)
        {
            if (shortForm) return { juce::String ("Polarity: ") + polarity::verdictName (v == polarity::Unknown ? s.polarityOf (0)->verdict : v),
                                    v == polarity::Normal ? p.good : v == polarity::Inverted ? p.bad : p.warn };
            return { polaritySentence (i, 0), v == polarity::Normal ? p.good : v == polarity::Inverted ? p.bad : p.warn };
        }
        juce::StringArray parts;
        for (int c = 0; c < s.count; ++c)
        {
            const auto* pc = s.polarityOf (c);
            parts.add ("cab " + juce::String (c + 1) + " " + (pc != nullptr ? polarity::verdictName (pc->verdict) : "not checked"));
        }
        const auto col = v == polarity::Normal ? p.good : v == polarity::Inverted ? p.bad : v == polarity::Unsure ? p.warn : p.textDim;
        return { "Polarity: " + parts.joinIntoString (", ") + (shortForm || v != polarity::Inverted ? juce::String() : ". Check the wiring of the inverted one."), col };
    }

    int idxOf (const SpeakerSource& s) const { return (int) (&s - sys.sources.data()); }

    // Works out a cabinet's polarity from a measurement `t` timed to its own arrival (or, from one spot, to the
    // locked timing) and the impulse sign from the timing search; compares it with the speaker it should match.
    void notePolarity (int i, int cab, int mic, const FreqTable& measured)
    {
        if (i < 0 || i >= (int) sys.sources.size()) return;
        auto& s = sys.sources[(size_t) i];
        auto* pc = s.polarityOf (cab);
        if (pc == nullptr) return;
        FreqTable t = measured;
        double maxTau = 1.0;
        if (! measureNear && s.hasArrival)
        {
            // from one spot: take out the time between the locked timing and this speaker's own arrival
            const double d = s.arrivalMs - sys.lockedDelayMs;
            for (size_t k = 0; k < t.freq.size(); ++k) t.phase[k] = std::remainder (t.phase[k] + 360.0 * t.freq[k] * d * 0.001, 360.0);
            maxTau = 2.0;
        }
        const double minCoh = juce::jmax (0.2f, settings.coherenceBlank.load());
        const auto own = polarity::absolute (t, minCoh, peakKnown, peakInverted, maxTau);
        *pc = {};
        pc->own = own.verdict;
        pc->verdict = own.verdict;
        pc->phaseDeg = own.band.phaseDeg;
        pc->peakInverted = peakInverted;
        pc->when = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        pc->mic = proc.mics.nameOf (mic);
        // against the speaker it should match: its "Same as" speaker, or its own first cabinet
        const int m = sys.master (i);
        const int refIdx = m != i ? m : (cab > 0 ? i : -1);
        if (refIdx >= 0)
        {
            const auto& ref = sys.sources[(size_t) refIdx];
            const auto* refPc = ref.polarityOf (0);
            if (ref.hasResponse() && ! ref.imported && ref.nearField)
            {
                const auto rel = polarity::relative (t, ref.response, minCoh);
                pc->against = refIdx;
                pc->relative = rel.verdict;
                const int refV = refPc != nullptr ? refPc->verdict : polarity::Unknown;
                if ((rel.verdict == polarity::Normal || rel.verdict == polarity::Inverted)
                    && (refV == polarity::Normal || refV == polarity::Inverted))
                {
                    const int implied = rel.verdict == polarity::Normal ? refV : (refV == polarity::Normal ? polarity::Inverted : polarity::Normal);
                    // the comparison with a matching speaker is the firmer test; a clear disagreement with its own reading is "check"
                    pc->verdict = (own.verdict == polarity::Normal || own.verdict == polarity::Inverted) && own.verdict != implied ? polarity::Unsure : implied;
                }
            }
        }
    }

    juce::String cabinetName (int i, int cab) const
    {
        const auto& s = sys.sources[(size_t) i];
        return s.count > 1 ? nameOf (i) + " cabinet " + juce::String (cab + 1) + " of " + juce::String (s.count) : nameOf (i);
    }

    // One sentence on a cabinet's polarity, for the status line.
    juce::String polaritySentence (int i, int cab) const
    {
        if (i < 0 || i >= (int) sys.sources.size()) return {};
        const auto* pc = sys.sources[(size_t) i].polarityOf (cab);
        if (pc == nullptr || ! pc->done()) return {};
        juce::String rel;
        if (pc->against >= 0 && pc->against < (int) sys.sources.size() && pc->against != i
            && (pc->relative == polarity::Normal || pc->relative == polarity::Inverted))
            rel = pc->relative == polarity::Normal ? " (matches " + nameOf (pc->against) + ")" : " (opposite to " + nameOf (pc->against) + ")";
        switch (pc->verdict)
        {
            case polarity::Normal: return "Polarity: normal" + rel + ".";
            case polarity::Inverted: return "Polarity looks INVERTED" + rel + ": check the wiring and that the processor's polarity for this output is normal.";
            case polarity::Unsure: return "Polarity: not clear" + rel + ". Measure again closer to the cone, or check with a polarity tester.";
            default: return {};
        }
    }

    void finishPolarity (int mic, const FreqTable& t)
    {
        const int i = measuring, cab = polarityCab;
        for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        proc.setAimOverride (-1);
        settings.genOn = false;
        phase = Phase::Idle;
        measuring = -1;
        polarityMode = false;
        notePolarity (i, cab, mic, t);
        ++measureSerial;
        guidedNow = -1;
        refreshMeasureButton();
        status = cabinetName (i, cab) + " checked. " + polaritySentence (i, cab);
        systemChanged (false);
    }

    // ---- guided steps on the right ----
    void openStepAt (int)   // each step is its own page now: nothing to open
    {
        resized();
        repaint();
    }

    void setStepVisibility()
    {
        const bool resp = mode == 1;
        for (auto* c : std::initializer_list<juce::Component*> { &micBox, &minCohBox, &placementBox, &measureButton, &guidedToggle, &polarityButton })
            c->setVisible (resp);
        relockButton.setVisible (resp && sys.placement == 1);
        const bool xo = mode == 2 && sys.xoNeeded();
        for (auto* c : std::initializer_list<juce::Component*> { &highBox, &lowBox, &swapPairButton, &autoButton, &manualButton })
            c->setVisible (xo);
        for (auto* c : std::initializer_list<juce::Component*> { &rangeBox, &findButton }) c->setVisible (xo && ! xoManual);
        for (auto* c : std::initializer_list<juce::Component*> { &freqSlider, &typeBox, &slopeBox, &delaySlider, &gainSlider, &invertButton })
            c->setVisible (xo && xoManual);
        invertedSwapButton.setVisible (xo && sys.pairInverted());
        for (auto& b : typeButtons) b.setVisible (xo && ! xoManual);
        for (auto& b : slopeButtons) b.setVisible (xo && ! xoManual);
        removeXoButton.setVisible (mode == 2 && ! sys.strayCrossoverFilters().isEmpty());
        autoButton.setToggleState (! xoManual, juce::dontSendNotification);
        manualButton.setToggleState (xoManual, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &alList, &alAddButton, &alRemoveButton, &alAlignBox, &alRefBox, &alSpotBox,
                                                                 &alExtraSlider, &alMeasureButton, &alSignalBox })
            c->setVisible (mode == 3);
        alApplyButton.setVisible (false);
        alRedoButton.setVisible (mode == 3 && currentAlignment() != nullptr && (currentAlignment()->hasRef || currentAlignment()->hasAlign));
    }

    bool allMeasured() const
    {
        if (sys.sources.empty()) return false;
        for (int i = 0; i < (int) sys.sources.size(); ++i) if (! sys.covered (i)) return false;
        return true;
    }

    bool crossoverSet() const { return sys.pairValid() && sys.sources[(size_t) sys.xoHigh].proc.hp.active(); }

    // Time alignment is needed when there are fills or delays, or separate left and right mains.
    bool alignmentNeeded() const
    {
        bool left = false, right = false, fill = false;
        for (const auto& src : sys.sources)
        {
            fill = fill || src.role == (int) SpeakerRole::Fill || src.role == (int) SpeakerRole::Delay;
            left = left || (src.role == (int) SpeakerRole::Main && src.channel == (int) SpeakerChannel::Left);
            right = right || (src.role == (int) SpeakerRole::Main && src.channel == (int) SpeakerChannel::Right);
        }
        juce::ignoreUnused (left, right);   // a symmetrical left / right pair needs no delay; aligning it stays optional
        return fill || sys.pairNearField();   // a crossover pair measured close up gets its timing here
    }

    bool alignmentDone() const
    {
        if (sys.alignments.empty()) return ! alignmentNeeded();
        for (const auto& a : sys.alignments)
        {
            if (! a.measured() || a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) return false;
            const double need = a.neededMs (sys.sources[(size_t) a.ref].proc.delayMs);
            if (need < -0.005 || std::abs (sys.sources[(size_t) a.align].proc.delayMs - need) >= 0.01) return false;
        }
        return true;
    }

    // Weakest reliability among the measured speakers (-1: none known).
    double weakestReliability() const
    {
        double w = 2.0;
        for (double r : reliability) if (r >= 0.0) w = std::min (w, r);
        return w > 1.5 ? -1.0 : w;
    }

    juce::String stepSummary (int st) const
    {
        if (st == 0)
        {
            if (phase != Phase::Idle) return phaseText();
            int measured = 0;
            for (int i = 0; i < (int) sys.sources.size(); ++i) measured += sys.covered (i) ? 1 : 0;
            juce::String t = proc.mics.nameOf (sys.micFor (selected, settings.micCount.load())) + "  -  " + juce::String (measured) + " of " + juce::String ((int) sys.sources.size()) + " measured";
            const double w = weakestReliability();
            if (w >= 0.0) t << "  -  " << juce::roundToInt (w * 100.0) << "% reliable";
            return t;
        }
        if (st == 1)
        {
            if (! crossoverSet()) return "Not set";
            const auto& f = sys.sources[(size_t) sys.xoHigh].proc.hp;
            return nameOf (sys.xoHigh) + " / " + nameOf (sys.xoLow) + "  -  " + xo::typeShort (f.type) + juce::String (f.slope) + " @ "
                   + freqLabel (f.freq) + " Hz";
        }
        if (sys.alignments.empty()) return alignmentNeeded() ? "Fills, delays or L / R to line up" : "Optional";
        int done = 0;
        for (const auto& a : sys.alignments)
            if (a.measured() && a.ref < (int) sys.sources.size() && a.align < (int) sys.sources.size()
                && std::abs (sys.sources[(size_t) a.align].proc.delayMs - a.neededMs (sys.sources[(size_t) a.ref].proc.delayMs)) < 0.01)
                ++done;
        return juce::String (done) + " of " + juce::String ((int) sys.alignments.size()) + " applied";
    }

    void paintStepCards (juce::Graphics& g)
    {
        const auto& p = pal();
        static const char* titles[] = { "Speaker response", "Crossover", "Time alignment" };
        const bool done[] = { allMeasured(), crossoverSet(), ! sys.alignments.empty() && alignmentDone() };
        const bool required[] = { true, true, alignmentNeeded() };
        for (int i = 0; i < 3; ++i)
        {
            if (stepCard[i].isEmpty()) continue;
            const auto card = stepCard[i].toFloat();
            const bool open = true, over = false;
            g.setColour (p.panel);
            g.fillRoundedRectangle (card, 8.0f);
            // done: green; required and not done: amber; open: accent
            juce::Colour edge = p.border;
            float width = 1.0f;
            if (! done[i] && required[i]) { edge = p.warn.withAlpha (0.6f); width = 1.4f; }
            else if (done[i]) { edge = p.good.withAlpha (0.5f); width = 1.2f; }
            if (open || over) { edge = p.accent.withAlpha (open ? 0.9f : 0.6f); width = 1.4f; }
            g.setColour (edge);
            g.drawRoundedRectangle (card.reduced (0.5f), 8.0f, width);

            auto h = stepHeader[i].toFloat().reduced (12.0f, 0.0f);
            const auto badge = h.removeFromLeft (22.0f).withSizeKeepingCentre (22.0f, 22.0f);
            if (done[i]) drawStatusBadge (g, badge, StepStatus::Ready);
            else
            {
                g.setColour (required[i] ? p.warn : p.textFaint);
                g.drawEllipse (badge.reduced (1.0f), 1.4f);
                g.setFont (font (11.5f, true));
                g.drawText (juce::String (i + 1), badge, juce::Justification::centred);
            }
            h.removeFromLeft (10.0f);
            g.setColour (p.text);
            g.setFont (font (14.0f, true));
            const juce::String title = titles[i];
            g.drawText (title, h, juce::Justification::centredLeft);
            const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), title) + 14.0f;
            g.setColour (i == 0 && phase != Phase::Idle ? p.accent : p.textDim);
            g.setFont (font (11.5f, i == 0 && phase != Phase::Idle));
            g.drawText (stepSummary (i), h.withTrimmedLeft (tw).withTrimmedRight (open ? 0.0f : 16.0f), juce::Justification::centredRight, true);
            if (! open)
            {
                juce::Path chev;   // closed: a chevron says "click to open"
                const float cx = h.getRight() - 4.0f, cy = h.getCentreY();
                chev.startNewSubPath (cx - 4.0f, cy - 2.0f);
                chev.lineTo (cx, cy + 2.0f);
                chev.lineTo (cx + 4.0f, cy - 2.0f);
                g.setColour (p.textDim);
                g.strokePath (chev, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                g.setColour (p.border);
                g.drawHorizontalLine (stepHeader[i].getBottom(), card.getX() + 12.0f, card.getRight() - 12.0f);
            }
        }
    }

    // Live signal quality at the measurement mic, and how reliable each measured speaker is.
    void paintQuality (juce::Graphics& g)
    {
        const auto& p = pal();
        auto q = qualityArea.toFloat();
        auto bar = q.removeFromTop (8.0f);
        g.setColour (p.border);
        g.fillRoundedRectangle (bar, 4.0f);
        juce::String t;
        juce::Colour c = p.textDim;
        if (liveQuality >= 0.0)
        {
            c = liveQuality >= 0.8 ? p.good : liveQuality >= 0.6 ? p.warn : p.bad;
            g.setColour (c);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, liveQuality)), 4.0f);
            t << juce::roundToInt (liveQuality * 100.0) << "% of the range is reliable right now"
              << (liveQuality >= 0.8 ? ": good." : liveQuality >= 0.6 ? ": fair, raise the level if you can." : ": poor, raise the level or reduce the noise.");
        }
        else if (liveQuality < -1.5) t = "Checking the signal... (once the speaker's timing is found)";
        else t = "Measure a speaker (or play the generator through it) to check the signal at the mic.";
        q.removeFromTop (6.0f);
        g.setColour (liveQuality >= 0.0 ? c : p.textDim);
        g.setFont (font (11.5f, liveQuality >= 0.0));
        g.drawFittedText (t, q.toNearestInt(), juce::Justification::topLeft, 2);

        // each measured speaker
        auto r = reliabilityArea;
        g.setFont (font (11.0f, true));
        g.setColour (p.textDim);
        g.drawText ("RELIABILITY OF EACH MEASUREMENT", r.removeFromTop (16), juce::Justification::centredLeft);
        r.removeFromTop (4);
        for (int i = 0; i < (int) sys.sources.size() && r.getHeight() >= 18; ++i)
        {
            auto line = r.removeFromTop (20);
            g.setColour (sourceColour (i));
            g.fillRoundedRectangle (line.removeFromLeft (4).toFloat().withSizeKeepingCentre (4.0f, 14.0f), 2.0f);
            line.removeFromLeft (8);
            g.setColour (p.text);
            g.setFont (font (12.0f));
            g.drawText (nameOf (i), line, juce::Justification::centredLeft, true);
            const double rel = i < (int) reliability.size() ? reliability[(size_t) i] : -1.0;
            juce::String v;
            juce::Colour vc = p.textDim;
            if (! sys.sources[(size_t) i].hasResponse() && sys.master (i) != i) { v = "as " + nameOf (sys.master (i)); vc = p.textDim; }
            else if (! sys.sources[(size_t) i].hasResponse()) { v = "not measured"; vc = p.warn; }
            else if (rel < 0.0) v = "imported (no coherence)";
            else { v = juce::String (juce::roundToInt (rel * 100.0)) + "%"; vc = rel >= 0.8 ? p.good : rel >= 0.6 ? p.warn : p.bad; }
            g.setColour (vc);
            g.setFont (font (12.0f, true));
            g.drawText (v, line, juce::Justification::centredRight, true);
        }
        if (r.getHeight() >= 30)
        {
            g.setColour (p.textFaint);
            g.setFont (font (11.0f));
            g.drawFittedText ("Share of each speaker's range measured with at least the minimum coherence. Below 80%, measure that "
                              "speaker again with more level or less noise.", r.removeFromTop (44), juce::Justification::topLeft, 3);
        }
    }

    // How much of the speaker's own range is reliable at the mic right now. Only judged once the measurement has its
    // timing and some averages (before that the mic and the reference are not lined up, and it would read poor), and
    // only where the speaker actually plays (within 20 dB of its loudest), smoothed so it does not jump.
    // -1: nothing playing; -2: playing, not ready to judge yet.
    void updateLiveQuality()
    {
        const int mic = phase != Phase::Idle && ! arrivalMode ? sys.micFor (measuring, settings.micCount.load())
                                                              : sys.micFor (selected, settings.micCount.load());
        if (snap == nullptr || mic >= (int) snap->mics.size() || ! settings.genOn.load()) { liveQuality = -1.0; return; }
        const auto& m = snap->mics[(size_t) mic];
        const bool ready = phase == Phase::Averaging && sawReset && m.tfFrames >= 4;
        if (! ready || m.tfCoh.size() != snap->tfFreq.size() || m.tfMag.size() != snap->tfFreq.size()) { liveQuality = phase == Phase::Idle ? -1.0 : -2.0; return; }
        // where the speaker plays: within 20 dB of its loudest (third-octave smoothed)
        const size_t n = snap->tfFreq.size();
        float peak = -300.0f;
        std::vector<float> sm (n, -300.0f);
        for (size_t i = 0; i < n; ++i)
        {
            float sum = 0.0f;
            int k = 0;
            for (size_t j = i > 12 ? i - 12 : 0; j < std::min (n, i + 13); ++j)
                if (std::isfinite (m.tfMag[j]) && snap->tfFreq[j] > snap->tfFreq[i] * 0.891 && snap->tfFreq[j] < snap->tfFreq[i] * 1.122) { sum += m.tfMag[j]; ++k; }
            if (k > 0) sm[i] = sum / (float) k;
            if (snap->tfFreq[i] >= 30.0 && snap->tfFreq[i] <= 16000.0) peak = std::max (peak, sm[i]);
        }
        const float minCoh = juce::jmax (0.2f, settings.coherenceBlank.load());
        int used = 0, good = 0;
        for (size_t i = 0; i < n; ++i)
        {
            if (snap->tfFreq[i] < 30.0 || snap->tfFreq[i] > 16000.0 || ! std::isfinite (m.tfCoh[i]) || sm[i] < peak - 20.0f) continue;
            ++used;
            good += m.tfCoh[i] >= minCoh ? 1 : 0;
        }
        if (used == 0) { liveQuality = -2.0; return; }
        const double now = (double) good / used;
        liveQuality = liveQuality >= 0.0 ? 0.75 * liveQuality + 0.25 * now : now;
    }

    void refreshReliability()
    {
        reliability.assign (sys.sources.size(), -1.0);
        const double minCoh = juce::jmax (0.2, (double) settings.coherenceBlank.load());
        for (size_t i = 0; i < sys.sources.size(); ++i)
            if (sys.sources[i].hasResponse()) reliability[i] = xo::usableReliability (sys.sources[i].response, minCoh);
        list.setReliability (reliability);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 3; ++i)
            if (stepHeader[i].contains (e.getPosition()) && i != openStep) { openStepAt (i); return; }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int h = -1;
        for (int i = 0; i < 3; ++i) if (stepHeader[i].contains (e.getPosition()) && i != openStep) h = i;
        if (h != hoverStep)
        {
            hoverStep = h;
            setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit (const juce::MouseEvent&) override { if (hoverStep >= 0) { hoverStep = -1; repaint(); } }

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
        // the crossover pair measured close up comes first: its timing is set here
        if (sys.pairNearField() && ! used (sys.xoHigh, sys.xoLow) && ! used (sys.xoLow, sys.xoHigh)) { a.ref = sys.xoHigh; a.align = sys.xoLow; }
        for (int i = 0; i < (int) src.size() && a.align < 0; ++i)
            if ((src[(size_t) i].role == (int) SpeakerRole::Fill || src[(size_t) i].role == (int) SpeakerRole::Delay) && ! used (mainIdx, i)) a.align = i;
        if (a.align < 0 && left >= 0 && right >= 0 && ! used (left, right)) { a.ref = left; a.align = right; }
        if (a.align < 0) a.align = mainIdx == 0 ? 1 : 0;
        const auto& al = src[(size_t) a.align];
        const bool pair = al.role == (int) SpeakerRole::Main;
        a.spot = al.role == (int) SpeakerRole::Sub ? juce::String ("FOH") : pair ? juce::String ("Centre") : al.role == (int) SpeakerRole::Fill ? juce::String ("Front rows")
                                                : al.role == (int) SpeakerRole::Delay ? juce::String ("Delay area") : juce::String ("Overlap");
        if (a.spot.isNotEmpty() && ! proc.positions.contains (a.spot)) proc.positions.add (a.spot);
        a.precedenceMs = al.role == (int) SpeakerRole::Delay ? 5.0 : 0.0;
        sys.alignments.push_back (a);
        alSelected = (int) sys.alignments.size() - 1;
        openStepAt (2);
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
        alApplyButton.setButtonText (need < -0.005 ? "Apply " + juce::String (-need, 2) + " ms to " + nameOf (a->ref) : applied ? "Applied: " + juce::String (need, 2) + " ms on " + nameOf (a->align)
                                                                               : "Apply " + juce::String (need, 2) + " ms to " + nameOf (a->align));
    }

    void applyAlignment()
    {
        proc.noteAsFound();
        auto* a = currentAlignment();
        if (a == nullptr || ! a->measured()) return;
        double need = a->neededMs (sys.sources[(size_t) a->ref].proc.delayMs);
        juce::String swapped;
        if (need < -0.005)
        {
            // the aligned speaker is already the later one: delay the other one instead
            std::swap (a->ref, a->align);
            std::swap (a->refArrivalMs, a->alignArrivalMs);
            std::swap (a->refLevelDb, a->alignLevelDb);
            std::swap (a->refBandDb, a->alignBandDb);
            std::swap (a->refTf, a->alignTf);
            std::swap (a->refDelayAtMeasure, a->alignDelayAtMeasure);
            a->xoOffsetMs = -a->xoOffsetMs;
            if (a->phaseValid) a->phaseDelayMs = -a->phaseDelayMs;
            swapped = " (" + nameOf (a->ref) + " was the later one, so " + nameOf (a->align) + " is delayed instead)";
            need = a->neededMs (sys.sources[(size_t) a->ref].proc.delayMs);
        }
        if (need >= -0.005)
        {
            sys.sources[(size_t) a->align].proc.delayMs = juce::jmax (0.0, need);
            status = nameOf (a->align) + " delay set to " + juce::String (juce::jmax (0.0, need), 2) + " ms" + swapped + ". Now check them together.";
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
        put ("Signal", alSignalBox);
        c.removeFromTop (6);
        alSteps = c.removeFromTop (4 * 22);
        c.removeFromTop (8);
        auto br2 = c.removeFromTop (36);
        alRedoButton.setBounds (br2.removeFromRight (96).withSizeKeepingCentre (96, 30));
        br2.removeFromRight (8);
        alMeasureButton.setBounds (br2);
        c.removeFromTop (8);
        alInfo = c;
    }

    void paintAlignment (juce::Graphics& g)
    {
        const auto& p = pal();
        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        g.drawFittedText (alSignal == 2
                              ? "Music from the console: send it to only the speaker each step names (mute the other on the console), "
                                "with the console's feed on the reference input. Put " + proc.mics.nameOf (sys.measureMic) + " where both speakers cover the seats."
                              : "Put " + proc.mics.nameOf (sys.measureMic) + " where both speakers cover the seats (centre line for left / right, "
                                "FOH for the main and sub). Measure with the processor set as shown here: filters, delays and polarity in place.",
                          alHint, juce::Justification::topLeft, 3);

        const auto* a = currentAlignment();
        // the four steps, with the current one highlighted
        if (a != nullptr)
        {
            const int stage = alignmentStage();
            const juce::String names[] = { "Play " + nameOf (a->ref) + " on its own", "Mute it, play " + nameOf (a->align) + " on its own",
                                           "Set the delay", "Unmute both and check" };
            auto r = alSteps;
            for (int i = 0; i < 4; ++i)
            {
                auto line = r.removeFromTop (22);
                const bool done = stage > i, now = stage == i || (i == 3 && stage == 4);
                const auto badge = line.removeFromLeft (18).toFloat().withSizeKeepingCentre (16.0f, 16.0f);
                if (done || (i == 3 && stage == 4)) drawStatusBadge (g, badge, StepStatus::Ready);
                else
                {
                    g.setColour (now ? p.accent : p.textFaint);
                    g.drawEllipse (badge.reduced (1.0f), 1.3f);
                    g.setFont (font (10.0f, true));
                    g.drawText (juce::String (i + 1), badge, juce::Justification::centred);
                }
                line.removeFromLeft (8);
                g.setColour (now ? p.text : done ? p.textDim : p.textFaint);
                g.setFont (font (12.0f, now));
                g.drawText (names[i], line, juce::Justification::centredLeft, true);
            }
        }

        juce::String t;
        juce::Colour c = p.textDim;
        if (a == nullptr) t = "Add an alignment: a fill or delay to the main, the sub to the main, or the right main to the left.";
        else if (a->ref != a->align && alignmentStage() <= 1)
        {
            const auto& sr = sys.sources[(size_t) (alignmentStage() == 0 ? a->ref : a->align)];
            const auto who = nameOf (alignmentStage() == 0 ? a->ref : a->align), other = nameOf (alignmentStage() == 0 ? a->align : a->ref);
            if (alignmentStage() == 0)
                t << "In your processor, unmute " << who << " and mute every other speaker. Press Play " << who << ": LiveAlign plays pink noise and finds when it arrives at " << spotOf (*a) << ".";
            else
                t << "Now mute " << other << " and unmute " << who << ". Press Play " << who << ".";
            t << (sr.output > 0 ? "\nThe generator plays output " + juce::String (sr.output) + "." : "\nThe generator plays all outputs: the processor's mutes pick the speaker.");
            c = p.text;
        }
        else if (a->ref != a->align && a->measured() && alignmentStage() >= 3)
        {
            const double need = a->neededMs (sys.sources[(size_t) a->ref].proc.delayMs);
            t << nameOf (a->align) << " delay " << juce::String (need, 2) << " ms is set. Enter it in the processor if you have not, unmute both and press Check.";
            if (a->checked())
            {
                const double got = a->sumGainDb(), ideal = a->idealSumGainDb();
                const bool ok = got >= ideal - 1.5;
                t << "\nTogether at " << freqLabel (a->checkFreq) << " Hz: " << signedText ((float) got, 1) << " dB over the louder one alone (in phase: up to "
                  << signedText ((float) ideal, 1) << " dB). " << (ok ? "They are lined up." : "Not fully adding: check the delay and the polarity.");
                c = ok ? p.good : p.warn;
            }
            else c = p.text;
        }
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
                t << "\n" << alName << " is already the later one: " << refName << " gets " << juce::String (-need, 2) << " ms instead.";
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
        for (const int who : { a->ref, a->align })
            if (const auto why = outputProblem (who); why.isNotEmpty()) { status = why; repaint(); return; }
        a->hasRef = a->hasAlign = false;
        a->sumBandDb = -200.0f;
        a->phaseValid = false;
        setXoOffset (*a);
        arrivalMode = true;
        checkMode = false;
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
        startSignal();
        settings.running = true;
        phase = Phase::Settling;
        phaseStart = juce::Time::getMillisecondCounterHiRes();
        settleUntil = proc.analyzer.samplesWritten() + (juce::uint64) (kSettleSeconds * rate());
        progress = 0.0f;
        refreshMeasureButton();
        repaint();
    }

    bool isXoPair (const Alignment& a) const
    {
        return sys.pairValid() && ((a.ref == sys.xoHigh && a.align == sys.xoLow) || (a.ref == sys.xoLow && a.align == sys.xoHigh));
    }

    // Average magnitude of a transfer function around f (1/3 octave either side), dB.
    static float tfBand (const FreqTable& t, double f)
    {
        double sum = 0.0;
        int n = 0;
        for (size_t i = 0; i < t.freq.size(); ++i)
            if (t.freq[i] >= f / 1.26 && t.freq[i] <= f * 1.26 && std::isfinite (t.db[i])) { sum += std::pow (10.0, t.db[i] / 20.0); ++n; }
        return n > 0 ? (float) (20.0 * std::log10 (sum / n)) : -200.0f;
    }

    // The transfer function of a crossover-pair speaker at the alignment spot (or of both together, for the check).
    void finishArrivalTf (int mic)
    {
        auto* a = alignmentAt (arrivalAlignment);
        if (a == nullptr || snap == nullptr || mic >= (int) snap->mics.size()) { cancelMeasure ("No data from the mic."); return; }
        if (checkMode)
        {
            FreqTable both;
            const auto& r0 = snap->mics[(size_t) mic];
            for (size_t i = 0; i < snap->tfFreq.size() && i < r0.tfMag.size(); ++i)
                if (std::isfinite (r0.tfMag[i])) { both.freq.push_back (snap->tfFreq[i]); both.db.push_back (r0.tfMag[i]); }
            for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
            a->refBandDb = tfBand (a->refTf, a->checkFreq);
            a->alignBandDb = tfBand (a->alignTf, a->checkFreq);
            a->sumBandDb = tfBand (both, a->checkFreq);
            finishCheck();
            return;
        }
        const auto& r = snap->mics[(size_t) mic];
        FreqTable t;
        for (size_t i = 0; i < snap->tfFreq.size() && i < r.tfMag.size(); ++i)
        {
            if (! std::isfinite (r.tfMag[i]) || i >= r.tfPhase.size() || ! std::isfinite (r.tfPhase[i])) continue;
            t.freq.push_back (snap->tfFreq[i]);
            t.db.push_back (r.tfMag[i]);
            t.phase.push_back (r.tfPhase[i]);
            t.extra.push_back (i < r.tfCoh.size() ? r.tfCoh[i] : 0.0);
        }
        (arrivalWhich == 0 ? a->refTf : a->alignTf) = std::move (t);
        for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        if (! arrivalQueue.empty()) nextArrival();
        else finishArrival();
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
            status = "Measured " + nameOf (arrivalWhich == 0 ? a->ref : a->align) + " at " + spotName() + ".";
            // the crossover pair: the delay that makes them add up best through the crossover, from their phase
            a->phaseValid = false;
            if (isXoPair (*a) && a->refTf.freq.size() > 8 && a->alignTf.freq.size() > 8)
            {
                const double f = sys.overlapFreq (a->ref, a->align);
                double d = 0.0;
                if (Alignment::phaseMatch (a->refTf, a->alignTf, f / 2.0, f * 2.0, d)) { a->phaseValid = true; a->phaseDelayMs = d; }
                if (a->phaseValid) status = "Measured both: matched their phase through the crossover (" + freqLabel (f) + " Hz).";
            }
        }
        arrivalAlignment = -1;
        refreshMeasureButton();
        systemChanged (false);
    }

    // ---- the time alignment, one step at a time ----
    // 0 play the reference speaker, 1 play the aligned speaker, 2 apply the delay, 3 check them together, 4 checked
    int alignmentStage() const
    {
        const auto* a = currentAlignment();
        if (a == nullptr) return -1;
        if (! a->hasRef) return 0;
        if (! a->hasAlign) return 1;
        if (! appliedNow (*a)) return 2;
        return a->checked() ? 4 : 3;
    }

    bool appliedNow (const Alignment& a) const
    {
        if (a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) return false;
        const double need = a.neededMs (sys.sources[(size_t) a.ref].proc.delayMs);
        return need >= -0.005 && std::abs (sys.sources[(size_t) a.align].proc.delayMs - need) < 0.01;
    }

    juce::String alignmentActionText() const
    {
        const auto* a = currentAlignment();
        switch (alignmentStage())
        {
            case 0: return alSignal == 2 ? "Measure " + nameOf (a->ref) + " (music on it only)" : "Play " + nameOf (a->ref);
            case 1: return alSignal == 2 ? "Measure " + nameOf (a->align) + " (music on it only)" : "Play " + nameOf (a->align);
            case 2:
            {
                const double need = a->neededMs (sys.sources[(size_t) a->ref].proc.delayMs);
                return need < -0.005 ? "Apply " + juce::String (-need, 2) + " ms to " + nameOf (a->ref) : "Apply " + juce::String (need, 2) + " ms to " + nameOf (a->align);
            }
            case 3: return alSignal == 2 ? juce::String ("Check: music on both") : juce::String ("Check: play both");
            case 4: return "Check again";
            default: return "Add an alignment";
        }
    }

    void alignmentAction()
    {
        switch (alignmentStage())
        {
            case 0: startArrivalOne (alSelected, 0); break;
            case 1: startArrivalOne (alSelected, 1); break;
            case 2: applyAlignment(); break;
            case 3:
            case 4: startCheck (alSelected); break;
            default: addAlignment(); break;
        }
    }

    // The crossover's own offset, when this is the crossover pair measured close up.
    void setXoOffset (Alignment& a) const
    {
        a.xoOffsetMs = 0.0;
        if (! sys.pairNearField()) return;
        if (a.ref == sys.xoHigh && a.align == sys.xoLow) a.xoOffsetMs = sys.xoRelDelayMs;
        else if (a.ref == sys.xoLow && a.align == sys.xoHigh) a.xoOffsetMs = -sys.xoRelDelayMs;
    }

    // ---- the signal for time alignment: pink noise, a sweep, or music from the console ----
    bool musicNow() const { return arrivalMode && alSignal == 2; }

    bool signalReady()
    {
        if (alSignal == 2 && settings.refSource.load() < 2)
        {
            status = "Music from the console needs the console's feed on an input as the reference: set it on Mics (Reference).";
            repaint();
            return false;
        }
        return true;
    }

    void startSignal()
    {
        settings.running = true;
        if (alSignal == 2) { settings.genOn = false; return; }   // the console plays: the reference input hears it
        settings.genType = alSignal == 1 ? 3 : 0;
        if (alSignal == 1) settings.genSweepSec = 2.0f;
        settings.genOn = true;
    }

    void startArrivalOne (int index, int which)
    {
        auto* a = alignmentAt (index);
        if (a == nullptr || a->ref == a->align) return;
        if (settings.refSource.load() == 0)
        {
            status = "Set a reference on Mic setup (the loopback, or the generator) to measure arrivals.";
            repaint();
            return;
        }
        if (! signalReady()) return;
        if (alSignal != 2)
            if (const auto why = outputProblem (which == 0 ? a->ref : a->align); why.isNotEmpty()) { status = why; repaint(); return; }
        (which == 0 ? a->hasRef : a->hasAlign) = false;
        a->sumBandDb = -200.0f;
        a->phaseValid = false;
        setXoOffset (*a);
        arrivalMode = true;
        checkMode = false;
        arrivalAlignment = index;
        arrivalQueue = { { index, which } };
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        status.clear();
        nextArrival();
    }

    // Both speakers together: the level where they overlap, against each alone.
    void startCheck (int index)
    {
        auto* a = alignmentAt (index);
        if (a == nullptr || ! a->measured()) return;
        if (! signalReady()) return;
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        arrivalMode = true;
        checkMode = true;
        arrivalAlignment = index;
        measuring = a->ref;
        const int o1 = sys.sources[(size_t) a->ref].output, o2 = sys.sources[(size_t) a->align].output;
        settings.genOutput = o1 == o2 ? o1 : 0;   // both speakers: one shared output, or all of them
        startSignal();
        settings.running = true;
        phase = Phase::Settling;
        phaseStart = juce::Time::getMillisecondCounterHiRes();
        settleUntil = proc.analyzer.samplesWritten() + (juce::uint64) (kSettleSeconds * rate());
        progress = 0.0f;
        status.clear();
        refreshMeasureButton();
        repaint();
    }

    void finishCheck()
    {
        settings.genOn = false;
        phase = Phase::Idle;
        measuring = -1;
        arrivalMode = checkMode = false;
        if (auto* a = alignmentAt (arrivalAlignment))
        {
            a->checkedOn = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
            const double got = a->sumGainDb(), ideal = a->idealSumGainDb();
            status = got >= ideal - 1.5 ? "Checked: they add up in phase at " + freqLabel (a->checkFreq) + " Hz."
                                        : "They do not add up fully: check the delay and polarity in the processor.";
        }
        arrivalAlignment = -1;
        refreshMeasureButton();
        systemChanged (false);
    }

    // Level of one mic's RTA around `f` (1/3 octave either side), dB.
    float bandLevel (int mic, double f) const
    {
        if (snap == nullptr || mic >= (int) snap->mics.size() || f <= 0.0) return -200.0f;
        const auto& r = snap->mics[(size_t) mic].rta;
        double sum = 0.0;
        int n = 0;
        for (size_t i = 0; i < snap->rtaFreq.size() && i < r.size(); ++i)
            if (snap->rtaFreq[i] >= f * 0.79 && snap->rtaFreq[i] <= f * 1.26 && std::isfinite (r[i]))
            {
                sum += std::pow (10.0, r[i] / 10.0);
                ++n;
            }
        return n > 0 ? (float) (10.0 * std::log10 (sum / n)) : -200.0f;
    }

    // Where to put the mic for the response, drawn: a speaker, the mic and the distance.
    void paintGuide (juce::Graphics& g)
    {
        const auto& p = pal();
        auto r = guideArea;
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (r.toFloat(), 6.0f);
        auto pic = r.removeFromLeft (112).reduced (10, 12).toFloat();
        const bool near = sys.placement == 0;
        // speaker
        auto box = pic.removeFromLeft (34.0f).withSizeKeepingCentre (30.0f, 60.0f);
        g.setColour (current() != nullptr ? sourceColour (selected) : p.textDim);
        g.drawRoundedRectangle (box, 3.0f, 1.6f);
        g.drawEllipse (box.withSizeKeepingCentre (18.0f, 18.0f).translated (0.0f, 10.0f), 1.4f);
        g.drawEllipse (box.withSizeKeepingCentre (7.0f, 7.0f).translated (0.0f, -16.0f), 1.2f);
        // mic and distance
        const float my = box.getCentreY() + 10.0f;
        const float mx = near ? box.getRight() + 26.0f : pic.getRight() - 6.0f;
        g.setColour (p.text);
        juce::Path mic;
        mic.startNewSubPath (mx - 10.0f, my);
        mic.lineTo (mx + 6.0f, my);
        g.strokePath (mic, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.fillEllipse (mx - 12.0f, my - 2.5f, 5.0f, 5.0f);
        g.setColour (p.accent);
        g.drawArrow (juce::Line<float> (box.getRight() + 2.0f, my + 12.0f, mx - 10.0f, my + 12.0f), 1.2f, 5.0f, 5.0f);
        g.drawArrow (juce::Line<float> (mx - 10.0f, my + 12.0f, box.getRight() + 2.0f, my + 12.0f), 1.2f, 5.0f, 5.0f);
        g.setFont (font (10.0f, true));
        g.drawText (near ? "12 in" : "FOH", juce::Rectangle<float> (box.getRight(), my + 15.0f, mx - box.getRight(), 12.0f), juce::Justification::centred);

        // the instruction
        r.reduce (6, 8);
        const int guideMic = sys.micFor (selected, settings.micCount.load());
        const auto micName = proc.mics.nameOf (guideMic), who = nameOf (selected);
        juce::String t;
        if (near)
            t << "Place " << micName << " 12 in (30 cm) from the centre of " << who << "'s woofer cone, pointed at it, "
              << "level with the cone. Then press Measure " << who << ".";
        else
            t << "Place " << micName << " where every speaker covers the seats (usually FOH), pointed at the speakers, and leave it "
              << "there for all of them. Then press Measure " << who << ".";
        if (proc.calibrations (guideMic).both()) t << " Its " << calAngleName (Cal0) << " calibration is used.";
        g.setColour (p.text);
        g.setFont (font (12.0f));
        g.drawFittedText (t, r, juce::Justification::topLeft, 6);
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
                out << nameOf (i) << (s.count > 1 ? " x" + juce::String (s.count) : juce::String())
                    << (s.model.isNotEmpty() ? " [" + s.model + "]" : juce::String())
                    << " (" << speakerRoleNames()[s.role] << ", " << speakerChannelNames()[s.channel] << ", output " << out1 << "): "
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
            if (need < -0.005) out << "delay " << nameOf (a.ref) << " " << juce::String (-need, 2) << " ms instead, not applied yet";
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
        // parametric EQ (from room tuning) on the speakers that have it
        head = false;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const auto& s = sys.sources[(size_t) i];
            if (s.proc.eq.empty() || sys.master (i) != i) continue;
            if (! head) { out << (forScreen ? "#EQ (ROOM TUNING)\n" : "\nParametric EQ (room tuning):\n"); head = true; }
            juce::StringArray b;
            for (const auto& e : s.proc.eq)
                b.add (freqLabel (e.freq) + " Hz " + signedText ((float) e.gainDb, 1) + " dB Q" + juce::String (e.q, forScreen ? 1 : 2));
            out << nameOf (i) << ": " << b.joinIntoString (", ") << "\n";
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
    juce::TextEditor modelEditor;
    juce::ComboBox countBox, linkBox;
    juce::ComboBox roleBox, channelBox, outputBox, micBox, highBox, lowBox, rangeBox, typeBox, slopeBox;
    juce::ToggleButton guidedToggle;
    juce::ComboBox alSignalBox;
    int alSignal = 0;
    juce::TextButton polarityButton;
    juce::TextButton measureButton { "Measure" }, importButton { "Import response..." }, clearButton { "Clear" }, relockButton { "Re-time" };
    juce::Slider offsetSlider, freqSlider, delaySlider, gainSlider;
    juce::ToggleButton processedToggle { "With processing" };
    juce::TextButton typeButtons[3], slopeButtons[kSlopeButtons];
    juce::TextButton findButton { "Find best crossover" }, invertButton { "Low normal" };
    juce::TextButton autoButton { "Automatic" }, manualButton { "By hand" }, removeXoButton { "Remove these crossover filters" };
    bool xoManual = false;
    juce::TextButton swapPairButton, invertedSwapButton { "Swap" }, alRedoButton { "Start over" };
    juce::ComboBox placementBox;
    juce::Rectangle<int> guideArea, modeHint, alSteps;
    int mode = 0;
    bool measureNear = false, checkMode = false;
    double nearDelayMs = 0.0;
    juce::Rectangle<int> invertedArea;
    juce::TextButton copyButton { "Copy settings" }, doneButton { "Mark speakers set up" };
    juce::ComboBox minCohBox;
    AlignmentList alList;
    juce::TextButton alAddButton { "Add alignment" }, alRemoveButton { "Remove" }, alMeasureButton { "Measure both" }, alApplyButton { "Apply delay" };
    juce::ComboBox alAlignBox, alRefBox, alSpotBox;
    juce::Slider alExtraSlider;
    juce::Rectangle<int> alHint, alInfo;
    static constexpr int kHeaderH = 46;
    juce::Rectangle<int> stepCard[3], stepHeader[3], qualityArea, reliabilityArea;
    int labelFrom[3] {}, labelTo[3] {};
    int openStep = 0, hoverStep = -1;
    double liveQuality = -1.0;
    float shownMinCoh = -1.0f;
    int qualityTick = 0;
    std::vector<double> reliability;
    int alSelected = 0, arrivalAlignment = -1, arrivalWhich = 0;
    bool arrivalMode = false;
    std::vector<std::pair<int, int>> arrivalQueue;   // (alignment, 0 = reference speaker / 1 = aligned speaker)

    juce::Rectangle<int> systemCard, selCard, summaryCard, selInfo, measureInfo, xoInfo, liveLine, summaryArea, statusLine;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;

    int selected = 0, nOuts = -1, measuring = -1, measureSerial = 0, guidedNow = -1, polarityCab = 0;
    bool polarityMode = false, peakKnown = false, peakInverted = false;
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    float progress = 0.0f;
    juce::uint64 delaySerial = 0, settleUntil = 0;
    juce::uint32 overflowsSeen = 0;
    int restarts = 0;
    std::array<float, kMaxMics> savedDelays {};
    bool searching = false, liveValid = false, sawReset = false;
    double liveEff = 0.0, liveDev = 0.0, liveReliable = 1.0;
    juce::String status;
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace la::ui
