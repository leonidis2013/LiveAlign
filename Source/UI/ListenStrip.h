// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The listening level, on every page (bottom of the navigation pane): what is being heard, a mute, and the level.
// Where it plays (a spare interface output, or another device) is set in Settings, Audio.
#include <functional>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class ListenStrip : public juce::Component
{
public:
    std::function<void()> onOpenSettings;   // Settings, Audio: what to hear and where

    explicit ListenStrip (LiveAlignProcessor& p) : proc (p)
    {
        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        level.setRange (-60.0, 12.0, 0.5);
        level.setDoubleClickReturnValue (true, -12.0);
        level.setTooltip ("Listening level (double-click: -12 dB). What to hear and where: Settings, Audio");
        level.onValueChange = [this] { proc.settings.listenGainDb = (float) level.getValue(); repaint(); };
        level.onDragEnd = [this] { proc.saveListenLevel(); };
        addAndMakeVisible (level);
        mute.setClickingTogglesState (true);
        mute.setTooltip ("Mute the headphones");
        mute.onClick = [this] { proc.settings.listenMute = mute.getToggleState(); proc.saveListenLevel(); refresh(); };
        addAndMakeVisible (mute);
        setup.setTooltip ("Listen to the reference or a mic on headphones: pick it in Settings, Audio");
        setup.onClick = [this] { if (onOpenSettings) onOpenSettings(); };
        addChildComponent (setup);
        refresh();
    }

    bool listening() const { return proc.settings.listenSource.load() > 0 && level.isVisible(); }

    void refresh()
    {
        const bool on = proc.settings.listenSource.load() > 0;
        level.setVisible (on);
        mute.setVisible (on);
        setup.setVisible (! on);
        if (! level.isMouseButtonDown()) level.setValue (proc.settings.listenGainDb.load(), juce::dontSendNotification);
        mute.setToggleState (proc.settings.listenMute.load(), juce::dontSendNotification);
        mute.setButtonText (mute.getToggleState() ? "MUTED" : "Mute");
        mute.setColour (juce::TextButton::buttonOnColourId, pal().warn);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        if (proc.settings.listenSource.load() == 0) return;
        const auto& p = pal();
        auto top = getLocalBounds().removeFromTop (20);
        const int src = proc.settings.listenSource.load();
        const auto what = src == 1 ? juce::String ("Reference") : proc.mics.nameOf (src - 2);
        g.setColour (p.textFaint);
        g.setFont (font (10.0f, true));
        g.drawText ("LISTEN", top.removeFromLeft (44), juce::Justification::centredLeft);
        g.setColour (proc.settings.listenMute.load() ? p.textDim : p.text);
        g.setFont (font (11.5f, true));
        g.drawText (what, top.withTrimmedRight (mute.getWidth() + 4), juce::Justification::centredLeft, true);
        // the level, at the right of the slider row
        auto row = getLocalBounds().withTrimmedTop (22);
        g.setColour (p.textDim);
        g.setFont (font (10.5f));
        const float db = proc.settings.listenGainDb.load();
        g.drawText ((db > 0.0f ? "+" : "") + juce::String (juce::roundToInt (db)) + " dB", row.removeFromRight (40), juce::Justification::centredRight);
        if (proc.listenError.isNotEmpty())
        {
            g.setColour (p.warn);
            g.drawText ("!", top.removeFromRight (mute.getWidth() + 14).removeFromLeft (10), juce::Justification::centred);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        setup.setBounds (r.withSizeKeepingCentre (r.getWidth(), 26));
        auto top = r.removeFromTop (20);
        mute.setBounds (top.removeFromRight (58).reduced (0, 1));
        r.removeFromTop (2);
        level.setBounds (r.withTrimmedRight (42));
    }

private:
    LiveAlignProcessor& proc;
    juce::Slider level;
    juce::TextButton mute, setup { "Listen on headphones..." };
};
} // namespace la::ui
