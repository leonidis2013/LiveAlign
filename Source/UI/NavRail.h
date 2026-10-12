// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Left navigation pane: the LiveAlign name, one entry per step with its own icon and status, and the venue,
// session, settings and full-screen controls at the bottom.
#include <functional>
#include "Theme.h"

namespace la::ui
{
enum class Page { Home = 0, Mics = 1, Speakers = 2, Room = 3, Monitor = 4 };
constexpr int kNumPages = 5;

enum class StepStatus { Ready, InProgress, Attention, NotStarted, Later, None };

inline const char* pageName (int i)
{
    static const char* names[] = { "Home", "Mic setup", "Speakers", "Room tuning", "Monitoring" };
    return names[juce::jlimit (0, kNumPages - 1, i)];
}

// Icons drawn as strokes in a 24 x 24 box.
inline juce::Path pageIcon (int page)
{
    juce::Path p;
    switch (page)
    {
        case 0:   // home: house
            p.startNewSubPath (3.0f, 11.0f); p.lineTo (12.0f, 3.5f); p.lineTo (21.0f, 11.0f);
            p.startNewSubPath (5.5f, 9.5f); p.lineTo (5.5f, 20.5f); p.lineTo (18.5f, 20.5f); p.lineTo (18.5f, 9.5f);
            p.startNewSubPath (10.0f, 20.5f); p.lineTo (10.0f, 14.5f); p.lineTo (14.0f, 14.5f); p.lineTo (14.0f, 20.5f);
            break;
        case 1:   // mic setup: measurement mic on a stand
            p.addRoundedRectangle (9.0f, 2.5f, 6.0f, 11.0f, 3.0f);
            p.startNewSubPath (6.0f, 10.0f);
            p.quadraticTo (6.0f, 17.0f, 12.0f, 17.0f);
            p.quadraticTo (18.0f, 17.0f, 18.0f, 10.0f);
            p.startNewSubPath (12.0f, 17.0f); p.lineTo (12.0f, 21.0f);
            p.startNewSubPath (8.0f, 21.0f); p.lineTo (16.0f, 21.0f);
            break;
        case 2:   // speakers: cabinet with woofer and tweeter
            p.addRoundedRectangle (5.0f, 2.5f, 14.0f, 19.0f, 2.0f);
            p.addEllipse (8.0f, 11.0f, 8.0f, 8.0f);
            p.addEllipse (10.5f, 5.0f, 3.0f, 3.0f);
            break;
        case 3:   // room tuning: EQ faders
            p.startNewSubPath (6.0f, 3.0f);  p.lineTo (6.0f, 21.0f);
            p.startNewSubPath (12.0f, 3.0f); p.lineTo (12.0f, 21.0f);
            p.startNewSubPath (18.0f, 3.0f); p.lineTo (18.0f, 21.0f);
            p.addRoundedRectangle (3.5f, 13.0f, 5.0f, 3.5f, 1.2f);
            p.addRoundedRectangle (9.5f, 6.0f, 5.0f, 3.5f, 1.2f);
            p.addRoundedRectangle (15.5f, 10.0f, 5.0f, 3.5f, 1.2f);
            break;
        default:  // monitoring: graph with a trace
            p.startNewSubPath (3.0f, 3.0f); p.lineTo (3.0f, 21.0f); p.lineTo (21.0f, 21.0f);
            p.startNewSubPath (5.5f, 16.0f);
            p.lineTo (9.0f, 10.0f); p.lineTo (12.5f, 14.0f); p.lineTo (16.0f, 7.0f); p.lineTo (20.0f, 9.5f);
            break;
    }
    return p;
}

inline void drawStatusBadge (juce::Graphics& g, juce::Rectangle<float> b, StepStatus s, float fraction = 0.0f)
{
    const auto& p = pal();
    switch (s)
    {
        case StepStatus::Ready:
        {
            g.setColour (p.good);
            g.fillEllipse (b);
            juce::Path tick;
            tick.startNewSubPath (b.getX() + b.getWidth() * 0.27f, b.getCentreY());
            tick.lineTo (b.getX() + b.getWidth() * 0.44f, b.getY() + b.getHeight() * 0.68f);
            tick.lineTo (b.getRight() - b.getWidth() * 0.25f, b.getY() + b.getHeight() * 0.32f);
            g.setColour (p.onAccent);
            g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case StepStatus::Attention:
            g.setColour (p.warn);
            g.fillEllipse (b);
            g.setColour (p.onAccent);
            g.setFont (font (b.getHeight() * 0.75f, true));
            g.drawText ("!", b, juce::Justification::centred);
            break;
        case StepStatus::InProgress:
        {
            g.setColour (p.border);
            g.drawEllipse (b.reduced (1.0f), 2.0f);
            juce::Path arc;
            arc.addCentredArc (b.getCentreX(), b.getCentreY(), b.getWidth() * 0.5f - 1.0f, b.getHeight() * 0.5f - 1.0f, 0.0f,
                               0.0f, juce::MathConstants<float>::twoPi * juce::jlimit (0.05f, 1.0f, fraction), true);
            g.setColour (p.accent);
            g.strokePath (arc, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            break;
        }
        case StepStatus::NotStarted:
            g.setColour (p.textDim);
            g.drawEllipse (b.reduced (1.0f), 1.4f);
            break;
        case StepStatus::Later:
            g.setColour (p.textFaint);
            g.drawEllipse (b.reduced (1.0f), 1.2f);
            g.drawLine (b.getX() + 5.0f, b.getCentreY(), b.getRight() - 5.0f, b.getCentreY(), 1.4f);
            break;
        case StepStatus::None:
        default:
            break;
    }
}

class NavRail : public juce::Component, public juce::SettableTooltipClient
{
public:
    std::function<void (int)> onSelect;
    std::function<void()> onSettings, onFullScreen, onVenue, onUpdate;

    NavRail()
    {
        settingsButton.onClick = [this] { if (onSettings) onSettings(); };
        fullScreenButton.onClick = [this] { if (onFullScreen) onFullScreen(); };
        settingsButton.setTooltip ("Inputs, analysis, screen layout, units and theme");
        fullScreenButton.setTooltip ("Fill the whole screen (key: F, Esc to leave)");
        addAndMakeVisible (settingsButton);
        addChildComponent (fullScreenButton);
        updateButton.onClick = [this] { if (onUpdate) onUpdate(); };
        addChildComponent (updateButton);
    }

    void setCurrent (int page) { if (page != current) { current = page; repaint(); } }
    void setPageVisible (int page, bool v) { if (visible[page] != v) { visible[page] = v; repaint(); } }
    void setFullScreenAvailable (bool a) { fullScreenButton.setVisible (a); resized(); }
    // Shown above the venue when a newer version is out.
    void setUpdate (const juce::String& version)
    {
        updateButton.setVisible (version.isNotEmpty());
        updateButton.setButtonText ("UPDATE: " + version);
        updateButton.setTooltip ("LiveAlign " + version + " is available. Click for what's new and the download.");
        updateButton.setColour (juce::TextButton::buttonColourId, pal().accent);
        updateButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
        resized();
        repaint();
    }
    void setFullScreenText (const juce::String& t) { fullScreenButton.setButtonText (t); }

    void setStatus (int page, StepStatus s, const juce::String& text, float fraction = 0.0f)
    {
        auto& e = entries[page];
        if (e.status == s && e.text == text && std::abs (e.fraction - fraction) < 0.01f) return;
        e.status = s; e.text = text; e.fraction = fraction;
        repaint();
    }

    void setVenue (const juce::String& venue, const juce::String& session, bool unsaved)
    {
        if (venue == venueName && session == sessionName && unsaved == dirty) return;
        venueName = venue; sessionName = session; dirty = unsaved;
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.setColour (p.panel);
        g.fillRect (getLocalBounds());
        g.setColour (p.border);
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());

        // name, as in the other Live plug-ins
        g.setColour (p.text);
        g.setFont (font (16.0f, true));
        g.drawText ("LIVEALIGN", juce::Rectangle<int> (14, 12, getWidth() - 20, 22), juce::Justification::centredLeft);
        g.setColour (p.textFaint);
        g.setFont (font (10.5f));
        g.drawText ("v" + juce::String (JucePlugin_VersionString), juce::Rectangle<int> (15, 32, getWidth() - 20, 14), juce::Justification::centredLeft);

        for (int i = 0; i < kNumPages; ++i)
        {
            if (! visible[i]) continue;
            const auto r = itemBounds (i);
            const bool active = i == current, over = i == hover;
            if (active || over)
            {
                g.setColour (active ? p.panelRaised : p.panelRaised.withAlpha (0.5f));
                g.fillRoundedRectangle (r.toFloat(), 7.0f);
            }
            if (active)
            {
                g.setColour (p.accent);
                g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 10.0f, 3.0f, (float) r.getHeight() - 20.0f, 1.5f);
            }

            auto iconBox = juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ (float) r.getX() + 21.0f, (float) r.getCentreY() });
            auto icon = pageIcon (i);
            icon.applyTransform (juce::AffineTransform::translation (iconBox.getX(), iconBox.getY()));
            g.setColour (active ? p.accent : p.textDim);
            g.strokePath (icon, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const auto& e = entries[i];
            auto text = r.withTrimmedLeft (40).withTrimmedRight (26);
            g.setColour (active ? p.text : shade (p.text, p.dark ? -0.15f : 0.2f));
            g.setFont (font (13.0f, true));
            if (e.text.isNotEmpty())
            {
                g.drawText (pageName (i), text.removeFromTop (r.getHeight() / 2 + 1).withTrimmedTop (3), juce::Justification::bottomLeft, true);
                g.setColour (e.status == StepStatus::Attention ? p.warn : p.textDim);
                g.setFont (font (10.5f));
                g.drawText (e.text, text.withTrimmedTop (1), juce::Justification::topLeft, true);
            }
            else
                g.drawText (pageName (i), text, juce::Justification::centredLeft, true);

            drawStatusBadge (g, juce::Rectangle<float> (15.0f, 15.0f).withCentre ({ (float) r.getRight() - 13.0f, (float) r.getCentreY() }),
                             e.status, e.fraction);
        }

        // venue and session at the bottom
        auto v = venueBounds();
        if (v.contains (hoverPoint))
        {
            g.setColour (p.panelRaised.withAlpha (0.6f));
            g.fillRoundedRectangle (v.toFloat(), 7.0f);
        }
        v = v.reduced (10, 6);
        g.setColour (p.textFaint);
        g.setFont (font (10.0f, true));
        g.drawText ("VENUE", v.removeFromTop (13), juce::Justification::centredLeft);
        g.setColour (p.text);
        g.setFont (font (13.0f, true));
        g.drawText (venueName, v.removeFromTop (18), juce::Justification::centredLeft, true);
        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        g.drawText (sessionName, v.removeFromTop (16), juce::Justification::centredLeft, true);
        g.setColour (dirty ? p.warn : p.textFaint);
        g.setFont (font (10.5f));
        g.drawText (dirty ? "Unsaved changes" : "Saved", v.removeFromTop (14), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 10);
        auto bottom = r.removeFromBottom (fullScreenButton.isVisible() ? 64 : 30);
        settingsButton.setBounds (bottom.removeFromBottom (30));
        if (fullScreenButton.isVisible())
        {
            bottom.removeFromBottom (4);
            fullScreenButton.setBounds (bottom.removeFromBottom (30));
        }
        updateButton.setBounds (settingsButton.getX(), settingsButton.getY() - (fullScreenButton.isVisible() ? 34 : 0) - 34,
                                settingsButton.getWidth(), 30);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverPoint = e.getPosition();
        const int h = itemAt (e.getPosition());
        if (h != hover) hover = h;
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; hoverPoint = {}; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = itemAt (e.getPosition());
        if (i >= 0 && onSelect) onSelect (i);
        else if (venueBounds().contains (e.getPosition()) && onVenue) onVenue();
    }

private:
    struct Entry { StepStatus status = StepStatus::None; juce::String text; float fraction = 0.0f; };

    juce::Rectangle<int> itemBounds (int i) const
    {
        int slot = 0;
        for (int k = 0; k < i; ++k) slot += visible[k] ? 1 : 0;
        return { 6, 58 + slot * 48, getWidth() - 12, 44 };
    }

    juce::Rectangle<int> venueBounds() const
    {
        const int top = settingsButton.getY() - (fullScreenButton.isVisible() ? 34 : 0) - (updateButton.isVisible() ? 34 : 0) - 78;
        return { 6, top, getWidth() - 12, 72 };
    }

    int itemAt (juce::Point<int> pt) const
    {
        for (int i = 0; i < kNumPages; ++i)
            if (visible[i] && itemBounds (i).contains (pt)) return i;
        return -1;
    }

    Entry entries[kNumPages];
    bool visible[kNumPages] { true, true, true, true, true };
    int current = 0, hover = -1;
    juce::Point<int> hoverPoint;
    juce::String venueName, sessionName;
    bool dirty = false;
    juce::TextButton settingsButton { "SETTINGS" }, fullScreenButton { "FULL SCREEN" }, updateButton;
public:
    static constexpr int kWidth = 184;
};
} // namespace la::ui
