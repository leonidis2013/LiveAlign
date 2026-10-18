// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Home: the venue, its sessions, and a dashboard of where each step stands, laid out the same way as the navigation
// pane (Setup, Tuning in order, Monitoring, Reports): the next thing to do, what needs attention, and the quick
// re-check of a system already tuned.
#include <array>
#include <functional>
#include "NavRail.h"
#include "../PluginProcessor.h"
#include "../VenueLibrary.h"
#include "VenuePage.h"

namespace la::ui
{
struct StepInfo
{
    StepStatus status = StepStatus::NotStarted;
    juce::String shortText;   // for the navigation pane
    juce::String detail;      // a sentence for the home page
    float fraction = 0.0f;
};

class HomePage : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
{
public:
    std::function<void()> onNewVenueFromThis;
    std::function<void()> onNewVenue, onOpenVenue, onSave, onSaveAs, onNewSession, onChanged, onReport;
    std::function<void (int)> onGoTo, onRenameSession, onDeleteSession;
    // the venue list: open, rename, duplicate or delete a venue file
    std::function<void (const juce::File&)> onOpenFile, onDeleteFile;
    std::function<void()> onUseCurrent;   // carry on with the venue in memory (the last one used)
    std::function<void (const VenueEntry&)> onRenameFile, onDuplicateFile;
    std::function<void()> onRecheck;

    explicit HomePage (LiveAlignProcessor& p) : proc (p)
    {
        searchEditor.setTextToShowWhenEmpty ("Search venues", pal().textFaint);
        searchEditor.setFont (font (13.0f));
        searchEditor.onTextChange = [this] { venueScroll = 0; filterVenues(); repaint (venueCard); };
        addAndMakeVisible (searchEditor);
        setupButton (newVenueButton, [this]
        {
            juce::PopupMenu m;
            m.addItem (1, "Blank venue");
            m.addItem (2, "Based on this one  (the same speakers, points and plan)");
            juce::Component::SafePointer<HomePage> safe (this);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&newVenueButton), [safe] (int r)
            {
                if (safe == nullptr) return;
                if (r == 1 && safe->onNewVenue) safe->onNewVenue();
                if (r == 2 && safe->onNewVenueFromThis) safe->onNewVenueFromThis();
            });
        }, "Start a new venue file: blank, or based on this one (for a touring rig or a similar room)");
        setupButton (openButton, [this] { if (onOpenVenue) onOpenVenue(); }, "Open a venue file from anywhere (it joins the list)");
        setupButton (saveButton, [this] { if (onSave) onSave(); }, "Save the venue and all its sessions (Cmd/Ctrl+S)");
        setupButton (saveAsButton, [this] { if (onSaveAs) onSaveAs(); }, "Save under a new name");

        setupButton (newSessionButton, [this] { if (onNewSession) onNewSession(); },
                     "Start a new visit to this venue. The mics, positions and speaker setup carry over; measurements start fresh.");
        setupButton (openSessionButton, [this] { proc.switchSession (selected); changed(); }, "Make the selected session the open one");
        setupButton (compareButton, [this] { toggleCompare(); }, "Show the selected session's traces beside the open session's");
        setupButton (renameButton, [this] { if (onRenameSession) onRenameSession (selected); }, "Rename the selected session");
        setupButton (deleteButton, [this] { if (onDeleteSession) onDeleteSession (selected); }, "Delete the selected session");

        speakerChangedButton.onClick = [this]
        {
            proc.speakers.changed = ! proc.speakers.changed;
            changed();
        };
        speakerChangedButton.setTooltip ("Mark the speaker system as changed (moved, replaced or re-cabled) so it is checked again");
        addAndMakeVisible (speakerChangedButton);
        goButton.onClick = [this] { if (nextPage >= 0 && onGoTo) onGoTo (nextPage); };
        addChildComponent (goButton);
        recheckButton.onClick = [this] { if (onRecheck) onRecheck(); };
        recheckButton.setTooltip ("For a system already tuned: one guided pass that measures each speaker close up, then each one and the whole "
                                  "system at FOH, and flags only what has drifted (polarity, level, delay, EQ)");
        addAndMakeVisible (recheckButton);

