// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Left navigation pane: the LiveAlign name, one entry per step with its own icon and status, and the venue,
// session, settings and full-screen controls at the bottom.
#include <functional>
#include <vector>
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
// The navigation pane: Home, then groups that open to their pages. Venue (Details, Speakers, Plan and points), Mics
// (this venue's mics, the locker), Tuning (the steps in order), Monitoring and Reports. The group that holds the open
// page is open; the others show one line with their state. The pane is the one way between the steps.
constexpr int kNavOrder[] = { 0, -1, 18, 2, 3, 1, 16, -2, 4, 20, 5, 6, 19, 7, 8, 9, 21, 10, -3, 11, 12, -4, 13, 14, 15 };
// The tuning steps in order: speaker response, gain and headroom, crossover, level, aim and coverage, time alignment,
// room acoustics, Room EQ, the listening check, the feedback test.
constexpr int kTuningOrder[] = { 4, 20, 5, 6, 19, 7, 8, 9, 21, 10 };
struct NavGroup { const char* name; int icon; std::vector<int> pages; };
inline const std::vector<NavGroup>& navGroups()
{
    static const std::vector<NavGroup> g {
        { "Venue", 18, { 18, 2, 3 } },
        { "Mics", 1, { 1, 16 } },
        { "Tuning", 4, { 4, 20, 5, 6, 19, 7, 8, 9, 21, 10 } },
        { "Monitoring", 12, { 11, 12 } },
        { "Reports", 14, { 13, 14, 15 } } };
    return g;
}
inline int navGroupOf (int page)
{
    const auto& gs = navGroups();
    for (int i = 0; i < (int) gs.size(); ++i) for (int pg : gs[(size_t) i].pages) if (pg == page) return i;
    return -1;
}
inline int navEntryOf (int page) { return page; }   // kept for callers: every page is its own entry now
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
    static const char* names[] = { "Home", "This venue's mics", "Speakers", "Plan and points", "Speaker response", "Crossover", "Level balance", "Time alignment",
                                   "Room acoustics", "Room EQ", "Feedback test", "Monitor mic", "Live monitoring", "Processor sheet",
                                   "Tuning report", "Monitoring report", "Mic locker", "Settings", "Venue details",
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
        settingsButton.setTooltip ("Audio, measurement, display, units, tuning defaults, generator safety, monitoring, reports and updates");
        addAndMakeVisible (settingsButton);
        updateButton.onClick = [this] { if (onUpdate) onUpdate(); };
        addChildComponent (updateButton);
        rebuildRows();
    }

    // Where an entry is (for the tour): in this component's coordinates. A page in a closed group gives its group's row.
    juce::Rectangle<int> entryBounds (int page) const
    {
        for (const auto& r : rows) if (r.page == page) return rowBounds (r);
        const int g = navGroupOf (page);
        for (const auto& r : rows) if (r.group == g && r.page < 0) return rowBounds (r);
        return {};
    }
    juce::Rectangle<int> venueArea() const { return venueBounds(); }
    juce::Rectangle<int> settingsArea() const { return settingsButton.getBounds(); }

    void setCurrent (int page)
    {
        if (page == current) return;
        current = page;
        const int g = navGroupOf (page);
        if (g >= 0) openGroup = g;
        rebuildRows();
        scrollToShow (page);
        repaint();
    }
    bool pageVisible (int page) const { return page >= 0 && page < kNumPages && visible[page]; }
    void setPageVisible (int page, bool v) { if (visible[page] != v) { visible[page] = v; rebuildRows(); repaint(); } }
    void setLockerAvailable (bool) {}
    void setSettingsActive (bool a)
    {
        settingsButton.setColour (juce::TextButton::buttonColourId, a ? pal().accent.withAlpha (0.35f) : pal().panelRaised);
    }
    void setLockerActive (bool) {}
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
            for (int i = 0; i < (int) rows.size(); ++i)
            {
                const auto& row = rows[(size_t) i];
                const auto r = rowBounds (row);
                if (r.getBottom() < listArea().getY() || r.getY() > listArea().getBottom()) continue;
                if (row.page >= 0 && row.group < 0) drawTopRow (g, row, r, i == hover);
                else if (row.page < 0) drawGroupRow (g, row, r, i == hover);
                else drawItemRow (g, row, r, i == hover);
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
        // bottom, top to bottom: update, settings, then the venue
        auto r = getLocalBounds().reduced (10, 0);
        r.removeFromBottom (kVenueH + 16);
        settingsButton.setBounds (r.removeFromBottom (30));
        r.removeFromBottom (6);
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
        const int h = rowAt (e.getPosition());
        hover = h;
        if (h >= 0)
        {
            const auto& row = rows[(size_t) h];
            if (row.page >= 0)
            {
                const auto& en = entries[row.page];
                if (row.page == nextPage && nextWhat.isNotEmpty()) setTooltip ("Next: " + nextWhat + (en.text.isNotEmpty() ? " (" + en.text + ")" : juce::String()));
                else if (en.text.isNotEmpty()) setTooltip (juce::String (pageName (row.page)) + ": " + en.text);
                else setTooltip ({});
            }
            else setTooltip (juce::String (navGroups()[(size_t) row.group].name) + ": " + groupText (row.group));
        }
        else setTooltip ({});
        repaint();
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; hoverPoint = {}; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int h = rowAt (e.getPosition());
        if (h >= 0)
        {
            const auto& row = rows[(size_t) h];
            if (row.page >= 0) { if (onSelect) onSelect (row.page); return; }
            // a group: open it and go to its page (the next step inside it, else the first; a closed group that is
            // open already just closes)
            if (openGroup == row.group && navGroupOf (current) != row.group) { openGroup = -1; rebuildRows(); repaint(); return; }
            int go = -1;
            for (int pg : navGroups()[(size_t) row.group].pages) if (pg == nextPage && visible[pg]) go = pg;
            if (go < 0) for (int pg : navGroups()[(size_t) row.group].pages) if (visible[pg] && (go < 0 || pg == current)) go = pg;
            if (go >= 0 && onSelect) onSelect (go);
            return;
        }
        if (venueBounds().contains (e.getPosition()) && onVenue) onVenue();
    }

