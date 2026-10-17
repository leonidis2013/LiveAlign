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
    std::function<void (int entry)> onRollBack;   // the editor rolls the processing back (an undo step)

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
            const bool all = allEntered();
            for (const auto& l : sheet::lines (proc.system)) sheet::setEntered (proc.system, l, ! all);
            changed();
        };
        stageBox.addItemList ({ "Processor: one output per speaker", "Processor: group stage, then outputs" }, 1);
        stageBox.setTooltip ("How your processor is laid out. One output per speaker: every output gets its full settings. Group stage "
                             "(a matrix or group EQ feeding several outputs): each group's filters, level, polarity and EQ are entered once "
                             "on the group, and each output keeps its own delay and trim. Kept with the venue.");
        stageBox.onChange = [this]
        {
            proc.system.groupStage = stageBox.getSelectedItemIndex() == 1;
            changed();
        };
        addAndMakeVisible (stageBox);
        copyButton.setTooltip ("Copy the settings as text, to paste in your notes or a show file");
        csvButton.setTooltip ("Save the settings as a CSV file (a spreadsheet)");
        pdfButton.setTooltip ("Save the sheet as a PDF to print and take to the processor");
        for (auto* b : { &copyButton, &csvButton, &pdfButton, &allButton }) addAndMakeVisible (*b);
        historyButton.setTooltip ("Every change to the outputs' EQ, delay, trim and polarity: when, on which step and why. Roll back to before any of them.");
        historyButton.onClick = [this] { showHistory = ! showHistory; selectedEntry = -1; refresh(); resized(); };
        addAndMakeVisible (historyButton);
        rollBackButton.setTooltip ("Put every output's processing back as it was before the selected change (Cmd/Ctrl+Z undoes it)");
        rollBackButton.onClick = [this] { if (onRollBack && selectedEntry >= 0) onRollBack (selectedEntry); };
        addChildComponent (rollBackButton);
    }

    void refresh()
    {
        repaint();
        allButton.setButtonText (allEntered() ? "Untick all" : "Tick all as entered");
        stageBox.setSelectedItemIndex (proc.system.groupStage ? 1 : 0, juce::dontSendNotification);
        stageBox.setVisible (! showHistory);
        historyButton.setButtonText (showHistory ? "Back to the settings" : "History (" + juce::String ((int) proc.history.entries.size()) + ")");
        rollBackButton.setVisible (showHistory);
        rollBackButton.setEnabled (selectedEntry >= 0 && selectedEntry < (int) proc.history.entries.size() && proc.history.entries[(size_t) selectedEntry].before != nullptr);
        rollBackButton.setButtonText ("Roll back to before this");
        for (auto* b : { &allButton, &copyButton, &csvButton, &pdfButton }) b->setVisible (! showHistory);
    }

    void setHistoryShown (bool h) { showHistory = h; selectedEntry = h && ! proc.history.entries.empty() ? (int) proc.history.entries.size() - 1 : -1; refresh(); resized(); }

    bool allEntered() const { return ! proc.system.sources.empty() && sheet::enteredCount (proc.system) == sheet::lineCount (proc.system); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        if (showHistory) { paintHistory (g); return; }
        drawPanel (g, card.toFloat(), proc.system.groupStage ? "Settings to enter, group by group" : "Settings to enter, output by output");
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
        for (const auto& ln : sheet::lines (sys))
        {
            const int i = ln.spk;
            const auto row = sheet::rowOf (sys, ln);
            const auto* was = row.member || row.group ? nullptr : wasOf (i);
            const bool eqRow = row.eq != "none" || row.amp.isNotEmpty() || row.limiter.isNotEmpty();
            auto rr = r.removeFromTop (eqRow ? 60 : (row.member ? 36 : 42));
            if (rr.getBottom() > table.getBottom()) break;
            rows.push_back ({ ln, rr });
            if (row.member) rr = rr.withTrimmedLeft (22);   // an output under its group
            const bool done = sheet::entered (sys, ln);
            g.setColour (done ? p.good.withAlpha (0.10f) : row.group ? p.accent.withAlpha (0.10f) : p.panelRaised.withAlpha (0.6f));
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
                auto a = line.removeFromLeft (w[(size_t) c] - (c == 1 && row.member ? 22 : 0)).reduced (2, 0);
                const bool changedNow = before.isNotEmpty() && before != v;
                g.setColour (v == "INVERTED" ? p.warn : v == "(group)" || v == "on each output" ? p.textFaint : p.text);
                g.setFont (font (12.5f, bold || changedNow));
                g.drawText (v, changedNow ? a.withTrimmedBottom (a.getHeight() / 2 - 2) : a, changedNow ? juce::Justification::bottomLeft : juce::Justification::centredLeft, true);
                if (changedNow)
                {
                    g.setColour (p.textDim);
                    g.setFont (font (10.0f));
                    g.drawText ("was " + before, a.withTrimmedTop (a.getHeight() / 2 + 1), juce::Justification::topLeft, true);
                }
            };
            g.setColour (row.group ? p.accent : traceColour (i));
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
                if (row.amp.isNotEmpty() || row.limiter.isNotEmpty())
                {
                    // as set on the amp and the limiter (from Gain and headroom)
                    auto ampArea = eqLine.removeFromRight (juce::jmin (320, eqLine.getWidth() / 3));
                    g.setColour (p.textDim);
                    g.setFont (font (11.0f));
                    juce::StringArray bits;
                    if (row.amp.isNotEmpty()) bits.add ("Amp " + row.amp);
                    if (row.limiter.isNotEmpty()) bits.add ("Limiter " + row.limiter);
                    g.drawText (bits.joinIntoString ("   "), ampArea, juce::Justification::centredRight, true);
                }
                if (row.eq != "none")
                {
                    g.setColour (p.textDim);
                    g.setFont (font (10.5f, true));
                    g.drawText ("EQ", eqLine.removeFromLeft (30), juce::Justification::centredLeft);
                    g.setColour (p.text);
                    g.setFont (font (11.5f));
                    g.drawText (row.eq, eqLine, juce::Justification::centredLeft, true);
                }
            }
            r.removeFromTop (6);
        }
        // the delays in feet too, for consoles that take distance
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        juce::StringArray ft;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
            if (sys.sources[(size_t) i].proc.delayMs > 0.005) ft.add (sys.nameOf (i) + " " + juce::String (sys.sources[(size_t) i].proc.delayMs * 1.1255, 2) + " ft");
        juce::String foot = sys.groupStage
            ? "Each group's line goes on the processor's group (or matrix) stage once; each output under it gets its own delay and trim. "
              "Tick each line once it is in; a change unticks it. Filters: dB/octave."
            : "Tick each output once its settings are in the processor; a change unticks it. Filters: dB/octave; gain includes the "
              "crossover's level and the level-balance trim.";
        if (! ft.isEmpty()) foot << "  Delays as distance: " << ft.joinIntoString (", ") << ".";
        if (proc.asFound.taken()) foot << "  \"was\" is how they were found (" << proc.asFound.takenOn << ").";
        g.drawFittedText (foot, footArea, juce::Justification::topLeft, 3);

        g.setColour (allEntered() ? p.good : p.textDim);
        g.setFont (font (12.5f, true));
        g.drawText (juce::String (sheet::enteredCount (sys)) + " of " + juce::String (sheet::lineCount (sys)) + (sys.groupStage ? " lines entered" : " outputs entered")
                        + (note.isNotEmpty() ? "   " + note : juce::String()),
                    statusLine, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        card = getLocalBounds();
        stageBox.setBounds (card.getRight() - 16 - 290, card.getY() + 6, 290, 24);
        auto c = card.reduced (16, 0).withTrimmedTop (38);
        auto bar = c.removeFromBottom (48).withSizeKeepingCentre (c.getWidth(), 32);
        pdfButton.setBounds (bar.removeFromRight (130));
        bar.removeFromRight (8);
        csvButton.setBounds (bar.removeFromRight (130));
        bar.removeFromRight (8);
        copyButton.setBounds (bar.removeFromRight (130));
        bar.removeFromRight (16);
        if (! showHistory) { allButton.setBounds (bar.removeFromLeft (180)); bar.removeFromLeft (8); }
        historyButton.setBounds (bar.removeFromLeft (170));
        bar.removeFromLeft (8);
        if (showHistory) rollBackButton.setBounds (bar.removeFromLeft (210));
        bar.removeFromLeft (14);
        statusLine = bar;
        footArea = c.removeFromBottom (48);
        table = c;
        refresh();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (showHistory)
        {
            for (const auto& [idx, rr] : historyRows)
                if (rr.contains (e.getPosition())) { selectedEntry = idx; refresh(); return; }
            return;
        }
        for (const auto& [ln, rr] : rows)
            if (rr.contains (e.getPosition()) && ln.spk >= 0 && ln.spk < (int) proc.system.sources.size())
            {
                sheet::setEntered (proc.system, ln, ! sheet::entered (proc.system, ln));
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
        for (const auto& ln : sheet::lines (proc.system))
        {
            const auto r = sheet::rowOf (proc.system, ln);
            rowsOut.push_back ({ juce::String (sheet::entered (proc.system, ln) ? "[x]" : "[ ]"), (r.member ? "  " : "") + r.name, r.output, r.hpf, r.lpf, r.delay, r.polarity, r.gain });
        }
        L.table ({ 24.0f, 82.0f, 52.0f, 78.0f, 78.0f, 80.0f, 44.0f, 74.0f }, { "", "Speaker", "Output", "High-pass", "Low-pass", "Delay", "Polarity", "Gain" }, rowsOut, 7.5f);
        L.y += 10.0f;
        for (const auto& ln : sheet::lines (proc.system))
        {
            const auto r = sheet::rowOf (proc.system, ln);
            if (r.eq == "none") continue;
            L.sub (r.name + " EQ");
            L.para (r.eq, 9.5f);
        }
        {
            // the amps and limiters, as set
            std::vector<juce::StringArray> amps;
            for (int i = 0; i < (int) proc.system.sources.size(); ++i)
            {
                const auto r = sheet::row (proc.system, i);
                if (r.amp.isNotEmpty() || r.limiter.isNotEmpty()) amps.push_back ({ r.name, r.output, r.amp, r.limiter });
            }
            if (! amps.empty())
            {
                L.sub ("Amps and limiters (as set)");
                L.table ({ 140.0f, 80.0f, 140.0f, 140.0f }, { "Speaker", "Output", "Amp gain", "Limiter" }, amps, 8.0f);
                L.y += 8.0f;
            }
        }
        if (! proc.history.entries.empty())
        {
            L.sub ("Change history (newest first)");
            std::vector<juce::StringArray> hist;
            for (int i = (int) proc.history.entries.size(); --i >= 0 && hist.size() < 40;)
            {
                const auto& h = proc.history.entries[(size_t) i];
                hist.push_back ({ h.when, h.output, h.what, h.step + ": " + h.why });
            }
            L.table ({ 70.0f, 70.0f, 230.0f, 130.0f }, { "When", "Output", "Change", "Step and why" }, hist, 7.0f);
        }
        L.footers();
        if (! doc.write (f, "Processor sheet - " + proc.venueName, "LiveAlign")) return "Could not write " + f.getFullPathName();
        return {};
    }

private:
    // The change history, newest first: click one, then Roll back.
    void paintHistory (juce::Graphics& g)
    {
        const auto& p = pal();
        drawPanel (g, card.toFloat(), "Change history: every output, newest first");
        historyRows.clear();
        auto r = table;
        const auto& h = proc.history.entries;
        if (h.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("No changes yet. Every change to an output's EQ, delay, trim or polarity is listed here, with when, the step and why.",
                              r, juce::Justification::centred, 3);
            return;
        }
        const int cw[] = { 120, 150, 0, 260 };
        {
            auto hd = r.removeFromTop (22);
            g.setColour (p.textDim);
            g.setFont (font (10.5f, true));
            g.drawText ("WHEN", hd.removeFromLeft (cw[0]), juce::Justification::centredLeft);
            g.drawText ("OUTPUT", hd.removeFromLeft (cw[1]), juce::Justification::centredLeft);
            g.drawText ("STEP AND WHY", hd.removeFromRight (cw[3]), juce::Justification::centredLeft);
            g.drawText ("CHANGE", hd, juce::Justification::centredLeft);
        }
        r.removeFromTop (4);
        for (int i = (int) h.size(); --i >= 0;)
        {
            auto row = r.removeFromTop (30);
            if (row.getBottom() > table.getBottom()) break;
            historyRows.push_back ({ i, row });
            const auto& e = h[(size_t) i];
            const bool sel = i == selectedEntry;
            g.setColour (sel ? p.accent.withAlpha (0.18f) : p.panelRaised.withAlpha ((h.size() - (size_t) i) % 2 == 0 ? 0.25f : 0.55f));
            g.fillRoundedRectangle (row.toFloat(), 5.0f);
            if (sel) { g.setColour (p.accent); g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 5.0f, 1.2f); }
            row.reduce (8, 0);
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawText (e.when, row.removeFromLeft (cw[0] - 8), juce::Justification::centredLeft, true);
            g.setColour (p.text);
            g.setFont (font (12.0f, true));
            g.drawText (e.output, row.removeFromLeft (cw[1]), juce::Justification::centredLeft, true);
            auto why = row.removeFromRight (cw[3]);
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawText (e.step + ": " + e.why, why, juce::Justification::centredLeft, true);
            g.setColour (e.why.startsWith ("Rolled") || e.why == "Undone" ? p.warn : p.text);
            g.setFont (font (12.0f));
            g.drawText (e.what, row.withTrimmedRight (8), juce::Justification::centredLeft, true);
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawFittedText ("Each change keeps the processing as it was before it: select one and Roll back to put every output back to then "
                          "(the processor sheet changes, and the outputs untick until entered again). Undo (Cmd/Ctrl+Z) takes a roll back back.",
                          footArea, juce::Justification::topLeft, 3);
    }

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
    juce::TextButton historyButton, rollBackButton;
    bool showHistory = false;
    int selectedEntry = -1;
    std::vector<std::pair<int, juce::Rectangle<int>>> historyRows;
    juce::Rectangle<int> card, table, footArea, statusLine;
    std::vector<std::pair<sheet::Line, juce::Rectangle<int>>> rows;
    juce::ComboBox stageBox;
    juce::String note;
};
} // namespace la::ui