        refresh();
        startTimerHz (4);
    }

    // Status of every page (indexed by la::ui::Page).
    void setSteps (const std::array<StepInfo, kNumPages>& s, bool setupChanged)
    {
        bool diff = setupChanged != changedSetup;
        for (int i = 0; i < kNumPages; ++i)
            diff = diff || s[(size_t) i].status != steps[(size_t) i].status || s[(size_t) i].detail != steps[(size_t) i].detail
                   || std::abs (s[(size_t) i].fraction - steps[(size_t) i].fraction) > 0.01f;
        if (! diff) return;
        steps = s;
        changedSetup = setupChanged;
        repaint();
    }

    // The step to do next, and what to do there (from the editor), for the banner.
    void setNext (int page, const juce::String& what)
    {
        if (page == nextPage && what == nextWhat) return;
        nextPage = page;
        nextWhat = what;
        goButton.setVisible (page >= 0 && started());
        goButton.setButtonText (page >= 0 ? juce::String ("Go to ") + pageName (page) : juce::String());
        resized();
        repaint();
    }

    // Reloads the editors after a venue is opened or created.
    void refresh()
    {
        rescanVenues();
        selected = proc.currentSession();
        updateButtons();
        recheckButton.setVisible (started() && ! proc.monitoringSession());   // it plays test signals: a tuning session's
        resized();
        repaint();
    }

    // A venue is open (or this is the VST3, which only monitors): until then Home shows the venues and a way in.
    bool started() const { return proc.venueOpen || ! (proc.isStandalone() || proc.uiAsStandalone); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, venueCard.toFloat(), "Venues");
        drawVenueList (g);
        drawPanel (g, sessionsCard.toFloat(), "Sessions");
        if (! started()) { paintStart (g); return; }
        drawPanel (g, stepsCard.toFloat(), proc.monitoringSession() ? "This show" : "Where this venue stands");
        // the next thing to do, and what needs attention
        {
            auto b = nextBanner;
            g.setColour (p.accent.withAlpha (0.14f));
            g.fillRoundedRectangle (b.toFloat(), 7.0f);
            g.setColour (p.accent);
            g.fillRoundedRectangle (b.toFloat().removeFromLeft (4.0f), 2.0f);
            b = b.reduced (14, 6).withTrimmedRight (goButton.isVisible() ? goButton.getWidth() + 10 : 0);
            g.setColour (p.accent);
            g.setFont (font (10.5f, true));
            g.drawText (nextPage >= 0 ? "NEXT" : "ALL DONE", b.removeFromTop (14), juce::Justification::centredLeft);
            g.setColour (p.text);
            g.setFont (font (13.5f, true));
            g.drawText (nextPage >= 0 ? juce::String (pageName (nextPage)) + ": " + nextWhat : juce::String ("The tuning is done. The reports and the processor sheet are ready."),
                        b, juce::Justification::centredLeft, true);
            juce::StringArray att;
            for (int i = 0; i < kNumPages; ++i) if (steps[(size_t) i].status == StepStatus::Attention) att.add (juce::String (pageName (i)) + ": " + steps[(size_t) i].shortText);
            {
                int issues = 0;
                for (const auto& l : proc.recheck.lines) issues += l.level > 0 ? 1 : 0;
                if (issues > 0) att.add ("Quick re-check: " + juce::String (issues) + (issues == 1 ? " thing has" : " things have") + " changed (below)");
                bool worth = false;
                const auto drift = VenueDetailsPage::driftText (proc, worth);
                if (worth && proc.room.tuned) att.add (drift.upToFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf (".", false, false) + ": run a quick re-check");
            }
            g.setColour (att.isEmpty() ? p.textFaint : p.warn);
            g.setFont (font (11.5f, ! att.isEmpty()));
            g.drawText (att.isEmpty() ? juce::String ("Nothing needs attention.") : "Needs attention: " + att.joinIntoString ("   "), attentionLine, juce::Justification::centredLeft, true);
        }

        // sessions list
        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawText ("A session is one visit, for tuning or for a show. The venue, mics, points and speakers carry over.",
                    sessionsHint, juce::Justification::centredLeft, true);
        const auto& list = proc.getSessions();
        for (int i = 0; i < (int) list.size() && i < visibleRows(); ++i)
        {
            const int idx = i + scroll;
            if (idx >= (int) list.size()) break;
            auto r = rowBounds (i);
            const bool sel = idx == selected, open = idx == proc.currentSession();
            if (sel)
            {
                g.setColour (p.panelRaised);
                g.fillRoundedRectangle (r.toFloat(), 6.0f);
                g.setColour (p.border);
                g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, 1.0f);
            }
            auto dot = r.removeFromLeft (26).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            g.setColour (open ? p.accent : p.border);
            g.fillEllipse (dot);
            const auto& s = list[(size_t) idx];
            auto right = r.removeFromRight (190);
            g.setColour (p.text);
            g.setFont (font (13.0f, open));
            g.drawText (s.name, r, juce::Justification::centredLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawText (s.created + "   " + juce::String (traceCount (idx)) + " traces" + (open ? "   OPEN" : ""),
                        right.withTrimmedRight (8), juce::Justification::centredRight, true);
            // what the session is for
            {
                const auto tag = juce::String (s.kind == 1 ? "SHOW" : "TUNING");
                const auto tf = font (9.5f, true);
                const float tw = juce::GlyphArrangement::getStringWidth (tf, tag) + 12.0f;
                const float nameW = juce::jmin ((float) r.getWidth() - tw - 8.0f, juce::GlyphArrangement::getStringWidth (font (13.0f, open), s.name));
                auto pill = juce::Rectangle<float> ((float) r.getX() + nameW + 8.0f, (float) r.getCentreY() - 8.0f, tw, 16.0f);
                const auto c = s.kind == 1 ? p.warn : p.accent;
                g.setColour (c.withAlpha (0.16f));
                g.fillRoundedRectangle (pill, 8.0f);
                g.setColour (c);
                g.setFont (tf);
                g.drawText (tag, pill, juce::Justification::centred);
            }
        }

        // the workflow, grouped as in the navigation pane
        for (const auto& h : headings)
        {
            g.setColour (p.textFaint);
            g.setFont (font (10.5f, true));
            g.drawText (navHeading (h.first), h.second, juce::Justification::centredLeft);
            g.setColour (p.border);
            g.drawHorizontalLine (h.second.getCentreY(), (float) h.second.getX() + juce::GlyphArrangement::getStringWidth (font (10.5f, true), navHeading (h.first)) + 8.0f,
                                  (float) h.second.getRight());
        }
        for (const auto& row : rows)
        {
            const int i = row.first;
            auto r = row.second;
            const auto& st = steps[(size_t) i];
            const bool over = i == hoverStep;
            g.setColour (over ? p.panelRaised.brighter (p.dark ? 0.05f : 0.0f) : p.panelRaised);
            g.fillRoundedRectangle (r.toFloat(), 7.0f);
            const bool required = (i == (int) Page::Mics || i == (int) Page::Response || i == (int) Page::Crossover || i == (int) Page::Level || i == (int) Page::Room)
                                  && (st.status == StepStatus::NotStarted || st.status == StepStatus::InProgress);
            juce::Colour edge = over ? p.accent.withAlpha (0.7f) : p.border;
            float width = over ? 1.4f : 1.0f;
            if (i == nextPage) { edge = p.accent; width = 1.8f; }
            if (st.status == StepStatus::Attention) { edge = p.warn.withAlpha (0.95f); width = 1.6f; }
            else if (required) { edge = p.warn.withAlpha (over ? 0.85f : 0.55f); width = 1.3f; }
            else if (st.status == StepStatus::Ready && ! over) { edge = p.good.withAlpha (0.5f); width = 1.2f; }
            g.setColour (edge);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 7.0f, width);

            auto icon = pageIcon (i);
            icon.applyTransform (juce::AffineTransform::translation ((float) r.getX() + 12.0f, (float) r.getCentreY() - 12.0f));
            g.setColour (p.accent);
            g.strokePath (icon, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            drawStatusBadge (g, juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ (float) r.getRight() - 18.0f, (float) r.getCentreY() }), st.status, st.fraction);

            auto text = r.withTrimmedLeft (48).withTrimmedRight (i == (int) Page::Venue ? 180 : 36);
            juce::String name = i == (int) Page::Venue ? "Venue" : i == (int) Page::Mics ? "Mics" : pageName (i);   // the groups, as the navigation names them
            if (isTuningStep (i)) name = juce::String (tuningStepNumber (i)) + "  " + name;
            g.setColour (p.text);
            g.setFont (font (13.5f, true));
            g.drawText (name, text.removeFromTop (r.getHeight() / 2).withTrimmedTop (3), juce::Justification::bottomLeft, true);
            g.setColour (st.status == StepStatus::Attention ? p.warn : p.textDim);
            g.setFont (font (11.5f));
            g.drawText (st.detail.isNotEmpty() ? st.detail : st.shortText, text.withTrimmedTop (1), juce::Justification::topLeft, true);
        }

        drawSessionSummary (g);

        if (changedSetup)
        {
            g.setColour (p.warn);
            g.setFont (font (12.0f, true));
            g.drawText ("The interface or the mic wiring has changed since this session's checks. Re-check the mic setup.",
                        changeLine, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (juce::jmin (520, r.getWidth() * 44 / 100));
        r.removeFromLeft (10);

        venueCard = left.removeFromTop (juce::jmax (250, left.getHeight() * 56 / 100));
        left.removeFromTop (10);
        sessionsCard = left;
        stepsCard = r;

        // venues card: search, the list, then the buttons
        auto v = venueCard.reduced (16).withTrimmedTop (26);
        searchEditor.setBounds (v.removeFromTop (28));
        v.removeFromTop (6);
        venueFileLine = v.removeFromBottom (18);
        v.removeFromBottom (4);
        auto buttons = v.removeFromBottom (30);
        const int bw = (buttons.getWidth() - 18) / 4;
        for (auto* b : { &newVenueButton, &openButton, &saveButton, &saveAsButton }) { b->setBounds (buttons.removeFromLeft (bw)); buttons.removeFromLeft (6); }
        v.removeFromBottom (8);
        venueListArea = v;

        // sessions card
        auto s = sessionsCard.reduced (16).withTrimmedTop (26);
        sessionsHint = s.removeFromTop (20);
        s.removeFromTop (4);
        auto sb = s.removeFromBottom (30);
        const int sw = (sb.getWidth() - 24) / 5;
        for (auto* b : { &newSessionButton, &openSessionButton, &compareButton, &renameButton, &deleteButton })
        {
            b->setBounds (sb.removeFromLeft (sw));
            sb.removeFromLeft (6);
        }
        s.removeFromBottom (8);
        listArea = s;

        // steps card: the next step and what needs attention, then two columns, the tuning steps on the right
        auto st = stepsCard.reduced (16).withTrimmedTop (30);
        {
            auto head = stepsCard.reduced (16, 0).withTrimmedTop (6).withHeight (26);
            recheckButton.setBounds (head.removeFromRight (190).withSizeKeepingCentre (190, 26).translated (0, 2));
        }
        nextBanner = st.removeFromTop (52);
        if (goButton.isVisible()) goButton.setBounds (nextBanner.reduced (10, 0).removeFromRight (190).withSizeKeepingCentre (190, 30));
        st.removeFromTop (4);
        attentionLine = st.removeFromTop (20);
        st.removeFromTop (4);
        changeLine = st.removeFromBottom (22);
        rows.clear();
        headings.clear();
        const int rowH = 40, gap = 4, headH = 22;
        auto colA = st.removeFromLeft ((st.getWidth() - 12) / 2);
        st.removeFromLeft (12);
        auto colB = st;
        auto column = [&] (juce::Rectangle<int>& col, std::initializer_list<int> order)
        {
            for (int k : order)
            {
                if (k < 0) { headings.push_back ({ k, col.removeFromTop (headH) }); continue; }
                rows.push_back ({ k, col.removeFromTop (rowH) });
                col.removeFromTop (gap);
            }
            return col;
        };
        // a tuning session: the setup and the tuning steps; a show: the venue, the monitor mic and live monitoring
        const bool show = proc.monitoringSession();
        auto restA = show ? column (colA, { -1, (int) Page::Venue }) : column (colA, { -1, (int) Page::Venue, (int) Page::Mics });
        auto restB = show ? column (colB, { -3, (int) Page::MonitorMic, (int) Page::Monitor })
                          : column (colB, { -2, (int) Page::Response, (int) Page::Headroom, (int) Page::Crossover, (int) Page::Level, (int) Page::Coverage,
                                            (int) Page::Align, (int) Page::Acoustics, (int) Page::Room, (int) Page::Listening, (int) Page::Feedback });
        auto bottom = colA.getBottom() > colB.getBottom() ? restA : restB;
        juce::ignoreUnused (bottom);
        // reports along the bottom of both columns
        auto reports = juce::Rectangle<int>::leftTopRightBottom (stepsCard.getX() + 16, juce::jmax (restA.getY(), restB.getY()), stepsCard.getRight() - 16,
                                                                 changeLine.getY());
        headings.push_back ({ -4, reports.removeFromTop (headH) });
        auto rr = reports.removeFromTop (rowH);
        const int half = (rr.getWidth() - 12) / 2;
        rows.push_back ({ (int) Page::Sheet, rr.removeFromLeft (half) });
        rr.removeFromLeft (12);
        rows.push_back ({ show ? (int) Page::MonitorReport : (int) Page::TuningReport, rr });
        reports.removeFromTop (gap + 6);
        summaryArea = reports;
        for (const auto& row : rows)
            if (row.first == (int) Page::Venue)
                speakerChangedButton.setBounds (row.second.withTrimmedRight (40).removeFromRight (130).withSizeKeepingCentre (130, 26));
        if (! started()) { rows.clear(); headings.clear(); }   // nothing to step through before a venue is open
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        {
            const int hv = venueListArea.contains (e.getPosition()) ? venueRowAt (e.getPosition()) : -1;
            if (hv != hoverVenue) { hoverVenue = hv; repaint (venueCard); }
            if (hv >= 0)
            {
                const auto& v = shownVenues[(size_t) hv];
                setMouseCursor (juce::MouseCursor::PointingHandCursor);
                setTooltip (v.file.getFullPathName() + (v.file == proc.projectFile ? "  (open)" : "\nClick to open; ... or right-click for more"));
                return;
            }
            setTooltip ({});
        }
        int h = -1;
        for (const auto& row : rows) if (row.second.contains (e.getPosition()) && ! speakerChangedButton.getBounds().contains (e.getPosition())) h = row.first;
        if (h != hoverStep) { hoverStep = h; setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint (stepsCard); }
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        if (hoverStep >= 0) { hoverStep = -1; repaint (stepsCard); }
        if (hoverVenue >= 0) { hoverVenue = -1; repaint (venueCard); }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (venueListArea.contains (e.getPosition()))
        {
            const int i = venueRowAt (e.getPosition());
            if (i < 0) return;
            const auto& v = shownVenues[(size_t) i];
            const bool menu = e.mods.isPopupMenu() || venueMenuButton (i).contains (e.getPosition());
            if (menu) { showVenueMenu (v); return; }
            // at start the last venue is already in memory (with any unsaved changes): clicking it carries on with it
            if (v.file == proc.projectFile && ! started()) { if (onUseCurrent) onUseCurrent(); }
            else if (v.file != proc.projectFile && onOpenFile) onOpenFile (v.file);
            return;
        }
        for (const auto& row : rows)
            if (row.second.contains (e.getPosition())) { if (onGoTo) onGoTo (row.first); return; }
        for (int i = 0; i < visibleRows() && started(); ++i)
            if (rowBounds (i).contains (e.getPosition()) && i + scroll < (int) proc.getSessions().size())
            {
                selected = i + scroll;
                if (e.getNumberOfClicks() > 1) { proc.switchSession (selected); changed(); }
                updateButtons();
                repaint();
            }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override { mouseUp (e); }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        if (venueListArea.contains (e.getPosition()))
        {
            venueScroll = juce::jlimit (0, juce::jmax (0, (int) shownVenues.size() - venueRows()), venueScroll - (w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0)));
            repaint (venueCard);
            return;
        }
        scroll = juce::jlimit (0, juce::jmax (0, (int) proc.getSessions().size() - visibleRows()), scroll - (w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0)));
        repaint();
    }

    // Reads the venue folder and the recent venues again (after a save, a rename, a delete ...).
    void rescanVenues()
    {
        juce::Component::SafePointer<HomePage> safe (this);
        const int ticket = ++scanTicket;
        juce::Thread::launch ([safe, ticket]
        {
            auto list = venues::scan();
            juce::MessageManager::callAsync ([safe, ticket, list = std::move (list)]() mutable
            {
                if (safe == nullptr || ticket != safe->scanTicket) return;
                safe->allVenues = std::move (list);
                safe->filterVenues();
                safe->repaint (safe->venueCard);
            });
        });
    }

