#pragma once
// Workflow tab bar and the placeholder pages for the tabs that come in later stages.
#include <functional>
#include "Theme.h"

namespace la::ui
{
struct TabInfo { const char* name; const char* tooltip; };

inline const TabInfo& tabInfo (int i)
{
    static const TabInfo tabs[] = {
        { "Mic setup", "Mic, inputs, calibration, loopback and room noise" },
        { "Speakers", "Measure each speaker or group, compare them, and get crossover settings" },
        { "Room tuning", "Measure across the room, compare with a target and tune" },
        { "Monitoring", "RTA, transfer function, impulse response and sound level during the event" } };
    return tabs[juce::jlimit (0, 3, i)];
}

class WorkflowTabs : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onSelect;

    void setCurrent (int i) { if (i != current) { current = i; repaint(); } }
    void setDone (int i, bool d) { if (done[i] != d) { done[i] = d; repaint(); } }
    void setProgress (int i, const juce::String& text) { if (progress[i] != text) { progress[i] = text; repaint(); } }
    void setTabVisible (int i, bool v) { if (visible[i] != v) { visible[i] = v; repaint(); } }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        for (int i = 0; i < 4; ++i)
        {
            if (! visible[i]) continue;
            const auto tab = tabBounds (i);
            const bool active = i == current;
            if (active || i == hover)
            {
                g.setColour (active ? p.panel : p.panelRaised.withAlpha (0.5f));
                g.fillRoundedRectangle (tab.toFloat().reduced (0.0f, 2.0f), 7.0f);
            }
            if (active)
            {
                g.setColour (p.accent);
                g.fillRoundedRectangle (juce::Rectangle<float> ((float) tab.getX() + 12.0f, (float) tab.getBottom() - 4.0f,
                                                                (float) tab.getWidth() - 24.0f, 2.5f), 1.0f);
            }

            auto r = tab.reduced (10, 0);
            auto badge = r.removeFromLeft (20).toFloat().withSizeKeepingCentre (18.0f, 18.0f);
            if (done[i])
            {
                g.setColour (p.good);
                g.fillEllipse (badge);
                juce::Path tick;
                tick.startNewSubPath (badge.getX() + 4.5f, badge.getCentreY());
                tick.lineTo (badge.getX() + 7.8f, badge.getCentreY() + 3.4f);
                tick.lineTo (badge.getRight() - 4.2f, badge.getCentreY() - 3.6f);
                g.setColour (p.onAccent);
                g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                g.setColour (active ? p.accent : p.border);
                g.drawEllipse (badge.reduced (0.5f), 1.2f);
                g.setColour (active ? p.accent : p.textDim);
                g.setFont (monoFont (10.5f, true));
                g.drawText (juce::String (i + 1), badge, juce::Justification::centred);
            }
            r.removeFromLeft (8);
            g.setColour (active ? p.text : p.textDim);
            g.setFont (font (13.5f, true));
            auto nameArea = r;
            if (progress[i].isNotEmpty())
            {
                g.drawText (tabInfo (i).name, nameArea.removeFromTop (r.getHeight() / 2 + 3).withTrimmedTop (4), juce::Justification::bottomLeft);
                g.setColour (p.textDim);
                g.setFont (font (10.5f));
                g.drawText (progress[i], nameArea, juce::Justification::topLeft);
            }
            else
                g.drawText (tabInfo (i).name, nameArea, juce::Justification::centredLeft);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = tabAt (e.getPosition());
        if (h != hover) { hover = h; setTooltip (h >= 0 ? juce::String (tabInfo (h).tooltip) : juce::String()); repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int t = tabAt (e.getPosition());
        if (t >= 0 && onSelect) onSelect (t);
    }

    int preferredWidth() const
    {
        int n = 0;
        for (bool v : visible) n += v ? 1 : 0;
        return n * kTabW;
    }

private:
    static constexpr int kTabW = 150;

    juce::Rectangle<int> tabBounds (int i) const
    {
        int slot = 0;
        for (int k = 0; k < i; ++k) slot += visible[k] ? 1 : 0;
        return { slot * kTabW, 0, kTabW - 4, getHeight() };
    }

    int tabAt (juce::Point<int> pt) const
    {
        for (int i = 0; i < 4; ++i)
            if (visible[i] && tabBounds (i).contains (pt)) return i;
        return -1;
    }

    int current = 0, hover = -1;
    bool done[4] {}, visible[4] { true, true, true, true };
    juce::String progress[4];
};

//==============================================================================
// Tabs 2 and 3 until their stage is built: what the step will do, with Back / Next.
class PlaceholderPage : public juce::Component
{
public:
    std::function<void()> onBack, onNext;

    PlaceholderPage (int tabIndex, juce::String introText, juce::StringArray bulletPoints)
        : tab (tabIndex), intro (std::move (introText)), bullets (std::move (bulletPoints))
    {
        backButton.setButtonText (juce::String ("Back: ") + tabInfo (tab - 1).name);
        nextButton.setButtonText (juce::String ("Next: ") + tabInfo (tab + 1).name);
        backButton.onClick = [this] { if (onBack) onBack(); };
        nextButton.onClick = [this] { if (onNext) onNext(); };
        addAndMakeVisible (backButton);
        addAndMakeVisible (nextButton);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto card = getLocalBounds().withTrimmedBottom (48).withSizeKeepingCentre (juce::jmin (760, getWidth()), juce::jmin (440, getHeight() - 48));
        g.setColour (p.panel);
        g.fillRoundedRectangle (card.toFloat(), 12.0f);
        g.setColour (p.border);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 12.0f, 1.0f);

        auto r = card.reduced (36, 30);
        g.setColour (p.accent);
        g.setFont (font (11.5f, true));
        g.drawText ("STEP " + juce::String (tab + 1) + "   COMING IN THE NEXT STAGE", r.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (p.text);
        g.setFont (font (24.0f, true));
        g.drawText (tabInfo (tab).name, r.removeFromTop (40), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (13.5f));
        g.drawFittedText (intro, r.removeFromTop (48), juce::Justification::topLeft, 3);
        r.removeFromTop (8);
        for (const auto& b : bullets)
        {
            auto line = r.removeFromTop (30);
            g.setColour (p.accent);
            g.fillEllipse (line.removeFromLeft (18).toFloat().withSizeKeepingCentre (6.0f, 6.0f));
            g.setColour (p.text);
            g.setFont (font (13.5f));
            g.drawText (b, line, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto bottom = getLocalBounds().removeFromBottom (36);
        backButton.setBounds (bottom.removeFromLeft (200));
        nextButton.setBounds (bottom.removeFromRight (200));
    }

private:
    int tab;
    juce::String intro;
    juce::StringArray bullets;
    juce::TextButton backButton, nextButton;
};
} // namespace la::ui
