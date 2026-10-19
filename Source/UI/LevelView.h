// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Live monitoring's sound level over time: the event's log (a point every few seconds) as a graph. LAeq (blue),
// LCeq (orange, thin), the loudest moment in each point (LAF max, pink), the rolling Leq over the limit window (teal,
// thick) against the limit (red dashes), the parts marked as the event ran, and the rings the feedback watch caught.
#include "Views.h"
#include "../Monitoring.h"

namespace la::ui
{
class LevelView : public juce::Component
{
public:
    explicit LevelView (const SplLog& l) : log (l)
    {
        const char* names[] = { "10 min", "30 min", "1 h", "All" };
        for (int i = 0; i < 4; ++i)
        {
            spans[i].setButtonText (names[i]);
            spans[i].setClickingTogglesState (true);
            spans[i].setRadioGroupId (0x1e7e1);
            spans[i].setToggleState (i == span, juce::dontSendNotification);
            spans[i].onClick = [this, i] { if (spans[i].getToggleState()) { span = i; repaint(); } };
            spans[i].setTooltip ("Show the last " + juce::String (names[i]) + (i == 3 ? " of the event" : ""));
            addAndMakeVisible (spans[i]);
        }
        spans[3].setTooltip ("Show the whole event");
    }

    // Repaints when the log has moved on (a point every few seconds), with the level now on the top line.
    void update (const Snapshot& snap)
    {
        const float a = snap.splAS;
        const bool moved = log.points.size() != shownPoints || log.parts.size() != shownParts || log.hits.size() != shownHits;
        const bool nowMoved = std::abs (a - nowA) >= 0.1f;
        nowA = a;
        spl = snap.rtaIsSpl;
        if (moved) { shownPoints = log.points.size(); shownParts = log.parts.size(); shownHits = log.hits.size(); repaint(); }
        else if (nowMoved) repaint (getLocalBounds().removeFromTop (30));
    }

    void resized() override
    {
        auto head = getLocalBounds().reduced (10, 6).removeFromTop (22);
        for (int i = 3; i >= 0; --i) { spans[i].setBounds (head.removeFromRight (58)); head.removeFromRight (2); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), {});
        auto r = getLocalBounds().toFloat().reduced (10.0f, 6.0f);
        auto head = r.removeFromTop (22.0f);
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("SOUND LEVEL", head.removeFromLeft (100.0f), juce::Justification::centredLeft);
        if (std::isfinite (nowA) && nowA > -150.0f)
        {
            g.setColour (p.text);
            g.setFont (monoFont (12.0f, true));
            g.drawText ("now " + juce::String (nowA, 1) + (spl ? " dBA" : " dB(A)FS"), head.removeFromLeft (150.0f), juce::Justification::centredLeft);
        }
        // the legend
        {
            auto lg = head.withTrimmedRight (4 * 60.0f + 10.0f);
            g.setFont (font (10.5f));
            struct Key { const char* name; juce::Colour c; };
            const Key keys[] = { { "LAeq", p.inputCol }, { "LCeq", p.outputCol }, { "LAF max", p.reduction },
                                 { "rolling", p.accent } };
            for (int i = 3; i >= 0; --i)
            {
                auto k = lg.removeFromRight (i == 3 ? 92.0f : 70.0f);
                g.setColour (keys[i].c);
                g.fillRect (k.getX(), k.getCentreY() - 1.5f, 12.0f, 3.0f);
                g.setColour (p.textDim);
                g.drawText (i == 3 ? juce::String (log.limitMinutes) + " min " + log.limitName() : juce::String (keys[i].name),
                            k.withTrimmedLeft (16.0f), juce::Justification::centredLeft, true);
            }
        }
        r.removeFromTop (4.0f);
        r.removeFromLeft (40.0f);
        r.removeFromRight (8.0f);
        const auto plot = r.withTrimmedBottom (16.0f);
        if (log.points.size() < 2)
        {
            drawWaiting (g, plot, "Sound level over time", "Logged every " + juce::String (juce::roundToInt (log.interval)) + " seconds while monitoring runs.");
            return;
        }

