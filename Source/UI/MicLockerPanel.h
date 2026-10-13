// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Mic locker page: the measurement mics you own, kept on this computer. Each has a name, model, serial number,
// its 0 and 90 degree calibration files and its last sound level calibration. Mics are put into a venue from the
// Locker button on a Mic setup row.
#include <functional>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class MicLockerPage : public juce::Component, private juce::Timer
{
public:
    std::function<void (const juce::String& id, int angle)> onLoadCal;   // the editor owns file choosers
    std::function<void()> onChanged;

    explicit MicLockerPage (LiveAlignProcessor& p) : proc (p)
    {
        addButton.onClick = [this]
        {
            LockerMic m;
            m.id = juce::Uuid().toString();
            m.model = "Measurement mic";
            m.savedOn = LiveAlignProcessor::today();
            proc.locker.mics.push_back (m);
            selected = (int) proc.locker.mics.size() - 1;
            saveAndRefresh();
            nameEditor.grabKeyboardFocus();
        };
        addButton.setTooltip ("Add a mic you own, then load its calibration file(s)");
        addAndMakeVisible (addButton);
        fromVenueButton.onClick = [this] { showFromVenueMenu(); };
        fromVenueButton.setTooltip ("Save one of this venue's mics (with its calibration) to the locker");
        addAndMakeVisible (fromVenueButton);

        auto setupEditor = [this] (juce::TextEditor& e, const juce::String& hint)
        {
            e.setFont (font (13.0f));
            e.setTextToShowWhenEmpty (hint, pal().textFaint);
            e.onTextChange = [this] { editorsChanged(); };
            addAndMakeVisible (e);
        };
        setupEditor (nameEditor, "Optional: shown instead of model and serial");
        setupEditor (modelEditor, "e.g. UMIK-1, ECM8000, M30");
        setupEditor (serialEditor, "Serial number");
        setupEditor (notesEditor, "Notes: windscreen, cable, last checked ...");
        notesEditor.setMultiLine (true);
        notesEditor.setReturnKeyStartsNewLine (true);

        for (int a = 0; a < 2; ++a)
        {
            loadButtons[a].setButtonText ("Load " + calAngleName (a) + " file...");
            loadButtons[a].onClick = [this, a] { if (auto* m = current(); m != nullptr && onLoadCal) onLoadCal (m->id, a); };
            clearButtons[a].onClick = [this, a] { if (auto* m = current()) { m->cals.clear (a); saveAndRefresh(); } };
            addAndMakeVisible (loadButtons[a]);
            addAndMakeVisible (clearButtons[a]);
        }
        loadBothButton.onClick = [this] { if (auto* m = current(); m != nullptr && onLoadCal) onLoadCal (m->id, -1); };
        loadBothButton.setTooltip ("Select the 0 and 90 degree files together; each goes to its angle by its name");
        addAndMakeVisible (loadBothButton);
        clearSplButton.onClick = [this] { if (auto* m = current()) { m->hasSpl = false; m->splDate.clear(); saveAndRefresh(); } };
        clearSplButton.setTooltip ("Forget the sound level calibration (calibrate again on Mic setup)");
        addAndMakeVisible (clearSplButton);
        removeButton.onClick = [this] { removeSelected(); };
        addAndMakeVisible (removeButton);

        refresh();
        startTimerHz (2);
    }

    void refresh()
    {
        selected = juce::jlimit (-1, (int) proc.locker.mics.size() - 1, selected < 0 && ! proc.locker.mics.empty() ? 0 : selected);
        const auto* m = current();
        for (auto* c : std::initializer_list<juce::Component*> { &nameEditor, &modelEditor, &serialEditor, &notesEditor, &loadBothButton,
                                                                 &loadButtons[0], &loadButtons[1], &clearButtons[0], &clearButtons[1],
                                                                 &clearSplButton, &removeButton })
            c->setVisible (m != nullptr);
        if (m != nullptr)
        {
            updating = true;
            if (! nameEditor.hasKeyboardFocus (true)) nameEditor.setText (m->name, juce::dontSendNotification);
            if (! modelEditor.hasKeyboardFocus (true)) modelEditor.setText (m->model, juce::dontSendNotification);
            if (! serialEditor.hasKeyboardFocus (true)) serialEditor.setText (m->serial, juce::dontSendNotification);
            if (! notesEditor.hasKeyboardFocus (true)) notesEditor.setText (m->notes, juce::dontSendNotification);
            updating = false;
            for (int a = 0; a < 2; ++a)
            {
                clearButtons[a].setButtonText ("Clear");
                clearButtons[a].setEnabled (m->cals.has (a));
            }
            clearSplButton.setEnabled (m->hasSpl);
        }
        for (auto* e : { &nameEditor, &modelEditor, &serialEditor, &notesEditor }) e->applyColourToAllText (pal().text);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, listCard.toFloat(), "Mics you own");
        drawPanel (g, editCard.toFloat(), current() != nullptr ? current()->label() : juce::String ("Mic"));

        const auto& lk = proc.locker.mics;
        g.setFont (font (11.5f));
        if (lk.empty())
        {
            g.setColour (p.textDim);
            g.drawFittedText ("No mics yet. Add the mics you own with their calibration files, then pick them on Mic setup with the "
                              "Locker button on a mic's row.", listArea.withHeight (60), juce::Justification::topLeft, 3);
        }
        for (int i = 0; i < (int) lk.size(); ++i)
        {
            const auto row = rowRect (i);
            if (row.getBottom() > listArea.getBottom()) break;
            const bool sel = i == selected, over = i == hoverRow;
            g.setColour (sel ? p.panelRaised : over ? p.panelRaised.withAlpha (0.5f) : p.panel);
            g.fillRoundedRectangle (row.toFloat(), 6.0f);
            if (sel) { g.setColour (p.accent); g.drawRoundedRectangle (row.toFloat().reduced (0.5f), 6.0f, 1.2f); }
            auto r = row.reduced (12, 5);
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            g.drawText (lk[(size_t) i].label(), r.removeFromTop (r.getHeight() / 2), juce::Justification::bottomLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (11.0f));
            juce::String info;
            if (lk[(size_t) i].name.isNotEmpty() && lk[(size_t) i].modelSerial().isNotEmpty()) info << lk[(size_t) i].modelSerial() << "  -  ";
            info << (lk[(size_t) i].cals.both() ? "0 + 90 deg files" : lk[(size_t) i].cals.any() ? "1 cal file" : "no cal file");
            info << "  -  " << (lk[(size_t) i].hasSpl ? "level calibrated" : "level not calibrated");
            const auto used = usedIn (lk[(size_t) i].id);
            if (used.isNotEmpty()) info << "  -  " << used;
            g.drawText (info, r, juce::Justification::topLeft, true);
        }
        g.setColour (p.textFaint);
        g.setFont (font (10.5f));
        g.drawText ("Saved on this computer: " + MicLocker::file().getFullPathName(), pathLine, juce::Justification::centredLeft, true);

        const auto* m = current();
        if (m == nullptr) return;
        for (const auto& l : labels)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            g.drawText (l.second.toUpperCase(), l.first, juce::Justification::centredLeft, true);
        }
        g.setFont (font (12.0f));
        for (int a = 0; a < 2; ++a)
        {
            g.setColour (m->cals.has (a) ? p.text : p.textFaint);
            g.drawText (m->cals.has (a) ? m->cals.name[a] : juce::String ("none"), calName[a], juce::Justification::centredLeft, true);
        }
        g.setColour (m->hasSpl ? p.text : p.textFaint);
        g.drawText (m->hasSpl ? juce::String (m->splOffset, 2) + " dB SPL at 0 dBFS, calibrated " + m->splDate
                              : juce::String ("Not calibrated yet: calibrate it on Mic setup and the locker keeps it"),
                    splLine, juce::Justification::centredLeft, true);
        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawFittedText ("The sound level holds for the interface gain used then; each session asks for a quick re-check. "
                          "A venue row picked from the locker stays linked: calibrating it there updates the locker.",
                          hintArea, juce::Justification::topLeft, 3);
    }

    void resized() override
    {
        labels.clear();
        auto r = getLocalBounds();
        listCard = r.removeFromLeft (juce::jmax (360, r.getWidth() * 42 / 100));
        r.removeFromLeft (10);
        editCard = r;
        {
            auto c = listCard.reduced (14, 0).withTrimmedTop (36);
            pathLine = c.removeFromBottom (24);
            auto b = c.removeFromBottom (40).withSizeKeepingCentre (c.getWidth(), 30);
            addButton.setBounds (b.removeFromLeft (120));
            b.removeFromLeft (8);
            fromVenueButton.setBounds (b.removeFromLeft (200));
            listArea = c.withTrimmedBottom (8);
        }
        {
            auto c = editCard.reduced (16, 0).withTrimmedTop (40);
            auto field = [&] (const juce::String& name, juce::Component& comp, int h = 30)
            {
                auto row = c.removeFromTop (h);
                labels.push_back ({ row.removeFromLeft (110), name });
                comp.setBounds (row);
                c.removeFromTop (8);
            };
            field ("Model", modelEditor);
            field ("Serial", serialEditor);
            field ("Name", nameEditor);
            c.removeFromTop (10);
            labels.push_back ({ c.removeFromTop (18), "Calibration files" });
            c.removeFromTop (4);
            loadBothButton.setBounds (c.removeFromTop (30).withWidth (260));
            c.removeFromTop (8);
            for (int a = 0; a < 2; ++a)
            {
                auto row = c.removeFromTop (30);
                labels.push_back ({ row.removeFromLeft (110), calAngleName (a) + (a == 0 ? "  at speaker" : "  pointing up") });
                clearButtons[a].setBounds (row.removeFromRight (70));
                row.removeFromRight (8);
                loadButtons[a].setBounds (row.removeFromRight (140));
                row.removeFromRight (12);
                calName[a] = row;
                c.removeFromTop (6);
            }
            c.removeFromTop (12);
            {
                auto row = c.removeFromTop (30);
                labels.push_back ({ row.removeFromLeft (110), "Sound level" });
                clearSplButton.setBounds (row.removeFromRight (70));
                row.removeFromRight (12);
                splLine = row;
            }
            c.removeFromTop (6);
            hintArea = c.removeFromTop (36).withTrimmedLeft (110);
            c.removeFromTop (10);
            field ("Notes", notesEditor, 80);
            auto bottom = editCard.reduced (16, 14).removeFromBottom (30);
            removeButton.setBounds (bottom.removeFromRight (150));
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int) proc.locker.mics.size(); ++i)
            if (rowRect (i).contains (e.getPosition())) { selected = i; refresh(); return; }
    }
    void mouseMove (const juce::MouseEvent& e) override
    {
        int h = -1;
        for (int i = 0; i < (int) proc.locker.mics.size(); ++i) if (rowRect (i).contains (e.getPosition())) h = i;
        if (h != hoverRow) { hoverRow = h; setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { if (hoverRow >= 0) { hoverRow = -1; repaint(); } }

    void selectId (const juce::String& id)
    {
        for (int i = 0; i < (int) proc.locker.mics.size(); ++i) if (proc.locker.mics[(size_t) i].id == id) selected = i;
        refresh();
    }

private:
    LockerMic* current() { return selected >= 0 && selected < (int) proc.locker.mics.size() ? &proc.locker.mics[(size_t) selected] : nullptr; }
    const LockerMic* current() const { return selected >= 0 && selected < (int) proc.locker.mics.size() ? &proc.locker.mics[(size_t) selected] : nullptr; }
    juce::Rectangle<int> rowRect (int i) const { return { listArea.getX(), listArea.getY() + i * 54, listArea.getWidth(), 48 }; }

    juce::String usedIn (const juce::String& id) const
    {
        juce::StringArray rows;
        for (int m = 0; m < proc.settings.micCount.load(); ++m) if (proc.mics.lockerId[(size_t) m] == id) rows.add (juce::String (m + 1));
        return rows.isEmpty() ? juce::String() : "mic " + rows.joinIntoString (", ") + " in this venue";
    }

    void editorsChanged()
    {
        if (updating) return;
        auto* m = current();
        if (m == nullptr) return;
        m->name = nameEditor.getText().trim();
        m->model = modelEditor.getText().trim();
        m->serial = serialEditor.getText().trim();
        m->notes = notesEditor.getText();
        proc.locker.save();
        proc.lockerEntryChanged (m->id);
        repaint();
        if (onChanged) onChanged();
    }

    void saveAndRefresh()
    {
        proc.locker.save();
        if (const auto* m = current()) proc.lockerEntryChanged (m->id);
        refresh();
        if (onChanged) onChanged();
    }

    void removeSelected()
    {
        auto* m = current();
        if (m == nullptr) return;
        const auto id = m->id;
        juce::Component::SafePointer<MicLockerPage> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Remove mic",
                                          "Remove \"" + m->label() + "\" from the locker? Venues that use it keep their copy of its calibration.",
                                          "Remove", "Cancel", this),
                                      [safe, id] (int r)
        {
            if (safe == nullptr || r != 1) return;
            safe->proc.locker.remove (id);
            safe->proc.locker.save();
            for (auto& l : safe->proc.mics.lockerId) if (l == id) l.clear();
            safe->selected = juce::jmax (0, safe->selected - 1);
            safe->refresh();
            if (safe->onChanged) safe->onChanged();
        });
    }

    void showFromVenueMenu()
    {
        juce::PopupMenu menu;
        for (int m = 0; m < proc.settings.micCount.load(); ++m)
            menu.addItem (m + 1, juce::String (m + 1) + "  " + proc.mics.nameOf (m) + (proc.calibrations (m).any() ? "  (" + proc.calibrations (m).label() + ")" : juce::String()),
                          proc.locker.find (proc.mics.lockerId[(size_t) m]) == nullptr);
        juce::Component::SafePointer<MicLockerPage> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&fromVenueButton), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            const auto id = safe->proc.saveMicToLocker (r - 1, safe->proc.mics.name[(size_t) (r - 1)]);
            safe->selectId (id);
            if (safe->onChanged) safe->onChanged();
        });
    }

    void timerCallback() override { if (isVisible()) repaint (listCard); }

    LiveAlignProcessor& proc;
    int selected = -1, hoverRow = -1;
    bool updating = false;
    juce::TextButton addButton { "+ Add mic" }, fromVenueButton { "Save a mic from this venue..." }, removeButton { "Remove from locker" };
    juce::TextButton loadBothButton { "Load both files (0 and 90 degrees)..." }, loadButtons[2], clearButtons[2], clearSplButton { "Clear" };
    juce::TextEditor nameEditor, modelEditor, serialEditor, notesEditor;
    juce::Rectangle<int> listCard, editCard, listArea, pathLine, calName[2], splLine, hintArea;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
};
} // namespace la::ui
