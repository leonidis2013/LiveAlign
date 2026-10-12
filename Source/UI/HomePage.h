// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Home: the venue, its sessions, and where each step of the setup stands.
#include <functional>
#include "NavRail.h"
#include "../PluginProcessor.h"

namespace la::ui
{
struct StepInfo
{
    StepStatus status = StepStatus::NotStarted;
    juce::String shortText;   // for the navigation pane
    juce::String detail;      // a sentence for the home page
    float fraction = 0.0f;
};

class HomePage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onNewVenue, onOpenVenue, onSave, onSaveAs, onNewSession, onChanged;
    std::function<void (int)> onGoTo, onRenameSession, onDeleteSession;

    explicit HomePage (LiveAlignProcessor& p) : proc (p)
    {
        nameEditor.setTextToShowWhenEmpty ("Venue name", pal().textFaint);
        nameEditor.onTextChange = [this] { proc.venueName = nameEditor.getText().trim().isEmpty() ? juce::String ("Untitled venue") : nameEditor.getText().trim(); changed(); };
        notesEditor.setMultiLine (true, true);
        notesEditor.setReturnKeyStartsNewLine (true);
        notesEditor.setTextToShowWhenEmpty ("Notes: room, system, contacts, anything worth remembering", pal().textFaint);
        notesEditor.onTextChange = [this] { proc.venueNotes = notesEditor.getText(); changed(); };
        for (auto* e : { &nameEditor, &notesEditor })
        {
            e->setFont (font (13.0f));
            addAndMakeVisible (*e);
        }
        nameEditor.setFont (font (16.0f, true));

        setupButton (newVenueButton, [this] { if (onNewVenue) onNewVenue(); }, "Start a new venue file");
        setupButton (openButton, [this] { if (onOpenVenue) onOpenVenue(); }, "Open a venue file");
        setupButton (saveButton, [this] { if (onSave) onSave(); }, "Save the venue and all its sessions (Cmd/Ctrl+S)");
        setupButton (saveAsButton, [this] { if (onSaveAs) onSaveAs(); }, "Save under a new name");

        setupButton (newSessionButton, [this] { if (onNewSession) onNewSession(); },
                     "Start a new visit to this venue. The mics, positions and speaker setup carry over; measurements start fresh.");
        setupButton (openSessionButton, [this] { proc.switchSession (selected); changed(); }, "Make the selected session the open one");
        setupButton (compareButton, [this] { toggleCompare(); }, "Show the selected session's traces beside the open session's");
        setupButton (renameButton, [this] { if (onRenameSession) onRenameSession (selected); }, "Rename the selected session");
        setupButton (deleteButton, [this] { if (onDeleteSession) onDeleteSession (selected); }, "Delete the selected session");

        speakerChangedButton.onClick = [this]
        {
            proc.speakers.changed = ! proc.speakers.changed;
            changed();
        };
        speakerChangedButton.setTooltip ("Mark the speaker system as changed (moved, replaced or re-cabled) so it is checked again");
        addAndMakeVisible (speakerChangedButton);

        refresh();
        startTimerHz (4);
    }

    void setSteps (const StepInfo (&s)[4], bool setupChanged)
    {
        bool diff = setupChanged != changedSetup;
        for (int i = 0; i < 4; ++i)
            diff = diff || s[i].status != steps[i].status || s[i].detail != steps[i].detail;
        if (! diff) return;
        for (int i = 0; i < 4; ++i) steps[i] = s[i];
        changedSetup = setupChanged;
        repaint();
    }

    // Reloads the editors after a venue is opened or created.
    void refresh()
    {
        nameEditor.setText (proc.venueName == "Untitled venue" ? juce::String() : proc.venueName, juce::dontSendNotification);
        notesEditor.setText (proc.venueNotes, juce::dontSendNotification);
        selected = proc.currentSession();
        updateButtons();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, venueCard.toFloat(), "Venue");
        drawPanel (g, sessionsCard.toFloat(), "Sessions");
        drawPanel (g, stepsCard.toFloat(), "Setup and tuning");

        // venue file line
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawText (proc.projectFile != juce::File() ? proc.projectFile.getFullPathName() : juce::String ("Not saved yet"),
                    venueFileLine, juce::Justification::centredLeft, true);