        // the time span shown
        const double t1 = log.end();
        const double spanSec[] = { 600.0, 1800.0, 3600.0, 0.0 };
        const double t0 = span == 3 ? log.start() : juce::jmax (log.start(), t1 - spanSec[span]);
        const double tw = span == 3 ? juce::jmax (60.0, t1 - t0) : spanSec[span];
        auto xAt = [&] (double t) { return plot.getX() + (float) ((t - t0) / tw) * plot.getWidth(); };

        // the level range: what is shown, in 10 dB steps
        float lo = 1e9f, hi = -1e9f;
        for (const auto& pt : log.points)
            if (pt.t >= t0) { lo = std::min (lo, std::min (pt.la, pt.lc)); hi = std::max (hi, std::max (pt.laMax, pt.lc)); }
        if (log.limitLaeq > 0.0) { hi = std::max (hi, (float) log.limitLaeq + 2.0f); lo = std::min (lo, (float) log.limitLaeq - 10.0f); }
        if (log.limit2 > 0.0) { hi = std::max (hi, (float) log.limit2 + 2.0f); lo = std::min (lo, (float) log.limit2 - 10.0f); }
        if (lo > hi) { lo = 40.0f; hi = 100.0f; }
        const float top = std::ceil ((hi + 2.0f) / 10.0f) * 10.0f, bot = std::floor ((lo - 2.0f) / 10.0f) * 10.0f;
        auto yAt = [&] (double db) { return plot.getY() + (float) ((top - juce::jlimit ((double) bot, (double) top, db)) / juce::jmax (1.0f, top - bot)) * plot.getHeight(); };

