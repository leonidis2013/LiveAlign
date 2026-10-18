// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Listening on headphones, set up where the audio interface is (the Audio interface window, and Settings > Audio):
// what to hear (the reference or a mic) and where it plays (a spare output of the interface, or another device on
// this computer). The level and mute are on the main screen (bottom of the menu).
#include "../PluginProcessor.h"

namespace la::ui
{
namespace listen
{
// What can be heard: Off, the reference, each mic.
inline juce::StringArray sources (const LiveAlignProcessor& p)
{
    juce::StringArray s { "Off", "The reference" };
    const int mics = juce::jmax (1, p.settings.micCount.load());
    for (int m = 0; m < mics; ++m) s.add (p.mics.nameOf (m));
    return s;
}

// Where it can play: each output of the interface (flagging the ones that feed a speaker), then every other output
// device on this computer. `targets` holds "#n" for interface output n (0-based), else the device's "Type: Name".
inline void destinations (LiveAlignProcessor& p, juce::StringArray& labels, juce::StringArray& targets)
{
    labels.clear();
    targets.clear();
    for (int c = 0; c < juce::jmax (0, p.numOutputs()); ++c)
    {
        juce::String feeds;
        for (int i = 0; i < (int) p.system.sources.size(); ++i)
            if (p.system.sources[(size_t) i].output == c + 1) feeds = p.system.nameOf (i);
        labels.add ("This interface: output " + juce::String (c + 1) + (feeds.isNotEmpty() ? "  (feeds " + feeds + "!)" : juce::String()));
        targets.add ("#" + juce::String (c));
    }
    for (const auto& d : p.listenDevice.deviceNames()) { labels.add (d); targets.add (d); }
}

inline int selectedDestination (const LiveAlignProcessor& p, const juce::StringArray& targets)
{
    const int ch = p.settings.listenChannel.load();
    return ch >= 0 ? targets.indexOf ("#" + juce::String (ch)) : targets.indexOf (p.listenDeviceName);
}

inline void apply (LiveAlignProcessor& p, int source, const juce::String& target)
{
    if (target.startsWith ("#")) p.setListening (source, target.substring (1).getIntValue(), {});
    else p.setListening (source, -1, target);
}

inline juce::String status (LiveAlignProcessor& p)
{
    if (p.settings.listenSource.load() == 0) return "Off";
    if (p.listenError.isNotEmpty()) return p.listenError;
    const int ch = p.settings.listenChannel.load();
    if (ch >= 0) return "On output " + juce::String (ch + 1) + " of the interface";
    if (p.listenDevice.isOpen()) return "On " + p.listenDevice.description();
    return "Pick where to listen";
}
} // namespace listen

// The section in the Audio interface window.
class ListenSetup : public juce::Component, private juce::Timer
{
public:
    explicit ListenSetup (LiveAlignProcessor& p) : proc (p)
    {
        for (auto* l : { &title, &srcLabel, &dstLabel, &state, &note })
        {
            l->setColour (juce::Label::textColourId, l == &title ? juce::Colours::white : juce::Colour (0xffb8c4cc));
            addAndMakeVisible (*l);
        }
        title.setText ("Headphones (listening)", juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        srcLabel.setText ("Listen to:", juce::dontSendNotification);
        dstLabel.setText ("Play on:", juce::dontSendNotification);
        srcLabel.setJustificationType (juce::Justification::centredRight);
        dstLabel.setJustificationType (juce::Justification::centredRight);
        note.setText ("A spare output of this interface (never one that feeds the PA), or another device such as the computer's headphone jack. "
                      "The level and Mute are on the main screen, at the bottom of the menu.", juce::dontSendNotification);
        note.setFont (juce::FontOptions (12.0f));
        src.onChange = [this] { listen::apply (proc, src.getSelectedItemIndex(), targetAt (dst.getSelectedItemIndex())); refresh(); };
        dst.onChange = [this] { listen::apply (proc, src.getSelectedItemIndex(), targetAt (dst.getSelectedItemIndex())); refresh(); };
        addAndMakeVisible (src);
        addAndMakeVisible (dst);
        refresh();
        startTimerHz (1);   // the interface may change above (fewer outputs, another device)
    }

    void refresh()
    {
        src.clear (juce::dontSendNotification);
        src.addItemList (listen::sources (proc), 1);
        src.setSelectedItemIndex (juce::jlimit (0, src.getNumItems() - 1, proc.settings.listenSource.load()), juce::dontSendNotification);
        juce::StringArray labels;
        listen::destinations (proc, labels, targets);
        dst.clear (juce::dontSendNotification);
        dst.addItemList (labels, 1);
        const int sel = listen::selectedDestination (proc, targets);
        if (sel >= 0) dst.setSelectedItemIndex (sel, juce::dontSendNotification);
        dst.setTextWhenNothingSelected (labels.isEmpty() ? "No outputs found" : "Pick where to listen");
        state.setText ("Status: " + listen::status (proc), juce::dontSendNotification);
        state.setColour (juce::Label::textColourId, proc.listenError.isNotEmpty() ? juce::Colour (0xfff2c94c) : juce::Colour (0xffb8c4cc));
        outputsSeen = proc.numOutputs();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (juce::Colour (0xff2a353d));
        g.drawHorizontalLine (0, 10.0f, (float) getWidth() - 10.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 8);
        title.setBounds (r.removeFromTop (24));
        r.removeFromTop (4);
        auto row = [&] (juce::Label& l, juce::ComboBox& c)
        {
            auto line = r.removeFromTop (28);
            l.setBounds (line.removeFromLeft (110));
            line.removeFromLeft (8);
            c.setBounds (line);
            r.removeFromTop (6);
        };
        row (srcLabel, src);
        row (dstLabel, dst);
        state.setBounds (r.removeFromTop (22).withTrimmedLeft (118));
        note.setBounds (r.removeFromTop (40).withTrimmedLeft (118));
    }

private:
    juce::String targetAt (int i) const { return i >= 0 && i < targets.size() ? targets[i] : juce::String(); }
    void timerCallback() override { if (proc.numOutputs() != outputsSeen && ! src.isPopupActive() && ! dst.isPopupActive()) refresh(); }

    LiveAlignProcessor& proc;
    juce::Label title, srcLabel, dstLabel, state, note;
    juce::ComboBox src, dst;
    juce::StringArray targets;
    int outputsSeen = -1;
};

} // namespace la::ui
