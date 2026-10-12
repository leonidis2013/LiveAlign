// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Manage the mic locker: every saved mic with its calibration file and last sound level calibration,
// which row of this venue uses it, and rename / remove.
#include <functional>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class MicLockerPanel : public juce::Component
{
public:
    std::function<void (const juce::String&)> onRename;   // locker id
    std::function<void()> onChanged;

    explicit MicLockerPanel (LiveAlignProcessor& p) : proc (p) { rebuild(); }

    void rebuild()
    {
        buttons.clear();
        const int n = (int) proc.locker.mics.size();
        for (int i = 0; i < n; ++i)
        {
            const auto id = proc.locker.mics[(size_t) i].id;
            auto rename = std::make_unique<juce::TextButton> ("Rename");
            auto remove = std::make_unique<juce::TextButton> ("Remove");
            rename->onClick = [this, id] { if (onRename) onRename (id); };
            remove->onClick = [this, id]
            {
                juce::Component::SafePointer<MicLockerPanel> safe (this);
                juce::MessageManager::callAsync ([safe, id]
                {
                    if (safe == nullptr) return;
                    safe->proc.locker.remove (id);
                    safe->proc.locker.save();
                    for (auto& l : safe->proc.mics.lockerId) if (l == id) l.clear();
                    safe->rebuild();
                    if (safe->onChanged) safe->onChanged();
                });
            };
            addAndMakeVisible (*rename);
            addAndMakeVisible (*remove);
            buttons.push_back (std::move (rename));
            buttons.push_back (std::move (remove));
        }
        setSize (620, 92 + juce::jmax (1, n) * kRowH + 40);
        resized();
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.fillAll (p.panel);
        drawPanel (g, getLocalBounds().toFloat(), "Mic locker");
        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        g.drawFittedText ("Mics you own, kept on this computer. Pick one from the " + juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbe"))
                              + " button beside a mic's name on Mic setup to bring its calibration file and sound level into a venue.",
                          getLocalBounds().reduced (16, 0).withTrimmedTop (36).withHeight (34), juce::Justification::topLeft, 2);
        const auto& lk = proc.locker.mics;
        if (lk.empty())
        {
            g.setColour (p.textFaint);
            g.drawText ("No mics saved yet. Use \"Save this mic to the locker\" on a mic's row.", rowArea (0), juce::Justification::centredLeft);
        }
        for (int i = 0; i < (int) lk.size(); ++i)
        {
            auto r = rowArea (i);
            g.setColour (p.panelRaised);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
            r = r.reduced (12, 4).withTrimmedRight (180);
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            juce::String name = lk[(size_t) i].name;
            for (int m = 0; m < proc.settings.micCount.load(); ++m)
                if (proc.mics.lockerId[(size_t) m] == lk[(size_t) i].id) name << "   (row " << (m + 1) << " in this venue)";
            g.drawText (name, r.removeFromTop (r.getHeight() / 2), juce::Justification::bottomLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (11.0f));
            g.drawText (lk[(size_t) i].summary(), r, juce::Justification::topLeft, true);
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawText ("Saved in " + MicLocker::file().getFullPathName(), getLocalBounds().reduced (16, 0).removeFromBottom (30),
                    juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        for (size_t i = 0; i + 1 < buttons.size(); i += 2)
        {
            auto r = rowArea ((int) i / 2).reduced (10, 9);
            buttons[i + 1]->setBounds (r.removeFromRight (80));
            r.removeFromRight (8);
            buttons[i]->setBounds (r.removeFromRight (80));
        }
    }

private:
    static constexpr int kRowH = 50;
    juce::Rectangle<int> rowArea (int i) const { return { 16, 80 + i * kRowH, getWidth() - 32, kRowH - 6 }; }

    LiveAlignProcessor& proc;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
};
} // namespace la::ui