        g.setFont (font (10.5f));
        for (float db = bot; db <= top + 0.1f; db += 10.0f)
        {
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) yAt (db), plot.getX(), plot.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 40.0f, yAt (db) - 7.0f, 34.0f, 14.0f), juce::Justification::centredRight);
        }
        // time ticks
        const double step = tw > 3 * 3600 ? 3600.0 : tw > 5400 ? 1800.0 : tw > 2400 ? 600.0 : tw > 900 ? 300.0 : 120.0;
        for (double t = std::ceil (t0 / step) * step; t <= t0 + tw; t += step)
        {
            const float x = xAt (t);
            g.setColour (p.grid);
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (p.textDim);
            g.drawText (juce::Time ((juce::int64) (t * 1000.0)).formatted ("%H:%M"), juce::Rectangle<float> (x - 30.0f, plot.getBottom() + 2.0f, 60.0f, 14.0f),
                        juce::Justification::centred);
        }

        // the parts: a line where each starts, its name at the top
        // Names that would run into the one before go down a row (up to four rows; then the oldest row is reused).
        const auto partFont = font (10.5f, true);
        g.setFont (partFont);
        {
            float rowEnd[4] = { -1e9f, -1e9f, -1e9f, -1e9f };
            for (size_t k = 0; k < log.parts.size(); ++k)
            {
                const auto& pt = log.parts[k];
                const double next = k + 1 < log.parts.size() ? log.parts[k + 1].t : t1;
                if (next < t0 || pt.t > t0 + tw) continue;
                const float x = juce::jmax (plot.getX(), xAt (pt.t));
                g.setColour (p.textDim.withAlpha (0.7f));
                if (pt.t >= t0) g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
                const float want = juce::jmin (160.0f, juce::GlyphArrangement::getStringWidth (partFont, pt.name) + 6.0f);
                int row = 0;
                for (int j = 0; j < 4; ++j) if (rowEnd[j] <= x + 2.0f) { row = j; break; } else if (rowEnd[j] < rowEnd[row]) row = j;
                const float w = juce::jmin (want, plot.getRight() - x - 4.0f);
                if (w < 12.0f) continue;
                juce::Rectangle<float> box (x + 4.0f, plot.getY() + 2.0f + row * 15.0f, w, 14.0f);
                rowEnd[row] = box.getRight() + 4.0f;
                g.setColour (p.panel.withAlpha (0.85f));
                g.fillRoundedRectangle (box.expanded (2.0f, 0.0f), 3.0f);
                g.setColour (p.text);
                g.drawText (pt.name, box, juce::Justification::centredLeft, true);
            }
        }

        juce::Path la, lc, mx, roll;
        const auto rolling = log.rollingLaeq();
        bool started = false;
        for (size_t i = 0; i < log.points.size(); ++i)
        {
            const auto& pt = log.points[i];
            if (pt.t < t0) continue;
            const float x = xAt (pt.t);
            if (! started)
            {
                la.startNewSubPath (x, yAt (pt.la)); lc.startNewSubPath (x, yAt (pt.lc)); mx.startNewSubPath (x, yAt (pt.laMax)); roll.startNewSubPath (x, yAt (rolling[i]));
                started = true;
            }
            else { la.lineTo (x, yAt (pt.la)); lc.lineTo (x, yAt (pt.lc)); mx.lineTo (x, yAt (pt.laMax)); roll.lineTo (x, yAt (rolling[i])); }
        }
        g.saveState();
        g.reduceClipRegion (plot.toNearestInt());
        g.setColour (p.reduction.withAlpha (0.45f));
        g.strokePath (mx, juce::PathStrokeType (1.0f));
        g.setColour (p.outputCol.withAlpha (0.7f));
        g.strokePath (lc, juce::PathStrokeType (1.0f));
        g.setColour (p.inputCol);
        g.strokePath (la, juce::PathStrokeType (1.5f));
        g.setColour (p.accent);
        g.strokePath (roll, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (log.limitLaeq > 0.0)
        {
            juce::Path lim, dashed;
            lim.startNewSubPath (plot.getX(), yAt (log.limitLaeq));
            lim.lineTo (plot.getRight(), yAt (log.limitLaeq));
            const float dashes[] = { 8.0f, 5.0f };
            juce::PathStrokeType (1.6f).createDashedStroke (dashed, lim, dashes, 2);
            g.setColour (p.bad);
            g.fillPath (dashed);
            g.setFont (font (10.5f, true));
            g.drawText ("limit " + juce::String (juce::roundToInt (log.limitLaeq)) + " " + log.limitUnit(),
                        juce::Rectangle<float> (plot.getRight() - 120.0f, yAt (log.limitLaeq) - 15.0f, 116.0f, 13.0f), juce::Justification::centredRight);
        }
        if (log.limit2 > 0.0)
        {
            // the second limit (the other weighting): orange dashes
            juce::Path lim, dashed;
            lim.startNewSubPath (plot.getX(), yAt (log.limit2));
            lim.lineTo (plot.getRight(), yAt (log.limit2));
            const float dashes[] = { 4.0f, 4.0f };
            juce::PathStrokeType (1.4f).createDashedStroke (dashed, lim, dashes, 2);
            g.setColour (p.outputCol);
            g.fillPath (dashed);
            g.setFont (font (10.5f, true));
            g.drawText ("limit " + juce::String (juce::roundToInt (log.limit2)) + " " + log.limit2Unit(),
                        juce::Rectangle<float> (plot.getRight() - 240.0f, yAt (log.limit2) - 15.0f, 116.0f, 13.0f), juce::Justification::centredRight);
        }
        // the feedback watch's catches: a red tick at the bottom
        g.setColour (p.bad);
        for (const auto& h : log.hits)
            if (h.t >= t0) g.fillRect (xAt (h.t) - 1.0f, plot.getBottom() - 10.0f, 2.0f, 10.0f);
        g.restoreState();
    }

private:
    const SplLog& log;
    juce::TextButton spans[4];
    int span = 1;
    size_t shownPoints = (size_t) -1, shownParts = 0, shownHits = 0;
    float nowA = NAN;
    bool spl = false;
};
} // namespace la::ui
