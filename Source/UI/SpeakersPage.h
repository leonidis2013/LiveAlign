// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include "../Clock.h"
// Speakers (setup) and the speaker tuning steps: Speaker response (each speaker close up, its polarity judged from
// the same capture), Crossover, and Time alignment. The speakers are edited only on the Speakers page; the steps show
// them read-only. Every measurement goes through the guided pop-up. The settings for the processor are on the
// processor sheet page.
#include <functional>
#include <thread>
#include "Graph.h"
#include "NavRail.h"
#include "GuidedMeasure.h"
#include "../PluginProcessor.h"
#include "../ProcessorSheet.h"

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
    void setShowAsFound (bool b) { showAsFound = b; rebuild(); }
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
        // as found: each speaker's first measurement with the processing it had then (dashed)
        found.clear();
        if (showAsFound)
            for (int i = 0; i < (int) sys.sources.size(); ++i)
            {
                const auto& s = sys.sources[(size_t) i];
                if (! s.show || s.asFound.freq.size() < 4) continue;
                Curve c;
                c.index = i;
                c.db.assign (grid.size(), NAN);
                for (size_t k = 0; k < grid.size(); ++k)
                {
                    if (grid[k] < s.asFound.freq.front() || grid[k] > s.asFound.freq.back()) continue;
                    const double v = s.asFound.dbAt (grid[k]) + (withProc ? 20.0 * std::log10 (std::abs (s.asFoundProc.at (grid[k])) + 1e-12) : 0.0);
                    if (std::isfinite (v) && v > -200.0) c.db[k] = (float) v;
                }
                found.push_back (std::move (c));
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
            for (const auto& c : found)
            {
                juce::Path d;
                const float dashes[] = { 5.0f, 4.0f };
                juce::PathStrokeType (1.3f).createDashedStroke (d, tracePath (grid.size(), xAt, [&] (size_t k) { return yDb (c.db[k]); }, [] (size_t) { return true; }),
                                                                dashes, 2);
                g.setColour (sourceColour (c.index).withAlpha (0.75f));
                g.fillPath (d);
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
        const float minX = r.getX() + 12.0f + (float) juce::GlyphArrangement::getStringWidthInt (font (13.0f, true), "Speaker responses") + 30.0f;
        auto addKey = [&] (const juce::String& name, juce::Colour c)
        {
            const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), name) + 26.0f;
            if (legend.getX() - w < minX) return;   // no room left beside the title
            legend.setX (legend.getX() - w);
            auto k = legend.withWidth (w);
            g.setColour (c);
            g.fillRoundedRectangle (k.removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, 3.0f), 1.5f);
            g.setColour (p.textDim);
            g.drawText (name, k.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
        };
        if (hasSum) addKey ("Top + sub sum", p.text);
        if (! found.empty()) addKey ("As found (dashed)", p.textDim);
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
    std::vector<Curve> curves, found;
    bool showAsFound = false;
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
    void setConfigMode (bool c) { configMode = c; repaint(); }   // the Speakers tab: the set-up only, no measuring state
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
            if (! configMode)
            {
            // eye: show / hide on the graph
            auto eye = r.removeFromRight (20.0f).withSizeKeepingCentre (16.0f, 16.0f);
            g.setColour (s.show ? sourceColour (i) : p.textFaint);
            if (s.show) g.fillEllipse (eye.reduced (3.0f));
            g.drawEllipse (eye.reduced (1.0f), 1.3f);
            }
            // polarity: + normal, - inverted, ? not clear, an empty ring when not checked yet
            if (! configMode)
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
                            : master != i && sys.covered (i) ? "AS " + sys.sources[(size_t) master].name.toUpperCase() : juce::String ("TO MEASURE");
            if (s.hasResponse() && rv >= 0.0)
            {
                st = juce::String (juce::roundToInt (rv * 100.0)) + "% RELIABLE";
                sc = rv >= 0.8 ? p.good : rv >= 0.6 ? p.warn : p.bad;
            }
            if (configMode) st = s.linkTo >= 0 ? "SAME AS " + sys.nameOf (s.linkTo).toUpperCase() : juce::String();
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
    bool configMode = false;
    int selected = 0, hoverRow = -1;
};

//==============================================================================
// The time alignments of the system, one row each: which speaker or set is lined up to which, where, and the delay.
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
            const auto alignName = sys.endName (a.align, a.alignSet), refName = sys.endName (a.ref, a.refSet);
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
                const double need = a.neededMs (sys.refDelayNow (a));
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
    const SpeakerSystem& sys;
    int selected = 0, hoverRow = -1;
};

//==============================================================================
// Time alignment: the two ends' impulse responses at the spot (a speaker, or a set playing together), one above the other on the same time base (the
// first one measured is time zero), each with its arrival marked, so the offset between them is seen rather than
// read. Below them, what that offset asks for in the processor.
class AlignImpulseView : public juce::Component
{
public:
    std::function<const Alignment*()> alignment;
    std::function<juce::String (int)> endName;   // 0 the reference end, 1 the end to align (a speaker or a set)
    std::function<double()> neededMs;
    int measuringWhich = -1;   // 0 the reference, 1 the one to align, while it plays

    juce::Rectangle<int> paneButtonArea (int which) const { return panes[which].withTrimmedLeft (panes[which].getWidth() - 160).withHeight (34).reduced (8, 4); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Impulse responses at the spot");
        const auto* a = alignment ? alignment() : nullptr;
        if (a == nullptr || a->ref == a->align)
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("Add an alignment on the right: the reference speaker, the one to delay, and the spot where their coverage meets. "
                              "Each is played alone and its impulse response is captured here.", getLocalBounds().reduced (40), juce::Justification::centred, 3);
            return;
        }
        // the window: from 5 ms before time zero to past both arrivals
        const double refAt = a->hasRef ? a->refArrivalMs - a->lockMs : 0.0, alAt = a->hasAlign ? a->alignArrivalMs - a->lockMs : 0.0;
        double tHi = juce::jmax (20.0, juce::jmax (refAt, alAt) + 12.0), tLo = juce::jmin (-5.0, juce::jmin (refAt, alAt) - 5.0);
        tHi = juce::jmin (tHi, 120.0);
        for (int which = 0; which < 2; ++which)
        {
            auto r = panes[which];
            const int spk = which == 0 ? a->ref : a->align;
            const bool has = which == 0 ? a->hasRef : a->hasAlign;
            const auto& ir = which == 0 ? a->refIr : a->alignIr;
            const int zero = which == 0 ? a->refIrZero : a->alignIrZero;
            g.setColour (p.bg.withAlpha (0.5f));
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
            auto head = r.removeFromTop (30).reduced (10, 0);
            g.setColour (sourceColour (spk));
            g.fillRoundedRectangle (head.removeFromLeft (4).toFloat().withSizeKeepingCentre (4.0f, 18.0f), 2.0f);
            head.removeFromLeft (8);
            g.setColour (p.text);
            g.setFont (font (12.5f, true));
            g.drawText ((which == 0 ? "Reference: " : "To align: ") + (endName ? endName (which) : juce::String()), head.removeFromLeft (220), juce::Justification::centredLeft, true);
            g.setColour (measuringWhich == which ? p.accent : has ? p.good : p.textDim);
            g.setFont (font (11.0f, measuringWhich == which));
            const auto at = which == 0 ? refAt : alAt;
            g.drawText (measuringWhich == which ? juce::String ("measuring...") : has ? "arrives at " + juce::String (at, 2) + " ms" : juce::String ("not measured"),
                        head.withTrimmedRight (160), juce::Justification::centredLeft, true);
            auto plot = r.reduced (10, 6).withTrimmedBottom (14);
            auto x = [&] (double ms) { return (float) plot.getX() + (float) ((ms - tLo) / (tHi - tLo)) * (float) plot.getWidth(); };
            // the time grid
            g.setFont (font (9.5f));
            for (double ms = std::ceil (tLo / 5.0) * 5.0; ms <= tHi; ms += 5.0)
            {
                g.setColour (p.grid);
                g.drawVerticalLine ((int) x (ms), (float) plot.getY(), (float) plot.getBottom());
                g.setColour (p.textFaint);
                g.drawText (juce::String ((int) ms), juce::Rectangle<float> (x (ms) - 16.0f, (float) plot.getBottom() + 1.0f, 32.0f, 12.0f), juce::Justification::centred);
            }
            g.setColour (p.grid);
            g.drawHorizontalLine (plot.getCentreY(), (float) plot.getX(), (float) plot.getRight());
            if (! ir.empty())
            {
                float peak = 1e-9f;
                for (float v : ir) peak = juce::jmax (peak, std::abs (v));
                juce::Path path;
                const double fs = a->irRate > 1000.0 ? a->irRate : 48000.0;
                const int n = (int) ir.size();
                bool started = false;
                for (int px = plot.getX(); px <= plot.getRight(); ++px)
                {
                    // each pixel: the extreme of the samples in it
                    const double ms0 = tLo + (double) (px - plot.getX()) / (double) plot.getWidth() * (tHi - tLo);
                    const double ms1 = tLo + (double) (px + 1 - plot.getX()) / (double) plot.getWidth() * (tHi - tLo);
                    const int i0 = zero + (int) std::floor (ms0 * 0.001 * fs), i1 = juce::jmax (i0 + 1, zero + (int) std::floor (ms1 * 0.001 * fs));
                    float ext = 0.0f;
                    for (int i = juce::jmax (0, i0); i < juce::jmin (n, i1); ++i) if (std::abs (ir[(size_t) i]) > std::abs (ext)) ext = ir[(size_t) i];
                    if (i0 >= n || i1 <= 0) continue;
                    const float y = (float) plot.getCentreY() - ext / peak * (float) plot.getHeight() * 0.46f;
                    if (! started) { path.startNewSubPath ((float) px, y); started = true; } else path.lineTo ((float) px, y);
                }
                g.setColour (sourceColour (spk));
                g.strokePath (path, juce::PathStrokeType (1.3f));
            }
            else if (! has)
            {
                g.setColour (p.textFaint);
                g.setFont (font (11.5f));
                g.drawText ("Play it alone to capture its impulse response.", plot, juce::Justification::centred);
            }
            if (has)
            {
                g.setColour (which == 0 ? p.accent : p.warn);
                g.drawLine (x (at), (float) plot.getY(), x (at), (float) plot.getBottom(), 1.4f);
            }
            if (a->hasRef && a->hasAlign && which == 1)
            {
                // the offset, drawn between the two arrivals
                const float xa = x (refAt), xb = x (alAt), yy = (float) plot.getY() + 10.0f;
                g.setColour (p.textDim);
                g.drawLine (xa, yy, xb, yy, 1.0f);
                g.setFont (font (10.5f, true));
                g.drawText (juce::String (std::abs (alAt - refAt), 2) + " ms", juce::Rectangle<float> (juce::jmin (xa, xb), yy + 2.0f, std::abs (xb - xa), 14.0f),
                            juce::Justification::centred, true);
            }
        }
        // what it asks for
        g.setFont (font (12.0f, true));
        if (a->hasRef && a->hasAlign)
        {
            const double need = neededMs ? neededMs() : 0.0;
            g.setColour (need >= -0.005 ? p.text : p.warn);
            const auto alName = endName ? endName (1) : juce::String(), refName = endName ? endName (0) : juce::String();
            g.drawFittedText (need >= -0.005 ? alName + " arrives " + juce::String (alAt - refAt, 2) + " ms "
                                                   + (alAt >= refAt ? "after " : "before ") + refName + ": delay it "
                                                   + juce::String (need, 2) + " ms" + (a->phaseValid ? " (matched by phase through the crossover)." : ".")
                                             : refName + " is the later one: delay it " + juce::String (-need, 2) + " ms instead (Apply swaps them).",
                              footer, juce::Justification::centredLeft, 2);
        }
        else
        {
            g.setColour (p.textDim);
            g.drawFittedText ("Measure each end on its own (a set plays all its speakers at once); time zero is the first arrival measured.", footer, juce::Justification::centredLeft, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0).withTrimmedTop (36).withTrimmedBottom (10);
        footer = r.removeFromBottom (36);
        r.removeFromBottom (6);
        panes[0] = r.removeFromTop ((r.getHeight() - 8) / 2);
        r.removeFromTop (8);
        panes[1] = r;
    }

private:
    juce::Rectangle<int> panes[2], footer;
};

