// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Tuning step: the listening check, after Room EQ and before the room is called tuned. Music you know well at show
// level, walked from point to point: a verdict and a note at each, and the EQ compared in and bypassed. The notes go in
// the tuning report; a problem at one point can be measured again there (Room EQ).
#include <functional>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class ListeningPage : public juce::Component
{
public:
    std::function<void()> onChanged;
    std::function<void (const juce::String& point)> onRemeasure;

    explicit ListeningPage (LiveAlignProcessor& p) : proc (p)
    {
        musicEditor.setTextToShowWhenEmpty ("The reference music played (artist, track)", pal().textFaint);
        musicEditor.setFont (font (13.0f));
        musicEditor.onTextChange = [this] { proc.listening.music = musicEditor.getText(); changed(); };
        addAndMakeVisible (musicEditor);
        doneButton.onClick = [this]
        {
            auto& l = proc.listening;
            l.done = ! l.done;
            l.doneOn = l.done ? LiveAlignProcessor::today() : juce::String();
            // the last step before the room is called tuned
            if (l.done && ! proc.room.tuned && proc.room.checkRound > 0) { proc.room.tuned = true; proc.room.tunedOn = LiveAlignProcessor::today(); }
            changed();
        };
        addAndMakeVisible (doneButton);
        refresh();
    }

    bool done() const { return proc.listening.done; }
    juce::String nextHint() const
    {
        if (proc.listening.done) return {};
        const int heard = proc.listening.count (1) + proc.listening.count (2);
        if (heard == 0) return "Listen at each point with music you know";
        if (proc.listening.count (2) > 0) return "Look into the problem points, or call it done";
        return "Call the listening check done";
    }

    void refresh()
    {
        if (! musicEditor.hasKeyboardFocus (true)) musicEditor.setText (proc.listening.music, juce::dontSendNotification);
        juce::StringArray names;
        for (const auto& r : rows) names.add (r->point);
        if (names != proc.positions)
        {
            rows.clear();
            for (const auto& pt : proc.positions)
            {
                auto r = std::make_unique<Row> (*this, pt);
                addAndMakeVisible (*r);
                rows.push_back (std::move (r));
            }
        }
        for (auto& r : rows) r->sync();
        doneButton.setButtonText (proc.listening.done ? "Listening check done: undo"
                                                      : proc.room.checkRound > 0 && ! proc.room.tuned ? "Done: the room is tuned" : "Listening check done");
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, pointsCard.toFloat(), "At each point");
        drawPanel (g, howCard.toFloat(), "How to listen");
        g.setColour (p.textFaint);
        g.setFont (font (10.5f, true));
        g.drawText ("POINT", head.withWidth (150), juce::Justification::centredLeft);
        g.drawText ("HOW IT SOUNDS", head.withTrimmedLeft (150).withWidth (170), juce::Justification::centredLeft);
        g.drawText ("NOTES", head.withTrimmedLeft (330), juce::Justification::centredLeft);
        if (rows.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("Add the tuning points on Venue, Plan and points first.", listArea, juce::Justification::centred, 2);
        }

        auto r = howText;
        auto step = [&] (int n, const juce::String& title, const juce::String& text)
        {
            g.setColour (p.accent);
            g.setFont (font (12.5f, true));
            g.drawText (juce::String (n) + "  " + title, r.removeFromTop (20), juce::Justification::centredLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            const int lines = 1 + text.length() / 52;
            g.drawFittedText (text, r.removeFromTop (lines * 16 + 4).withTrimmedLeft (18), juce::Justification::topLeft, lines + 1, 1.0f);
            r.removeFromTop (8);
        };
        step (1, "Music you know", "A few tracks you know well (on the system you mix on), at the show level. Speech too: a spoken-word track, or a voice on the lectern mic.");
        step (2, "Walk the points", "Sit at each point. Listen for harsh or bright highs, a boomy or muddy low end, thin or honky mids, and whether speech is clear.");
        step (3, "EQ in, then bypassed", "Bypass the Room EQ in the processor for a moment, then put it back: it should sound better with it in at most seats. Tick Compared.");
        step (4, "One seat, or everywhere?", "A problem at one point only is usually that seat (leave it, or check the aim). The same problem everywhere: measure there again and refine the EQ.");
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        const auto& l = proc.listening;
        g.drawFittedText (juce::String (l.count (1)) + " good, " + juce::String (l.count (2)) + " with a problem, of " + juce::String (proc.positions.size())
                              + " points." + (l.done ? " Done " + l.doneOn + "." : juce::String()),
                          summaryArea, juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        howCard = r.removeFromRight (380);
        r.removeFromRight (10);
        pointsCard = r;
        auto c = pointsCard.reduced (14, 0).withTrimmedTop (38).withTrimmedBottom (12);
        musicEditor.setBounds (c.removeFromTop (30));
        c.removeFromTop (12);
        head = c.removeFromTop (18);
        c.removeFromTop (4);
        listArea = c;
        for (auto& row : rows)
        {
            const auto b = c.removeFromTop (row->heightWanted());
            c.removeFromTop (6);
            row->setVisible (b.getBottom() <= listArea.getBottom());
            row->setBounds (b);
        }
        auto h = howCard.reduced (14, 0).withTrimmedTop (38).withTrimmedBottom (12);
        doneButton.setBounds (h.removeFromBottom (32));
        h.removeFromBottom (8);
        summaryArea = h.removeFromBottom (34);
        howText = h;
    }

private:
    struct Row : public juce::Component
    {
        Row (ListeningPage& o, const juce::String& pt) : owner (o), point (pt)
        {
            for (int k = 0; k < 2; ++k)
            {
                verdict[k].setButtonText (k == 0 ? "Good" : "A problem");
                verdict[k].setClickingTogglesState (false);
                verdict[k].onClick = [this, k]
                {
                    auto& v = owner.proc.listening.points[point].verdict;
                    v = v == k + 1 ? 0 : k + 1;
                    owner.changed();
                };
                addAndMakeVisible (verdict[k]);
            }
            notes.setTextToShowWhenEmpty ("What you hear here", pal().textFaint);
            notes.setFont (font (12.5f));
            notes.onTextChange = [this] { owner.proc.listening.points[point].notes = notes.getText(); owner.changedQuietly(); };
            addAndMakeVisible (notes);
            words.setButtonText ("+");
            words.setTooltip ("Add a word: harsh, boomy, muddy ...");
            words.onClick = [this]
            {
                juce::PopupMenu m;
                const juce::StringArray w { "Harsh", "Bright", "Dull", "Boomy", "Muddy", "Thin", "Honky", "Speech unclear", "Echo", "Too loud", "Too quiet", "Lacks low end" };
                for (int i = 0; i < w.size(); ++i) m.addItem (i + 1, w[i]);
                juce::Component::SafePointer<Row> safe (this);
                m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&words), [safe, w] (int r)
                {
                    if (safe == nullptr || r <= 0) return;
                    auto t = safe->notes.getText().trim();
                    safe->notes.setText (t.isEmpty() ? w[r - 1] : t + ", " + w[r - 1].toLowerCase(), juce::sendNotification);
                });
            };
            addAndMakeVisible (words);
            compared.setButtonText ("Compared");
            compared.setTooltip ("The EQ was compared in and bypassed here");
            compared.onClick = [this] { owner.proc.listening.points[point].compared = compared.getToggleState(); owner.changed(); };
            addAndMakeVisible (compared);
            remeasure.setButtonText ("Measure here again");
            remeasure.setTooltip ("Room EQ: measure this point again (the guided pop-up)");
            remeasure.onClick = [this] { if (owner.onRemeasure) owner.onRemeasure (point); };
            addChildComponent (remeasure);
        }
        int heightWanted() const { return 40; }
        void sync()
        {
            const auto it = owner.proc.listening.points.find (point);
            const ListeningData::Point pt = it != owner.proc.listening.points.end() ? it->second : ListeningData::Point {};
            for (int k = 0; k < 2; ++k)
            {
                const bool on = pt.verdict == k + 1;
                verdict[k].setColour (juce::TextButton::buttonColourId, on ? (k == 0 ? pal().good : pal().warn) : pal().panelRaised);
                verdict[k].setColour (juce::TextButton::textColourOffId, on ? pal().onAccent : pal().text);
            }
            if (! notes.hasKeyboardFocus (true)) notes.setText (pt.notes, juce::dontSendNotification);
            compared.setToggleState (pt.compared, juce::dontSendNotification);
            remeasure.setVisible (pt.verdict == 2);
            resized();
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (pal().panelRaised.withAlpha (0.45f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
            g.setColour (pal().text);
            g.setFont (font (13.0f, true));
            g.drawText (point, getLocalBounds().reduced (10, 0).withWidth (136), juce::Justification::centredLeft, true);
        }
        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 5);
            r.removeFromLeft (144);
            verdict[0].setBounds (r.removeFromLeft (72));
            r.removeFromLeft (4);
            verdict[1].setBounds (r.removeFromLeft (84));
            r.removeFromLeft (10);
            compared.setBounds (r.removeFromRight (124));
            r.removeFromRight (6);
            if (remeasure.isVisible()) { remeasure.setBounds (r.removeFromRight (150)); r.removeFromRight (6); }
            words.setBounds (r.removeFromRight (30));
            r.removeFromRight (4);
            notes.setBounds (r);
        }
        ListeningPage& owner;
        juce::String point;
        juce::TextButton verdict[2], words, remeasure;
        juce::TextEditor notes;
        juce::ToggleButton compared;
    };

    void changed() { refresh(); if (onChanged) onChanged(); }
    void changedQuietly() { repaint (summaryArea); if (onChanged) onChanged(); }

    LiveAlignProcessor& proc;
    juce::TextEditor musicEditor;
    juce::TextButton doneButton;
    std::vector<std::unique_ptr<Row>> rows;
    juce::Rectangle<int> pointsCard, howCard, head, listArea, howText, summaryArea;
};
} // namespace la::ui