private:
    struct Entry { StepStatus status = StepStatus::None; juce::String text; float fraction = 0.0f; };
    struct Row { int page = -1, group = -1, y = 0, h = 0; };   // page >= 0: a page; group >= 0 with page < 0: a group's own line
    static constexpr int kTop = 52, kRowH = 36, kItemH = 28, kGap = 2, kVenueH = 72;

    void rebuildRows()
    {
        rows.clear();
        int y = 0;
        auto add = [&] (int page, int group, int h) { rows.push_back ({ page, group, y, h }); y += h + kGap; };
        if (visible[0]) add (0, -1, kRowH);
        const auto& gs = navGroups();
        for (int g = 0; g < (int) gs.size(); ++g)
        {
            bool any = false;
            for (int pg : gs[(size_t) g].pages) any = any || visible[pg];
            if (! any) continue;
            y += 4;
            add (-1, g, kRowH);
            if (g == openGroup)
                for (int pg : gs[(size_t) g].pages) if (visible[pg]) add (pg, g, kItemH);
        }
        content = y + 4;
    }

    juce::Rectangle<int> rowBounds (const Row& r) const
    {
        const bool item = r.page >= 0 && r.group >= 0;
        return { item ? 18 : 6, kTop - scroll + r.y, getWidth() - (item ? 24 : 12), r.h };
    }
    int rowAt (juce::Point<int> pt) const
    {
        if (! listArea().contains (pt)) return -1;
        for (int i = 0; i < (int) rows.size(); ++i) if (rowBounds (rows[(size_t) i]).contains (pt)) return i;
        return -1;
    }

    // A group's state from its pages: attention first, else how many are ready.
    StepStatus groupStatus (int g, float& fraction, int& ready, int& total) const
    {
        ready = 0; total = 0;
        bool attention = false, started = false;
        for (int pg : navGroups()[(size_t) g].pages)
        {
            if (! visible[pg]) continue;
            const auto& e = entries[pg];
            if (e.status == StepStatus::None) continue;
            ++total;
            if (e.status == StepStatus::Attention) attention = true;
            if (e.status == StepStatus::Ready) ++ready;
            if (e.status == StepStatus::InProgress || e.status == StepStatus::Ready) started = true;
        }
        fraction = total > 0 ? (float) ready / (float) total : 0.0f;
        if (attention) return StepStatus::Attention;
        if (total == 0) return StepStatus::None;
        if (ready == total) return StepStatus::Ready;
        return started ? StepStatus::InProgress : StepStatus::NotStarted;
    }
    juce::String groupText (int g) const
    {
        float f; int ready, total;
        const auto st = groupStatus (g, f, ready, total);
        if (st == StepStatus::Attention) { for (int pg : navGroups()[(size_t) g].pages) if (entries[pg].status == StepStatus::Attention) return juce::String (pageName (pg)) + ": " + entries[pg].text; }
        if (total == 0) return {};
        if (st == StepStatus::Ready) return "Done";
        return juce::String (ready) + " of " + juce::String (total) + " done";
    }

    void drawIcon (juce::Graphics& g, int page, juce::Rectangle<int> r, juce::Colour c, float scale = 0.85f) const
    {
        auto iconBox = juce::Rectangle<float> (24.0f, 24.0f).withCentre ({ (float) r.getX() + 20.0f, (float) r.getCentreY() });
        auto icon = pageIcon (page);
        icon.applyTransform (juce::AffineTransform::scale (scale, scale, 12.0f, 12.0f).translated (iconBox.getX(), iconBox.getY()));
        g.setColour (c);
        g.strokePath (icon, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawTopRow (juce::Graphics& g, const Row& row, juce::Rectangle<int> r, bool over)
    {
        const auto& p = pal();
        const bool active = row.page == current;
        if (active || over) { g.setColour (active ? p.panelRaised : p.panelRaised.withAlpha (0.5f)); g.fillRoundedRectangle (r.toFloat(), 6.0f); }
        if (active) { g.setColour (p.accent); g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 8.0f, 3.0f, (float) r.getHeight() - 16.0f, 1.5f); }
        drawIcon (g, row.page, r, active ? p.accent : p.textDim);
        g.setColour (active ? p.text : shade (p.text, p.dark ? -0.15f : 0.2f));
        g.setFont (font (12.5f, true));
        g.drawText (pageName (row.page), r.withTrimmedLeft (38), juce::Justification::centredLeft, true);
    }

    void drawGroupRow (juce::Graphics& g, const Row& row, juce::Rectangle<int> r, bool over)
    {
        const auto& p = pal();
        const auto& grp = navGroups()[(size_t) row.group];
        const bool open = row.group == openGroup, holds = navGroupOf (current) == row.group;
        const bool nextInside = nextPage >= 0 && navGroupOf (nextPage) == row.group && ! open;
        if (over) { g.setColour (p.panelRaised.withAlpha (0.5f)); g.fillRoundedRectangle (r.toFloat(), 6.0f); }
        if (holds && ! open) { g.setColour (p.accent); g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 8.0f, 3.0f, (float) r.getHeight() - 16.0f, 1.5f); }
        if (nextInside) { g.setColour (p.accent.withAlpha (0.8f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.3f); }
        drawIcon (g, grp.icon, r, holds ? p.accent : p.textDim);
        float f; int ready, total;
        const auto st = groupStatus (row.group, f, ready, total);
        auto text = r.withTrimmedLeft (38).withTrimmedRight (st == StepStatus::None ? 22 : 40);
        g.setColour (holds ? p.text : shade (p.text, p.dark ? -0.15f : 0.2f));
        g.setFont (font (12.5f, true));
        const auto sub = groupText (row.group);
        if (sub.isNotEmpty() && ! open)
        {
            g.drawText (grp.name, text.removeFromTop (r.getHeight() / 2 + 1).withTrimmedTop (2), juce::Justification::bottomLeft, true);
            g.setColour (st == StepStatus::Attention ? p.warn : nextInside ? p.accent : p.textDim);
            g.setFont (font (10.0f, nextInside));
            g.drawText (nextInside && nextWhat.isNotEmpty() ? nextWhat : sub, text, juce::Justification::topLeft, true);
        }
        else g.drawText (grp.name, text, juce::Justification::centredLeft, true);
        if (st != StepStatus::None && ! open)
            drawStatusBadge (g, juce::Rectangle<float> (13.0f, 13.0f).withCentre ({ (float) r.getRight() - 28.0f, (float) r.getCentreY() }), st, f);
        // the chevron
        juce::Path ch;
        const float cx = (float) r.getRight() - 12.0f, cy = (float) r.getCentreY();
        if (open) { ch.startNewSubPath (cx - 4.0f, cy - 2.0f); ch.lineTo (cx, cy + 2.0f); ch.lineTo (cx + 4.0f, cy - 2.0f); }
        else { ch.startNewSubPath (cx - 2.0f, cy - 4.0f); ch.lineTo (cx + 2.0f, cy); ch.lineTo (cx - 2.0f, cy + 4.0f); }
        g.setColour (p.textFaint);
        g.strokePath (ch, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawItemRow (juce::Graphics& g, const Row& row, juce::Rectangle<int> r, bool over)
    {
        const auto& p = pal();
        const int i = row.page;
        const bool active = i == current;
        if (active || over) { g.setColour (active ? p.panelRaised : p.panelRaised.withAlpha (0.5f)); g.fillRoundedRectangle (r.toFloat(), 5.0f); }
        if (active) { g.setColour (p.accent); g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 7.0f, 3.0f, (float) r.getHeight() - 14.0f, 1.5f); }
        if (i == nextPage && ! active) { g.setColour (p.accent.withAlpha (0.8f)); g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 5.0f, 1.3f); }
        const auto& e = entries[i];
        auto text = r.withTrimmedLeft (12).withTrimmedRight (e.status == StepStatus::None ? 6 : 22);
        juce::String name = pageName (i);
        if (isTuningStep (i)) name = juce::String (tuningStepNumber (i)) + "  " + name;
        g.setColour (active ? p.text : shade (p.text, p.dark ? -0.15f : 0.2f));
        g.setFont (font (12.0f, active || i == nextPage));
        if (e.text.isNotEmpty())
        {
            g.drawText (name, text.removeFromTop (r.getHeight() / 2 + 1).withTrimmedTop (1), juce::Justification::bottomLeft, true);
            g.setColour (e.status == StepStatus::Attention ? p.warn : i == nextPage ? p.accent : p.textDim);
            g.setFont (font (9.5f, i == nextPage));
            g.drawText (i == nextPage && nextWhat.isNotEmpty() ? "Next: " + nextWhat : e.text, text, juce::Justification::topLeft, true);
        }
        else g.drawText (name, text, juce::Justification::centredLeft, true);
        if (e.status != StepStatus::None)
            drawStatusBadge (g, juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ (float) r.getRight() - 11.0f, (float) r.getCentreY() }), e.status, e.fraction);
    }

    juce::Rectangle<int> listArea() const
    {
        const int bottom = (updateButton.isVisible() ? updateButton.getY() : settingsButton.getY()) - 8;
        return { 0, kTop, getWidth() - 1, juce::jmax (0, bottom - kTop) };
    }
    int contentHeight() const { return content; }

    void scrollToShow (int page)
    {
        const auto r = entryBounds (page);
        const auto la = listArea();
        if (r.isEmpty() || la.isEmpty()) return;
        if (r.getY() < la.getY()) scroll -= la.getY() - r.getY();
        else if (r.getBottom() > la.getBottom()) scroll += r.getBottom() - la.getBottom();
        scroll = juce::jlimit (0, juce::jmax (0, contentHeight() - la.getHeight()), scroll);
    }

    juce::Rectangle<int> venueBounds() const { return { 6, getHeight() - kVenueH - 8, getWidth() - 12, kVenueH }; }

    Entry entries[kNumPages];
    bool visible[kNumPages] { true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, false, true, true, true, true };
    std::vector<Row> rows;
    int content = 0, openGroup = -1;
    int nextPage = -1;
    juce::String nextWhat;
    int current = 0, hover = -1, scroll = 0;
    juce::Point<int> hoverPoint;
    juce::String venueName, sessionName;
    bool dirty = false;
    juce::TextButton settingsButton { "SETTINGS" }, updateButton;
public:
    static constexpr int kWidth = 196;
};
} // namespace la::ui
