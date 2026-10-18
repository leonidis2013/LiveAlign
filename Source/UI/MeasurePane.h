#pragma once
// One graph area of Live monitoring. Up to three of these share the screen; each has its own
// RTA / Transfer / Impulse / Waterfall / Level selector, so any combination can be shown at once.
#include "Views.h"
#include "LevelView.h"
#include "../UpdateChecker.h"   // UserPrefs

namespace la::ui
{
class MeasurePane : public juce::Component
{
public:
    MeasurePane (Settings& s, TraceStore& t, const SplLog& log, int paneIndex)
        : settings (s), index (paneIndex), transfer (s, t), rta (s, t), impulse (s), waterfall (s), level (log)
    {
        addChildComponent (transfer);
        addChildComponent (rta);
        addChildComponent (impulse);
        addChildComponent (waterfall);
        addChildComponent (level);

        const char* names[] = { "RTA", "Transfer", "Impulse", "Waterfall", "Level" };
        for (int i = 0; i < kViews; ++i)
        {
            tabs[i].setButtonText (names[i]);
            tabs[i].setClickingTogglesState (true);
            tabs[i].setRadioGroupId (400 + index);
            tabs[i].onClick = [this, i] { if (tabs[i].getToggleState()) { settings.paneViewRef (index) = i; sync(); } };
            addAndMakeVisible (tabs[i]);
        }
        if (index == 0)
        {
            tabs[0].setTooltip ("Real-time analyser of the mic (key: 1)");
            tabs[1].setTooltip ("Transfer function: mic against reference (key: 2)");
            tabs[2].setTooltip ("Impulse response: arrival, reflections, polarity (key: 3)");
            tabs[3].setTooltip ("Waterfall: the spectrum over time, to spot feedback building and room ringing (key: 4)");
            tabs[4].setTooltip ("Level: the sound level over time, against the limit, with the parts of the event (key: 5)");
        }

        // what the RTA and the waterfall show: the mic, the console feed (the reference), or both (remembered per graph)
        sourceBox.addItemList ({ "Mic", "Console feed", "Mic + feed" }, 1);
        sourceBox.setSelectedItemIndex (juce::jlimit (0, 2, UserPrefs::get ("paneSource" + juce::String (index), "0").getIntValue()), juce::dontSendNotification);
        sourceBox.setTooltip ("What the RTA and the waterfall show: the mic, the console feed (the reference input), or both. "
                              "A fault in the feed but not the mic is before the interface (the console, a digital link); in both at once, the interface or USB.");
        sourceBox.onChange = [this]
        {
            UserPrefs::set ("paneSource" + juce::String (index), juce::String (sourceBox.getSelectedItemIndex()));
            applySource();
        };
        addChildComponent (sourceBox);
        applySource();

        centreButton.setTooltip ("Put the transfer function on the 0 dB line (or double-click the graph). Drag the graph to shift it.");
        centreButton.onClick = [this] { transfer.centreOnLive(); };
        addChildComponent (centreButton);
        sync();
    }

    // Follows the settings (keys, state recall).
    void sync()
    {
        const int v = juce::jlimit (0, kViews - 1, settings.paneViewRef (index).load());
        if (v == shown) return;
        shown = v;
        for (int i = 0; i < kViews; ++i) tabs[i].setToggleState (i == v, juce::dontSendNotification);
        rta.setVisible (v == 0);
        transfer.setVisible (v == 1);
        impulse.setVisible (v == 2);
        waterfall.setVisible (v == 3);
        level.setVisible (v == 4);
        centreButton.setVisible (v == 1);
        sourceBox.setVisible (v == 0 || v == 3);
        resized();
        if (last != nullptr) update (*last);
    }

    void update (const Snapshot& snap)
    {
        last = &snap;
        waterfall.update (snap);   // keeps its history even while another view is shown
        if (shown == 0) rta.update (snap);
        else if (shown == 1) transfer.update (snap);
        else if (shown == 2) impulse.update (snap);
        else if (shown == 4) level.update (snap);
    }

    // The RTA's and waterfall's source: 0 the mic, 1 the console feed, 2 both.
    void setSource (int src) { sourceBox.setSelectedItemIndex (juce::jlimit (0, 2, src), juce::sendNotificationSync); }
    int sourceShown() const { return sourceBox.getSelectedItemIndex(); }

    void setStatus (const juce::String& s) { if (s != status) { status = s; repaint (0, 0, getWidth(), 30); } }

    void paint (juce::Graphics& g) override
    {
        if (status.isEmpty()) return;
        auto head = getLocalBounds().removeFromTop (30).withTrimmedLeft (kViews * 80 + 16 + (sourceBox.isVisible() ? 140 : 0)).withTrimmedRight (centreButton.isVisible() ? 84 : 6);
        g.setColour (status.contains ("BEHIND") ? pal().warn : pal().textDim);
        g.setFont (monoFont (11.0f));
        g.drawText (status, head, juce::Justification::centredRight, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto head = r.removeFromTop (30);
        for (auto& t : tabs) { t.setBounds (head.removeFromLeft (78).withSizeKeepingCentre (78, 24)); head.removeFromLeft (2); }
        head.removeFromLeft (10);
        sourceBox.setBounds (head.removeFromLeft (128).withSizeKeepingCentre (128, 24));
        centreButton.setBounds (head.removeFromRight (70).withSizeKeepingCentre (70, 24));
        r.removeFromTop (4);
        transfer.setBounds (r);
        rta.setBounds (r);
        impulse.setBounds (r);
        waterfall.setBounds (r);
        level.setBounds (r);
    }

private:
    void applySource()
    {
        const int src = sourceBox.getSelectedItemIndex();
        rta.setSource (src);
        waterfall.setSource (src);
    }

    Settings& settings;
    const int index;
    int shown = -1;
    const Snapshot* last = nullptr;
    TransferView transfer;
    RtaView rta;
    ImpulseView impulse;
    WaterfallView waterfall;
    LevelView level;
    static constexpr int kViews = 5;
    juce::TextButton tabs[kViews], centreButton { "Centre" };
    juce::ComboBox sourceBox;
    juce::String status;
};
} // namespace la::ui