class SpeakersPage : public juce::Component, public GuidedSource, private juce::Timer
{
public:
    std::function<void (int)> onImport;   // the editor owns the file chooser
    std::function<void (GuidedSource&)> onGuided;   // opens the guided pop-up
    std::function<void()> onChanged;
    std::function<void()> onEditSpeakers, onOpenSheet;   // to the Speakers page, to the processor sheet

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
        coverBox.setTooltip ("Its coverage: the data sheet's horizontal x vertical angles (to -6 dB). From them comes its directivity (Q): "
                             "how much of its sound goes straight to the audience rather than into the room. Room acoustics uses it for "
                             "the critical distance (where the room's sound gets as loud as the speaker's), and coverage for its hints. "
                             "Not in the list? Pick \"Enter the angles...\" (a wide column such as a CBT: 150 x 20).");
        coverBox.onChange = [this]
        {
            auto* s = current();
            const int id = coverBox.getSelectedId();
            if (s == nullptr || id <= 0 || id == 99) return;
            if (id == 98) { enterCoverAngles(); return; }
            s->directivityQ = id == 1 ? 0.0 : coverPatterns()[(size_t) (id - 2)].q;
            s->coverH = id == 1 ? 0.0 : coverPatterns()[(size_t) (id - 2)].h;
            s->coverV = id == 1 ? 0.0 : coverPatterns()[(size_t) (id - 2)].v;
            systemChanged (false);
        };
        onboardButton.setTooltip ("A filter built into the speaker (a powered sub's own low-pass, a column's high-pass): its family, slope, the "
                                  "settings it offers, whether it can be bypassed, what setting the imported curve was taken at, and its DSP latency. "
                                  "The crossover search then models the whole chain and says what to set on the speaker and in the processor.");
        onboardButton.onClick = [this] { editOnboard(); };
        protectButton.setTooltip ("From the maker's data: the lowest high-pass the speaker should be run with, and its RMS voltage limit. "
                                  "The crossover search keeps clear of the limit (Settings, Tuning: Optimise for), and the sheet works out the limiter.");
        protectButton.onClick = [this] { editProtection(); };
        for (auto* b : { &onboardButton, &protectButton }) addAndMakeVisible (*b);
        linkBox.setTooltip ("Its group: speakers in a group share the crossover, level, polarity and EQ (Main L, Main R and the centre; "
                            "every sub). Each is still measured and checked on its own, and keeps its own delay and trim. "
                            "\"Own settings\" takes it out of the group.");
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
        measureButton.setTooltip ("Measures each speaker close up, one after another, in the guided pop-up: its response, and its polarity from "
                                  "the same capture. A speaker set \"Same as\" another is measured too, to check it against that one.");
        editSpeakerButton.onClick = [this] { if (onEditSpeakers) onEditSpeakers(); };
        editSpeakerButton.setTooltip ("Name, role, channel, output and cabinets are set on the Speakers page");
        addChildComponent (editSpeakerButton);
        sheetButton.onClick = [this] { if (onOpenSheet) onOpenSheet(); };
        sheetButton.setTooltip ("Every setting to enter in your processor, output by output, ticked off as you enter them");
        addAndMakeVisible (sheetButton);
        asFoundToggle.onClick = [this] { graph.setShowAsFound (asFoundToggle.getToggleState()); };
        asFoundToggle.setTooltip ("Show each speaker as it was first measured (dashed), with the settings it had then");
        addChildComponent (asFoundToggle);
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
                                                                 &modelEditor, &countBox, &linkBox, &coverBox })
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
        relockButton.setTooltip ("Find the timing again on the next measurement (after moving the mic). Measure every speaker again afterwards.");
        relockButton.onClick = [this] { sys.timingLocked = false; status = "Timing will be found again on the next measurement."; systemChanged (false); };
        addAndMakeVisible (micBox);
        addChildComponent (relockButton);

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
        procDetailsButton.setTooltip ("The processor's frequency range and steps, its delay per output, the limiter's units, and which outputs have no high-pass or low-pass");
        procDetailsButton.onClick = [this] { editProcessorDetails(); };
        addAndMakeVisible (procDetailsButton);
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
        topHpButton.setTooltip ("No sub: a protective high-pass on each top, near where it runs out of low end (Butterworth 24 dB/oct). "
                                "It goes on the processor sheet.");
        topHpButton.onClick = [this]
        {
            juce::StringArray done;
            for (int i = 0; i < (int) sys.sources.size(); ++i)
            {
                xo::Filter f;
                if (! sys.topHighPass (i, f)) continue;
                sys.sources[(size_t) i].proc.hp = f;
                sys.sources[(size_t) i].hpProtective = true;
                done.add (nameOf (i) + " " + juce::String (juce::roundToInt (f.freq)) + " Hz");
            }
            sys.syncLinks();
            status = done.isEmpty() ? juce::String ("The tops have a high-pass already.")
                                    : "High-pass added: " + done.joinIntoString (", ") + ". Enter it in the processor (Processor sheet).";
            graph.rebuild();
            systemChanged (false);
            resized();
            repaint();
        };
        addChildComponent (topHpButton);
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
        alAddButton.setTooltip ("Line up the next set: Stereo (L + R together) to the sub first, then each fill or delay to Stereo");
        alRemoveButton.onClick = [this]
        {
            if (alSelected < 0 || alSelected >= (int) sys.alignments.size() || phase != Phase::Idle) return;
            sys.alignments.erase (sys.alignments.begin() + alSelected);
            alSelected = juce::jmax (0, alSelected - 1);
            systemChanged (false);
        };
        alAlignBox.setTooltip ("What gets the delay: a set (every speaker in it plays at once, as in the show, and each keeps its own trim), "
                               "or a single speaker for a small trim of its own");
        alRefBox.setTooltip ("What it is lined up to: usually Stereo (L + R together). Stereo (L + R together) to Sub: both sides play at once, as in the show");
        alAlignBox.onChange = [this]
        {
            auto* a = currentAlignment();
            const int id = alAlignBox.getSelectedId();
            if (a == nullptr || id <= 0) return;
            a->align = endIndexOfId (id);
            a->alignSet = endIsSetId (id);
            a->hasAlign = a->hasRef = false;
            a->phaseValid = false;
            a->phaseInvert = false;
            a->refMemberUsed = -1;
            systemChanged (false);
        };
        alRefBox.onChange = [this]
        {
            auto* a = currentAlignment();
            const int id = alRefBox.getSelectedId();
            if (a == nullptr || id <= 0) return;
            a->ref = endIndexOfId (id);
            a->refSet = endIsSetId (id);
            a->hasAlign = a->hasRef = false;
            a->phaseValid = false;
            a->phaseInvert = false;
            a->refMemberUsed = -1;
            systemChanged (false);
        };
        alSignalBox.addItemList ({ "Pink noise", "Sweep", "Music from the console" }, 1);
        alSignal = juce::jlimit (0, 2, UserPrefs::get ("alignSignal", "0").getIntValue());
        alSignalBox.setSelectedItemIndex (alSignal, juce::dontSendNotification);
        alSignalBox.setTooltip ("What plays while each end is measured. Pink noise (the default) or a sweep from the generator; or music "
                                "from the console when noise is not possible: route it to one end at a time on the console (both sides of a "
                                "set at once), with the console's feed on an input as the reference (Mics, Reference).");
        alSignalBox.onChange = [this]
        {
            alSignal = alSignalBox.getSelectedItemIndex();
            UserPrefs::set ("alignSignal", juce::String (alSignal));
            repaint();
        };
        addChildComponent (alSignalBox);
        alSpotBox.setEditableText (true);
        alSpotBox.setTextWhenNothingSelected ("Where they meet");
        alSpotBox.setTooltip ("Where both ends cover the seats: FOH for Stereo to the sub, front rows for a front fill, under the "
                              "balcony for a delay. Type a new name to add a position.");
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
                                  "(precedence effect). 0 for the sub and for front fills; often 5 to 10 ms for delay speakers.");
        alExtraSlider.onValueChange = [this] { if (auto* a = currentAlignment()) { a->precedenceMs = alExtraSlider.getValue(); refreshApply(); changedOutside(); alList.repaint(); } };
        alMeasureButton.onClick = [this] { if (phase == Phase::Idle) alignmentAction(); else cancelMeasure ("Stopped."); };
        // the two impulse panes, with a Play button on each
        alImpulse.alignment = [this] { return currentAlignment(); };
        alImpulse.endName = [this] (int which)
        {
            const auto* a = currentAlignment();
            if (a == nullptr) return juce::String();
            return which == 0 && a->refMemberUsed >= 0 ? refUsedName (*a) + " (the nearer side)" : sideName (*a, which);
        };
        alImpulse.neededMs = [this] { const auto* a = currentAlignment(); return a != nullptr ? a->neededMs (sys.refDelayNow (*a)) : 0.0; };
        addChildComponent (alImpulse);
        for (int w = 0; w < 2; ++w)
        {
            alPlayButtons[w].onClick = [this, w]
            {
                if (phase != Phase::Idle) { cancelMeasure ("Stopped."); return; }
                startArrivalOne (alSelected, w);
            };
            alPlayButtons[w].setTooltip (w == 0 ? "Play the reference on its own (all of a set at once) and capture its arrival"
                                                : "Play the one to align on its own (all of a set at once) and capture its arrival");
            addChildComponent (alPlayButtons[w]);
        }
        alGuide.page = this;
        alMeasureButton.setTooltip ("The next step of this alignment: play each end on its own, apply the delay, then check them together");
        alApplyButton.onClick = [this] { applyAlignment(); };
        alRedoButton.onClick = [this]
        {
            if (auto* a = currentAlignment()) { a->hasRef = a->hasAlign = false; a->sumBandDb = a->refBandDb = a->alignBandDb = -200.0f; a->checkedOn.clear(); a->phaseValid = false; a->refTf = a->alignTf = {}; }
            status = "Starting this alignment again.";
            systemChanged (false);
        };
        alRedoButton.setTooltip ("Measure this alignment again from the first step");
        for (auto* c : std::initializer_list<juce::Component*> { &alAddButton, &alRemoveButton, &alAlignBox, &alRefBox, &alSpotBox, &alExtraSlider,
                                                                 &alMeasureButton, &alRedoButton })
            addChildComponent (*c);


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
        if (! resp)
        {
            int left = 0;
            for (const auto& t : guidedTasks()) left += ! taskDone (t) ? 1 : 0;
            measureButton.setButtonText (left == 0 ? "Measure again: " + nameOf (selected) : left == (int) guidedTasks().size() ? juce::String ("Measure the speakers")
                                                                                       : "Measure the rest (" + juce::String (left) + " left)");
        }
        alMeasureButton.setButtonText (arr ? "Stop" : alignmentActionText());
        if (mode == 3) setStepVisibility();   // the Play buttons on the impulse panes follow the phase
        alMeasureButton.setColour (juce::TextButton::buttonColourId, arr ? pal().bad : pal().accent);
        alMeasureButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
        alMeasureButton.setEnabled (! resp && currentAlignment() != nullptr && (proc.isStandalone() || proc.uiAsStandalone));
    }

    // After a venue is opened, or mics or outputs changed.
    void refreshChoices()
    {
        sys.placement = 0;   // speaker response is always close up (the room is Room EQ's and Room acoustics')
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
        if (alignmentNeeded() && ! alignmentDone()) return "Time-align the speakers";
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
        list.setConfigMode (mode == 0);
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
    void assumeMuted (bool b) { mutesConfirmed = b; }   // for the preview: no dialog
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
        if (sys.sources.empty()) { status = "Add the speakers first (Venue, Speakers)."; repaint(); return; }
        selected = juce::jlimit (0, (int) sys.sources.size() - 1, selected);
        // the first one not measured yet; when every one is, the selected speaker again
        bool allDone = true;
        for (const auto& t : guidedTasks()) allDone = allDone && taskDone (t);
        guidedNow = allDone && ! sys.sources[(size_t) selected].imported ? SpkTask { true, selected, 0 }.id() : -1;
        if (onGuided) onGuided (*this);
        else startMeasure (selected);
    }

    // ---- guided measuring: one point per speaker that needs its own measurement ----
    juce::String guidedTitle() const override { return "Speaker response"; }

    // The guided points: every speaker close up (its response, and its polarity from the same capture; a speaker set
    // "Same as" another is measured too, to check it against that one), then each further cabinet on one output.
    struct SpkTask { bool response = true; int spk = 0, cab = 0; int id() const { return (response ? 0 : 10000) + spk * 100 + cab; } };

    std::vector<SpkTask> guidedTasks() const
    {
        std::vector<SpkTask> out;
        const int n = juce::jmin ((int) sys.sources.size(), VenuePlan::kMaxSpeakers);
        for (int i = 0; i < n; ++i) if (! sys.sources[(size_t) i].imported) out.push_back ({ true, i, 0 });
        for (int i = 0; i < n; ++i)
            for (int c = 1; c < sys.sources[(size_t) i].count; ++c) out.push_back ({ false, i, c });
        return out;
    }

    bool taskDone (const SpkTask& t) const
    {
        const auto& s = sys.sources[(size_t) t.spk];
        if (t.response) return s.hasResponse() && ! s.imported;
        const auto* pc = s.polarityOf (t.cab);
        return pc != nullptr && pc->done();
    }

    bool taskProblem (const SpkTask& t) const
    {
        const auto* pc = sys.sources[(size_t) t.spk].polarityOf (t.cab);
        return pc != nullptr && (pc->verdict == polarity::Inverted || pc->verdict == polarity::Unsure || pc->notClose);
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
            sp.label = t.response ? nameOf (i) : cabinetName (i, t.cab);
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
                if (! t.response && sys.sources[(size_t) i].role == (int) SpeakerRole::Sub && sys.sources[(size_t) i].count > 1)
                    sp.how = "a few inches from this cabinet's cone, " + aimHowText (proc, sp.mic, SpeakerAim) + " (the others play too)";
                else sp.how = proc.closeUpText() + " from the cone, on its axis, " + aimHowText (proc, sp.mic, SpeakerAim);
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
        if (const auto m = muteText ({ t.spk }); m.isNotEmpty()) return "First: " + m;
        if (! t.response)
            return "Only " + who + "'s output plays. Its level and polarity are compared with the first cabinet; leave the processor's polarity "
                   "for this output at normal.";
        if (sys.master (t.spk) != t.spk)
            return "Only " + who + " plays. Its settings follow " + nameOf (sys.master (t.spk)) + "; this capture checks it against "
                   + nameOf (sys.master (t.spk)) + " (polarity, level and response). Bypass its crossover filters while it is measured.";
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
        mutesConfirmed = true;   // the pop-up showed which speakers to mute; Measure confirms it
        if (t.response) startMeasure (t.spk);
        else startPolarity (t.spk, t.cab);
        mutesConfirmed = false;
        return phase != Phase::Idle;
    }

    bool guidedCanSkip() const override
    {
        const auto tasks = guidedTasks();
        const int k = guidedCurrentIndex();
        return k >= 0 && ! tasks[(size_t) k].response;
    }
    int guidedAimKind() const override { return SpeakerAim; }
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
    // For the preview's simulated console: the speakers playing in the time alignment now (the others muted).
    std::vector<int> alignmentPlayingForPreview() const
    {
        const auto* a = alignmentAt (arrivalAlignment);
        if (phase == Phase::Idle || ! arrivalMode || a == nullptr) return {};
        if (! checkMode && arrivalMember >= 0) return { arrivalMember };   // one side of the reference set
        return checkMode ? bothMembers (*a) : sideMembers (*a, arrivalWhich);
    }
    void checkAlignment (int i) { selectAlignment (i); startCheck (i); }
    juce::String statusText() const { return status; }
    juce::String sheetLine (int i) const { return sheet::signature (sys, i); }

    // ---- Time alignment, guided: each end alone at the spot where they meet (a set plays all its speakers), then both together ----
    struct AlignGuide : public GuidedSource
    {
        SpeakersPage* page = nullptr;
        int stageNow() const
        {
            const auto* a = page->currentAlignment();
            if (a == nullptr) return -1;
            if (! a->hasRef) return 0;
            if (! a->hasAlign) return 1;
            return 2;
        }
        juce::String guidedTitle() const override
        {
            const auto* a = page->currentAlignment();
            return a != nullptr ? "Time alignment: " + page->sideName (*a, 1) + " to " + page->sideName (*a, 0) : juce::String ("Time alignment");
        }
        std::vector<GuideSpot> guidedSpots() override
        {
            std::vector<GuideSpot> out;
            const auto* a = page->currentAlignment();
            if (a == nullptr) return out;
            auto& pr = page->proc;
            const auto spot = a->spot.isNotEmpty() ? a->spot : juce::String ("FOH");
            if (! pr.positions.contains (spot)) pr.positions.add (spot);
            const int marker = pr.plan.ensureMarker (spot);
            const int now = stageNow();
            const juce::String labels[] = { page->sideName (*a, 0) + (page->refOneByOne (*a) ? " one side at a time" : " alone"), page->sideName (*a, 1) + " alone", "Both together" };
            for (int k = 0; k < 3; ++k)
            {
                GuideSpot sp;
                sp.id = k;
                sp.label = labels[k] + " at " + spot;
                sp.marker = marker;
                sp.x = pr.plan.items[(size_t) marker].x;
                sp.y = pr.plan.items[(size_t) marker].y;
                const bool done = k == 0 ? a->hasRef : k == 1 ? a->hasAlign : a->checked() && redoCheck < 0;
                sp.state = k == now || (now == 2 && k == 2 && ! done) ? GuideSpot::Now : done ? GuideSpot::Done : GuideSpot::ToDo;
                if (now == 2 && k == 2 && redoCheck >= 0) sp.state = GuideSpot::Now;
                if (sp.state == GuideSpot::Now)
                {
                    sp.mic = juce::jlimit (0, juce::jmax (0, pr.tuningMicCount() - 1), page->sys.measureMic);
                    sp.how = aimHowText (pr, sp.mic, RoomAim);
                }
                if (k == 2 && done)
                {
                    const double got = a->sumGainDb(), ideal = a->idealSumGainDb();
                    sp.problem = got < ideal - 1.5;
                }
                out.push_back (sp);
            }
            return out;
        }
        bool guidedNeedsProcessor() const override { return true; }
        juce::String guidedHowTo() const override
        {
            const auto* a = page->currentAlignment();
            if (a == nullptr) return "Add an alignment first.";
            const int st = stageNow();
            if (page->alSignal == 2)
                return st == 2 ? "First: route the music to both " + page->sideName (*a, 0) + " and " + page->sideName (*a, 1) + " on the console."
                               : "First: route the music only to " + page->sideName (*a, st, true) + " on the console.";
            if (st == 2)
            {
                const double need = a->neededMs (page->sys.refDelayNow (*a));
                const int side = need < -0.005 ? 0 : 1;
                juce::String t = "First: enter " + juce::String (juce::jmax (0.0, std::abs (need)), 2) + " ms delay on "
                                 + page->sideName (*a, side) + " in the processor";
                if (page->sideMembers (*a, side).size() > 1) t << " (on each of its speakers, keeping their own trims)";
                int lowIdx = -1;
                bool wantInvert = false;
                if (page->lowInvertWanted (*a, lowIdx, wantInvert) && wantInvert) t << ", invert " << page->sys.endName (lowIdx, page->sys.inGroup (lowIdx));
                if (const auto m = page->muteText (page->bothMembers (*a)); m.isNotEmpty()) t << ", and " << m.replace ("Mute", "mute");
                else t << ", and unmute both.";
                return t;
            }
            if (st == 0 && page->refOneByOne (*a))
            {
                const auto members = page->sideMembers (*a, 0);
                juce::String t = "Each side of " + page->sideName (*a, 0) + " plays on its own in turn, and " + page->sideName (*a, 1)
                                 + " is lined up to the louder side here, the nearer one. The mic stays at the same spot for both ends.";
                if (! members.empty())
                    if (const auto m = page->muteText ({ members.front() }); m.isNotEmpty()) return "First: " + m + " You are asked to swap the mutes for the next side.";
                return t;
            }
            if (const auto m = page->muteText (page->sideMembers (*a, st)); m.isNotEmpty()) return "First: " + m;
            if (page->sideMembers (*a, st).size() > 1)
                return "Only " + page->sideName (*a, st, true) + " plays, all at once as in the show. The mic stays at the same spot for both ends.";
            return "Only " + page->sideName (*a, st) + " plays. The mic stays at the same spot for both ends.";
        }
        bool guidedStart() override
        {
            const int st = stageNow();
            if (st < 0) return false;
            page->mutesConfirmed = true;
            if (st == 2)
            {
                if (! page->appliedNow (*page->currentAlignment())) page->applyAlignment();
                page->startCheck (page->alSelected);
            }
            else page->startArrivalOne (page->alSelected, st);
            page->mutesConfirmed = false;
            redoCheck = -1;
            return page->phase != Phase::Idle;
        }
        void guidedStop() override { page->cancelMeasure ("Stopped."); }
        bool guidedBusy() const override { return page->phase != Phase::Idle; }
        float guidedProgress() const override { return page->progress; }
        juce::String guidedPhase() const override { return page->phaseText(); }
        int guidedSerial() const override { return page->measureSerial; }
        juce::String guidedStatus() const override { return page->status; }
        void guidedRedo (const std::vector<GuideSpot>& was) override
        {
            auto* a = page->currentAlignment();
            if (a == nullptr || was.empty()) return;
            if (was.front().id == 0) a->hasRef = false;
            else if (was.front().id == 1) a->hasAlign = false;
            else redoCheck = 2;
        }
        void guidedClosed() override { page->resized(); page->repaint(); }
        int guidedAimKind() const override { return RoomAim; }
        int guidedMic() const override { return page->sys.measureMic; }
        int redoCheck = -1;
    };
    AlignGuide alGuide;

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, systemCard.toFloat(), mode == 0 ? "Speakers" : "Pick a speaker");
        drawPanel (g, selCard.toFloat(), mode == 0 ? "Selected speaker" : (current() != nullptr ? nameOf (selected) : juce::String ("Selected speaker")),
                   current() != nullptr ? sourceColour (selected) : juce::Colour());
        if (mode == 0) paintOverview (g);
        paintStepCards (g);

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

        // the selected speaker, read-only on the tuning steps (it is edited on the Speakers page)
        if (const auto* s = current(); s != nullptr && mode > 0)
        {
            auto r = selRead;
            auto line = [&] (const juce::String& k, const juce::String& v)
            {
                auto row = r.removeFromTop (19);
                g.setColour (p.textDim);
                g.setFont (font (10.5f, true));
                g.drawText (k.toUpperCase(), row.removeFromLeft (64), juce::Justification::centredLeft);
                g.setColour (p.text);
                g.setFont (font (12.0f));
                g.drawText (v, row, juce::Justification::centredLeft, true);
            };
            line ("Name", nameOf (selected) + (s->model.isNotEmpty() ? "  (" + s->model + ")" : juce::String()));
            line ("Role", speakerRoleNames()[s->role] + ", " + speakerChannelNames()[s->channel]);
            line ("Output", (s->output == 0 ? juce::String ("All outputs") : "Output " + juce::String (s->output))
                                + (s->count > 1 ? ",  " + juce::String (s->count) + " cabinets" : juce::String()));
            line ("Group", sys.inGroup (selected) ? sys.groupName (selected) + (sys.master (selected) == selected ? " (lead)" : ", led by " + nameOf (sys.master (selected)))
                                                  : juce::String ("None (its own settings)"));
        }
        if (const auto* s = current())
        {
            g.setColour (s->hasResponse() ? p.textDim : p.warn);
            g.setFont (font (11.5f));
            juce::String t;
            if (s->hasResponse()) t << (s->imported ? "Imported: " : "Measured: ") << s->source;
            else if (sys.covered (selected))
                t << "Follows " << nameOf (sys.master (selected)) << " (imported): nothing to compare it with.";
            else if (sys.master (selected) != selected)
                t << "Not measured yet: its settings follow " << nameOf (sys.master (selected)) << "; measure it to check it matches.";
            else t << "Not measured yet.";
            if (s->hasArrival) t << "\nArrives " << juce::String (s->arrivalMs, 2) << " ms after the reference";
            double late = 0.0;
            if (sys.lateArrival (selected, late)) t << " (" << juce::String (late, 1) << " ms after the earliest: was the mic close?)";
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
            t << "Each speaker is timed to its own arrival; its polarity is judged from the same capture.";
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
        {
            // the processor sheet, in a line
            const int entered = sheet::enteredCount (sys), total = sheet::lineCount (sys);
            g.setColour (entered == total ? p.good : p.textDim);
            g.setFont (font (11.5f, entered == total));
            g.drawText (juce::String (entered) + " of " + juce::String (total) + " entered", sheetInfo, juce::Justification::centredRight, true);
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
                if (! sys.hasSub())
                    for (int i = 0; i < (int) sys.sources.size(); ++i)
                    {
                        xo::Filter f;
                        if (sys.topHighPass (i, f)) { t << "\nThe tops have no high-pass: a protective one (Butterworth 24 dB/oct, about " << juce::String (juce::roundToInt (f.freq)) << " Hz for " << nameOf (i) << ") keeps them from being driven below what they can play."; break; }
                    }
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
                t << resultText (r);
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
                g.drawText ("Sum through the crossover: " + juce::String (juce::roundToInt (q * 100.0)) + "% in phase, " + shapeText (liveShape)
                                + (liveReliable < 0.8 ? juce::String (" (") + juce::String (juce::roundToInt (liveReliable * 100.0)) + "% reliable)" : juce::String()),
                            liveLine, juce::Justification::centredLeft, true);
            }
        }

        if (status.isNotEmpty())
        {
            g.setColour (p.accent);
            g.setFont (font (11.5f, true));
            g.drawFittedText (status, statusLine, juce::Justification::centredLeft, 2);
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
        addButton.setVisible (mode == 0);
        removeButton.setVisible (mode == 0);
        systemCard = left.removeFromTop (34 + listH + (mode == 0 ? 46 : 10));
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
            const bool edit = mode == 0;   // the speakers are edited on the Speakers page only
            for (auto* comp : std::initializer_list<juce::Component*> { &nameEditor, &modelEditor, &roleBox, &channelBox, &outputBox, &countBox, &linkBox, &coverBox, &onboardButton, &protectButton })
                comp->setVisible (edit);
            editSpeakerButton.setVisible (! edit);
            auto field = [&] (const juce::String& name, juce::Component& comp, int h = 28)
            {
                auto row = c.removeFromTop (h);
                labels.push_back ({ row.removeFromLeft (64), name });
                comp.setBounds (row);
                c.removeFromTop (8);
            };
            selRead = {};
            if (edit)
            {
                field ("Name", nameEditor, 26);
                field ("Model", modelEditor, 26);
                field ("Role", roleBox, 26);
                field ("Channel", channelBox, 26);
                field ("Output", outputBox, 26);
                auto row = c.removeFromTop (26);
                labels.push_back ({ row.removeFromLeft (64), "Count" });
                countBox.setBounds (row.removeFromLeft (row.getWidth() * 45 / 100));
                row.removeFromLeft (6);
                linkBox.setBounds (row);
                c.removeFromTop (8);
                field ("Coverage", coverBox, 26);
                {
                    auto row = c.removeFromTop (26);
                    labels.push_back ({ row.removeFromLeft (64), "Built in" });
                    onboardButton.setBounds (row.removeFromLeft (row.getWidth() * 55 / 100));
                    row.removeFromLeft (6);
                    protectButton.setBounds (row);
                    c.removeFromTop (8);
                }
            }
            else
            {
                selRead = c.removeFromTop (4 * 19);
                c.removeFromTop (6);
                editSpeakerButton.setBounds (c.removeFromTop (26).withWidth (200));
                c.removeFromTop (8);
            }
            const bool resp = mode <= 1;
            importButton.setVisible (resp);
            clearButton.setVisible (resp);
            if (resp)
            {
                c.removeFromTop (4);
                auto row = c.removeFromTop (28);
                importButton.setBounds (row.removeFromLeft (row.getWidth() * 62 / 100));
                row.removeFromLeft (8);
                clearButton.setBounds (row);
                c.removeFromTop (10);
            }
            const auto* s = current();
            offsetSlider.setVisible (resp && s != nullptr && s->imported);
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
            micBox.setBounds (row);
            c.removeFromTop (10);
            guideArea = c.removeFromTop (118);
            c.removeFromTop (8);
            measureButton.setBounds (c.removeFromTop (36));
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
                xoInfo = c.removeFromTop (100);
                c.removeFromTop (6);
                removeXoButton.setBounds (c.removeFromTop (32));
                c.removeFromTop (6);
                topHpButton.setBounds (c.removeFromTop (32));
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
                    {
                        auto hr = c.removeFromTop (22);
                        procDetailsButton.setBounds (hr.removeFromRight (96).withHeight (20));
                        labels.push_back ({ hr.withHeight (16), "Filters your processor has" });
                    }
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

        // ---- middle: the graph, and a line with the status and the processor sheet ----
        {
            auto foot = r.removeFromBottom (32);
            r.removeFromBottom (8);
            sheetButton.setBounds (foot.removeFromRight (170).withSizeKeepingCentre (170, 30));
            foot.removeFromRight (10);
            sheetInfo = foot.removeFromRight (130);
            foot.removeFromRight (10);
            asFoundToggle.setVisible (mode == 1);
            if (mode == 1) { asFoundToggle.setBounds (foot.removeFromLeft (110).withSizeKeepingCentre (110, 26)); foot.removeFromLeft (8); }
            statusLine = foot;
        }
        // the Speakers tab shows the system as a table, the tuning steps the responses, time alignment the impulse responses
        graph.setVisible (mode == 1 || mode == 2);
        processedToggle.setVisible (mode == 1 || mode == 2);
        alImpulse.setBounds (r);
        for (int w = 0; w < 2; ++w) alPlayButtons[w].setBounds (alImpulse.paneButtonArea (w).translated (r.getX(), r.getY()));
        sheetButton.setVisible (mode > 0);
        overviewArea = r;
        graph.setBounds (r);
        processedToggle.setBounds (juce::Rectangle<int> (r.getRight() - 12 - 150, r.getY() + 5, 150, 24));
        graph.setLegendRight (162);
    }

