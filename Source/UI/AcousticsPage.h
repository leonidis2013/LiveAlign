// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Tuning step 6: room acoustics, once the system is tuned. Worked out from the impulse responses the Room EQ
// measurements capture at each position: reverberation time and early decay per octave, clarity for speech (C50) and
// music (C80), a speech intelligibility estimate (STI) per position, the room modes (low-frequency peaks that move
// from seat to seat), the coverage (level across the seats) and the background noise rating (NC / NR) from the
// noise floor check on Mic setup. Nothing is measured here: it reads what Room EQ and Mic setup measured.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "RoomPage.h"
#include "../PluginProcessor.h"

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
            g.drawFittedText ("No room measurements with impulse responses yet. Measure the listening positions on 4 Room EQ: "
                              "each measurement there also captures what the room does.", plot.reduced (40.0f).toNearestInt(), juce::Justification::centred, 3);
            return;
        }
        line (room.acousticsAverage(), p.text, 2.6f, true);
    }

private:
    LiveAlignProcessor& proc;
    int metric = 0;
};

class AcousticsPage : public juce::Component
{
public:
    std::function<void()> onChanged;

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
        useBox.onChange = [this] { proc.room.use = useBox.getSelectedItemIndex(); if (onChanged) onChanged(); repaint(); chart.repaint(); };
        addAndMakeVisible (useBox);
        refresh();
    }

    void refresh()
    {
        useBox.setSelectedItemIndex (juce::jlimit (0, 3, proc.room.use), juce::dontSendNotification);
        repaint();
        chart.repaint();
    }

    juce::String nextHint() const { return proc.room.acousticsCount() == 0 ? juce::String ("Measure the positions on Room EQ") : juce::String(); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto& room = proc.room;
        const auto avg = room.acousticsAverage();
        double rtLo, rtHi, stiMin;
        RoomTuning::useTargets (room.use, rtLo, rtHi, stiMin);

        // tiles
        auto tile = [&] (int i, const juce::String& label, const juce::String& value, const juce::String& unit, juce::Colour c)
        {
            drawTile (g, tiles[(size_t) i].toFloat(), label, value, unit, c, 22.0f);
        };
        auto num = [] (double v, int d) { return ! std::isfinite (v) ? juce::String ("-") : d == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, d); };
        const double rt = avg.rtMid(), sti = avg.sti;
        tile (0, "REVERB (RT, 500-1k)", num (rt, 2), "s", ! std::isfinite (rt) ? p.textDim : rt < rtLo * 0.85 || rt > rtHi * 1.15 ? p.warn : p.good);
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

        drawPanel (g, posCard.toFloat(), "Each position");
        drawPanel (g, modesCard.toFloat(), "Room modes");
        drawPanel (g, notesCard.toFloat(), "What it means");

        // positions table
        {
            auto r = posCard.reduced (12, 0).withTrimmedTop (34);
            const int cw[] = { 0, 56, 56, 64, 52 };   // name gets the rest
            const char* heads[] = { "POSITION", "RT", "EDT", "C50/C80", "STI" };
            auto header = r.removeFromTop (18);
            const int nameW = header.getWidth() - 56 - 56 - 64 - 52;
            g.setColour (p.textDim);
            g.setFont (font (10.5f, true));
            g.drawText (heads[0], header.removeFromLeft (nameW), juce::Justification::centredLeft);
            for (int c = 1; c < 5; ++c) g.drawText (heads[c], header.removeFromLeft (cw[c]), juce::Justification::centredLeft);
            for (const auto& m : room.meas)
            {
                if (! m.ac.valid || r.getHeight() < 22) continue;
                auto row = r.removeFromTop (22);
                g.setColour (positionColour (juce::jmax (0, proc.positions.indexOf (m.position))));
                g.fillEllipse (row.removeFromLeft (12).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
                g.setColour (p.text);
                g.setFont (font (12.0f));
                g.drawText (m.position + (m.after ? " (after EQ)" : ""), row.removeFromLeft (nameW - 12), juce::Justification::centredLeft, true);
                g.setFont (monoFont (11.5f));
                g.setColour (p.textDim);
                g.drawText (num (m.ac.rtMid(), 2), row.removeFromLeft (cw[1]), juce::Justification::centredLeft);
                g.drawText (num (m.ac.mid (&acoustics::Band::edt), 2), row.removeFromLeft (cw[2]), juce::Justification::centredLeft);
                g.drawText (num (m.ac.mid (&acoustics::Band::c50), 0) + "/" + num (m.ac.mid (&acoustics::Band::c80), 0), row.removeFromLeft (cw[3]),
                            juce::Justification::centredLeft);
                g.setColour (std::isfinite (m.ac.sti) && m.ac.sti < stiMin ? p.warn : p.text);
                g.drawText (num (m.ac.sti, 2), row.removeFromLeft (cw[4]), juce::Justification::centredLeft);
            }
        }

        // modes
        {
            auto r = modesCard.reduced (12, 0).withTrimmedTop (34);
            const auto modes = room.modes (room.count (true) > 0 && room.count (false) == 0);
            g.setFont (font (11.5f));
            if (modes.empty())
            {
                g.setColour (p.textFaint);
                g.drawFittedText (room.count (false) + room.count (true) == 0 ? "Measured from the Room EQ positions."
                                                                              : "No strong low-frequency peaks in the average.",
                                  r.removeFromTop (40), juce::Justification::topLeft, 2);
            }
            for (const auto& m : modes)
            {
                if (r.getHeight() < 20) break;
                auto row = r.removeFromTop (20);
                g.setColour (p.text);
                g.setFont (monoFont (12.0f, true));
                g.drawText (freqLabel (m.freq) + " Hz", row.removeFromLeft (80), juce::Justification::centredLeft);
                g.setFont (font (11.5f));
                g.setColour (p.textDim);
                juce::String t = "+" + juce::String (m.heightDb, 1) + " dB";
                if (std::isfinite (m.seatSpreadDb)) t << ", varies " << juce::String (m.seatSpreadDb, 1) << " dB between seats";
                g.drawText (t, row, juce::Justification::centredLeft, true);
            }
        }

        // notes
        {
            juce::StringArray lines;
            if (std::isfinite (rt))
            {
                if (rt > rtHi * 1.15) lines.add ("The room is livelier than usual for " + RoomTuning::useNames()[room.use].toLowerCase()
                                                 + ": keep the PA aimed at the seats and off the walls; absorption (panels, curtains) would help.");
                else if (rt < rtLo * 0.85) lines.add ("The room is drier than usual for this use: fine for the PA; music may feel a little dead.");
                else lines.add ("The reverberation suits " + RoomTuning::useNames()[room.use].toLowerCase() + ".");
            }
            if (std::isfinite (sti)) lines.add ("Speech intelligibility is " + juce::String (acoustics::stiRating (sti)) + " (STI " + juce::String (sti, 2)
                                                + "; aim for " + juce::String (stiMin, 2) + " or more here).");
            if (std::isfinite (spread) && spread > 6.0) lines.add ("The level varies " + juce::String (spread, 1) + " dB across the seats: fills or delays, "
                                                                   "or re-aiming, would even it out.");
            if (! room.modes().empty()) lines.add ("Room modes are peaks EQ can only tame at some seats: cut them gently; moving subs changes them.");
            if (lines.isEmpty()) lines.add ("Measure the listening positions on 4 Room EQ; the room's acoustics are worked out from the same measurements.");
            g.setColour (p.text);
            g.setFont (font (12.0f));
            auto r = notesCard.reduced (12, 0).withTrimmedTop (34);
            for (const auto& l : lines)
            {
                const int h = juce::jmin (r.getHeight(), 50);
                if (h < 16) break;
                g.drawFittedText (l, r.removeFromTop (h), juce::Justification::topLeft, 3, 1.0f);
                r.removeFromTop (4);
            }
            g.setColour (p.textFaint);
            g.setFont (font (10.5f));
            g.drawFittedText ("Estimates from the system measurement (ISO 3382 / IEC 60268-16 methods); a full acoustic survey uses an "
                              "omnidirectional source.", r.removeFromBottom (32), juce::Justification::bottomLeft, 2);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (70);
        const int tw = (top.getWidth() - 5 * 8) / 6;
        for (int i = 0; i < 6; ++i) { tiles[(size_t) i] = top.removeFromLeft (tw); top.removeFromLeft (8); }
        r.removeFromTop (10);
        auto right = r.removeFromRight (380);
        r.removeFromRight (10);
        auto bar = r.removeFromBottom (32);
        r.removeFromBottom (6);
        chart.setBounds (r);
        const int bw = 64;
        for (auto& b : metricButtons) { b.setBounds (bar.removeFromLeft (bw).reduced (0, 2)); bar.removeFromLeft (4); }
        bar.removeFromLeft (12);
        useBox.setBounds (bar.removeFromRight (juce::jmin (330, bar.getWidth())).reduced (0, 2));
        posCard = right.removeFromTop (juce::jmax (150, right.getHeight() * 40 / 100));
        right.removeFromTop (10);
        modesCard = right.removeFromTop (150);
        right.removeFromTop (10);
        notesCard = right;
    }

private:
    LiveAlignProcessor& proc;
    AcousticsChart chart;
    juce::TextButton metricButtons[4];
    juce::ComboBox useBox;
    std::array<juce::Rectangle<int>, 6> tiles;
    juce::Rectangle<int> posCard, modesCard, notesCard;
};
} // namespace la::ui
