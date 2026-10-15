// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The processor sheet: every setting to enter, output by output (crossover filters, delay, polarity, level with the
// level-balance trim, the room EQ), in plain words for any processor or console. Tick each output once it is entered;
// a change to its settings unticks it. Copy as text, or export a CSV or a PDF.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "../PluginProcessor.h"
#include "../ProcessorSheet.h"
#include "../Report/ReportCommon.h"

namespace la::ui
{
class SheetPage : public juce::Component
{
public:
    std::function<void()> onChanged;
    std::function<void (bool csv)> onExport;   // the editor owns the file chooser

    explicit SheetPage (LiveAlignProcessor& p) : proc (p)
    {
        copyButton.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (sheet::text (proc.system, proc.venueName, LiveAlignProcessor::today()));
            note = "Copied to the clipboard.";
            repaint();
        };
        csvButton.onClick = [this] { if (onExport) onExport (true); };
        pdfButton.onClick = [this] { if (onExport) onExport (false); };
        allButton.onClick = [this]
        {
            const bool all = sheet::enteredCount (proc.system) == (int) proc.system.sources.size();
            for (int i = 0; i < (int) proc.system.sources.size(); ++i)
                proc.system.sources[(size_t) i].sheetEntered = all ? juce::String() : sheet::signature (proc.system, i);
            changed();
        };
        copyButton.setTooltip ("Copy the settings as text, to paste in your notes or a show file");
        csvButton.setTooltip ("Save the settings as a CSV file (a spreadsheet)");
        pdfButton.setTooltip ("Save the sheet as a PDF to print and take to the processor");
        for (auto* b : { &copyButton, &csvButton, &pdfButton, &allButton }) addAndMakeVisible (*b);
    }

    void refresh() { repaint(); allButton.setButtonText (sheet::enteredCount (proc.system) == (int) proc.system.sources.size() ? "Untick all" : "Tick all as entered"); }