        // sessions list
        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawText ("A session is one visit. Mics, positions and the speaker setup belong to the venue and carry over.",
                    sessionsHint, juce::Justification::centredLeft, true);
        const auto& list = proc.getSessions();
        for (int i = 0; i < (int) list.size() && i < visibleRows(); ++i)
        {
            const int idx = i + scroll;
            if (idx >= (int) list.size()) break;
            auto r = rowBounds (i);
            const bool sel = idx == selected, open = idx == proc.currentSession();
            if (sel)
            {
                g.setColour (p.panelRaised);
                g.fillRoundedRectangle (r.toFloat(), 6.0f);
                g.setColour (p.border);
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
            }
            auto dot = r.removeFromLeft (26).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            g.setColour (open ? p.accent : p.border);
            g.fillEllipse (dot);
            const auto& s = list[(size_t) idx];
            auto right = r.removeFromRight (190);
            g.setColour (p.text);
            g.setFont (font (13.0f, open));
            g.drawText (s.name, r, juce::Justification::centredLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawText (s.created + "   " + juce::String (traceCount (idx)) + " traces" + (open ? "   OPEN" : ""),
                        right.withTrimmedRight (8), juce::Justification::centredRight, true);
        }

        // steps
        for (int i = 0; i < 4; ++i)
        {
            auto r = stepRow (i);
            const bool over = i == hoverStep;
            g.setColour (over ? p.panelRaised.brighter (p.dark ? 0.05f : 0.0f) : p.panelRaised);
            g.fillRoundedRectangle (r.toFloat(), 7.0f);
            // required and not done yet: amber, as on the mic setup steps; needs attention: stronger amber; done: green
            const auto st = steps[i].status;
            const bool required = (i == 0 || i == 1) && (st == StepStatus::NotStarted || st == StepStatus::InProgress);
            juce::Colour edge = over ? p.accent.withAlpha (0.7f) : p.border;
            float width = over ? 1.4f : 1.0f;
            if (st == StepStatus::Attention) { edge = p.warn.withAlpha (0.95f); width = 1.6f; }
            else if (required) { edge = p.warn.withAlpha (over ? 0.85f : 0.6f); width = 1.4f; }
            else if (st == StepStatus::Ready && ! over) { edge = p.good.withAlpha (0.5f); width = 1.2f; }
            g.setColour (edge);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, width);

            // chevron: the whole card opens the step
            juce::Path chev;
            const float cx = (float) r.getRight() - 24.0f, cy = (float) r.getCentreY();
            chev.startNewSubPath (cx - 3.0f, cy - 7.0f);
            chev.lineTo (cx + 4.0f, cy);
            chev.lineTo (cx - 3.0f, cy + 7.0f);
            g.setColour (over ? p.accent : p.textDim);
            g.strokePath (chev, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            auto icon = pageIcon (i + 1);
            icon.applyTransform (juce::AffineTransform::translation ((float) r.getX() + 14.0f, (float) r.getCentreY() - 12.0f));
            g.setColour (p.accent);
            g.strokePath (icon, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            drawStatusBadge (g, juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ (float) r.getX() + 60.0f, (float) r.getCentreY() }),
                             steps[i].status, steps[i].fraction);

            auto text = r.withTrimmedLeft (82).withTrimmedRight (i == 1 ? 230 : 50);
            g.setColour (p.text);
            g.setFont (font (14.0f, true));
            g.drawText (juce::String (i + 1) + "  " + pageName (i + 1), text.removeFromTop (r.getHeight() / 2).withTrimmedTop (4),
                        juce::Justification::bottomLeft, true);
            g.setColour (steps[i].status == StepStatus::Attention ? p.warn : p.textDim);
            g.setFont (font (12.0f));
            g.drawText (steps[i].detail, text.withTrimmedTop (2), juce::Justification::topLeft, true);
        }

        drawSessionSummary (g);