private:
    void filterVenues()
    {
        shownVenues.clear();
        const auto q = searchEditor.getText().trim();
        // the open venue first when it has not been saved yet (it is in no folder)
        // (at start only when it has changes worth carrying on with: the last session's unsaved venue)
        if ((started() || proc.projectChanged()) && proc.projectFile == juce::File() && (q.isEmpty() || proc.venueName.containsIgnoreCase (q)))
        {
            VenueEntry e;
            e.name = proc.venueName;
            e.sessions = (int) proc.getSessions().size();
            e.tuned = proc.room.tuned ? 1 : 0;
            shownVenues.push_back (e);
        }
        for (const auto& v : allVenues)
            if (q.isEmpty() || v.name.containsIgnoreCase (q) || v.file.getFileName().containsIgnoreCase (q)) shownVenues.push_back (v);
        venueScroll = juce::jlimit (0, juce::jmax (0, (int) shownVenues.size() - venueRows()), venueScroll);
    }

    static constexpr int kVenueRowH = 40;
    int venueRows() const { return juce::jmax (1, venueListArea.getHeight() / kVenueRowH); }
    juce::Rectangle<int> venueRowBounds (int i) const { return venueListArea.withHeight (kVenueRowH - 4).translated (0, (i - venueScroll) * kVenueRowH); }
    juce::Rectangle<int> venueMenuButton (int i) const { return venueRowBounds (i).removeFromRight (34).reduced (4, 6); }
    int venueRowAt (juce::Point<int> pt) const
    {
        for (int i = venueScroll; i < (int) shownVenues.size() && i < venueScroll + venueRows(); ++i)
            if (venueRowBounds (i).contains (pt)) return i;
        return -1;
    }

    void drawVenueList (juce::Graphics& g)
    {
        const auto& p = pal();
        if (shownVenues.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawFittedText (searchEditor.getText().trim().isNotEmpty() ? juce::String ("No venue matches.")
                                                                          : "No venues yet. Saved venues go to " + venues::folder().getFullPathName() + ".",
                              venueListArea.withHeight (40), juce::Justification::topLeft, 2);
        }
        for (int i = venueScroll; i < (int) shownVenues.size() && i < venueScroll + venueRows(); ++i)
        {
            const auto& v = shownVenues[(size_t) i];
            auto r = venueRowBounds (i);
            const bool open = v.file == proc.projectFile, over = i == hoverVenue;
            if (open || over)
            {
                g.setColour (open ? p.accent.withAlpha (0.12f) : p.panelRaised);
                g.fillRoundedRectangle (r.toFloat(), 6.0f);
            }
            g.setColour (open ? p.accent : p.border);
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 6.0f, open ? 1.4f : 1.0f);
            auto dot = r.removeFromLeft (24).toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            g.setColour (open ? p.accent : v.tuned == 1 ? p.good : p.border);
            g.fillEllipse (dot);
            // the "..." menu
            auto mb = r.removeFromRight (34);
            g.setColour (over ? p.text : p.textFaint);
            g.setFont (font (16.0f, true));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")), mb, juce::Justification::centred);
            auto top = r.removeFromTop (r.getHeight() / 2 + 1);
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            g.drawText (v.name + (open ? (! started() ? juce::String (unsavedNow ? "   (last used, unsaved changes)" : "   (last used)")
                                                      : (unsavedNow ? "   (open, unsaved changes)" : "   (open)")) : juce::String()),
                        top.withTrimmedTop (2), juce::Justification::bottomLeft, true);
            juce::StringArray bits;
            bits.add (v.file == juce::File() ? juce::String ("not saved yet") : "saved " + venues::when (v.saved));
            if (v.lastUsed.toMilliseconds() > 0) bits.add ("used " + venues::when (v.lastUsed));
            if (v.sessions >= 0) bits.add (juce::String (v.sessions) + (v.sessions == 1 ? " session" : " sessions"));
            if (v.tuned >= 0) bits.add (v.tuned == 1 ? "tuned" : "not tuned yet");
            if (v.file != juce::File() && ! v.inFolder) bits.add ("in " + v.file.getParentDirectory().getFileName());
            g.setColour (p.textDim);
            g.setFont (font (11.0f));
            g.drawText (bits.joinIntoString ("  \xc2\xb7  "), r, juce::Justification::topLeft, true);
        }
        if ((int) shownVenues.size() > venueRows())
        {
            g.setColour (p.textFaint);
            g.setFont (font (10.5f));
            g.drawText (juce::String (venueScroll + 1) + "-" + juce::String (juce::jmin ((int) shownVenues.size(), venueScroll + venueRows())) + " of "
                            + juce::String ((int) shownVenues.size()) + "  (scroll for more)",
                        venueFileLine, juce::Justification::centredRight);
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawText ("Folder: " + venues::folder().getFullPathName(), venueFileLine, juce::Justification::centredLeft, true);
    }

    void showVenueMenu (const VenueEntry& v)
    {
        juce::PopupMenu m;
        const bool open = v.file == proc.projectFile, saved = v.file != juce::File();
        m.addItem (1, "Open", ! open && saved);
        m.addItem (2, "Rename...", saved);
        m.addItem (3, "Duplicate...", saved);
        m.addItem (4, "Show in folder", saved);
        m.addSeparator();
        m.addItem (5, "Delete (to the trash)...", saved);
        juce::Component::SafePointer<HomePage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safe, v] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1 && safe->onOpenFile) safe->onOpenFile (v.file);
            if (r == 2 && safe->onRenameFile) safe->onRenameFile (v);
            if (r == 3 && safe->onDuplicateFile) safe->onDuplicateFile (v);
            if (r == 4) v.file.revealToUser();
            if (r == 5 && safe->onDeleteFile) safe->onDeleteFile (v.file);
        });
    }

    // The open session at a glance: every mic's state, the loopback and the delays.
    // A show session: the monitor mic, what it is compared with, and the show's level so far.
    // Before a venue is open: what to do first.
    void paintStart (juce::Graphics& g)
    {
        const auto& p = pal();
        drawPanel (g, stepsCard.toFloat(), "Get started");
        g.setColour (p.textDim);
        g.setFont (font (12.0f));
        g.drawFittedText ("Open a venue to see its sessions.", sessionsCard.reduced (16).withTrimmedTop (30).withHeight (40), juce::Justification::topLeft, 2);
        auto r = stepsCard.reduced (24).withTrimmedTop (34);
        auto item = [&] (const juce::String& title, const juce::String& text)
        {
            g.setColour (p.text);
            g.setFont (font (15.0f, true));
            g.drawText (title, r.removeFromTop (24), juce::Justification::centredLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (12.5f));
            g.drawFittedText (text, r.removeFromTop (54), juce::Justification::topLeft, 3, 1.0f);
            r.removeFromTop (6);
        };
        item ("Open a venue", "Click one in the list on the left, or Open... for a venue file somewhere else.");
        item ("Or start a new one", "New venue... starts a blank venue: then add the room, the speakers and the points.");
        item ("Then pick the session", "Each venue has sessions, one per visit: a tuning session (the tuning steps, with test signals) "
                                       "or a show session (live monitoring). The rest of LiveAlign appears once a venue is open.");
        g.setColour (p.textFaint);
        g.setFont (font (11.5f));
        g.drawFittedText ("Settings (bottom of the menu) are available now: the audio interface, units, and your defaults.", r.removeFromTop (36),
                          juce::Justification::topLeft, 2);
    }

    void drawShowSummary (juce::Graphics& g, juce::Rectangle<int> r)
    {
        const auto& p = pal();
        const auto& mm = proc.monitorMic;
        auto line = [&] (const juce::String& label, const juce::String& value, juce::Colour c)
        {
            if (r.getHeight() < 20) return;
            auto row = r.removeFromTop (22);
            g.setColour (p.textDim);
            g.setFont (font (11.5f, true));
            g.drawText (label, row.removeFromLeft (170).withTrimmedLeft (10), juce::Justification::centredLeft, true);
            g.setColour (c);
            g.setFont (font (12.0f));
            g.drawText (value, row, juce::Justification::centredLeft, true);
        };
        const bool calOk = mm.cal.any() || mm.noCalFile;
        line ("Monitor mic", mm.name + " at " + mm.position + (calOk ? juce::String() : ", calibration file not loaded"), calOk ? p.text : p.warn);
        line ("Sound level", mm.splCalibrated ? "calibrated" + (mm.splDate.isNotEmpty() ? " (" + mm.splDate + ")" : juce::String()) : juce::String ("not calibrated: levels are relative"), mm.splCalibrated ? p.text : p.warn);
        const auto ref = proc.tuningRoomName();
        line ("Compared with", ref.isNotEmpty() ? "the tuning in " + ref : juce::String ("no tuning session with Room EQ measurements yet"), ref.isNotEmpty() ? p.text : p.textDim);
        const auto& log = proc.splLog;
        if (log.empty()) line ("The show", "nothing logged yet: open Live monitoring", p.textDim);
        else
        {
            const double mins = (log.end() - log.start()) / 60.0;
            line ("The show", juce::String (juce::roundToInt (mins)) + " min logged, LAeq " + juce::String (log.leq (false), 1) + " dB, loudest LAF "
                                  + juce::String (log.maxLaf(), 1) + " dB", p.text);
            if (log.limitLaeq > 0.0)
            {
                const double over = log.minutesOver();
                line ("Against the limit", over > 0.0 ? juce::String (juce::roundToInt (over)) + " min over " + juce::String (juce::roundToInt (log.limitLaeq)) + " dBA"
                                                      : juce::String ("within ") + juce::String (juce::roundToInt (log.limitLaeq)) + " dBA", over > 0.0 ? p.warn : p.good);
            }
        }
        line ("Generator", "off for the whole session (a show session never plays test signals)", p.textDim);
    }

    void drawSessionSummary (juce::Graphics& g)
    {
        const auto& p = pal();
        auto r = summaryArea;
        if (r.getHeight() < 60) return;
        // the quick re-check, when one was run this session
        if (proc.recheck.when.isNotEmpty())
        {
            const int w = proc.recheck.worst();
            g.setColour (w == 0 ? p.good : w == 1 ? p.warn : p.bad);
            g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 7.0f, 3.0f, 12.0f, 1.5f);
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            g.drawText ("Quick re-check " + proc.recheck.when + (w == 0 ? ":  nothing has drifted" : w == 1 ? ":  a few things to look at" : ":  something has changed"),
                        r.removeFromTop (26).withTrimmedLeft (10), juce::Justification::centredLeft, true);
            // what drifted first, then how many are as tuned
            int fine = 0;
            for (int pass = 2; pass >= 1; --pass)
                for (const auto& l : proc.recheck.lines)
                {
                    if (l.level != pass || r.getHeight() < 70) continue;
                    g.setColour (l.level == 1 ? p.warn : p.bad);
                    g.setFont (font (11.5f, true));
                    g.drawText (l.text, r.removeFromTop (18).withTrimmedLeft (10), juce::Justification::centredLeft, true);
                }
            for (const auto& l : proc.recheck.lines) fine += l.level == 0 ? 1 : 0;
            if (fine > 0 && r.getHeight() >= 60)
            {
                g.setColour (p.textDim);
                g.setFont (font (11.5f));
                g.drawText (juce::String (fine) + (fine == 1 ? " point is" : " points are") + " as tuned.", r.removeFromTop (18).withTrimmedLeft (10),
                            juce::Justification::centredLeft, true);
            }
            r.removeFromTop (10);
        }
        g.setColour (p.accent);
        g.fillRoundedRectangle ((float) r.getX(), (float) r.getY() + 7.0f, 3.0f, 12.0f, 1.5f);
        g.setColour (p.text);
        g.setFont (font (13.0f, true));
        const auto& list = proc.getSessions();
        const int cur = proc.currentSession();
        g.drawText ("This session: " + (cur < (int) list.size() ? list[(size_t) cur].name : juce::String()), r.removeFromTop (26).withTrimmedLeft (10),
                    juce::Justification::centredLeft, true);
        g.setColour (p.border);
        g.drawHorizontalLine (r.getY(), (float) r.getX(), (float) r.getRight());
        r.removeFromTop (6);
        if (proc.monitoringSession()) { drawShowSummary (g, r); return; }

        const int cols[] = { 26, 150, 110, 90, 150, 110, 100 };
        const char* heads[] = { "", "Mic", "Position", "Input", "Calibration file", "Sound level", "Delay" };
        auto header = r.removeFromTop (20);
        g.setFont (font (10.5f, true));
        g.setColour (p.textDim);
        for (int c = 0; c < 7; ++c) g.drawText (juce::String (heads[c]).toUpperCase(), header.removeFromLeft (cols[c]), juce::Justification::centredLeft);

        const auto& s = proc.settings;
        for (int m = 0; m < s.micCount.load() && r.getHeight() >= 22; ++m)
        {
            auto row = r.removeFromTop (22);
            g.setColour (micColour (m));
            g.fillEllipse (row.removeFromLeft (cols[0]).toFloat().withSizeKeepingCentre (8.0f, 8.0f));
            g.setFont (font (12.0f));
            g.setColour (p.text);
            g.drawText (proc.mics.nameOf (m), row.removeFromLeft (cols[1]), juce::Justification::centredLeft, true);
            g.setColour (p.textDim);
            g.drawText (proc.mics.position[(size_t) m].isNotEmpty() ? proc.mics.position[(size_t) m] : juce::String ("-"), row.removeFromLeft (cols[2]), juce::Justification::centredLeft, true);
            g.drawText ("In " + juce::String (s.micInput[m].load() + 1), row.removeFromLeft (cols[3]), juce::Justification::centredLeft);
            const auto cal = proc.calibrations (m).label();
            g.drawText (cal.isNotEmpty() ? cal : (proc.mics.noCalFile[(size_t) m] ? juce::String ("none (flat)") : juce::String ("not loaded")),
                        row.removeFromLeft (cols[4]), juce::Justification::centredLeft, true);
            juce::String lvl = proc.mics.noCalibrator ? "dBFS" : (! s.micSplCalibrated[m].load() ? "not calibrated"
                                   : (proc.checks.splVerified[(size_t) m] ? "checked" : "re-check"));
            g.setColour (lvl == "re-check" || lvl == "not calibrated" ? p.warn : p.textDim);
            g.drawText (lvl, row.removeFromLeft (cols[5]), juce::Justification::centredLeft);
            g.setColour (p.textDim);
            g.drawText (juce::String (s.micDelayMs[m].load(), 2) + " ms", row.removeFromLeft (cols[6]), juce::Justification::centredLeft);
        }

        r.removeFromTop (6);
        if (r.getHeight() >= 20)
        {
            const auto& c = proc.checks;
            juce::String t = "Loopback: ";
            if (c.loopbackDone) t << juce::String (c.loopbackLatencyMs, 1) << " ms, " << (c.loopbackInverted ? "INVERTED" : "polarity normal");
            else t << (c.loopbackSkipped ? "skipped" : "not checked");
            t << "     Noise floor: ";
            if (c.noiseDone)
            {
                float worst = -200.0f;
                for (int m = 0; m < c.noiseMics; ++m) worst = juce::jmax (worst, c.noiseA[(size_t) m]);
                t << juce::String (worst, 1) << (c.noiseIsSpl[0] ? " dBA" : " dBFS(A)");
            }
            else t << "not measured";
            t << "     Traces: " << traceCount (cur);
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawText (t, r.removeFromTop (20), juce::Justification::centredLeft, true);
        }
    }

    void setupButton (juce::TextButton& b, std::function<void()> f, const juce::String& tip)
    {
        b.onClick = std::move (f);
        b.setTooltip (tip);
        addAndMakeVisible (b);
    }

    void changed() { updateButtons(); repaint(); if (onChanged) onChanged(); }

    void toggleCompare()
    {
        if (proc.traces.hasComparisons()) proc.traces.clearComparisons();
        else proc.compareWithSession (selected);
        changed();
    }

    void updateButtons()
    {
        {
            // before a venue is open: the venue list and its buttons only
            const bool on = started();
            for (auto* b : { &newSessionButton, &openSessionButton, &compareButton, &renameButton, &deleteButton, &goButton, &speakerChangedButton })
                if (! on) b->setVisible (false);
            if (on) for (auto* b : { &newSessionButton, &openSessionButton, &compareButton, &renameButton, &deleteButton, &speakerChangedButton }) b->setVisible (true);
            if (! on) recheckButton.setVisible (false);
            saveButton.setEnabled (on);
            saveAsButton.setEnabled (on);
        }
        const int n = (int) proc.getSessions().size();
        selected = juce::jlimit (0, n - 1, selected);
        const bool other = selected != proc.currentSession();
        openSessionButton.setEnabled (other);
        compareButton.setEnabled (other || proc.traces.hasComparisons());
        compareButton.setButtonText (proc.traces.hasComparisons() ? "Stop compare" : "Compare");
        deleteButton.setEnabled (n > 1);
        speakerChangedButton.setButtonText (proc.speakers.changed ? "Changed: undo" : "Mark as changed");
    }

    int traceCount (int idx) const
    {
        if (idx == proc.currentSession())
        {
            int n = 0;
            for (const auto& t : proc.traces.all()) n += t.fromSession.isEmpty() ? 1 : 0;
            return n;
        }
        const auto& s = proc.getSessions()[(size_t) idx];
        if (s.body == nullptr) return 0;
        if (const auto* t = s.body->getChildByName ("Traces")) return t->getNumChildElements();
        return 0;
    }

    void timerCallback() override
    {
        updateButtons();
        repaint (sessionsCard);
        repaint (summaryArea);
        if (isShowing())
        {
            const bool u = proc.projectChanged();
            if (u != unsavedNow) { unsavedNow = u; repaint (venueCard); }
        }
        // the folder may change behind our back (a venue copied in): look again now and then
        if (isShowing() && ++scanTick % 40 == 0) rescanVenues();
    }

    int visibleRows() const { return juce::jmax (1, listArea.getHeight() / 34); }
    juce::Rectangle<int> rowBounds (int i) const { return listArea.withHeight (30).translated (0, i * 34); }

    LiveAlignProcessor& proc;
    juce::TextEditor searchEditor;
    std::vector<VenueEntry> allVenues, shownVenues;
    juce::Rectangle<int> venueListArea;
    int venueScroll = 0, hoverVenue = -1, scanTicket = 0, scanTick = 0;
    bool unsavedNow = false;
    juce::TextButton newVenueButton { "New venue..." }, openButton { "Open..." }, saveButton { "Save" }, saveAsButton { "Save as..." },
        newSessionButton { "New session" }, openSessionButton { "Open" }, compareButton { "Compare" }, renameButton { "Rename" },
        deleteButton { "Delete" }, speakerChangedButton { "Mark as changed" }, goButton, recheckButton { "Quick re-check..." };
    juce::Rectangle<int> nextBanner, attentionLine;
    int nextPage = -1;
    juce::String nextWhat;
    juce::Rectangle<int> summaryArea;
    juce::Rectangle<int> venueCard, sessionsCard, stepsCard, venueFileLine, sessionsHint, listArea, changeLine;
    std::vector<std::pair<int, juce::Rectangle<int>>> rows, headings;   // (page or heading, bounds)
    std::array<StepInfo, kNumPages> steps {};
    bool changedSetup = false;
    int selected = 0, scroll = 0, hoverStep = -1;
};
} // namespace la::ui