    bool allEntered() const { return ! proc.system.sources.empty() && sheet::enteredCount (proc.system) == (int) proc.system.sources.size(); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, card.toFloat(), "Settings to enter, output by output");
        const auto& sys = proc.system;
        auto r = table;
        static const char* heads[] = { "", "SPEAKER", "OUTPUT", "HIGH-PASS", "LOW-PASS", "DELAY", "POLARITY", "GAIN" };
        const auto w = colWidths (r.getWidth());
        {
            auto h = r.removeFromTop (22);
            g.setColour (p.textDim);
            g.setFont (font (10.5f, true));
            for (int c = 0; c < 8; ++c) g.drawText (heads[c], h.removeFromLeft (w[(size_t) c]), juce::Justification::centredLeft, true);
        }
        r.removeFromTop (4);
        rows.clear();
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const auto row = sheet::row (sys, i);
            const auto* was = wasOf (i);
            const bool eqRow = row.eq != "none";
            auto rr = r.removeFromTop (eqRow ? 60 : 42);
            if (rr.getBottom() > table.getBottom()) break;
            rows.push_back (rr);
            const bool done = sheet::entered (sys, i);
            g.setColour (done ? p.good.withAlpha (0.10f) : p.panelRaised.withAlpha (0.6f));
            g.fillRoundedRectangle (rr.toFloat(), 6.0f);
            if (done) { g.setColour (p.good.withAlpha (0.5f)); g.drawRoundedRectangle (rr.toFloat().reduced (0.5f), 6.0f, 1.0f); }
            auto line = rr.removeFromTop (34);
            // tick box
            {
                auto box = line.removeFromLeft (w[0]).toFloat().withSizeKeepingCentre (18.0f, 18.0f);
                g.setColour (done ? p.good : p.textDim);
                if (done)
                {
                    g.fillRoundedRectangle (box, 4.0f);
                    juce::Path tick;
                    tick.startNewSubPath (box.getX() + 4.0f, box.getCentreY());
                    tick.lineTo (box.getX() + 7.5f, box.getBottom() - 4.5f);
                    tick.lineTo (box.getRight() - 4.0f, box.getY() + 4.5f);
                    g.setColour (p.onAccent);
                    g.strokePath (tick, juce::PathStrokeType (2.0f));
                }
                else g.drawRoundedRectangle (box.reduced (0.5f), 4.0f, 1.4f);
            }
            auto cell = [&] (int c, const juce::String& v, bool bold, const juce::String& before)
            {
                auto a = line.removeFromLeft (w[(size_t) c]).reduced (2, 0);
                const bool changedNow = before.isNotEmpty() && before != v;
                g.setColour (v == "INVERTED" ? p.warn : p.text);
                g.setFont (font (12.5f, bold || changedNow));
                g.drawText (v, changedNow ? a.withTrimmedBottom (a.getHeight() / 2 - 2) : a, changedNow ? juce::Justification::bottomLeft : juce::Justification::centredLeft, true);
                if (changedNow)
                {
                    g.setColour (p.textDim);
                    g.setFont (font (10.0f));
                    g.drawText ("was " + before, a.withTrimmedTop (a.getHeight() / 2 + 1), juce::Justification::topLeft, true);
                }
            };
            g.setColour (traceColour (i));
            g.fillRoundedRectangle (line.getX() + 0.0f, (float) line.getCentreY() - 9.0f, 4.0f, 18.0f, 2.0f);
            line.removeFromLeft (8);
            cell (1, row.name, true, {});
            cell (2, row.output, false, {});
            cell (3, row.hpf, false, was != nullptr ? sheet::filterWords (was->proc.hp) : juce::String());
            cell (4, row.lpf, false, was != nullptr ? sheet::filterWords (was->proc.lp) : juce::String());
            cell (5, row.delay.upToFirstOccurrenceOf ("  (", false, false), false, was != nullptr ? sheet::delayWords (was->proc.delayMs).upToFirstOccurrenceOf ("  (", false, false) : juce::String());
            cell (6, row.polarity, false, was != nullptr ? juce::String (was->proc.invert ? "INVERTED" : "normal") : juce::String());
            cell (7, row.gain, false, {});
            if (eqRow)
            {
                auto eqLine = rr.withTrimmedLeft (w[0] + 8).withTrimmedRight (8);
                g.setColour (p.textDim);
                g.setFont (font (10.5f, true));
                g.drawText ("EQ", eqLine.removeFromLeft (30), juce::Justification::centredLeft);
                g.setColour (p.text);
                g.setFont (font (11.5f));
                g.drawText (row.eq, eqLine, juce::Justification::centredLeft, true);
            }
            r.removeFromTop (6);
        }
        // the delays in feet too, for consoles that take distance
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        juce::StringArray ft;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
            if (sys.sources[(size_t) i].proc.delayMs > 0.005) ft.add (sys.nameOf (i) + " " + juce::String (sys.sources[(size_t) i].proc.delayMs * 1.1255, 2) + " ft");
        juce::String foot = "Tick each output once its settings are in the processor; a change unticks it. Filters: dB/octave; gain includes the "
                            "crossover's level and the level-balance trim.";
        if (! ft.isEmpty()) foot << "  Delays as distance: " << ft.joinIntoString (", ") << ".";
        if (proc.asFound.taken()) foot << "  \"was\" is how they were found (" << proc.asFound.takenOn << ").";
        g.drawFittedText (foot, footArea, juce::Justification::topLeft, 3);

        g.setColour (allEntered() ? p.good : p.textDim);
        g.setFont (font (12.5f, true));
        g.drawText (juce::String (sheet::enteredCount (sys)) + " of " + juce::String ((int) sys.sources.size()) + " outputs entered"
                        + (note.isNotEmpty() ? "   " + note : juce::String()),
                    statusLine, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        card = getLocalBounds();
        auto c = card.reduced (16, 0).withTrimmedTop (38);
        auto bar = c.removeFromBottom (48).withSizeKeepingCentre (c.getWidth(), 32);
        pdfButton.setBounds (bar.removeFromRight (130));
        bar.removeFromRight (8);
        csvButton.setBounds (bar.removeFromRight (130));
        bar.removeFromRight (8);
        copyButton.setBounds (bar.removeFromRight (130));
        bar.removeFromRight (16);
        allButton.setBounds (bar.removeFromLeft (180));
        bar.removeFromLeft (14);
        statusLine = bar;
        footArea = c.removeFromBottom (48);
        table = c;
        refresh();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int) rows.size(); ++i)
            if (rows[(size_t) i].contains (e.getPosition()))
            {
                auto& s = proc.system.sources[(size_t) i];
                s.sheetEntered = sheet::entered (proc.system, i) ? juce::String() : sheet::signature (proc.system, i);
                note.clear();
                changed();
                return;
            }
    }

    // The sheet as a PDF (portrait, one row per output with its EQ).
    juce::String writePdf (const juce::File& f) const
    {
        using namespace la::report::detail;
        pdf::Document doc;
        Layout L (doc, "LiveAlign processor sheet - " + proc.venueName);
        cover (L, "PROCESSOR SHEET", proc.venueName, LiveAlignProcessor::today());
        L.y = 232.0f;
        L.para ("Every setting to enter, output by output. Filters in dB/octave; delay in ms (and feet); gain includes the crossover's level "
                "and the level-balance trim.", 10.0f);
        L.y += 8.0f;
        std::vector<juce::StringArray> rowsOut;
        for (int i = 0; i < (int) proc.system.sources.size(); ++i)
        {
            const auto r = sheet::row (proc.system, i);
            rowsOut.push_back ({ juce::String (sheet::entered (proc.system, i) ? "[x]" : "[ ]"), r.name, r.output, r.hpf, r.lpf, r.delay, r.polarity, r.gain });
        }
        L.table ({ 24.0f, 82.0f, 52.0f, 78.0f, 78.0f, 80.0f, 44.0f, 74.0f }, { "", "Speaker", "Output", "High-pass", "Low-pass", "Delay", "Polarity", "Gain" }, rowsOut, 7.5f);
        L.y += 10.0f;
        for (int i = 0; i < (int) proc.system.sources.size(); ++i)
        {
            const auto r = sheet::row (proc.system, i);
            if (r.eq == "none") continue;
            L.sub (r.name + " EQ");
            L.para (r.eq, 9.5f);
        }
        L.footers();
        if (! doc.write (f, "Processor sheet - " + proc.venueName, "LiveAlign")) return "Could not write " + f.getFullPathName();
        return {};
    }

private:
    const AsFound::Speaker* wasOf (int i) const
    {
        if (! proc.asFound.taken() || i < 0 || i >= (int) proc.system.sources.size()) return nullptr;
        for (const auto& s : proc.asFound.speakers) if (s.name == proc.system.sources[(size_t) i].name) return &s;
        return nullptr;
    }

    static std::array<int, 8> colWidths (int w)
    {
        const int tick = 34, rest = w - tick;
        return { tick, rest * 14 / 100, rest * 8 / 100, rest * 18 / 100, rest * 18 / 100, rest * 10 / 100, rest * 8 / 100, rest * 24 / 100 };
    }

    void changed() { refresh(); if (onChanged) onChanged(); }

    LiveAlignProcessor& proc;
    juce::TextButton copyButton { "Copy as text" }, csvButton { "Export CSV..." }, pdfButton { "Export PDF..." }, allButton { "Tick all as entered" };
    juce::Rectangle<int> card, table, footArea, statusLine;
    std::vector<juce::Rectangle<int>> rows;
    juce::String note;
};
} // namespace la::ui
