#pragma once
// One graph area of the Measure screen. Up to three of these share the screen; each has its own
// RTA / Transfer / Impulse selector, so any combination can be shown at once.
#include "Views.h"

namespace la::ui
{
class MeasurePane : public juce::Component
{
public:
    MeasurePane (Settings& s, TraceStore& t, int paneIndex)
        : settings (s), index (paneIndex), transfer (s, t), rta (s, t), impulse (s), waterfall (s)
    {
        addChildComponent (transfer);
        addChildComponent (rta);
        addChildComponent (impulse);
        addChildComponent (waterfall);

        const char* names[] = { "RTA", "Transfer", "Impulse", "Waterfall" };
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
        }

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
        centreButton.setVisible (v == 1);
        if (last != nullptr) update (*last);
    }

    void update (const Snapshot& snap)
    {
        last = &snap;
        waterfall.update (snap);   // keeps its history even while another view is shown
        if (shown == 0) rta.update (snap);
        else if (shown == 1) transfer.update (snap);
        else if (shown == 2) impulse.update (snap);
    }

    void setStatus (const juce::String& s) { if (s != status) { status = s; repaint (0, 0, getWidth(), 30); } }

    void paint (juce::Graphics& g) override
    {
        if (status.isEmpty()) return;
        auto head = getLocalBounds().removeFromTop (30).withTrimmedLeft (kViews * 80 + 16).withTrimmedRight (centreButton.isVisible() ? 84 : 6);
        g.setColour (status.contains ("BEHIND") ? pal().warn : pal().textDim);
        g.setFont (monoFont (11.0f));
        g.drawText (status, head, juce::Justification::centredRight, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto head = r.removeFromTop (30);
        for (auto& t : tabs) { t.setBounds (head.removeFromLeft (78).withSizeKeepingCentre (78, 24)); head.removeFromLeft (2); }
        centreButton.setBounds (head.removeFromRight (70).withSizeKeepingCentre (70, 24));
        r.removeFromTop (4);
        transfer.setBounds (r);
        rta.setBounds (r);
        impulse.setBounds (r);
        waterfall.setBounds (r);
    }

private:
    Settings& settings;
    const int index;
    int shown = -1;
    const Snapshot* last = nullptr;
    TransferView transfer;
    RtaView rta;
    ImpulseView impulse;
    WaterfallView waterfall;
    static constexpr int kViews = 4;
    juce::TextButton tabs[kViews], centreButton { "Centre" };
    juce::String status;
};
} // namespace la::ui
