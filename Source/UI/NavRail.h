// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Left navigation pane: the LiveAlign name, one entry per step with its own icon and status, and the venue,
// session, settings and full-screen controls at the bottom.
#include <functional>
#include "Theme.h"
#include "Logo.h"

namespace la::ui
{
enum class Page
{
    Home = 0,
    Mics, Speakers, Plan,                                                    // setup
    Response, Crossover, Level, Align, Acoustics, Room, Feedback,            // tuning, in order
    MonitorMic, Monitor,                                                     // monitoring
    Sheet, TuningReport, MonitorReport,                                      // reports
    Locker, Settings,
    Venue,                                                                   // setup: the venue's details (a tab of Venue)
    Coverage, Headroom, Listening                                            // tuning (see kTuningOrder)
};
constexpr int kNumPages = 22;
// The navigation pane, top to bottom. Negative entries are section headings. The mic locker is opened from its
// own button at the bottom. The pane is the one way between the steps: each shows its state, and the next step to do
// is marked.
constexpr int kNavOrder[] = { 0, -1, 18, 1, -2, 4, 5, 6, 19, 7, 8, 9, 20, 21, 10, -3, 11, 12, -4, 13, 14, 15 };
// The tuning steps in order: speaker response, crossover, level, aim and coverage, time alignment, room acoustics,
// Room EQ, gain and headroom, the listening check, the feedback test.
constexpr int kTuningOrder[] = { 4, 5, 6, 19, 7, 8, 9, 20, 21, 10 };
// Setup is two entries with tabs: Venue (Details, Speakers, Plan and points) and Mics (this venue's mics, the locker).
inline int navEntryOf (int page)
{
    if (page == (int) Page::Speakers || page == (int) Page::Plan) return (int) Page::Venue;
    if (page == (int) Page::Locker) return (int) Page::Mics;
    return page;
}
inline bool isVenueTab (int page) { return navEntryOf (page) == (int) Page::Venue; }
inline bool isMicsTab (int page) { return navEntryOf (page) == (int) Page::Mics; }
inline const char* navHeading (int h) { static const char* n[] = { "SETUP", "TUNING", "MONITORING", "REPORTS" }; return n[juce::jlimit (0, 3, -h - 1)]; }
inline int tuningStepNumber (int page)
{
    for (int i = 0; i < (int) (sizeof (kTuningOrder) / sizeof (kTuningOrder[0])); ++i) if (kTuningOrder[i] == page) return i + 1;
    return 0;
}
inline bool isTuningStep (int page) { return tuningStepNumber (page) > 0; }

enum class StepStatus { Ready, InProgress, Attention, NotStarted, Later, None };

inline const char* pageName (int i)
{
    static const char* names[] = { "Home", "Mics", "Speakers", "Venue plan", "Speaker response", "Crossover", "Level balance", "Time alignment",
                                   "Room acoustics", "Room EQ", "Feedback test", "Monitor mic", "Live monitoring", "Processor sheet",
                                   "Tuning report", "Monitoring report", "Mic locker", "Settings", "Venue",
                                   "Aim and coverage", "Gain and headroom", "Listening check" };
    return names[juce::jlimit (0, kNumPages - 1, i)];
}

// Icons drawn as strokes in a 24 x 24 box.
inline juce::Path pageIcon (int page)
{
    juce::Path p;
    switch ((Page) page)
    {
        case Page::Home:   // house
            p.startNewSubPath (3.0f, 11.0f); p.lineTo (12.0f, 3.5f); p.lineTo (21.0f, 11.0f);
            p.startNewSubPath (5.5f, 9.5f); p.lineTo (5.5f, 20.5f); p.lineTo (18.5f, 20.5f); p.lineTo (18.5f, 9.5f);
            p.startNewSubPath (10.0f, 20.5f); p.lineTo (10.0f, 14.5f); p.lineTo (14.0f, 14.5f); p.lineTo (14.0f, 20.5f);
            break;
        case Page::Mics:
        case Page::MonitorMic:   // measurement mic on a stand
            p.addRoundedRectangle (9.0f, 2.5f, 6.0f, 11.0f, 3.0f);
            p.startNewSubPath (6.0f, 10.0f);
            p.quadraticTo (6.0f, 17.0f, 12.0f, 17.0f);
            p.quadraticTo (18.0f, 17.0f, 18.0f, 10.0f);
            p.startNewSubPath (12.0f, 17.0f); p.lineTo (12.0f, 21.0f);
            p.startNewSubPath (8.0f, 21.0f); p.lineTo (16.0f, 21.0f);
            break;
        case Page::Speakers:   // cabinet with woofer and tweeter
            p.addRoundedRectangle (5.0f, 2.5f, 14.0f, 19.0f, 2.0f);
            p.addEllipse (8.0f, 11.0f, 8.0f, 8.0f);
            p.addEllipse (10.5f, 5.0f, 3.0f, 3.0f);
            break;
        case Page::Venue:   // a hall with a pitched roof
            p.startNewSubPath (3.0f, 10.0f); p.lineTo (12.0f, 3.5f); p.lineTo (21.0f, 10.0f);
            p.addRectangle (5.0f, 10.0f, 14.0f, 10.5f);
            p.startNewSubPath (10.5f, 20.5f); p.lineTo (10.5f, 15.0f); p.lineTo (13.5f, 15.0f); p.lineTo (13.5f, 20.5f);
            break;
        case Page::Plan:   // room outline with a speaker and a mic
            p.addRoundedRectangle (3.0f, 3.5f, 18.0f, 17.0f, 2.0f);
            p.addRectangle (7.0f, 6.5f, 4.0f, 3.0f);
            p.addRectangle (13.0f, 6.5f, 4.0f, 3.0f);
            p.addEllipse (10.0f, 14.0f, 4.0f, 4.0f);
            break;
        case Page::Response:   // a speaker and its response curve
            p.addRoundedRectangle (2.5f, 6.0f, 7.0f, 12.0f, 1.5f);
            p.addEllipse (4.0f, 11.0f, 4.0f, 4.0f);
            p.startNewSubPath (12.0f, 15.0f); p.quadraticTo (14.0f, 9.0f, 16.5f, 10.5f); p.quadraticTo (19.0f, 12.0f, 21.5f, 8.0f);
            break;
        case Page::Crossover:   // two slopes crossing
            p.startNewSubPath (2.5f, 7.0f); p.lineTo (9.0f, 7.0f); p.quadraticTo (14.0f, 7.0f, 19.0f, 19.0f);
            p.startNewSubPath (21.5f, 7.0f); p.lineTo (15.0f, 7.0f); p.quadraticTo (10.0f, 7.0f, 5.0f, 19.0f);
            break;
        case Page::Align:   // two arrivals meeting on a line
            p.startNewSubPath (12.0f, 3.0f); p.lineTo (12.0f, 21.0f);
            p.startNewSubPath (2.5f, 12.0f); p.lineTo (5.5f, 12.0f); p.lineTo (7.5f, 6.0f); p.lineTo (9.5f, 17.0f); p.lineTo (10.5f, 12.0f);
            p.startNewSubPath (21.5f, 12.0f); p.lineTo (18.5f, 12.0f); p.lineTo (16.5f, 6.0f); p.lineTo (14.5f, 17.0f); p.lineTo (13.5f, 12.0f);
            break;
        case Page::Room:   // EQ faders
            p.startNewSubPath (6.0f, 3.0f);  p.lineTo (6.0f, 21.0f);
            p.startNewSubPath (12.0f, 3.0f); p.lineTo (12.0f, 21.0f);
            p.startNewSubPath (18.0f, 3.0f); p.lineTo (18.0f, 21.0f);
            p.addRoundedRectangle (3.5f, 13.0f, 5.0f, 3.5f, 1.2f);
            p.addRoundedRectangle (9.5f, 6.0f, 5.0f, 3.5f, 1.2f);
            p.addRoundedRectangle (15.5f, 10.0f, 5.0f, 3.5f, 1.2f);
            break;
        case Page::Coverage:   // a speaker's coverage fan
            p.addRoundedRectangle (2.5f, 9.0f, 5.0f, 6.0f, 1.0f);
            p.startNewSubPath (7.5f, 10.5f); p.lineTo (20.5f, 4.0f);
            p.startNewSubPath (7.5f, 13.5f); p.lineTo (20.5f, 20.0f);
            p.startNewSubPath (20.5f, 4.0f); p.quadraticTo (23.0f, 12.0f, 20.5f, 20.0f);
            break;
        case Page::Headroom:   // a meter with a ceiling
            p.startNewSubPath (3.0f, 5.0f); p.lineTo (21.0f, 5.0f);
            p.addRectangle (5.0f, 11.0f, 3.5f, 9.5f);
            p.addRectangle (10.25f, 8.5f, 3.5f, 12.0f);
            p.addRectangle (15.5f, 13.5f, 3.5f, 7.0f);
            break;
        case Page::Listening:   // headphones
            p.startNewSubPath (4.5f, 15.0f); p.lineTo (4.5f, 12.0f);
            p.quadraticTo (4.5f, 4.0f, 12.0f, 4.0f);
            p.quadraticTo (19.5f, 4.0f, 19.5f, 12.0f);
            p.lineTo (19.5f, 15.0f);
            p.addRoundedRectangle (3.0f, 13.0f, 4.5f, 7.5f, 1.5f);
            p.addRoundedRectangle (16.5f, 13.0f, 4.5f, 7.5f, 1.5f);
            break;
        case Page::Feedback:   // a narrow ringing peak
            p.startNewSubPath (2.5f, 19.0f); p.lineTo (9.0f, 18.0f); p.lineTo (11.0f, 4.0f); p.lineTo (13.0f, 18.0f); p.lineTo (21.5f, 19.0f);
            p.startNewSubPath (15.5f, 6.0f); p.quadraticTo (17.5f, 8.0f, 15.5f, 10.0f);
            p.startNewSubPath (18.0f, 4.0f); p.quadraticTo (21.0f, 8.0f, 18.0f, 12.0f);
            break;
        case Page::Acoustics:   // a decaying impulse
            p.startNewSubPath (2.5f, 20.5f); p.lineTo (21.5f, 20.5f);
            p.startNewSubPath (4.0f, 20.5f); p.lineTo (4.0f, 3.5f);
            p.startNewSubPath (7.5f, 20.5f); p.lineTo (7.5f, 9.0f);
            p.startNewSubPath (11.0f, 20.5f); p.lineTo (11.0f, 13.0f);
            p.startNewSubPath (14.5f, 20.5f); p.lineTo (14.5f, 16.0f);
            p.startNewSubPath (18.0f, 20.5f); p.lineTo (18.0f, 18.0f);
            break;
        case Page::Level:   // three level bars, the middle one trimmed
            p.startNewSubPath (5.0f, 20.5f); p.lineTo (5.0f, 8.0f);
            p.startNewSubPath (12.0f, 20.5f); p.lineTo (12.0f, 11.0f);
            p.startNewSubPath (19.0f, 20.5f); p.lineTo (19.0f, 8.0f);
            p.startNewSubPath (2.5f, 8.0f); p.lineTo (21.5f, 8.0f);
            p.startNewSubPath (9.5f, 4.0f); p.lineTo (12.0f, 6.5f); p.lineTo (14.5f, 4.0f);
            break;
        case Page::Sheet:   // a sheet with ticks
            p.addRoundedRectangle (4.0f, 2.5f, 16.0f, 19.0f, 1.5f);
            p.startNewSubPath (7.0f, 8.0f); p.lineTo (8.5f, 9.5f); p.lineTo (11.0f, 6.5f);
            p.startNewSubPath (13.0f, 8.0f); p.lineTo (17.0f, 8.0f);
            p.startNewSubPath (7.0f, 14.0f); p.lineTo (8.5f, 15.5f); p.lineTo (11.0f, 12.5f);
            p.startNewSubPath (13.0f, 14.0f); p.lineTo (17.0f, 14.0f);
            break;
        case Page::TuningReport:
        case Page::MonitorReport:   // a page with lines
            p.startNewSubPath (14.0f, 2.5f); p.lineTo (5.0f, 2.5f); p.lineTo (5.0f, 21.5f); p.lineTo (19.0f, 21.5f); p.lineTo (19.0f, 7.5f); p.lineTo (14.0f, 2.5f); p.lineTo (14.0f, 7.5f); p.lineTo (19.0f, 7.5f);
            p.startNewSubPath (8.0f, 12.0f); p.lineTo (16.0f, 12.0f);
            p.startNewSubPath (8.0f, 15.5f); p.lineTo (16.0f, 15.5f);
            p.startNewSubPath (8.0f, 19.0f); p.lineTo (13.0f, 19.0f);
            break;
        case Page::Locker:   // a box
            p.addRoundedRectangle (3.0f, 7.0f, 18.0f, 13.5f, 1.5f);
            p.startNewSubPath (3.0f, 11.0f); p.lineTo (21.0f, 11.0f);
            p.startNewSubPath (10.0f, 14.0f); p.lineTo (14.0f, 14.0f);
            p.startNewSubPath (8.0f, 7.0f); p.lineTo (8.0f, 4.0f); p.lineTo (16.0f, 4.0f); p.lineTo (16.0f, 7.0f);
            break;
        case Page::Monitor:
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
    std::function<void()> onSettings, onLocker, onVenue, onUpdate;

    NavRail()
    {
        settingsButton.onClick = [this] { if (onSettings) onSettings(); };
        lockerButton.onClick = [this] { if (onLocker) onLocker(); };
        settingsButton.setTooltip ("Audio, measurement, display, units, tuning defaults, generator safety, monitoring, reports and updates");
        lockerButton.setTooltip ("The measurement mics you own: model, serial number, calibration files and sound level");
        addAndMakeVisible (settingsButton);
        addAndMakeVisible (lockerButton);
        updateButton.onClick = [this] { if (onUpdate) onUpdate(); };
        addChildComponent (updateButton);
    }

    // Where an entry is (for the tour): in this component's coordinates.
    juce::Rectangle<int> entryBounds (int page) const { return itemBounds (navEntryOf (page)); }
    juce::Rectangle<int> venueArea() const { return venueBounds(); }
    juce::Rectangle<int> settingsArea() const { return settingsButton.getBounds(); }

    void setCurrent (int page) { page = navEntryOf (page); if (page != current) { current = page; scrollToShow (page); repaint(); } }
    void setPageVisible (int page, bool v) { if (visible[page] != v) { visible[page] = v; repaint(); } }
    void setLockerAvailable (bool a) { lockerButton.setVisible (a); resized(); }
    void setSettingsActive (bool a)
    {
        settingsButton.setColour (juce::TextButton::buttonColourId, a ? pal().accent.withAlpha (0.35f) : pal().panelRaised);
    }
    void setLockerActive (bool a)
    {
        lockerButton.setColour (juce::TextButton::buttonColourId, a ? pal().accent.withAlpha (0.35f) : pal().panelRaised);
    }
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

    void setStatus (int page, StepStatus s, const juce::String& text, float fraction = 0.0f)
    {
        auto& e = entries[page];
        if (e.status == s && e.text == text && std::abs (e.fraction - fraction) < 0.01f) return;
        e.status = s; e.text = text; e.fraction = fraction;
        repaint();
    }

    // The step to do next: marked in the list ("NEXT" and an accent edge), with what to do there as its tooltip.
    void setNext (int page, const juce::String& what)
    {
        page = page >= 0 ? navEntryOf (page) : page;
        if (page == nextPage && what == nextWhat) return;
        nextPage = page;
        nextWhat = what;
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

        // logo and name
        drawLogo (g, juce::Rectangle<float> (12.0f, 10.0f, 30.0f, 30.0f), true);
        g.setColour (p.text);
        g.setFont (font (15.5f, true));
        g.drawText ("LIVEALIGN", juce::Rectangle<int> (50, 11, getWidth() - 56, 18), juce::Justification::centredLeft);
        g.setColour (p.textFaint);
        g.setFont (font (10.5f));
        g.drawText ("v" + juce::String (JucePlugin_VersionString), juce::Rectangle<int> (51, 28, getWidth() - 56, 13), juce::Justification::centredLeft);

        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (listArea());

            // section headings
            for (int k : kNavOrder)
            {
                if (k >= 0 || ! headingVisible (k)) continue;
                const int y = headingY (k);
                g.setColour (p.textFaint);
                g.setFont (font (10.0f, true));
                g.drawText (navHeading (k), juce::Rectangle<int> (16, y + 4, getWidth() - 28, 16), juce::Justification::centredLeft);
                g.setColour (p.border);
                g.drawHorizontalLine (y + 12, 16.0f + (float) juce::GlyphArrangement::getStringWidthInt (font (10.0f, true), navHeading (k)) + 8.0f,
                                      (float) getWidth() - 14.0f);
            }

            for (int i = 0; i < kNumPages; ++i)
            {
                if (! visible[i] || ! inNav (i)) continue;
                const auto r = itemBounds (i);
                const bool active = i == current, over = i == hover;
                if (active || over)
                {
                    g.setColour (active ? p.panelRaised : p.panelRaised.withAlpha (0.5f));
                    g.fillRoundedRectangle (r.toFloat(), 6.0f);
                }
                if (active)
                {
                    g.setColour (p.accent);
                    g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 8.0f, 3.0f, (float) r.getHeight() - 16.0f, 1.5f);
                }
                if (i == nextPage && ! active)
                {
                    g.setColour (p.accent.withAlpha (0.8f));
                    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.3f);
                }

                auto iconBox = juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ (float) r.getX() + 20.0f, (float) r.getCentreY() });
                auto icon = pageIcon (i);
                icon.applyTransform (juce::AffineTransform::scale (0.85f, 0.85f, 12.0f, 12.0f).translated (iconBox.getX(), iconBox.getY()));
                g.setColour (active ? p.accent : p.textDim);
                g.strokePath (icon, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                const auto& e = entries[i];
                auto text = r.withTrimmedLeft (38).withTrimmedRight (e.status == StepStatus::None ? 6 : 24);
                if (i == nextPage)
                {
                    // "NEXT" before the status, and what to do there as the second line
                    g.setColour (p.accent);
                    g.setFont (font (9.0f, true));
                    g.drawText ("NEXT", text.withTrimmedTop (3).withHeight (12), juce::Justification::topRight);
                }
                juce::String name = pageName (i);
                if (isTuningStep (i)) name = juce::String (tuningStepNumber (i)) + "  " + name;
                g.setColour (active ? p.text : shade (p.text, p.dark ? -0.15f : 0.2f));
                g.setFont (font (12.5f, true));
                if (e.text.isNotEmpty())
                {
                    g.drawText (name, text.removeFromTop (r.getHeight() / 2 + 1).withTrimmedTop (2), juce::Justification::bottomLeft, true);
                    g.setColour (e.status == StepStatus::Attention ? p.warn : i == nextPage ? p.accent : p.textDim);
                    g.setFont (font (10.0f, i == nextPage));
                    g.drawText (i == nextPage && nextWhat.isNotEmpty() ? nextWhat : e.text, text, juce::Justification::topLeft, true);
                }
                else
                    g.drawText (name, text, juce::Justification::centredLeft, true);

                drawStatusBadge (g, juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ (float) r.getRight() - 12.0f, (float) r.getCentreY() }),
                                 e.status, e.fraction);
            }
        }
        // more below / above the visible part of the list
        const auto la = listArea();
        if (scroll > 0 || contentHeight() - scroll > la.getHeight())
        {
            g.setColour (p.textFaint);
            if (scroll > 0) g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb2")), la.withHeight (12).withTrimmedRight (8), juce::Justification::centredRight);
            if (contentHeight() - scroll > la.getHeight())
                g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xbc")), la.withTrimmedTop (la.getHeight() - 12).withTrimmedRight (8), juce::Justification::centredRight);
        }

        // venue and session at the very bottom
        auto v = venueBounds();
        if (v.contains (hoverPoint))
        {
            g.setColour (p.panelRaised.withAlpha (0.6f));
            g.fillRoundedRectangle (v.toFloat(), 7.0f);
        }
        g.setColour (p.border);
        g.drawHorizontalLine (v.getY() - 6, 10.0f, (float) getWidth() - 10.0f);
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
        // bottom, top to bottom: update, mic locker, settings, then the venue
        auto r = getLocalBounds().reduced (10, 0);
        r.removeFromBottom (kVenueH + 16);
        settingsButton.setBounds (r.removeFromBottom (30));
        r.removeFromBottom (6);
        if (lockerButton.isVisible()) { lockerButton.setBounds (r.removeFromBottom (30)); r.removeFromBottom (6); }
        if (updateButton.isVisible()) updateButton.setBounds (r.removeFromBottom (30));
        scroll = juce::jlimit (0, juce::jmax (0, contentHeight() - listArea().getHeight()), scroll);
    }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        const int before = scroll;
        scroll = juce::jlimit (0, juce::jmax (0, contentHeight() - listArea().getHeight()), scroll - juce::roundToInt (w.deltaY * 200.0f));
        if (scroll != before) repaint();
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        hoverPoint = e.getPosition();
        const int h = itemAt (e.getPosition());
        if (h != hover) hover = h;
        if (h >= 0 && h == nextPage && nextWhat.isNotEmpty()) setTooltip ("Next: " + nextWhat + (entries[h].text.isNotEmpty() ? " (" + entries[h].text + ")" : juce::String()));
        else if (h >= 0 && entries[h].text.isNotEmpty()) setTooltip (juce::String (pageName (h)) + ": " + entries[h].text);
        else setTooltip ({});
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
    static constexpr int kTop = 52, kItemH = 34, kItemGap = 2, kHeadingH = 22, kVenueH = 72;

    static bool inNav (int page) { for (int k : kNavOrder) if (k == page) return true; return false; }

    juce::Rectangle<int> listArea() const
    {
        const int bottom = (updateButton.isVisible() ? updateButton.getY() : lockerButton.isVisible() ? lockerButton.getY() : settingsButton.getY()) - 8;
        return { 0, kTop, getWidth() - 1, juce::jmax (0, bottom - kTop) };
    }

    int contentHeight() const
    {
        int y = 0;
        for (int k : kNavOrder)
        {
            if (k < 0) { if (headingVisible (k)) y += kHeadingH; }
            else if (visible[k]) y += kItemH + kItemGap;
        }
        return y + 4;
    }

    int headingY (int h) const
    {
        int y = kTop - scroll;
        for (int k : kNavOrder)
        {
            if (k == h) return y;
            if (k < 0) { if (headingVisible (k)) y += kHeadingH; }
            else if (visible[k]) y += kItemH + kItemGap;
        }
        return y;
    }

    juce::Rectangle<int> itemBounds (int i) const
    {
        int y = kTop - scroll;
        for (int k : kNavOrder)
        {
            if (k == i) return { 6, y, getWidth() - 12, kItemH };
            if (k < 0) { if (headingVisible (k)) y += kHeadingH; }
            else if (visible[k]) y += kItemH + kItemGap;
        }
        return {};
    }

    void scrollToShow (int page)
    {
        const auto r = itemBounds (page);
        const auto la = listArea();
        if (r.isEmpty() || la.isEmpty()) return;
        if (r.getY() < la.getY()) scroll -= la.getY() - r.getY();
        else if (r.getBottom() > la.getBottom()) scroll += r.getBottom() - la.getBottom();
        scroll = juce::jlimit (0, juce::jmax (0, contentHeight() - la.getHeight()), scroll);
    }

    juce::Rectangle<int> venueBounds() const { return { 6, getHeight() - kVenueH - 8, getWidth() - 12, kVenueH }; }

    int itemAt (juce::Point<int> pt) const
    {
        if (! listArea().contains (pt)) return -1;
        for (int i = 0; i < kNumPages; ++i)
            if (visible[i] && inNav (i) && itemBounds (i).contains (pt)) return i;
        return -1;
    }

    Entry entries[kNumPages];
    bool visible[kNumPages] { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, false, false, true, true, true, true };
    int nextPage = -1;
    juce::String nextWhat;

    // A heading shows when any page under it does.
    bool headingVisible (int h) const
    {
        bool under = false, seen = false;
        for (int k : kNavOrder)
        {
            if (k < 0) { if (seen) break; seen = k == h; continue; }
            if (seen && visible[k]) under = true;
        }
        return under;
    }
    int current = 0, hover = -1, scroll = 0;
    juce::Point<int> hoverPoint;
    juce::String venueName, sessionName;
    bool dirty = false;
    juce::TextButton settingsButton { "SETTINGS" }, lockerButton { "MIC LOCKER" }, updateButton;
public:
    static constexpr int kWidth = 196;
};
} // namespace la::ui