        if (changedSetup)
        {
            g.setColour (p.warn);
            g.setFont (font (12.0f, true));
            g.drawText ("The interface or the mic wiring has changed since this session's checks. Re-check the mic setup.",
                        changeLine, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (juce::jmin (520, r.getWidth() * 44 / 100));
        r.removeFromLeft (10);

        venueCard = left.removeFromTop (250);
        left.removeFromTop (10);
        sessionsCard = left;
        stepsCard = r;

        // venue card
        auto v = venueCard.reduced (16).withTrimmedTop (26);
        nameEditor.setBounds (v.removeFromTop (34));
        v.removeFromTop (8);
        venueFileLine = v.removeFromBottom (18);
        v.removeFromBottom (6);
        auto buttons = v.removeFromBottom (30);
        const int bw = (buttons.getWidth() - 18) / 4;
        for (auto* b : { &newVenueButton, &openButton, &saveButton, &saveAsButton }) { b->setBounds (buttons.removeFromLeft (bw)); buttons.removeFromLeft (6); }
        v.removeFromBottom (8);
        notesEditor.setBounds (v);

        // sessions card
        auto s = sessionsCard.reduced (16).withTrimmedTop (26);
        sessionsHint = s.removeFromTop (20);
        s.removeFromTop (4);
        auto sb = s.removeFromBottom (30);
        const int sw = (sb.getWidth() - 24) / 5;
        for (auto* b : { &newSessionButton, &openSessionButton, &compareButton, &renameButton, &deleteButton })
        {
            b->setBounds (sb.removeFromLeft (sw));
            sb.removeFromLeft (6);
        }
        s.removeFromBottom (8);
        listArea = s;

        // steps card
        auto st = stepsCard.reduced (16).withTrimmedTop (30);
        changeLine = st.removeFromBottom (22);
        const int rowH = 74;
        for (int i = 0; i < 4; ++i)
        {
            stepRows[i] = st.removeFromTop (rowH);
            st.removeFromTop (10);
        }
        summaryArea = st.withTrimmedTop (6);
        speakerChangedButton.setBounds (stepRows[1].withTrimmedRight (50).removeFromRight (160).withSizeKeepingCentre (160, 28));
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        int h = -1;
        for (int i = 0; i < 4; ++i) if (stepRows[i].contains (e.getPosition())) h = i;
        if (h != hoverStep) { hoverStep = h; setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint (stepsCard); }
    }

    void mouseExit (const juce::MouseEvent&) override { if (hoverStep >= 0) { hoverStep = -1; repaint (stepsCard); } }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < 4; ++i)
            if (stepRows[i].contains (e.getPosition())) { if (onGoTo) onGoTo (i + 1); return; }
        for (int i = 0; i < visibleRows(); ++i)
            if (rowBounds (i).contains (e.getPosition()) && i + scroll < (int) proc.getSessions().size())
            {
                selected = i + scroll;
                if (e.getNumberOfClicks() > 1) { proc.switchSession (selected); changed(); }
                updateButtons();
                repaint();
            }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override { mouseUp (e); }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scroll = juce::jlimit (0, juce::jmax (0, (int) proc.getSessions().size() - visibleRows()), scroll - (w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0)));
        repaint();
    }