private:
    enum class Phase { Idle, Settling, FindingTime, Averaging };

    // The Speakers tab's right side: every output as the processor will see it, and what the system asks for.
    juce::Rectangle<int> overviewArea;
    void paintOverview (juce::Graphics& g)
    {
        const auto& p = pal();
        drawPanel (g, overviewArea.toFloat(), "The system, output by output");
        auto r = overviewArea.reduced (16, 0).withTrimmedTop (40).withTrimmedBottom (12);
        if (sys.sources.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("Add the speakers: a left / right pair, subs, fills, delays. Each gets a role, a channel, a processor output and how widely it covers. "
                              "A mono sub is fed from both sides (L+R), so it measures with all outputs playing.", r.withHeight (60), juce::Justification::topLeft, 3);
            return;
        }
        static const char* heads[] = { "OUTPUT", "SPEAKER", "ROLE", "CHANNEL", "CABINETS", "COVERAGE", "GROUP" };
        const int w[] = { 70, 150, 110, 90, 80, 120, 0 };
        auto head = r.removeFromTop (20);
        g.setColour (p.textDim);
        g.setFont (font (10.5f, true));
        for (int c = 0; c < 7; ++c) g.drawText (heads[c], w[c] > 0 ? head.removeFromLeft (w[c]) : head, juce::Justification::centredLeft);
        r.removeFromTop (4);
        std::vector<int> order ((size_t) sys.sources.size());
        for (int i = 0; i < (int) order.size(); ++i) order[(size_t) i] = i;
        std::stable_sort (order.begin(), order.end(), [this] (int a, int b) { return (sys.sources[(size_t) a].output == 0 ? 99 : sys.sources[(size_t) a].output) < (sys.sources[(size_t) b].output == 0 ? 99 : sys.sources[(size_t) b].output); });
        for (int i : order)
        {
            const auto& s = sys.sources[(size_t) i];
            auto row = r.removeFromTop (30);
            if (row.getBottom() > overviewArea.getBottom() - 80) break;
            g.setColour (p.panelRaised.withAlpha (i == selected ? 0.9f : 0.4f));
            g.fillRoundedRectangle (row.toFloat(), 5.0f);
            auto cell = row.reduced (8, 0);
            g.setColour (sourceColour (i));
            g.fillRoundedRectangle (cell.removeFromLeft (4).toFloat().withSizeKeepingCentre (4.0f, 18.0f), 2.0f);
            cell.removeFromLeft (6);
            g.setFont (font (12.0f, true));
            g.setColour (p.text);
            g.drawText (s.output == 0 ? juce::String ("L + R") : "Out " + juce::String (s.output), cell.removeFromLeft (w[0] - 18), juce::Justification::centredLeft);
            g.drawText (nameOf (i) + (s.model.isNotEmpty() ? "  " + s.model : juce::String()), cell.removeFromLeft (w[1]), juce::Justification::centredLeft, true);
            g.setFont (font (12.0f));
            g.setColour (p.textDim);
            g.drawText (speakerRoleNames()[s.role], cell.removeFromLeft (w[2]), juce::Justification::centredLeft, true);
            g.drawText (speakerChannelNames()[s.channel], cell.removeFromLeft (w[3]), juce::Justification::centredLeft, true);
            g.drawText (juce::String (s.count), cell.removeFromLeft (w[4]), juce::Justification::centredLeft);
            g.drawText ("Q " + juce::String (juce::roundToInt (s.qFactor())), cell.removeFromLeft (w[5]), juce::Justification::centredLeft);
            g.drawText (sys.inGroup (i) ? sys.groupName (i) : juce::String ("Own"), cell, juce::Justification::centredLeft, true);
            r.removeFromTop (4);
        }
        r.removeFromTop (10);
        juce::StringArray notes;
        int subs = 0, tops = 0, fills = 0;
        for (const auto& s : sys.sources) { subs += s.role == (int) SpeakerRole::Sub; fills += s.role == (int) SpeakerRole::Fill || s.role == (int) SpeakerRole::Delay; tops += s.role == (int) SpeakerRole::Main; }
        notes.add (sys.xoNeeded() ? "A sub and tops: the Crossover step finds the hand-over between them." : "No sub: the Crossover step offers a protective high-pass for the tops instead.");
        if (fills > 0) notes.add (juce::String (fills) + (fills == 1 ? " fill or delay: Time alignment lines it up with the mains where they meet." : " fills or delays: Time alignment lines each up with the mains where they meet."));
        for (const auto& s : sys.sources) if (s.role == (int) SpeakerRole::Sub && s.channel == (int) SpeakerChannel::Mono && s.output != 0) { notes.add ("A mono sub is usually fed from both sides (L+R): set its output to All outputs if the processor sums it."); break; }
        notes.add ("Measuring, crossover, level and alignment are the tuning steps; this tab is only the set-up.");
        g.setColour (p.textDim);
        g.setFont (font (12.0f));
        g.drawFittedText (notes.joinIntoString ("\n"), r, juce::Justification::topLeft, 6);
    }
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
                                                                 &modelEditor, &countBox, &linkBox, &coverBox })
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
        {
            coverBox.clear (juce::dontSendNotification);
            coverBox.addItem ("Typical for a " + speakerRoleNames()[s->role].toLowerCase() + " (Q " + juce::String (juce::roundToInt (SpeakerSource::typicalQ (s->role))) + ")", 1);
            int sel = 1;
            const auto& pats = coverPatterns();
            for (size_t k = 0; k < pats.size(); ++k)
            {
                coverBox.addItem (juce::String (pats[k].name) + " (Q " + juce::String (juce::roundToInt (pats[k].q)) + ")", (int) k + 2);
                if (s->directivityQ > 0.0 && std::abs (s->directivityQ - pats[k].q) < 0.01) sel = (int) k + 2;
            }
            if (s->directivityQ > 0.0 && sel == 1)   // angles entered, or a value from an older venue that is not in the list
            {
                coverBox.addItem (s->coverH > 0.0 && s->coverV > 0.0
                                      ? juce::String (juce::roundToInt (s->coverH)) + " x " + juce::String (juce::roundToInt (s->coverV)) + " (Q " + juce::String (s->directivityQ, 1) + ")"
                                      : "Q " + juce::String (s->directivityQ, 1), 99);
                sel = 99;
            }
            coverBox.addItem ("Enter the angles...", 98);
            coverBox.setSelectedId (sel, juce::dontSendNotification);
        }
        onboardButton.setButtonText (s->onboard.present ? juce::String (s->onboard.lowPass ? "Own LPF " : "Own HPF ") + (s->onboard.setHz > 0.0 ? juce::String (juce::roundToInt (s->onboard.setHz)) + " Hz" : juce::String ("(bypassed)"))
                                                        : juce::String ("No own filter..."));
        protectButton.setButtonText (s->minSafeHpHz > 0.0 || s->rmsVoltLimit > 0.0
                                         ? (s->minSafeHpHz > 0.0 ? "HPF >= " + juce::String (juce::roundToInt (s->minSafeHpHz)) + " Hz" : juce::String()) + (s->rmsVoltLimit > 0.0 ? (s->minSafeHpHz > 0.0 ? ", " : "") + juce::String (juce::roundToInt (s->rmsVoltLimit)) + " V" : juce::String())
                                         : juce::String ("Limits..."));
        linkBox.clear (juce::dontSendNotification);
        linkBox.addItem ("Own settings (no group)", 1);
        for (int i = 0; i < (int) sys.sources.size(); ++i)
            if (i != selected && sys.sources[(size_t) i].linkTo < 0)
                linkBox.addItem ("Group with " + nameOf (i) + (sys.inGroup (i) ? " (" + sys.groupName (i) + ")" : juce::String()), i + 2);
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
        m.addItem (1, "Left / right pair (mains, one group)", room >= 2);
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
            auto leadOf = [&system] (SpeakerRole role)
            {
                for (int k = 0; k < (int) system.sources.size(); ++k)
                    if (system.sources[(size_t) k].role == (int) role && system.sources[(size_t) k].linkTo < 0) return k;
                return -1;
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
                case 1: { const int l = add ("Stereo (L)", SpeakerRole::Main, SpeakerChannel::Left); add ("Stereo (R)", SpeakerRole::Main, SpeakerChannel::Right, l); break; }
                // a speaker joins the group of its kind when there is one (its settings shared; "Own settings" splits it off)
                case 2:
                {
                    const int g = leadOf (SpeakerRole::Sub);
                    const int k = add (g >= 0 ? "Sub " + juce::String (system.sources.size() + 1) : juce::String ("Sub"), SpeakerRole::Sub, SpeakerChannel::Mono, g);
                    if (g < 0) system.sources[(size_t) k].output = 0;   // a mono sub is fed from both sides (L+R)
                    break;
                }
                case 3: add (leadOf (SpeakerRole::Fill) >= 0 ? "Front fill " + juce::String (system.sources.size() + 1) : juce::String ("Front fill"),
                             SpeakerRole::Fill, SpeakerChannel::Mono, leadOf (SpeakerRole::Fill)); break;
                case 4: add (leadOf (SpeakerRole::Delay) >= 0 ? "Delay " + juce::String (system.sources.size() + 1) : juce::String ("Delay"),
                             SpeakerRole::Delay, SpeakerChannel::Mono, leadOf (SpeakerRole::Delay)); break;
                case 5: add ("Centre", SpeakerRole::Main, SpeakerChannel::Centre, leadOf (SpeakerRole::Main)); break;
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
            safe->proc.level.speakerRemoved (i);
            safe->proc.coverage.speakerRemoved (i);
            safe->selected = juce::jmax (0, i - 1);
            safe->systemChanged (true);
        });
    }

    // The data sheet's coverage angles, for a speaker whose pattern is not in the list (a wide column, say).
    void enterCoverAngles()
    {
        auto* s = current();
        if (s == nullptr) return;
        angleWindow = std::make_unique<juce::AlertWindow> ("Coverage angles",
            "From the data sheet: the horizontal and vertical coverage (to -6 dB), in degrees. A wide column is about 150 x 20; "
            "a small point source 90 x 60.", juce::MessageBoxIconType::NoIcon, this);
        angleWindow->addTextEditor ("h", s->coverH > 0.0 ? juce::String (juce::roundToInt (s->coverH)) : juce::String ("90"), "Horizontal");
        angleWindow->addTextEditor ("v", s->coverV > 0.0 ? juce::String (juce::roundToInt (s->coverV)) : juce::String ("60"), "Vertical");
        angleWindow->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        angleWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SpeakersPage> safe (this);
        const int index = selected;
        angleWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe, index] (int result)
        {
            if (safe == nullptr || safe->angleWindow == nullptr) return;
            const double h = safe->angleWindow->getTextEditorContents ("h").getDoubleValue();
            const double v = safe->angleWindow->getTextEditorContents ("v").getDoubleValue();
            safe->angleWindow->setVisible (false);
            if (result == 1 && h >= 5.0 && v >= 5.0 && index >= 0 && index < (int) safe->sys.sources.size())
            {
                auto& src = safe->sys.sources[(size_t) index];
                src.coverH = std::min (h, 360.0);
                src.coverV = std::min (v, 360.0);
                src.directivityQ = SpeakerSource::qFromAngles (h, v);
            }
            safe->systemChanged (false);   // and back to what is set, on Cancel
        }), false);
    }
    std::unique_ptr<juce::AlertWindow> angleWindow;

    // The speaker's own (built-in) filter, as the data sheet gives it.
    void editOnboard()
    {
        auto* s = current();
        if (s == nullptr) return;
        auto& ob = s->onboard;
        angleWindow = std::make_unique<juce::AlertWindow> ("Built-in filter",
            "A filter inside the speaker (a powered sub's low-pass, a column's high-pass). Frequencies: fixed choices as \"80 / 100 / 120\", "
            "or a range as \"60 - 200\". Leave the frequencies empty for no built-in filter.", juce::MessageBoxIconType::NoIcon, this);
        angleWindow->addComboBox ("kind", { "High-pass", "Low-pass" }, "Type");
        angleWindow->getComboBoxComponent ("kind")->setSelectedItemIndex (ob.lowPass ? 1 : 0, juce::dontSendNotification);
        angleWindow->addComboBox ("family", { "Unknown", "Butterworth", "Linkwitz-Riley", "Bessel" }, "Family");
        angleWindow->getComboBoxComponent ("family")->setSelectedItemIndex (juce::jlimit (0, 3, ob.type), juce::dontSendNotification);
        angleWindow->addTextEditor ("slope", juce::String (ob.slope), "Slope (dB/oct)");
        juce::String fr;
        if (! ob.choices.empty()) { juce::StringArray c; for (double v : ob.choices) c.add (juce::String (juce::roundToInt (v))); fr = c.joinIntoString (" / "); }
        else if (ob.rangeHi > ob.rangeLo) fr = juce::String (juce::roundToInt (ob.rangeLo)) + " - " + juce::String (juce::roundToInt (ob.rangeHi));
        angleWindow->addTextEditor ("freqs", fr, "Frequencies (Hz)");
        angleWindow->addComboBox ("bypass", { "Can be bypassed", "Always in" }, "Bypass");
        angleWindow->getComboBoxComponent ("bypass")->setSelectedItemIndex (ob.bypassable ? 0 : 1, juce::dontSendNotification);
        angleWindow->addTextEditor ("measured", ob.measuredAtHz > 0.0 ? juce::String (juce::roundToInt (ob.measuredAtHz)) : juce::String(), "Curve taken with it at (Hz)");
        angleWindow->addTextEditor ("latency", ob.latencyMs >= 0.0 ? juce::String (ob.latencyMs, 2) : juce::String(), "DSP latency (ms, empty: unknown)");
        angleWindow->addTextEditor ("set", ob.setHz > 0.0 ? juce::String (juce::roundToInt (ob.setHz)) : juce::String(), "Set now at (Hz, empty: bypassed)");
        angleWindow->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        angleWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SpeakersPage> safe (this);
        const int index = selected;
        angleWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe, index] (int result)
        {
            if (safe == nullptr || safe->angleWindow == nullptr) return;
            auto& w = *safe->angleWindow;
            if (result == 1 && index >= 0 && index < (int) safe->sys.sources.size())
            {
                auto& o = safe->sys.sources[(size_t) index].onboard;
                SpeakerSource::Onboard n;
                n.lowPass = w.getComboBoxComponent ("kind")->getSelectedItemIndex() == 1;
                n.type = w.getComboBoxComponent ("family")->getSelectedItemIndex();
                n.slope = juce::jlimit (6, 48, (w.getTextEditorContents ("slope").getIntValue() / 6) * 6);
                const auto fr = w.getTextEditorContents ("freqs").trim();
                if (fr.containsChar ('-') && ! fr.containsChar ('/'))
                {
                    n.rangeLo = fr.upToFirstOccurrenceOf ("-", false, false).getDoubleValue();
                    n.rangeHi = fr.fromFirstOccurrenceOf ("-", false, false).getDoubleValue();
                }
                else
                {
                    juce::StringArray c;
                    c.addTokens (fr, "/,; ", {});
                    for (const auto& v : c) if (v.getDoubleValue() > 0.0) n.choices.push_back (v.getDoubleValue());
                    std::sort (n.choices.begin(), n.choices.end());
                }
                n.bypassable = w.getComboBoxComponent ("bypass")->getSelectedItemIndex() == 0;
                n.measuredAtHz = w.getTextEditorContents ("measured").getDoubleValue();
                const auto lat = w.getTextEditorContents ("latency").trim();
                n.latencyMs = lat.isEmpty() ? -1.0 : std::max (0.0, lat.getDoubleValue());
                n.setHz = w.getTextEditorContents ("set").getDoubleValue();
                n.present = ! n.choices.empty() || n.rangeHi > n.rangeLo;
                if (n.present && n.setHz > 0.0) n.setHz = n.snap (n.setHz);
                if (n.present && ! n.bypassable && n.setHz <= 0.0) n.setHz = n.snap (n.measuredAtHz > 0.0 ? n.measuredAtHz : 100.0);
                o = n;
            }
            safe->angleWindow->setVisible (false);
            safe->systemChanged (true);
        }), false);
    }

    // Protection from the data sheet: the lowest safe high-pass and the voltage limit.
    void editProtection()
    {
        auto* s = current();
        if (s == nullptr) return;
        angleWindow = std::make_unique<juce::AlertWindow> ("Limits from the data sheet",
            "The lowest high-pass the speaker should be run with (its \"recommended high-pass\"), and its RMS voltage limit (from the "
            "continuous power rating and impedance: V = sqrt (W x ohms)). Leave empty when not known.", juce::MessageBoxIconType::NoIcon, this);
        angleWindow->addTextEditor ("hp", s->minSafeHpHz > 0.0 ? juce::String (juce::roundToInt (s->minSafeHpHz)) : juce::String(), "Minimum safe high-pass (Hz)");
        angleWindow->addTextEditor ("slope", juce::String (s->minSafeHpSlope), "at (dB/oct)");
        angleWindow->addTextEditor ("volts", s->rmsVoltLimit > 0.0 ? juce::String (s->rmsVoltLimit, 1) : juce::String(), "RMS voltage limit (V)");
        angleWindow->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        angleWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SpeakersPage> safe (this);
        const int index = selected;
        angleWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe, index] (int result)
        {
            if (safe == nullptr || safe->angleWindow == nullptr) return;
            auto& w = *safe->angleWindow;
            if (result == 1 && index >= 0 && index < (int) safe->sys.sources.size())
            {
                auto& src = safe->sys.sources[(size_t) index];
                src.minSafeHpHz = std::max (0.0, w.getTextEditorContents ("hp").getDoubleValue());
                src.minSafeHpSlope = juce::jlimit (6, 48, (w.getTextEditorContents ("slope").getIntValue() / 6) * 6);
                src.rmsVoltLimit = std::max (0.0, w.getTextEditorContents ("volts").getDoubleValue());
            }
            safe->angleWindow->setVisible (false);
            safe->systemChanged (false);
        }), false);
    }

    // What the processor can do, as data: its filters' range and steps, the delay per output, the limiter's units,
    // and which outputs have a high-pass and a low-pass at all.
    void editProcessorDetails()
    {
        auto& c = sys.caps;
        angleWindow = std::make_unique<juce::AlertWindow> ("Processor details",
            "From its manual. No processor is named anywhere: only what it offers. Outputs without a filter: list them, e.g. \"HPF: 3 4\" "
            "means outputs 3 and 4 have no high-pass.", juce::MessageBoxIconType::NoIcon, this);
        angleWindow->addTextEditor ("lo", juce::String (juce::roundToInt (c.freqLo)), "Filter frequency from (Hz)");
        angleWindow->addTextEditor ("hi", juce::String (juce::roundToInt (c.freqHi)), "to (Hz)");
        angleWindow->addComboBox ("step", { "Free (any Hz)", "1 Hz steps", "Steps of 1/12 octave", "Steps of 1/24 octave", "Steps of 1/3 octave" }, "Frequency steps");
        angleWindow->getComboBoxComponent ("step")->setSelectedItemIndex (c.freqStepHz > 0.0 ? 1 : std::abs (c.freqStepOct - 1.0 / 12) < 1e-4 ? 2 : std::abs (c.freqStepOct - 1.0 / 24) < 1e-4 ? 3 : std::abs (c.freqStepOct - 1.0 / 3) < 1e-4 ? 4 : 0, juce::dontSendNotification);
        angleWindow->addTextEditor ("delay", c.delayMaxMs > 0.0 ? juce::String (c.delayMaxMs, 1) : juce::String(), "Delay per output, at most (ms; empty: no limit known)");
        angleWindow->addComboBox ("lim", { "dBu", "dBFS", "Volts" }, "Limiter threshold in");
        angleWindow->getComboBoxComponent ("lim")->setSelectedItemIndex (c.limiterUnit, juce::dontSendNotification);
        angleWindow->addTextEditor ("fs", juce::String (c.fullScaleDbu, 1), "0 dBFS at the output is (dBu)");
        juce::StringArray noHp, noLp;
        for (int o = 0; o < SpeakerSystem::kMaxOutputs; ++o) { if (! sys.outputHasHp[(size_t) o]) noHp.add (juce::String (o + 1)); if (! sys.outputHasLp[(size_t) o]) noLp.add (juce::String (o + 1)); }
        angleWindow->addTextEditor ("nohp", noHp.joinIntoString (" "), "Outputs with no high-pass");
        angleWindow->addTextEditor ("nolp", noLp.joinIntoString (" "), "Outputs with no low-pass");
        angleWindow->addButton ("Set", 1, juce::KeyPress (juce::KeyPress::returnKey));
        angleWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<SpeakersPage> safe (this);
        angleWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
        {
            if (safe == nullptr || safe->angleWindow == nullptr) return;
            auto& w = *safe->angleWindow;
            if (result == 1)
            {
                auto& cp = safe->sys.caps;
                cp.freqLo = juce::jlimit (1.0, 20000.0, w.getTextEditorContents ("lo").getDoubleValue());
                cp.freqHi = juce::jlimit (cp.freqLo, 40000.0, w.getTextEditorContents ("hi").getDoubleValue());
                const int st = w.getComboBoxComponent ("step")->getSelectedItemIndex();
                cp.freqStepHz = st == 1 ? 1.0 : 0.0;
                cp.freqStepOct = st == 2 ? 1.0 / 12 : st == 3 ? 1.0 / 24 : st == 4 ? 1.0 / 3 : 0.0;
                cp.delayMaxMs = std::max (0.0, w.getTextEditorContents ("delay").getDoubleValue());
                cp.limiterUnit = w.getComboBoxComponent ("lim")->getSelectedItemIndex();
                cp.fullScaleDbu = w.getTextEditorContents ("fs").getDoubleValue();
                auto parse = [] (const juce::String& t, std::vector<bool>& has)
                {
                    has.assign (SpeakerSystem::kMaxOutputs, true);
                    juce::StringArray a;
                    a.addTokens (t, " ,;", {});
                    for (const auto& v : a) { const int o = v.getIntValue(); if (o >= 1 && o <= SpeakerSystem::kMaxOutputs) has[(size_t) (o - 1)] = false; }
                };
                parse (w.getTextEditorContents ("nohp"), safe->sys.outputHasHp);
                parse (w.getTextEditorContents ("nolp"), safe->sys.outputHasLp);
            }
            safe->angleWindow->setVisible (false);
            safe->xoChanged();
        }), false);
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

    // "within 2.1 dB of flat (+2.1 dB at 82 Hz, -0.4 dB at 140 Hz)"
    static juce::String shapeText (const xo::SumShape& sh)
    {
        if (! sh.valid) return "sum not known";
        return "within " + juce::String (sh.maxDevDb, 1) + " dB of flat (" + signedText ((float) sh.peakDb, 1) + " dB at " + freqLabel (sh.peakHz) + " Hz, "
               + signedText ((float) sh.dipDb, 1) + " dB at " + freqLabel (sh.dipHz) + " Hz)";
    }

    // The result in words: the setting, who gets the delay, the overlap (low to high), and what the sum does
    // (a dip or a bump, from the computed sum itself, never from a guess).
    juce::String resultText (const xo::Result& r) const
    {
        const auto& hiName = sys.groupName (sys.xoHigh), loName = sys.groupName (sys.xoLow);
        juce::String t;
        t << "Best: " << xo::typeName (r.filter.type) << " " << r.filter.slope << " dB/oct at " << freqLabel (r.fc > 0.0 ? r.fc : r.filter.freq) << " Hz";
        if (r.lowFilter.active() && (r.lowFilter.type != r.filter.type || r.lowFilter.slope != r.filter.slope))
            t << " (" << loName << ": " << xo::typeName (r.lowFilter.type) << " " << r.lowFilter.slope << " dB/oct)";
        if (! r.lowFilter.active() && r.lowExtra.active()) t << " (" << loName << ": its own filter, processor low-pass OUT)";
        t << "; ";
        if (std::abs (r.delayLowMs) < 0.005) t << "no delay between them";
        else t << "delay the " << (r.delayLowMs > 0 ? loName : hiName) << " " << juce::String (std::abs (r.delayLowMs), 2) << " ms";
        if (r.invertLow) t << ", " << loName << " polarity inverted";
        if (sys.sources[(size_t) sys.xoHigh].dspLatencyUnknown() || sys.sources[(size_t) sys.xoLow].dspLatencyUnknown())
            t << " (model only: a speaker's DSP latency is unknown; measure it in Time alignment)";
        if (sys.caps.delayMaxMs > 0.0 && std::abs (r.delayLowMs) > sys.caps.delayMaxMs + 1e-6)
            t << " (more than the processor's " << juce::String (sys.caps.delayMaxMs, 1) << " ms per output: delay the " << (r.delayLowMs > 0 ? hiName : loName)
              << " by the rest, or put the delay on the other output)";
        t << ".\n";
        if (r.tag != SpeakerSystem::TagPlain || r.lowExtra.active() || r.highExtra.active())
            t << (r.tag == SpeakerSystem::TagStacked ? "Stacked (the processor's filter on top of the speaker's own): " : "") << sys.chainSettingsText (r) << "\n";
        // protection: what a lower crossover costs the top in headroom (its power below the safe high-pass is wasted
        // on excursion: roughly 3 dB of max output per halving of the frequency under it, at 24 dB/oct)
        {
            const auto& hiS = sys.sources[(size_t) sys.xoHigh];
            const double fcHp = r.filter.active() ? r.filter.freq : r.highExtra.active() ? r.highExtra.freq : r.fc;
            if (hiS.minSafeHpHz > 0.0 && fcHp < hiS.minSafeHpHz * 1.2)
            {
                const double costDb = 3.0 * std::max (0.0, std::log2 (hiS.minSafeHpHz / fcHp)) + (fcHp < hiS.minSafeHpHz ? 1.0 : 0.0);
                t << freqLabel (fcHp) << " Hz is " << (fcHp < hiS.minSafeHpHz ? "under" : "close to") << " the " << hiName << "'s safe high-pass ("
                  << freqLabel (hiS.minSafeHpHz) << " Hz, " << hiS.minSafeHpSlope << " dB/oct): it costs about " << juce::String (costDb, 1)
                  << " dB of their maximum output compared with " << freqLabel (hiS.minSafeHpHz) << " Hz. ";
            }
        }
        // the overlap, from the responses (low to high, always)
        const auto grid = xo::analysisGrid();
        const auto h = sys.groupResponse (sys.xoHigh, grid), l = sys.groupResponse (sys.xoLow, grid);
        double from = 0.0, to = 0.0;
        if (xo::overlapRange (h, l, r.gainLowDb, from, to)) t << "They overlap from " << freqLabel (from) << " to " << freqLabel (to) << " Hz (within 6 dB of each other).";
        else t << "No overlap: nowhere do both play within 6 dB of each other.";
        // the sum, from the result as it would be set
        xo::Processing ph, pl;
        ph.hp = r.filter;
        pl.lp = r.lowFilter;
        pl.gainDb = r.gainLowDb;
        pl.invert = r.invertLow;
        if (r.delayLowMs >= 0.0) pl.delayMs = r.delayLowMs; else ph.delayMs = -r.delayLowMs;
        const auto sh = xo::sumShape (h, l, ph, pl, r.fc > 0.0 ? r.fc : r.filter.freq, proc.pref ("xoEvalOct"));
        if (sh.valid)
        {
            t << " The sum is " << shapeText (sh);
            if (sh.bump()) t << ": expect a bump";
            else if (sh.dip()) t << ": expect a dip";
            t << ".";
            if (r.filter.type == xo::Butterworth && r.lowFilter.type == xo::Butterworth && sh.efficiency > 0.85 && sh.peakDb >= 1.0 && r.filter.slope % 12 == 0)
            {
                // two Butterworths in phase: each -3 dB at the crossover, so +3 dB together; the Linkwitz-Riley of the
                // same order (each -6 dB) sums flat. Say what it would give.
                xo::Processing ph2 = ph, pl2 = pl;
                ph2.hp.type = pl2.lp.type = xo::LinkwitzRiley;
                const auto lr = xo::sumShape (h, l, ph2, pl2, r.fc > 0.0 ? r.fc : r.filter.freq, proc.pref ("xoEvalOct"));
                t << " Two Butterworths in phase add up to a bump at the crossover (each is -3 dB there). Linkwitz-Riley " << r.filter.slope << " dB/oct at the same "
                  << freqLabel (r.fc > 0.0 ? r.fc : r.filter.freq) << " Hz would be " << shapeText (lr) << (sys.caps.types[xo::LinkwitzRiley] ? "." : " (not enabled for your processor).");
            }
        }
        return t;
    }

    void updateLive()
    {
        liveValid = false;
        if (! sys.pairValid()) return;
        const auto grid = xo::analysisGrid();
        const auto h = sys.groupResponse (sys.xoHigh, grid), l = sys.groupResponse (sys.xoLow, grid);
        if (h.empty() || l.empty()) return;
        const auto& ph = sys.sources[(size_t) sys.xoHigh].proc;
        const auto& pl = sys.sources[(size_t) sys.xoLow].proc;
        if (! ph.hp.active()) return;
        double eff = 0.0;
        xo::assess (h, l, ph, pl, ph.hp.freq, eff, settings.coherenceBlank.load(), &liveReliable, nullptr);
        liveEff = eff;
        liveShape = xo::sumShape (h, l, ph, pl, ph.hp.freq, proc.pref ("xoEvalOct"));   // as heard, not the optimiser's score
        liveDev = liveShape.maxDevDb;
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
        // each side as its group: the members' responses averaged (Main L and Main R together, every sub)
        int nh = 0, nl = 0;
        auto h = sys.groupResponse (sys.xoHigh, grid, &nh), l = sys.groupResponse (sys.xoLow, grid, &nl);
        if (h.empty() || l.empty()) { repaint(); return; }
        groupNote.clear();
        if (nh > 1 || nl > 1)
            groupNote = "From " + (nh > 1 ? sys.groupName (sys.xoHigh) + " (" + juce::String (nh) + " averaged)" : nameOf (sys.xoHigh)) + " and "
                        + (nl > 1 ? sys.groupName (sys.xoLow) + " (" + juce::String (nl) + " averaged)" : nameOf (sys.xoLow)) + ". ";
        searching = true;
        findButton.setEnabled (false);
        repaint();
        const auto caps = sys.caps;
        auto opt = sys.opt;
        opt.minCoherence = settings.coherenceBlank.load();
        if (caps.delayMaxMs > 0.0) { opt.delayMinMs = std::max (opt.delayMinMs, -caps.delayMaxMs); opt.delayMaxMs = std::min (opt.delayMaxMs, caps.delayMaxMs); }
        // the whole chain: the processor's filters where its outputs have them, the speakers' own filters where they
        // are always in, protection and the headroom goal
        {
            const int goal = juce::roundToInt (proc.pref ("xoGoal"));
            const SpeakerSystem chain = sys;   // a copy for the worker thread
            opt.candidates = [chain, goal] (double fLo, double fHi) { return chain.chainCandidates (fLo, fHi, goal); };
            std::string why;
            if (chain.chainCandidates (opt.fMin, opt.fMax, goal, &why).empty()) { searching = false; findButton.setEnabled (true); status = juce::String (why); repaint(); return; }
        }
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
        if (groupNote.isNotEmpty()) sys.lastResult.note = (groupNote + juce::String (r.note)).trim().toStdString();
        if (r.valid && sys.pairValid())
        {
            auto& hi = sys.sources[(size_t) sys.xoHigh].proc;
            auto& lo = sys.sources[(size_t) sys.xoLow].proc;
            hi.hp = r.filter;
            lo.lp = r.lowFilter;
            // the speakers' own switches, as the result needs them
            if (r.lowExtra.active()) sys.sources[(size_t) sys.xoLow].onboard.setHz = r.lowExtra.freq;
            if (r.highExtra.active()) sys.sources[(size_t) sys.xoHigh].onboard.setHz = r.highExtra.freq;
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
            status = "Crossover set: enter its filters, polarity and level in the processor now (Processor sheet), then Level balance.";
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

    // Who else plays when `targets` are measured: speakers on the same output, or every speaker when one plays from
    // "All outputs" (or two targets on different outputs, which plays all of them). On most systems that is fine: the
    // others are muted at the console or processor for the measurement. Linked speakers share settings, not a feed.
    std::vector<int> alsoPlaying (const std::vector<int>& targets) const
    {
        std::vector<int> out;
        if (sys.sources.size() < 2 || targets.empty()) return out;
        std::vector<int> outs;
        for (int t : targets) if (t >= 0 && t < (int) sys.sources.size()) outs.push_back (sys.sources[(size_t) t].output);
        bool all = false;
        for (int o : outs) all = all || o == 0 || o != outs.front();
        for (int k = 0; k < (int) sys.sources.size(); ++k)
        {
            if (std::find (targets.begin(), targets.end(), k) != targets.end()) continue;
            const int o = sys.sources[(size_t) k].output;
            if (all || o == 0 || std::find (outs.begin(), outs.end(), o) != outs.end()) out.push_back (k);
        }
        return out;
    }

    // "Mute Main R and Sub so only Main L plays." Empty when nothing else plays.
    juce::String muteText (const std::vector<int>& targets) const
    {
        const auto others = alsoPlaying (targets);
        if (others.empty()) return {};
        juce::StringArray o, t;
        for (int k : others) o.add (nameOf (k));
        for (int k : targets) t.add (nameOf (k));
        auto join = [] (const juce::StringArray& a) { return a.size() <= 1 ? a.joinIntoString ("") : a.joinIntoString (", ", 0, a.size() - 1) + " and " + a[a.size() - 1]; };
        return "Mute " + join (o) + " on the console or processor, so only " + join (t) + (t.size() == 1 ? " plays." : " play.");
    }

    // Measures `go` once the other speakers are muted: asks first when they would play too (not in the guided pop-up,
    // where the instruction is shown and Measure confirms it).
    void withMutes (const std::vector<int>& targets, std::function<void()> go)
    {
        const auto text = muteText (targets);
        if (text.isEmpty() || mutesConfirmed) { go(); return; }
        juce::String why;
        for (int t : targets)
            if (t >= 0 && t < (int) sys.sources.size() && sys.sources[(size_t) t].output == 0) { why = nameOf (t) + " plays from All outputs. "; break; }
        if (why.isEmpty())
        {
            int first = -1;
            bool split = false;
            for (int t : targets)
            {
                if (t < 0 || t >= (int) sys.sources.size()) continue;
                const int o = sys.sources[(size_t) t].output;
                if (first < 0) first = o; else split = split || o != first;
            }
            why = split ? "These play from different outputs, so the generator plays all outputs. " : "These speakers share an output. ";
        }
        juce::Component::SafePointer<SpeakersPage> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::InfoIcon)
                                          .withTitle ("Mute the other speakers")
                                          .withMessage (why + text + "\n\nUnmute them again when you are done.")
                                          .withButton ("They're muted: measure")
                                          .withButton ("Cancel")
                                          .withAssociatedComponent (this),
                                      [safe, go] (int r)
                                      {
                                          if (safe == nullptr || r != 1) return;
                                          safe->mutesConfirmed = true;
                                          go();
                                          safe->mutesConfirmed = false;
                                      });
    }

    void startMeasure (int i)
    {
        if (i < 0 || i >= (int) sys.sources.size()) return;
        if (! mutesConfirmed && muteText ({ i }).isNotEmpty()) { withMutes ({ i }, [this, i] { startMeasure (i); }); return; }
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
        proc.setAimOverride (proc.speakerAim);   // the mic is pointed at the speaker: its 0 degree file (or the angle picked)
        settings.genOutput = sys.sources[(size_t) i].output;
        settings.genType = 0;   // pink noise
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

    void cancelMeasure (const juce::String& why)
    {
        settings.genOn = false;
        settings.captureAllIr = false;
        proc.setAimOverride (-1);
        if (phase == Phase::FindingTime || phase == Phase::Averaging)
            for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        phase = Phase::Idle;
        measuring = -1;
        arrivalMode = false;
        polarityMode = false;
        arrivalQueue.clear();
        refPending.clear();
        refCaptures.clear();
        arrivalMember = -1;
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
        const auto who = measuringName();
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
        if (juce::jmax (1, proc.numOutputs()) != nOuts) refreshChoices();   // the same count refreshChoices() keeps
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
        const double elapsed = la::clock::ms() - phaseStart;
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
                        a->checkFreq = overlapOf (*a);
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
                                phaseStart = la::clock::ms();
                                return;
                            }
                            a->sumBandDb = bandLevel (mic, a->checkFreq);
                            finishCheck();
                            return;
                        }
                        // the level in the band where they overlap comes from the transfer function, once averaged
                        (arrivalWhich == 0 ? a->refLevelDb : a->alignLevelDb) = -200.0f;
                        (arrivalWhich == 0 ? a->refBandDb : a->alignBandDb) = bandLevel (mic, a->checkFreq);
                    }
                delaySerial = proc.analyzer.readDelayReport().serial;
                settings.requestFindDelay = true;
                phase = Phase::FindingTime;
                phaseStart = la::clock::ms();
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
                    cancelMeasure ("No clear arrival of " + measuringName() + " at " + proc.mics.nameOf (mic)
                                   + ". Check that it is playing and the mic hears it, then measure again.");
                    return;
                }
                if (arrivalWhich == 0)
                {
                    // one side of a reference set: the end counts as measured once the nearer side is picked
                    a->hasRef = arrivalMember < 0;
                    a->refArrivalMs = rep.ms[(size_t) mic];
                    a->refDelayAtMeasure = sys.sources[(size_t) (arrivalMember >= 0 ? arrivalMember : a->ref)].proc.delayMs;
                    a->refInvertAtMeasure = sys.sources[(size_t) a->ref].proc.invert;
                }
                else
                {
                    a->hasAlign = true;
                    a->alignArrivalMs = rep.ms[(size_t) mic];
                    a->alignDelayAtMeasure = sys.sources[(size_t) a->align].proc.delayMs;
                    a->alignInvertAtMeasure = sys.sources[(size_t) a->align].proc.invert;
                }
                {
                    // then its transfer function and impulse response, timed the same way for both (the first arrival
                    // measured is time zero; a set's sides share it): the impulse responses show the offset, and a
                    // crossover pair matches its phase
                    const bool firstOfRef = arrivalWhich == 0 && (arrivalMember < 0 || refCaptures.empty());
                    if (firstOfRef || a->lockMs <= 0.0) a->lockMs = rep.ms[(size_t) mic];
                    if (firstOfRef) a->alignTf = {};
                    settings.micDelayMs[mic] = (float) a->lockMs;
                    settings.requestResetAverages = true;
                    sawReset = false;
                    overflowsSeen = proc.analyzer.overflowCount();
                    restarts = 0;
                    phase = Phase::Averaging;
                    phaseStart = la::clock::ms();
                    return;
                }
            }
            if (rep.serial != delaySerial)
            {
                auto& s = sys.sources[(size_t) measuring];
                const bool valid = mic < rep.micCount && rep.valid[(size_t) mic];
                peakKnown = valid && rep.confidenceDb[(size_t) mic] >= 12.0;
                peakInverted = valid && rep.inverted[(size_t) mic];
                capArrivalMs = valid ? rep.ms[(size_t) mic] : -1.0;
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
                    phaseStart = la::clock::ms();
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
                phaseStart = la::clock::ms();
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
                phaseStart = la::clock::ms();
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
        if (s.asFound.freq.size() < 4 || s.imported)
        {
            // as found: the first time this speaker is measured (its processing then), for the before / after view
            s.asFound = {};
            for (size_t k = 0; k < t.freq.size(); k += 2) { s.asFound.freq.push_back (t.freq[k]); s.asFound.db.push_back (t.db[k]); }
            s.asFoundProc = s.proc;
            s.asFoundWhen = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        }
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
        if (const auto m = matchText (idxOf (s)); m.isNotEmpty()) status << " " << m;
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
            if (! s.imported && (sys.master (i) != i || s.count > 1))
                return { shortForm ? juce::String ("Polarity: not checked") : "Polarity not checked yet: measure " + nameOf (i) + ".", p.textDim };
            return {};
        }
        if (s.count == 1)
        {
            if (shortForm)
            {
                const auto* pc0 = s.polarityOf (0);
                const int vv = v == polarity::Unknown ? pc0->verdict : v;
                juce::String t = juce::String ("Polarity: ") + polarity::verdictName (vv);
                if (pc0->against >= 0 && pc0->against != i && pc0->relative == polarity::Normal) t << " (matches " << nameOf (pc0->against) << ")";
                if (pc0->against >= 0 && pc0->against != i && pc0->relative == polarity::Inverted) t << " (OPPOSITE to " << nameOf (pc0->against) << ")";
                if (pc0->notClose) t = "Polarity: check (mic not close?)";
                if (pc0->atCrossover && vv == polarity::Unknown) return { "Polarity: checked at the crossover", p.textDim };
                return { t, vv == polarity::Normal ? p.good : vv == polarity::Inverted ? p.bad : p.warn };
            }
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

    // Works out a cabinet's polarity from a close-up capture `t` timed to its own arrival, and the impulse sign from
    // the timing search. A speaker set "Same as" another (and each further cabinet on one output) is compared with that
    // one: the firmest test, and given as how the two relate ("matches Main L", "OPPOSITE to Main L"). On its own,
    // "inverted" needs the phase and the impulse to agree; one sign alone is "check". A capture that arrived much later
    // than the other close-up ones was probably not taken close up: its own reading is not trusted.
    void notePolarity (int i, int cab, int mic, const FreqTable& measured)
    {
        if (i < 0 || i >= (int) sys.sources.size()) return;
        auto& s = sys.sources[(size_t) i];
        auto* pc = s.polarityOf (cab);
        if (pc == nullptr) return;
        const FreqTable& t = measured;
        const double minCoh = juce::jmax (0.2f, settings.coherenceBlank.load());
        auto own = polarity::absolute (t, minCoh, peakKnown, peakInverted, 1.0);
        *pc = {};
        // the capture is taken with the processing in place: an output inverted on purpose (a sub inverted by the
        // crossover, say) measures inverted and is right
        if (s.proc.invert)
        {
            pc->processorInverted = true;
            if (own.verdict == polarity::Inverted) own.verdict = polarity::Normal;
            else if (own.verdict == polarity::Normal) own.verdict = polarity::Inverted;
        }
        pc->own = own.verdict;
        pc->verdict = own.verdict;
        pc->phaseDeg = own.band.phaseDeg;
        pc->peakInverted = s.proc.invert ? ! peakInverted : peakInverted;
        // a sub's impulse rings: close up its own polarity is rarely clear; the crossover judges it against the tops
        if (s.role == (int) SpeakerRole::Sub && own.verdict == polarity::Unsure) { pc->verdict = polarity::Unknown; pc->atCrossover = true; }
        pc->when = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        pc->mic = proc.mics.nameOf (mic);
        {
            double lo, hi;
            sys.playRange (i, lo, hi);
            pc->levelDb = SpeakerSystem::bandLevel (t, lo, hi);
        }
        // much later than the other close-up captures: probably not close
        if (capArrivalMs > 0.0)
        {
            double first = 1e9;
            for (int k = 0; k < (int) sys.sources.size(); ++k)
            {
                const auto& o = sys.sources[(size_t) k];
                if (o.imported || ! o.nearField || ! o.hasArrival || (k == i && cab == 0)) continue;
                if ((o.role == (int) SpeakerRole::Sub) != (s.role == (int) SpeakerRole::Sub)) continue;   // subs arrive later than tops
                first = std::min (first, o.arrivalMs);
            }
            if (first < 1e8 && capArrivalMs > first + SpeakerSystem::kLateMs)
            {
                pc->notClose = true;
                if (pc->verdict == polarity::Inverted || pc->verdict == polarity::Normal) pc->verdict = polarity::Unsure;
            }
        }
        // against the speaker it should match: its "Same as" speaker, or its own first cabinet
        const int m = sys.master (i);
        const int refIdx = m != i ? m : (cab > 0 ? i : -1);
        if (refIdx < 0) return;
        const auto& ref = sys.sources[(size_t) refIdx];
        if (! ref.hasResponse() || ref.imported) return;
        const auto rel = polarity::relative (t, ref.response, minCoh);
        pc->against = refIdx;
        pc->relative = rel.verdict;
        const auto* refPc = ref.polarityOf (0);
        const int refV = refPc != nullptr ? refPc->verdict : polarity::Unknown;
        if (rel.verdict == polarity::Normal)
            pc->verdict = refV == polarity::Inverted ? polarity::Inverted : refV == polarity::Unsure ? polarity::Unsure : polarity::Normal;
        else if (rel.verdict == polarity::Inverted)
            pc->verdict = refV == polarity::Inverted ? polarity::Normal : polarity::Inverted;   // opposite to one that is itself inverted
    }

    // "Main R against Main L: level +0.3 dB, response within 1.1 dB": a speaker set "Same as" another, from the
    // close-up captures (same model, same distance).
    juce::String matchText (int i) const
    {
        if (i < 0 || i >= (int) sys.sources.size()) return {};
        const int m = sys.master (i);
        if (m == i) return {};
        const auto& a = sys.sources[(size_t) i];
        const auto& b = sys.sources[(size_t) m];
        if (! a.hasResponse() || ! b.hasResponse() || a.imported || b.imported) return {};
        double lo, hi;
        sys.playRange (m, lo, hi);
        const double la = SpeakerSystem::bandLevel (a.response, lo, hi), lb = SpeakerSystem::bandLevel (b.response, lo, hi);
        if (! std::isfinite (la) || ! std::isfinite (lb)) return {};
        const double d = la - lb;
        // shape: the RMS difference through the range, 1/3 octave, with the level taken out
        double sum = 0.0;
        int n = 0;
        for (double f = lo; f <= hi; f *= 1.26)
        {
            double sa = 0.0, sb = 0.0;
            int k = 0;
            for (double x = f / 1.12; x <= f * 1.12; x *= 1.02) { sa += a.response.dbAt (x); sb += b.response.dbAt (x); ++k; }
            if (k == 0) continue;
            const double v = (sa - sb) / k - d;
            if (! std::isfinite (v)) continue;
            sum += v * v;
            ++n;
        }
        const double shape = n > 0 ? std::sqrt (sum / n) : NAN;
        juce::String t = nameOf (i) + " against " + nameOf (m) + ": level " + (d >= 0.0 ? "+" : "") + juce::String (d, 1) + " dB";
        if (std::isfinite (shape)) t << ", response within " << juce::String (shape, 1) << " dB";
        t << (std::abs (d) > 1.0 ? " (more than 1 dB apart: check its amp channel and gain)" : "") << ".";
        return t;
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
        if (pc != nullptr && pc->atCrossover && pc->relative != polarity::Inverted)
            return cabinetName (i, cab) + ": a sub's close-up capture can't settle its polarity; the Crossover step checks it against the tops.";
        if (pc == nullptr || ! pc->done()) return {};
        const bool against = pc->against >= 0 && pc->against < (int) sys.sources.size()
                             && (pc->relative == polarity::Normal || pc->relative == polarity::Inverted);
        const auto other = against ? (pc->against == i ? juce::String ("cabinet 1") : nameOf (pc->against)) : juce::String();
        const auto who = cabinetName (i, cab);
        if (pc->notClose)
            return who + ": arrived much later than the other close-up captures, so the mic was probably not close. Measure it again " + proc.closeUpShort() + " from the cone to check its polarity.";
        if (against && pc->relative == polarity::Inverted)
            return who + " is OPPOSITE to " + other + ": one of the two is wired the other way round"
                   + (pc->verdict == polarity::Normal ? juce::String (" (") + other + " looks inverted)." : juce::String (". Check its wiring and the processor's polarity for this output."));
        if (against && pc->relative == polarity::Normal)
        {
            if (pc->verdict == polarity::Inverted) return who + " matches " + other + ", and both look inverted: check the wiring of both.";
            if (pc->verdict == polarity::Unsure) return who + " matches " + other + " (whose own polarity is not clear).";
            return who + " matches " + other + ": polarity normal.";
        }
        switch (pc->verdict)
        {
            case polarity::Normal: return pc->processorInverted ? "Polarity: as set (the processor inverts this output)." : "Polarity: normal.";
            case polarity::Inverted: return "Polarity looks INVERTED (the phase and the impulse agree): check the wiring and that the processor's polarity for this output is normal.";
            case polarity::Unsure:
                return pc->own == polarity::Unsure && std::abs (pc->phaseDeg) > 120.0
                           ? "Polarity: check. The phase alone points to inverted, which a close-up capture of one box cannot confirm; compare it with a matching speaker, or use a polarity tester."
                           : "Polarity: not clear. Measure again closer to the cone, or check with a polarity tester.";
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
        for (auto* c : std::initializer_list<juce::Component*> { &micBox, &minCohBox, &measureButton })
            c->setVisible (resp);
        relockButton.setVisible (false);
        const bool xo = mode == 2 && sys.xoNeeded();
        for (auto* c : std::initializer_list<juce::Component*> { &highBox, &lowBox, &swapPairButton, &autoButton, &manualButton })
            c->setVisible (xo);
        for (auto* c : std::initializer_list<juce::Component*> { &rangeBox, &findButton, &procDetailsButton }) c->setVisible (xo && ! xoManual);
        for (auto* c : std::initializer_list<juce::Component*> { &freqSlider, &typeBox, &slopeBox, &delaySlider, &gainSlider, &invertButton })
            c->setVisible (xo && xoManual);
        invertedSwapButton.setVisible (xo && sys.pairInverted());
        for (auto& b : typeButtons) b.setVisible (xo && ! xoManual);
        for (auto& b : slopeButtons) b.setVisible (xo && ! xoManual);
        removeXoButton.setVisible (mode == 2 && ! sys.strayCrossoverFilters().isEmpty());
        {
            bool want = false;
            for (int i = 0; i < (int) sys.sources.size() && ! want; ++i) { xo::Filter f; want = sys.topHighPass (i, f); }
            topHpButton.setVisible (mode == 2 && ! sys.xoNeeded() && want);
        }
        autoButton.setToggleState (! xoManual, juce::dontSendNotification);
        manualButton.setToggleState (xoManual, juce::dontSendNotification);
        for (auto* c : std::initializer_list<juce::Component*> { &alList, &alAddButton, &alRemoveButton, &alAlignBox, &alRefBox, &alSpotBox,
                                                                 &alExtraSlider, &alMeasureButton, &alSignalBox })
            c->setVisible (mode == 3);
        alApplyButton.setVisible (false);
        alRedoButton.setVisible (mode == 3 && currentAlignment() != nullptr && (currentAlignment()->hasRef || currentAlignment()->hasAlign));
        alImpulse.setVisible (mode == 3);
        for (int w = 0; w < 2; ++w)
        {
            const auto* a = currentAlignment();
            alPlayButtons[w].setVisible (mode == 3 && a != nullptr);
            if (a != nullptr)
            {
                const bool playing = phase != Phase::Idle && arrivalMode && ! checkMode && arrivalWhich == w;
                alPlayButtons[w].setButtonText (playing ? "Stop" : "Play " + sideName (*a, w));
                alPlayButtons[w].setEnabled (endsValid (*a) && (phase == Phase::Idle || playing) && (proc.isStandalone() || proc.uiAsStandalone));
            }
            alImpulse.measuringWhich = phase != Phase::Idle && arrivalMode && ! checkMode ? arrivalWhich : -1;
        }
        alImpulse.repaint();
    }

    bool allMeasured() const
    {
        if (sys.sources.empty()) return false;
        for (int i = 0; i < (int) sys.sources.size(); ++i) if (! sys.covered (i)) return false;
        return true;
    }

    bool crossoverSet() const { return sys.pairValid() && sys.sources[(size_t) sys.xoHigh].proc.hp.active(); }

    // Time alignment is needed when there are fills or delays, or a crossover pair measured close up (Stereo to Sub).
    // A symmetrical left / right pair is one set (Stereo) and needs no delay of its own; a trim between them stays optional.
    bool alignmentNeeded() const
    {
        bool left = false, right = false, fill = false;
        for (const auto& src : sys.sources)
        {
            fill = fill || src.role == (int) SpeakerRole::Fill || src.role == (int) SpeakerRole::Delay;
            left = left || (src.role == (int) SpeakerRole::Main && src.channel == (int) SpeakerChannel::Left);
            right = right || (src.role == (int) SpeakerRole::Main && src.channel == (int) SpeakerChannel::Right);
        }
        juce::ignoreUnused (left, right);   // a symmetrical left / right pair is one set: aligning L to R stays optional
        return fill || sys.pairNearField();   // a crossover pair measured close up gets its timing here
    }

    bool alignmentDone() const
    {
        if (sys.alignments.empty()) return ! alignmentNeeded();
        for (const auto& a : sys.alignments)
        {
            if (! a.measured() || a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) return false;
            if (! appliedNow (a)) return false;
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
        if (sys.alignments.empty()) return alignmentNeeded() ? "Stereo, sub and fills to line up" : "Optional";
        int done = 0;
        for (const auto& a : sys.alignments)
            if (a.measured() && a.ref < (int) sys.sources.size() && a.align < (int) sys.sources.size() && appliedNow (a))
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
        else if (const double rel = selected < (int) reliability.size() ? reliability[(size_t) selected] : -1.0; rel >= 0.0)
        {
            // nothing playing: the last capture of the selected speaker
            c = rel >= 0.8 ? p.good : rel >= 0.6 ? p.warn : p.bad;
            g.setColour (c.withAlpha (0.6f));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) juce::jlimit (0.0, 1.0, rel)), 4.0f);
            t << nameOf (selected) << ", last capture: " << juce::roundToInt (rel * 100.0) << "% of its range reliable"
              << (rel >= 0.8 ? "." : rel >= 0.6 ? ": fair." : ": poor, measure it again louder.");
        }
        else t = "Shown while a speaker is measured, and for the last capture of the selected speaker.";
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
            if (! sys.sources[(size_t) i].hasResponse() && sys.covered (i)) { v = "as " + nameOf (sys.master (i)); vc = p.textDim; }
            else if (! sys.sources[(size_t) i].hasResponse()) { v = "not measured"; vc = p.warn; }
            else if (rel < 0.0) v = "imported (no coherence)";
            else { v = juce::String (juce::roundToInt (rel * 100.0)) + "%"; vc = rel >= 0.8 ? p.good : rel >= 0.6 ? p.warn : p.bad; }
            g.setColour (vc);
            g.setFont (font (12.0f, true));
            g.drawText (v, line, juce::Justification::centredRight, true);
        }
        // speakers that should match ("Same as"): level and response from the close-up captures
        {
            juce::StringArray matches;
            for (int i = 0; i < (int) sys.sources.size(); ++i) if (const auto m = matchText (i); m.isNotEmpty()) matches.add (m);
            if (! matches.isEmpty() && r.getHeight() >= 40)
            {
                r.removeFromTop (6);
                g.setColour (p.textDim);
                g.setFont (font (11.0f, true));
                g.drawText ("MATCHING SPEAKERS", r.removeFromTop (16), juce::Justification::centredLeft);
                g.setFont (font (11.5f));
                for (const auto& m : matches)
                {
                    if (r.getHeight() < 30) break;
                    g.setColour (m.contains ("more than 1 dB") ? p.warn : p.text);
                    g.drawFittedText (m, r.removeFromTop (32), juce::Justification::topLeft, 2);
                }
            }
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
    const Alignment* alignmentAt (int i) const { return i >= 0 && i < (int) sys.alignments.size() ? &sys.alignments[(size_t) i] : nullptr; }
    Alignment* currentAlignment() { return alignmentAt (alSelected); }
    const Alignment* currentAlignment() const { return alSelected >= 0 && alSelected < (int) sys.alignments.size() ? &sys.alignments[(size_t) alSelected] : nullptr; }

    static juce::String spotOf (const Alignment& a) { return a.spot.isNotEmpty() ? a.spot : juce::String ("the mic position"); }

    // An alignment end: one speaker, or a set (the speaker's whole group, its lead and every speaker linked to it,
    // playing together). "Stereo" for Stereo (L) and Stereo (R); otherwise the names joined with " + ".
    std::vector<int> endMembers (int idx, bool set) const { return sys.endMembers (idx, set); }
    juce::String endName (int idx, bool set) const { return sys.endName (idx, set); }
    // 0 the reference end, 1 the end to align. `together` names who plays: "Stereo (L + R together)".
    std::vector<int> sideMembers (const Alignment& a, int which) const { return which == 0 ? endMembers (a.ref, a.refSet) : endMembers (a.align, a.alignSet); }
    juce::String sideName (const Alignment& a, int which, bool together = false) const
    {
        return which == 0 ? sys.endName (a.ref, a.refSet, together && a.refSet) : sys.endName (a.align, a.alignSet, together && a.alignSet);
    }
    // Every speaker that plays when both ends play together (for the check, and its mutes).
    std::vector<int> bothMembers (const Alignment& a) const
    {
        auto out = sideMembers (a, 0);
        for (int k : sideMembers (a, 1)) if (std::find (out.begin(), out.end(), k) == out.end()) out.push_back (k);
        return out;
    }
    // Two different ends that share no speaker.
    bool endsValid (const Alignment& a) const
    {
        const int n = (int) sys.sources.size();
        if (a.ref < 0 || a.align < 0 || a.ref >= n || a.align >= n || a.ref == a.align) return false;
        const auto r = sideMembers (a, 0);
        for (int k : sideMembers (a, 1)) if (std::find (r.begin(), r.end(), k) != r.end()) return false;
        return true;
    }
    // What is playing now, for the status line: the alignment end (or both, for the check), else the speaker measured.
    juce::String measuringName() const
    {
        if (arrivalMode && arrivalAlignment >= 0 && arrivalAlignment < (int) sys.alignments.size())
        {
            const auto& a = sys.alignments[(size_t) arrivalAlignment];
            if (! checkMode && arrivalMember >= 0) return nameOf (arrivalMember) + " on its own";
            return checkMode ? sideName (a, 0) + " and " + sideName (a, 1) : sideName (a, arrivalWhich, true);
        }
        return nameOf (measuring);
    }
    // A set as the reference end, not the crossover pair: its sides are measured one at a time, and the one to align
    // follows the side nearer to it (the louder one in its band at the spot). The pair plays together, as in the show.
    bool refOneByOne (const Alignment& a) const { return a.refSet && sideMembers (a, 0).size() > 1 && ! isXoPair (a); }
    // The reference as used: "Stereo (L)" when one side was picked, else the end's name.
    juce::String refUsedName (const Alignment& a) const
    {
        return a.refMemberUsed >= 0 && a.refMemberUsed < (int) sys.sources.size() ? nameOf (a.refMemberUsed) : sideName (a, 0);
    }
    // "Front fill to Stereo (L), the nearer side" when one side of the reference set was picked; empty otherwise.
    juce::String nearerSideText (const Alignment& a) const
    {
        if (a.refMemberUsed < 0 || a.refMemberUsed >= (int) sys.sources.size()) return {};
        return sideName (a, 1) + " to " + nameOf (a.refMemberUsed) + ", the nearer side";
    }
    // The frequency the two ends' levels are compared at: the crossover when a sub is involved, otherwise the middle
    // of the band the one to align plays (where a fill or delay overlaps the main).
    double levelFreqOf (const Alignment& a) const
    {
        const auto isSub = [this] (int i) { return i >= 0 && i < (int) sys.sources.size() && sys.sources[(size_t) i].role == (int) SpeakerRole::Sub; };
        if (isXoPair (a) || isSub (a.ref) || isSub (a.align)) return sys.overlapFreq (sys.xoHigh, sys.xoLow);
        double lo = 200.0, hi = 5000.0;
        sys.playRange (a.align, lo, hi);
        return juce::jlimit (100.0, 4000.0, std::sqrt (juce::jmax (20.0, lo) * juce::jmax (20.0, hi)));
    }
    // The frequency where the two ends overlap, for the check: the crossover for the crossover pair, else 500 Hz.
    double overlapOf (const Alignment& a) const { return isXoPair (a) ? sys.overlapFreq (sys.xoHigh, sys.xoLow) : 500.0; }

    // Combo box ids for the ends: a single speaker i is i + 1, a set led by i is kSetId + i + 1 (stable as speakers change).
    static constexpr int kSetId = 100;
    static int endIndexOfId (int id) { return id > kSetId ? id - kSetId - 1 : id - 1; }
    static bool endIsSetId (int id) { return id > kSetId; }
    int endIdOf (int idx, bool set) const
    {
        if (set && sys.inGroup (idx)) return kSetId + sys.master (idx) + 1;
        return idx + 1;
    }

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

    // The next alignment, by sets: Stereo (L + R together) to the sub first (the crossover pair's timing), then each
    // fill or delay (or fill set) to Stereo. A left / right pair is one set and is not lined up against itself; a
    // single speaker can still be picked by hand for a trim of its own.
    void addAlignment()
    {
        if ((int) sys.alignments.size() >= SpeakerSystem::kMaxAlignments || sys.sources.size() < 2) return;
        const auto& src = sys.sources;
        const int n = (int) src.size();
        // already lined up: an alignment whose ends play the same speakers (either way round)
        auto used = [this] (int r, bool rs, int al, bool as)
        {
            const auto R = endMembers (r, rs), A = endMembers (al, as);
            for (const auto& x : sys.alignments)
            {
                const auto r2 = endMembers (x.ref, x.refSet), a2 = endMembers (x.align, x.alignSet);
                if ((r2 == R && a2 == A) || (r2 == A && a2 == R)) return true;
            }
            return false;
        };
        // a speaker as an end: its whole group as a set when it has one
        auto asEnd = [this] (int i, int& idx, bool& set) { set = sys.inGroup (i); idx = set ? sys.master (i) : i; };

        int mainIdx = -1;
        for (int i = 0; i < n && mainIdx < 0; ++i) if (src[(size_t) i].role == (int) SpeakerRole::Main) mainIdx = i;
        for (int i = 0; i < n && mainIdx < 0; ++i) if (src[(size_t) i].role != (int) SpeakerRole::Sub) mainIdx = i;
        if (mainIdx < 0) mainIdx = 0;
        int mainEnd = mainIdx;
        bool mainSet = false;
        asEnd (mainIdx, mainEnd, mainSet);

        Alignment a;
        a.align = -1;
        auto tryPair = [&]
        {
            if (! sys.pairValid() || a.align >= 0) return;
            int hi, lo;
            bool hs, ls;
            asEnd (sys.xoHigh, hi, hs);
            asEnd (sys.xoLow, lo, ls);
            Alignment t;
            t.ref = hi; t.refSet = hs; t.align = lo; t.alignSet = ls;
            if (endsValid (t) && ! used (hi, hs, lo, ls)) a = t;
        };
        // 1. the crossover pair measured close up: its timing is set here (Stereo, both sides at once, to the sub)
        if (sys.pairNearField()) tryPair();
        // 2. each fill or delay (a fill set as one) to the main set
        for (int i = 0; i < n && a.align < 0; ++i)
        {
            const int role = src[(size_t) i].role;
            if (role != (int) SpeakerRole::Fill && role != (int) SpeakerRole::Delay) continue;
            int idx;
            bool set;
            asEnd (i, idx, set);
            if (idx != i) continue;   // a member: its lead stands for the set
            Alignment t;
            t.ref = mainEnd; t.refSet = mainSet; t.align = idx; t.alignSet = set;
            if (endsValid (t) && ! used (mainEnd, mainSet, idx, set)) a = t;
        }
        // 3. the crossover pair at FOH (measured from one spot), then anything else not lined up yet
        if (sys.realPair() && sys.xoNeeded()) tryPair();
        for (int i = 0; i < n && a.align < 0; ++i)
        {
            int idx;
            bool set;
            asEnd (i, idx, set);
            if (idx != i) continue;
            Alignment t;
            t.ref = mainEnd; t.refSet = mainSet; t.align = idx; t.alignSet = set;
            if (endsValid (t) && ! used (mainEnd, mainSet, idx, set)) a = t;
        }
        if (a.align < 0) { a.ref = mainIdx; a.refSet = false; a.align = mainIdx == 0 ? 1 : 0; a.alignSet = false; }   // picked by hand from here

        const auto& al = src[(size_t) a.align];
        const bool sub = al.role == (int) SpeakerRole::Sub || src[(size_t) a.ref].role == (int) SpeakerRole::Sub;
        const bool pair = al.role == (int) SpeakerRole::Main;
        a.spot = sub ? juce::String ("FOH") : pair ? juce::String ("Centre") : al.role == (int) SpeakerRole::Fill ? juce::String ("Front rows")
                                            : al.role == (int) SpeakerRole::Delay ? juce::String ("Delay area") : juce::String ("Overlap");
        if (a.spot.isNotEmpty() && ! proc.positions.contains (a.spot)) proc.positions.add (a.spot);
        a.precedenceMs = al.role == (int) SpeakerRole::Delay ? 5.0 : 0.0;
        sys.alignments.push_back (a);
        alSelected = (int) sys.alignments.size() - 1;
        openStepAt (2);
        systemChanged (false);
    }

    // Each end's choices: every speaker on its own (for a trim), and every group of more than one as a set.
    void fillEndBox (juce::ComboBox& b) const
    {
        b.clear (juce::dontSendNotification);
        for (int i = 0; i < (int) sys.sources.size(); ++i) b.addItem (nameOf (i), i + 1);
        bool any = false;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            if (sys.master (i) != i || ! sys.inGroup (i)) continue;
            if (! any) { b.addSeparator(); any = true; }
            b.addItem (sys.endName (i, true, true), kSetId + i + 1);
        }
    }

    void refreshAlignment()
    {
        alSelected = juce::jlimit (0, juce::jmax (0, (int) sys.alignments.size() - 1), alSelected);
        alList.setSelected (alSelected);
        fillEndBox (alAlignBox);
        fillEndBox (alRefBox);
        alSpotBox.clear (juce::dontSendNotification);
        for (int i = 0; i < proc.positions.size(); ++i) alSpotBox.addItem (proc.positions[i], i + 1);
        const auto* a = currentAlignment();
        for (auto* c : std::initializer_list<juce::Component*> { &alAlignBox, &alRefBox, &alSpotBox, &alExtraSlider, &alRemoveButton })
            c->setEnabled (a != nullptr);
        alAddButton.setEnabled ((int) sys.alignments.size() < SpeakerSystem::kMaxAlignments && sys.sources.size() > 1);
        if (a != nullptr)
        {
            alAlignBox.setSelectedId (endIdOf (a->align, a->alignSet), juce::dontSendNotification);
            alRefBox.setSelectedId (endIdOf (a->ref, a->refSet), juce::dontSendNotification);
            alSpotBox.setText (a->spot, juce::dontSendNotification);
            alExtraSlider.setValue (a->precedenceMs, juce::dontSendNotification);
        }
        refreshApply();
        refreshMeasureButton();
    }

    void refreshApply()
    {
        const auto* a = currentAlignment();
        const bool ok = a != nullptr && a->measured() && endsValid (*a);
        alApplyButton.setEnabled (ok);
        if (! ok) { alApplyButton.setButtonText ("Apply delay"); return; }
        const double need = a->neededMs (sys.refDelayNow (*a));
        const bool applied = appliedNow (*a);
        int lowIdx = -1;
        bool wantInvert = false;
        const juce::String inv = lowInvertWanted (*a, lowIdx, wantInvert) && wantInvert ? juce::String (" + invert") : juce::String();
        alApplyButton.setEnabled (! applied);
        alApplyButton.setButtonText (need < -0.005 ? "Apply " + juce::String (-need, 2) + " ms to " + sideName (*a, 0) : applied ? "Applied: " + juce::String (need, 2) + " ms" + inv + " on " + sideName (*a, 1)
                                                                               : "Apply " + juce::String (need, 2) + " ms" + inv + " to " + sideName (*a, 1));
    }

    // The delay goes on the align end. A set: every member moves by the same amount, so each keeps its own trim (its
    // offset from the align end's delay); the align end itself gets exactly the delay needed.
    void applyAlignment()
    {
        proc.noteAsFound();
        auto* a = currentAlignment();
        if (a == nullptr || ! a->measured() || ! endsValid (*a)) return;
        double need = a->neededMs (sys.refDelayNow (*a));
        juce::String swapped;
        if (need < -0.005)
        {
            // the aligned end is already the later one: delay the other one instead
            std::swap (a->ref, a->align);
            std::swap (a->refSet, a->alignSet);
            std::swap (a->refArrivalMs, a->alignArrivalMs);
            std::swap (a->refLevelDb, a->alignLevelDb);
            std::swap (a->refBandDb, a->alignBandDb);
            std::swap (a->refTf, a->alignTf);
            std::swap (a->refIr, a->alignIr);
            std::swap (a->refIrZero, a->alignIrZero);
            std::swap (a->refDelayAtMeasure, a->alignDelayAtMeasure);
            std::swap (a->refInvertAtMeasure, a->alignInvertAtMeasure);
            a->refMemberUsed = -1;   // the set now gets the delay as a whole (its measured side's arrival stands for it)
            a->xoOffsetMs = -a->xoOffsetMs;
            if (a->phaseValid) a->phaseDelayMs = -a->phaseDelayMs;
            swapped = " (" + sideName (*a, 0) + " was the later one, so " + sideName (*a, 1) + " is delayed instead)";
            need = a->neededMs (sys.refDelayNow (*a));
        }
        if (need >= -0.005)
        {
            const double target = juce::jmax (0.0, need);
            const double shift = target - sys.sources[(size_t) a->align].proc.delayMs;
            const auto members = sideMembers (*a, 1);
            for (int k : members)
            {
                auto& d = sys.sources[(size_t) k].proc.delayMs;
                d = k == a->align ? target : juce::jmax (0.0, d + shift);
            }
            // the crossover pair: the polarity its phase match asked for goes on the low side (every speaker of its
            // group, so a set's members all get it; syncLinks keeps them together after)
            juce::String inverted;
            int lowIdx = -1;
            bool wantInvert = false;
            if (lowInvertWanted (*a, lowIdx, wantInvert))
            {
                bool was = sys.sources[(size_t) lowIdx].proc.invert;
                for (int k : sys.groupOf (lowIdx)) sys.sources[(size_t) k].proc.invert = wantInvert;
                const auto lowName = sys.endName (lowIdx, sys.inGroup (lowIdx));
                if (wantInvert) inverted = " and invert " + lowName;
                else if (was) inverted = " and set " + lowName + " back to normal polarity";
            }
            proc.noteTunedConditions();
            const auto nearer = nearerSideText (*a);
            status = sideName (*a, 1) + " delay set to " + juce::String (target, 2) + " ms" + swapped
                     + (members.size() > 1 ? " (each of its speakers keeps its own trim)" : juce::String())
                     + (nearer.isNotEmpty() ? " (" + nearer + ")" : juce::String())
                     + inverted + ". Enter it in the processor, then check them together.";
            graph.rebuild();
        }
        systemChanged (false);
    }

    // The crossover pair, once its phase is matched: the polarity its low side needs now (relative to what each end
    // had while measured, and allowing for the high side having been changed since). False: not the pair, or no match.
    bool lowInvertWanted (const Alignment& a, int& lowIdx, bool& want) const
    {
        if (! isXoPair (a) || ! a.phaseValid) return false;
        const bool lowIsAlign = sys.endHas (a.align, a.alignSet, sys.xoLow);
        lowIdx = lowIsAlign ? a.align : a.ref;
        const int highIdx = lowIsAlign ? a.ref : a.align;
        const bool lowAtMeasure = lowIsAlign ? a.alignInvertAtMeasure : a.refInvertAtMeasure;
        const bool highAtMeasure = lowIsAlign ? a.refInvertAtMeasure : a.alignInvertAtMeasure;
        const bool highChanged = sys.sources[(size_t) highIdx].proc.invert != highAtMeasure;
        want = (lowAtMeasure != a.phaseInvert) != highChanged;
        return true;
    }
    // The pair's polarity is as the match asked (or there is nothing to ask).
    bool lowInvertApplied (const Alignment& a) const
    {
        int lowIdx = -1;
        bool want = false;
        if (! lowInvertWanted (a, lowIdx, want)) return true;
        for (int k : sys.groupOf (lowIdx)) if (sys.sources[(size_t) k].proc.invert != want) return false;
        return true;
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
                              ? "Music from the console: send it to only what each step names (both sides of a set at once; mute the rest on the console), "
                                "with the console's feed on the reference input. Put " + proc.mics.nameOf (sys.measureMic) + " where both ends cover the seats."
                              : "Put " + proc.mics.nameOf (sys.measureMic) + " where both ends cover the seats (FOH for Stereo and the sub, "
                                "the front rows for a fill). A set plays all its speakers at once; as the reference for a fill or delay, one side at a "
                                "time, so the fill follows the nearer side. Measure with the processor set as shown here.",
                          alHint, juce::Justification::topLeft, 3);

        const auto* a = currentAlignment();
        // the four steps, with the current one highlighted
        if (a != nullptr)
        {
            const int stage = alignmentStage();
            const bool oneByOne = refOneByOne (*a);
            const juce::String names[] = { oneByOne ? "Play each side of " + sideName (*a, 0) + " on its own" : "Play " + sideName (*a, 0) + " on its own",
                                           "Mute it, play " + sideName (*a, 1) + " on its own",
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
        if (a == nullptr) t = "Add an alignment: Stereo (L + R together) to the sub first, then each fill or delay to Stereo.";
        else if (endsValid (*a) && alignmentStage() <= 1)
        {
            const int st = alignmentStage();
            const int out = sys.sharedOutput (sideMembers (*a, st));
            const auto who = sideName (*a, st), other = sideName (*a, 1 - st);
            const bool set = sideMembers (*a, st).size() > 1;
            const bool oneByOne = st == 0 && refOneByOne (*a);
            if (oneByOne)
            {
                // each side alone in turn: a fill or delay follows the side nearer to it
                const auto members = sideMembers (*a, 0);
                bool ownOutputs = true;
                for (int k : members) ownOutputs = ownOutputs && muteText ({ k }).isEmpty();
                t << "In your processor, unmute " << sideName (*a, 0, true) << " and mute every other speaker. Press Play " << who
                  << ": each side plays on its own in turn, and " << other << " is lined up to the louder side here, the nearer one.";
                t << (ownOutputs ? "\nThe generator plays each side's output in turn." : "\nThe sides share an output: you are asked to swap the mutes between them.");
            }
            else
            {
                if (st == 0)
                    t << "In your processor, unmute " << (set ? sideName (*a, st, true) : who) << " and mute every other speaker. Press Play " << who
                      << ": LiveAlign plays pink noise and finds when it arrives at " << spotOf (*a) << ".";
                else
                    t << "Now mute " << other << " and unmute " << (set ? sideName (*a, st, true) : who) << ". Press Play " << who << ".";
                if (set) t << " Both sides play at once, as in the show.";
                t << (out > 0 ? "\nThe generator plays output " + juce::String (out) + "." : "\nThe generator plays all outputs: the processor's mutes pick what plays.");
            }
            c = p.text;
        }
        else if (endsValid (*a) && a->measured() && alignmentStage() >= 3)
        {
            const double need = a->neededMs (sys.refDelayNow (*a));
            t << sideName (*a, 1) << " delay " << juce::String (need, 2) << " ms is set";
            if (sideMembers (*a, 1).size() > 1) t << " (on each of its speakers, each keeping its own trim)";
            {
                int lowIdx = -1;
                bool wantInvert = false;
                if (lowInvertWanted (*a, lowIdx, wantInvert) && wantInvert) t << ", with " << sys.endName (lowIdx, sys.inGroup (lowIdx)) << " inverted";
            }
            if (const auto nearer = nearerSideText (*a); nearer.isNotEmpty()) t << " (" << nearer << ")";
            t << ". Enter it in the processor if you have not, unmute both and press Check.";
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
        else if (! endsValid (*a)) { t = "Pick two different ends: a set cannot include the speaker it is lined up with."; c = p.warn; }
        else if (! a->measured()) { t = "Not measured at " + spotOf (*a) + " yet."; c = p.warn; }
        else
        {
            const auto refName = refUsedName (*a), alName = sideName (*a, 1);
            const double diff = a->alignArrivalMs - a->refArrivalMs;
            const double refDelay = sys.refDelayNow (*a);
            const double need = a->neededMs (refDelay);
            if (const auto nearer = nearerSideText (*a); nearer.isNotEmpty())
                t << nearer << " (the louder of " << sideName (*a, 0) << " in its band at " << spotOf (*a) << ").\n";
            t << alName << " arrives " << juce::String (std::abs (diff), 2) << " ms " << (diff < 0.0 ? "before " : "after ") << refName
              << " at " << spotOf (*a) << ".";
            if (refDelay > 0.0) t << " " << refName << " has " << juce::String (refDelay, 2) << " ms of its own delay.";
            if (need < -0.005)
                t << "\n" << alName << " is already the later one: " << sideName (*a, 0) << " gets " << juce::String (-need, 2) << " ms instead.";
            else
            {
                t << "\nDelay " << alName << " " << juce::String (need, 2) << " ms";
                if (a->precedenceMs > 0.0) t << " (" << juce::String (a->precedenceMs, 1) << " ms extra so " << refName << " leads)";
                int lowIdx = -1;
                bool wantInvert = false;
                if (lowInvertWanted (*a, lowIdx, wantInvert) && wantInvert) t << " and invert " << sys.endName (lowIdx, sys.inGroup (lowIdx));
                t << ".";
                if (appliedNow (*a)) t << " Applied.";
            }
            if (a->refLevelDb > -150.0f && a->alignLevelDb > -150.0f)
            {
                const float d = a->alignLevelDb - a->refLevelDb;
                t << "\nLevel here at " << freqLabel (levelFreqOf (*a)) << " Hz: " << alName << " " << signedText (d, 1) << " dB against " << refName;
                const bool lr = ! a->refSet && ! a->alignSet && sys.sources[(size_t) a->align].role == (int) SpeakerRole::Main
                                && sys.sources[(size_t) a->ref].role == (int) SpeakerRole::Main;
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
        if (a == nullptr || ! endsValid (*a)) return;
        if (settings.refSource.load() == 0)
        {
            status = "Set a reference on Mic setup (the loopback, or the generator) to measure arrivals.";
            repaint();
            return;
        }
        const bool oneByOne = refOneByOne (*a);
        bool mutesChange = alSignal == 2 || muteText (sideMembers (*a, 1)).isNotEmpty();
        if (oneByOne) { for (int k : sideMembers (*a, 0)) mutesChange = mutesChange || muteText ({ k }).isNotEmpty(); }
        else mutesChange = mutesChange || muteText (sideMembers (*a, 0)).isNotEmpty();
        if (mutesChange)
        {
            // the mutes change between the captures: one at a time
            startArrivalOne (index, 0);
            return;
        }
        a->hasRef = a->hasAlign = false;
        a->refIr.clear();
        a->alignIr.clear();
        a->lockMs = 0.0;
        a->sumBandDb = -200.0f;
        a->phaseValid = false;
        a->refMemberUsed = -1;
        setXoOffset (*a);
        settings.captureAllIr = true;
        arrivalMode = true;
        checkMode = false;
        arrivalAlignment = index;
        arrivalQueue.clear();
        refCaptures.clear();
        refPending.clear();
        // the reference end: as set, or each side of the set on its own (the generator picks its output)
        if (oneByOne) for (int k : sideMembers (*a, 0)) arrivalQueue.push_back ({ index, 0, k });
        else arrivalQueue.push_back ({ index, 0, -1 });
        arrivalQueue.push_back ({ index, 1, -1 });
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        status.clear();
        nextArrival();
    }

    void nextArrival()
    {
        if (arrivalQueue.empty()) { cancelMeasure ("Nothing to measure."); return; }
        const auto next = arrivalQueue.front();
        arrivalQueue.erase (arrivalQueue.begin());
        const auto* a = alignmentAt (next.alignment);
        if (a == nullptr) { cancelMeasure ("The alignment was removed."); return; }
        if (a->ref < 0 || a->ref >= (int) sys.sources.size() || a->align < 0 || a->align >= (int) sys.sources.size())
        {
            cancelMeasure ("This alignment points at a speaker that is no longer in the system: remove it and add it again.");
            return;
        }
        arrivalWhich = next.which;
        arrivalMember = next.which == 0 && next.member >= 0 && next.member < (int) sys.sources.size() ? next.member : -1;
        measuring = arrivalMember >= 0 ? arrivalMember : arrivalWhich == 0 ? a->ref : a->align;
        // a set: every member plays, from their shared output or all outputs (the rest muted at the console);
        // one side of the reference set on its own: from its output
        settings.genOutput = arrivalMember >= 0 ? sys.sharedOutput ({ arrivalMember }) : sys.sharedOutput (sideMembers (*a, arrivalWhich));
        startSignal();
        settings.running = true;
        phase = Phase::Settling;
        phaseStart = la::clock::ms();
        settleUntil = proc.analyzer.samplesWritten() + (juce::uint64) (kSettleSeconds * rate());
        progress = 0.0f;
        refreshMeasureButton();
        repaint();
    }

    // The crossover pair: one end is (or is a set containing) the high side, the other the low side.
    // `highFirst` says which way round: the reference end has the high side.
    bool isXoPair (const Alignment& a, bool* highFirst = nullptr) const
    {
        if (! sys.pairValid()) return false;
        const bool rh = sys.endHas (a.ref, a.refSet, sys.xoHigh), rl = sys.endHas (a.ref, a.refSet, sys.xoLow);
        const bool ah = sys.endHas (a.align, a.alignSet, sys.xoHigh), al = sys.endHas (a.align, a.alignSet, sys.xoLow);
        if (rh && al && ! rl && ! ah) { if (highFirst != nullptr) *highFirst = true; return true; }
        if (rl && ah && ! rh && ! al) { if (highFirst != nullptr) *highFirst = false; return true; }
        return false;
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
        // the level where the two overlap, from the transfer function (the same generator level for both ends)
        (arrivalWhich == 0 ? a->refLevelDb : a->alignLevelDb) = tfBand (arrivalWhich == 0 ? a->refTf : a->alignTf, levelFreqOf (*a));
        // the impulse response (every mic's is captured while an arrival is measured)
        {
            const auto& src = ! r.ir.empty() ? r.ir : (snap->shownMic == mic ? snap->ir : std::vector<float>());
            const int zero = ! r.ir.empty() ? r.irZero : snap->irZero;
            (arrivalWhich == 0 ? a->refIr : a->alignIr) = src;
            (arrivalWhich == 0 ? a->refIrZero : a->alignIrZero) = zero;
            a->irRate = snap->sampleRate;
        }
        for (int m = 0; m < kMaxMics; ++m) settings.micDelayMs[m] = savedDelays[(size_t) m];
        if (arrivalWhich == 0 && arrivalMember >= 0)
        {
            // one side of the reference set: keep what it gave, the nearer side is picked once every side has played
            RefCapture cap;
            cap.member = arrivalMember;
            cap.arrivalMs = a->refArrivalMs;
            cap.delayAtMeasure = a->refDelayAtMeasure;
            cap.invertAtMeasure = a->refInvertAtMeasure;
            cap.levelDb = a->refLevelDb;
            cap.bandDb = a->refBandDb;
            cap.tf = a->refTf;
            cap.ir = a->refIr;
            cap.irZero = a->refIrZero;
            refCaptures.erase (std::remove_if (refCaptures.begin(), refCaptures.end(), [&] (const RefCapture& c) { return c.member == cap.member; }), refCaptures.end());
            refCaptures.push_back (std::move (cap));
            if (arrivalQueue.empty() && ! refPending.empty())
            {
                // the next side needs the mutes swapped: stop, then ask and go on
                const int index = arrivalAlignment;
                settings.genOn = false;
                settings.captureAllIr = false;
                phase = Phase::Idle;
                measuring = -1;
                arrivalMode = false;
                arrivalMember = -1;
                refreshMeasureButton();
                repaint();
                nextRefMember (index);
                return;
            }
        }
        if (! arrivalQueue.empty()) nextArrival();
        else finishArrival();
    }

    // Every side of the reference set has played: the one loudest in the align speaker's band at the spot is the
    // reference (the nearer side), and its capture stands for the end.
    void chooseRefMember (Alignment& a)
    {
        if (refCaptures.empty()) return;
        const double f = levelFreqOf (a);
        const RefCapture* best = nullptr;
        float bestDb = -300.0f;
        for (const auto& c : refCaptures)
        {
            const float db = tfBand (c.tf, f);
            if (best == nullptr || db > bestDb) { best = &c; bestDb = db; }
        }
        a.refMemberUsed = best->member;
        a.refArrivalMs = best->arrivalMs;
        a.refDelayAtMeasure = best->delayAtMeasure;
        a.refInvertAtMeasure = best->invertAtMeasure;
        a.refLevelDb = best->levelDb;
        a.refBandDb = best->bandDb;
        a.refTf = best->tf;
        a.refIr = best->ir;
        a.refIrZero = best->irZero;
        a.hasRef = true;
        refCaptures.clear();
        refPending.clear();
    }

    void finishArrival()
    {
        settings.genOn = false;
        settings.captureAllIr = false;
        phase = Phase::Idle;
        measuring = -1;
        arrivalMode = false;
        if (auto* a = alignmentAt (arrivalAlignment))
        {
            a->measuredOn = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
            const bool sides = ! refCaptures.empty();
            if (sides) chooseRefMember (*a);
            if (sides && a->refMemberUsed >= 0)
                status = "Measured " + sideName (*a, 0, true) + " one side at a time at " + spotName() + ": " + nearerSideText (*a) + ".";
            else
                status = "Measured " + sideName (*a, arrivalWhich, true) + " at " + spotName() + "."
                         + (a->refMemberUsed >= 0 ? " " + nearerSideText (*a) + "." : juce::String());
            // the crossover pair: the delay, and the polarity of the low side, that make them add up best through the crossover
            a->phaseValid = false;
            a->phaseInvert = false;
            if (isXoPair (*a) && a->refTf.freq.size() > 8 && a->alignTf.freq.size() > 8)
            {
                const double f = overlapOf (*a);
                double d = 0.0;
                bool inv = false;
                if (Alignment::phaseMatch (a->refTf, a->alignTf, f, d, inv)) { a->phaseValid = true; a->phaseDelayMs = d; a->phaseInvert = inv; }
                if (a->phaseValid)
                {
                    int lowIdx = -1;
                    bool want = false;
                    const bool invertLow = lowInvertWanted (*a, lowIdx, want) && want;
                    status = "Measured both: matched their phase through the crossover (" + freqLabel (f) + " Hz)"
                             + (invertLow ? ", with " + sys.endName (lowIdx, sys.inGroup (lowIdx)) + " inverted" : juce::String()) + ".";
                }
            }
        }
        arrivalMember = -1;
        arrivalAlignment = -1;
        ++measureSerial;
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
        const double need = a.neededMs (sys.refDelayNow (a));
        return need >= -0.005 && std::abs (sys.sources[(size_t) a.align].proc.delayMs - need) < 0.01 && lowInvertApplied (a);
    }

    juce::String alignmentActionText() const
    {
        const auto* a = currentAlignment();
        switch (alignmentStage())
        {
            case 0: return "Measure " + sideName (*a, 0) + " and " + sideName (*a, 1);
            case 1: return "Measure " + sideName (*a, 1);
            case 2:
            {
                const double need = a->neededMs (sys.refDelayNow (*a));
                return need < -0.005 ? "Apply " + juce::String (-need, 2) + " ms to " + sideName (*a, 0) : "Apply " + juce::String (need, 2) + " ms to " + sideName (*a, 1);
            }
            case 3: return alSignal == 2 ? juce::String ("Check: music on both") : juce::String ("Check: play both");
            case 4: return "Check again";
            default: return "Add an alignment";
        }
    }

    void alignmentAction()
    {
        const int st = alignmentStage();
        if (st < 0) { addAlignment(); return; }
        if (st == 2) { applyAlignment(); return; }
        if (! signalReady()) return;
        if (st == 4) alGuide.redoCheck = 2;
        if (onGuided) onGuided (alGuide);
        else if (st <= 1) startArrivalOne (alSelected, st);
        else startCheck (alSelected);
    }

    // The crossover's own offset, when this is the crossover pair measured close up.
    void setXoOffset (Alignment& a) const
    {
        a.xoOffsetMs = 0.0;
        if (! sys.pairNearField()) return;
        bool highFirst = true;
        if (isXoPair (a, &highFirst)) a.xoOffsetMs = highFirst ? sys.xoRelDelayMs : -sys.xoRelDelayMs;
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
        if (a == nullptr || ! endsValid (*a)) return;
        if (settings.refSource.load() == 0)
        {
            status = "Set a reference on Mic setup (the loopback, or the generator) to measure arrivals.";
            repaint();
            return;
        }
        if (! signalReady()) return;
        if (which == 0 && refOneByOne (*a))
        {
            // the reference set, one side at a time
            a->hasRef = false;
            a->refIr.clear();
            a->lockMs = 0.0;
            a->sumBandDb = -200.0f;
            a->phaseValid = false;
            a->refMemberUsed = -1;
            setXoOffset (*a);
            refCaptures.clear();
            refPending = sideMembers (*a, 0);
            nextRefMember (index);
            return;
        }
        const auto who = sideMembers (*a, which);   // a set: all its speakers play at once
        if (alSignal != 2 && ! mutesConfirmed && muteText (who).isNotEmpty())
        {
            withMutes (who, [this, index, which] { startArrivalOne (index, which); });
            return;
        }
        (which == 0 ? a->hasRef : a->hasAlign) = false;
        (which == 0 ? a->refIr : a->alignIr).clear();
        if (which == 0) a->refMemberUsed = -1;
        a->sumBandDb = -200.0f;
        a->phaseValid = false;
        setXoOffset (*a);
        settings.captureAllIr = true;
        arrivalMode = true;
        checkMode = false;
        arrivalAlignment = index;
        arrivalQueue = { { index, which, -1 } };
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        status.clear();
        nextArrival();
    }

    // The next side of the reference set on its own (asking for the mutes when it shares an output); when every side
    // has played, the nearer one is picked.
    void nextRefMember (int index)
    {
        auto* a = alignmentAt (index);
        if (a == nullptr || ! endsValid (*a)) { refPending.clear(); refCaptures.clear(); return; }
        if (refPending.empty())
        {
            arrivalAlignment = index;
            arrivalWhich = 0;
            finishArrival();
            return;
        }
        const int member = refPending.front();
        if (alSignal != 2 && ! mutesConfirmed && muteText ({ member }).isNotEmpty())
        {
            withMutes ({ member }, [this, index] { nextRefMember (index); });
            return;
        }
        refPending.erase (refPending.begin());
        settings.captureAllIr = true;
        arrivalMode = true;
        checkMode = false;
        arrivalAlignment = index;
        arrivalQueue = { { index, 0, member } };
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        status.clear();
        nextArrival();
    }

    // Both ends together (every speaker of a set): the level where they overlap, against each alone.
    void startCheck (int index)
    {
        auto* a = alignmentAt (index);
        if (a == nullptr || ! a->measured() || ! endsValid (*a)) return;
        if (! signalReady()) return;
        const auto both = bothMembers (*a);
        if (alSignal != 2 && ! mutesConfirmed && muteText (both).isNotEmpty())
        {
            withMutes (both, [this, index] { startCheck (index); });
            return;
        }
        for (int m = 0; m < kMaxMics; ++m) savedDelays[(size_t) m] = settings.micDelayMs[m].load();
        arrivalMode = true;
        checkMode = true;
        arrivalAlignment = index;
        measuring = a->ref;
        settings.genOutput = sys.sharedOutput (both);   // both ends: one shared output, or all of them
        startSignal();
        settings.running = true;
        phase = Phase::Settling;
        phaseStart = la::clock::ms();
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
        ++measureSerial;
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
        const bool near = true;
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
        g.drawText (near ? proc.closeUpShort() : juce::String ("FOH"), juce::Rectangle<float> (box.getRight(), my + 15.0f, mx - box.getRight(), 12.0f), juce::Justification::centred);

        // the instruction
        r.reduce (6, 8);
        const int guideMic = sys.micFor (selected, settings.micCount.load());
        const auto micName = proc.mics.nameOf (guideMic), who = nameOf (selected);
        juce::String t;
        juce::ignoreUnused (near);
        t << "Place " << micName << " " << proc.closeUpText() << " from the centre of " << who << "'s woofer cone, pointed at it, "
          << "level with the cone. Measure goes through every speaker in turn.";
        if (proc.calibrations (guideMic).any()) t << " " << aimHowText (proc, guideMic, GuidedSource::SpeakerAim).substring (0, 1).toUpperCase()
                                                    << aimHowText (proc, guideMic, GuidedSource::SpeakerAim).substring (1) << ".";
        if (const auto m = muteText ({ selected }); m.isNotEmpty()) t << " " << m;
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

        // time alignment: where each pair of ends (a speaker or a set) was lined up, and whether the delay is in the table above
        bool head = false;
        for (const auto& a : sys.alignments)
        {
            if (! a.measured() || a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) continue;
            if (! head) { out << (forScreen ? "#TIME ALIGNMENT\n" : "\nTime alignment:\n"); head = true; }
            const double need = a.neededMs (sys.refDelayNow (a));
            out << sideName (a, 1) << " to " << sideName (a, 0);
            if (a.refMemberUsed >= 0) out << " (" << refUsedName (a) << ", the nearer side)";
            out << " at " << spotOf (a) << ": ";
            if (need < -0.005) out << "delay " << sideName (a, 0) << " " << juce::String (-need, 2) << " ms instead, not applied yet";
            else
            {
                out << "delay " << juce::String (need, 2) << " ms";
                if (a.precedenceMs > 0.0) out << " (incl. " << juce::String (a.precedenceMs, 1) << " ms extra)";
                int lowIdx = -1;
                bool wantInvert = false;
                if (lowInvertWanted (a, lowIdx, wantInvert) && wantInvert) out << ", " << sys.endName (lowIdx, sys.inGroup (lowIdx)) << " inverted";
                out << (appliedNow (a) ? ", applied" : ", not applied yet");
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
    juce::ComboBox roleBox, channelBox, outputBox, micBox, highBox, lowBox, rangeBox, typeBox, slopeBox, coverBox;
    // Coverage patterns (horizontal x vertical) and their directivity factor, Q = 180 / asin (sin (h/2) sin (v/2)) (Molloy).
    struct CoverPattern { const char* name; double q, h = 0.0, v = 0.0; };
    static const std::vector<CoverPattern>& coverPatterns()
    {
        static const std::vector<CoverPattern> v = [] {
            std::vector<CoverPattern> r;
            auto q = [] (double h, double vv)
            {
                const double a = std::asin (std::sin (juce::degreesToRadians (h / 2.0)) * std::sin (juce::degreesToRadians (vv / 2.0)));
                return std::round (180.0 / juce::radiansToDegrees (a) * 10.0) / 10.0;
            };
            r.push_back ({ "Nearly all round (sub, omni)", 2.0 });
            r.push_back ({ "150 x 20 (wide column)", q (150, 20), 150, 20 });
            r.push_back ({ "150 x 40 (wide column)", q (150, 40), 150, 40 });
            r.push_back ({ "120 x 60", q (120, 60), 120, 60 });
            r.push_back ({ "120 x 30 (column)", q (120, 30), 120, 30 });
            r.push_back ({ "90 x 90", q (90, 90), 90, 90 });
            r.push_back ({ "90 x 60", q (90, 60), 90, 60 });
            r.push_back ({ "90 x 40", q (90, 40), 90, 40 });
            r.push_back ({ "60 x 40", q (60, 40), 60, 40 });
            r.push_back ({ "60 x 30", q (60, 30), 60, 30 });
            r.push_back ({ "Line array (narrow vertical)", 30.0 });
            return r;
        }();
        return v;
    }
    juce::ToggleButton guidedToggle;
    juce::ComboBox alSignalBox;
    bool mutesConfirmed = false;
    int alSignal = 0;
    juce::TextButton polarityButton;
    juce::TextButton measureButton { "Measure" }, importButton { "Import response..." }, clearButton { "Clear" }, relockButton { "Re-time" };
    juce::Slider offsetSlider, freqSlider, delaySlider, gainSlider;
    juce::ToggleButton processedToggle { "With processing" };
    juce::TextButton typeButtons[3], slopeButtons[kSlopeButtons];
    juce::TextButton onboardButton { "No own filter..." }, protectButton { "Limits..." }, procDetailsButton { "Details..." };
    juce::TextButton findButton { "Find best crossover" }, invertButton { "Low normal" };
    juce::TextButton autoButton { "Automatic" }, manualButton { "By hand" }, removeXoButton { "Remove these crossover filters" },
        topHpButton { "Add a protective high-pass to the tops" };
    bool xoManual = false;
    juce::TextButton swapPairButton, invertedSwapButton { "Swap" }, alRedoButton { "Start over" };
    juce::ComboBox placementBox;
    juce::Rectangle<int> guideArea, modeHint, alSteps;
    int mode = 0;
    bool measureNear = false, checkMode = false;
    double nearDelayMs = 0.0;
    juce::Rectangle<int> invertedArea;
    juce::TextButton editSpeakerButton { "Edit on the Speakers page" }, sheetButton { juce::String (juce::CharPointer_UTF8 ("Processor sheet  \xe2\x86\x92")) };
    juce::ToggleButton asFoundToggle { "As found" };
    juce::Rectangle<int> selRead, sheetInfo;
    juce::ComboBox minCohBox;
    AlignmentList alList;
    juce::TextButton alAddButton { "Add alignment" }, alRemoveButton { "Remove" }, alMeasureButton { "Measure both" }, alApplyButton { "Apply delay" };
    AlignImpulseView alImpulse;
    juce::TextButton alPlayButtons[2];
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
    int arrivalMember = -1;   // the one member of the reference set playing now (one side at a time), -1 = the whole end
    bool arrivalMode = false;
    // One capture to make: which end (0 the reference, 1 the one to align), and for a reference set measured one
    // side at a time, the member that plays alone (-1: the end as set).
    struct ArrivalStep { int alignment = -1, which = 0, member = -1; };
    std::vector<ArrivalStep> arrivalQueue;
    // A reference set measured one side at a time: what each side gave, until the nearer one is picked.
    struct RefCapture
    {
        int member = -1;
        double arrivalMs = 0.0, delayAtMeasure = 0.0;
        bool invertAtMeasure = false;
        float levelDb = -200.0f, bandDb = -200.0f;
        FreqTable tf;
        std::vector<float> ir;
        int irZero = 0;
    };
    std::vector<RefCapture> refCaptures;
    std::vector<int> refPending;   // the members still to play, when they go one at a time with the mutes swapped between

    juce::Rectangle<int> systemCard, selCard, summaryCard, selInfo, measureInfo, xoInfo, liveLine, summaryArea, statusLine;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;

    int selected = 0, nOuts = -1, measuring = -1, measureSerial = 0, guidedNow = -1, polarityCab = 0;
    bool polarityMode = false, peakKnown = false, peakInverted = false;
    double capArrivalMs = -1.0;   // the arrival of the capture now (close up)
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    float progress = 0.0f;
    juce::uint64 delaySerial = 0, settleUntil = 0;
    juce::uint32 overflowsSeen = 0;
    int restarts = 0;
    std::array<float, kMaxMics> savedDelays {};
    bool searching = false, liveValid = false, sawReset = false;
    double liveEff = 0.0, liveDev = 0.0, liveReliable = 1.0;
    xo::SumShape liveShape;
    juce::String status;
    juce::String groupNote;   // which groups the last crossover search averaged
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
};
} // namespace la::ui