private:
    // The open session at a glance: every mic's state, the loopback and the delays.
    void drawSessionSummary (juce::Graphics& g)
    {
        const auto& p = pal();
        auto r = summaryArea;
        if (r.getHeight() < 60) return;
        g.setColour (p.accent);
        g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 7.0f, 3.0f, 12.0f, 1.5f);
        g.setColour (p.text);
        g.setFont (font (13.0f, true));
        const auto& list = proc.getSessions();
        const int cur = proc.currentSession();
        g.drawText ("This session: " + (cur < (int) list.size() ? list[(size_t) cur].name : juce::String()), r.removeFromTop (26).withTrimmedLeft (10),
                    juce::Justification::centredLeft, true);
        g.setColour (p.border);
        g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
        r.removeFromTop (6);

        const int cols[] = { 26, 150, 110, 90, 150, 110, 100 };
        const char* heads[] = { "", "Mic", "Position", "Input", "Calibration file", "Sound level", "Delay" };
        auto header = r.removeFromTop (20);
        g.setFont (font (10.5f, true));
        g.setColour (p.textDim);
        for (int c = 0; c < 7; ++c) g.drawText (juce::String (heads[c]).toUpperCase(), header.removeFromLeft (cols[c]), juce::Justification::centredLeft);

        const auto& s = proc.settings;
        for (int m = 0; m < s.micCount.load() && r.getHeight() >= 22; ++m)
        {
            auto row = r.removeFromTop (22);
            g.setColour (micColour (m));
            g.fillEllipse (row.removeFromLeft (cols[0]).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
            g.setFont (font (12.0f));
            g.setColour (p.text);
            g.drawText (proc.mics.nameOf (m), row.removeFromLeft (cols[1]), juce::Justification::centredLeft, true);
            g.setColour (p.textDim);
            g.drawText (proc.mics.position[(size_t) m].isNotEmpty() ? proc.mics.position[(size_t) m] : juce::String ("-"), row.removeFromLeft (cols[2]), juce::Justification::centredLeft, true);
            g.drawText ("In " + juce::String (s.micInput[m].load() + 1), row.removeFromLeft (cols[3]), juce::Justification::centredLeft);
            const auto cal = proc.calibrations (m).label();
            g.drawText (cal.isNotEmpty() ? cal : (proc.mics.noCalFile[(size_t) m] ? juce::String ("none (flat)") : juce::String ("not loaded")),
                        row.removeFromLeft (cols[4]), juce::Justification::centredLeft, true);
            juce::String lvl = proc.mics.noCalibrator ? "dBFS" : (! s.micSplCalibrated[m].load() ? "not calibrated"
                                   : (proc.checks.splVerified[(size_t) m] ? "checked" : "re-check"));
            g.setColour (lvl == "re-check" || lvl == "not calibrated" ? p.warn : p.textDim);
            g.drawText (lvl, row.removeFromLeft (cols[5]), juce::Justification::centredLeft);
            g.setColour (p.textDim);
            g.drawText (juce::String (s.micDelayMs[m].load(), 2) + " ms", row.removeFromLeft (cols[6]), juce::Justification::centredLeft);
        }

        r.removeFromTop (6);
        if (r.getHeight() >= 20)
        {
            const auto& c = proc.checks;
            juce::String t = "Loopback: ";
            if (c.loopbackDone) t << juce::String (c.loopbackLatencyMs, 1) << " ms, " << (c.loopbackInverted ? "INVERTED" : "polarity normal");
            else t << (c.loopbackSkipped ? "skipped" : "not checked");
            t << "     Noise floor: ";
            if (c.noiseDone)
            {
                float worst = -200.0f;
                for (int m = 0; m < c.noiseMics; ++m) worst = juce::jmax (worst, c.noiseA[(size_t) m]);
                t << juce::String (worst, 1) << (c.noiseIsSpl[0] ? " dBA" : " dBFS(A)");
            }
            else t << "not measured";
            t << "     Traces: " << traceCount (cur);
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawText (t, r.removeFromTop (20), juce::Justification::centredLeft, true);
        }
    }

    void setupButton (juce::TextButton& b, std::function<void()> f, const juce::String& tip)
    {
        b.onClick = std::move (f);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    }

    void changed() { updateButtons(); repaint(); if (onChanged) onChanged(); }

    void toggleCompare()
    {
        if (proc.traces.hasComparisons()) proc.traces.clearComparisons();
        else proc.compareWithSession (selected);
        changed();
    }

    void updateButtons()
    {
        const int n = (int) proc.getSessions().size();
        selected = juce::jlimit (0, n - 1, selected);
        const bool other = selected != proc.currentSession();
        openSessionButton.setEnabled (other);
        compareButton.setEnabled (other || proc.traces.hasComparisons());
        compareButton.setButtonText (proc.traces.hasComparisons() ? "Stop compare" : "Compare");
        deleteButton.setEnabled (n > 1);
        speakerChangedButton.setButtonText (proc.speakers.changed ? "Changed: undo" : "Mark as changed");
    }

    int traceCount (int idx) const
    {
        if (idx == proc.currentSession())
        {
            int n = 0;
            for (const auto& t : proc.traces.all()) n += t.fromSession.isEmpty() ? 1 : 0;
            return n;
        }
        const auto& s = proc.getSessions()[(size_t) idx];
        if (s.body == nullptr) return 0;
        if (const auto* t = s.body->getChildByName ("Traces")) return t->getNumChildElements();
        return 0;
    }

    void timerCallback() override { updateButtons(); repaint (sessionsCard); repaint (summaryArea); }

    int visibleRows() const { return juce::jmax (1, listArea.getHeight() / 34); }
    juce::Rectangle<int> rowBounds (int i) const { return listArea.withHeight (30).translated (0, i * 34); }
    juce::Rectangle<int> stepRow (int i) const { return stepRows[i]; }

    LiveAlignProcessor& proc;
    juce::TextEditor nameEditor, notesEditor;
    juce::TextButton newVenueButton { "New venue" }, openButton { "Open..." }, saveButton { "Save" }, saveAsButton { "Save as..." },
        newSessionButton { "New session" }, openSessionButton { "Open" }, compareButton { "Compare" }, renameButton { "Rename" },
        deleteButton { "Delete" }, speakerChangedButton { "Mark as changed" };
    juce::Rectangle<int> summaryArea;
    juce::Rectangle<int> venueCard, sessionsCard, stepsCard, venueFileLine, sessionsHint, listArea, changeLine;
    juce::Rectangle<int> stepRows[4];
    StepInfo steps[4];
    bool changedSetup = false;
    int selected = 0, scroll = 0, hoverStep = -1;
};
} // namespace la::ui
