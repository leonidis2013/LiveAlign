// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Reports: the tuning report and the live monitoring report. Pick the chapters to include (remembered on this
// computer), who prepared it, and export the PDF.
#include <functional>
#include "NavRail.h"
#include "../PluginProcessor.h"
#include "../UpdateChecker.h"
#include "../Report/MonitoringReport.h"

namespace la::ui
{
class ReportPage : public juce::Component
{
public:
    std::function<void()> onExport;

    ReportPage (LiveAlignProcessor& p, bool monitoringReport) : proc (p), monitoring (monitoringReport)
    {
        if (monitoring) for (const auto& s : report::monitorSections()) sections.push_back ({ s.id, s.title, s.what });
        else for (const auto& s : report::tuningSections()) sections.push_back ({ s.id, s.title, s.what });
        for (size_t i = 0; i < sections.size(); ++i)
        {
            auto t = std::make_unique<juce::ToggleButton>();
            t->onClick = [this] { saveSkip(); repaint(); };
            addAndMakeVisible (*t);
            toggles.push_back (std::move (t));
        }
        authorEditor.setFont (font (13.0f));
        authorEditor.setTextToShowWhenEmpty ("Your name or company", pal().textFaint);
        authorEditor.onTextChange = [this] { UserPrefs::set ("reportAuthor", authorEditor.getText().trim()); };
        addAndMakeVisible (authorEditor);
        asFoundToggle.setButtonText ("Show the settings as found beside as left");
        asFoundToggle.onClick = [this] { UserPrefs::set ("reportAsFound", asFoundToggle.getToggleState() ? "1" : "0"); };
        asFoundToggle.setVisible (! monitoring);
        addChildComponent (asFoundToggle);
        exportButton.setButtonText (monitoring ? "Export the monitoring report (PDF)..." : "Export the tuning report (PDF)...");
        exportButton.onClick = [this] { if (onExport) onExport(); };
        addAndMakeVisible (exportButton);
        refresh();
    }

    report::ReportOptions options() const
    {
        report::ReportOptions o;
        for (size_t i = 0; i < sections.size(); ++i) if (! toggles[i]->getToggleState()) o.skip.add (sections[i].id);
        o.asFoundLeft = asFoundToggle.getToggleState();
        o.author = authorEditor.getText().trim();
        return o;
    }

    void refresh()
    {
        juce::StringArray skip;
        skip.addTokens (UserPrefs::get (monitoring ? "monitorReportSkip" : "tuningReportSkip"), ",", {});
        for (size_t i = 0; i < sections.size(); ++i) toggles[i]->setToggleState (! skip.contains (sections[i].id), juce::dontSendNotification);
        authorEditor.setText (UserPrefs::get ("reportAuthor"), juce::dontSendNotification);
        asFoundToggle.setToggleState (UserPrefs::get ("reportAsFound", "1") == "1", juce::dontSendNotification);
        lookAndFeelChanged();
        repaint();
    }

    void lookAndFeelChanged() override
    {
        exportButton.setColour (juce::TextButton::buttonColourId, pal().accent);
        exportButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, listCard.toFloat(), "Chapters");
        drawPanel (g, optCard.toFloat(), "Report");
        for (size_t i = 0; i < sections.size(); ++i)
        {
            auto r = rowBounds ((int) i);
            g.setColour (p.panelRaised);
            g.fillRoundedRectangle (r.toFloat(), 6.0f);
            r.removeFromLeft (40);
            const auto status = statusOf (sections[i].id);
            auto right = r.removeFromRight (220);
            g.setColour (p.text);
            g.setFont (font (13.5f, true));
            g.drawText (juce::String ((int) i + 1).paddedLeft ('0', 2) + "   " + sections[i].title, r.removeFromTop (r.getHeight() / 2).withTrimmedTop (4),
                        juce::Justification::bottomLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawText (sections[i].what, r.withTrimmedTop (2), juce::Justification::topLeft, true);
            g.setColour (status.first ? p.good : p.textFaint);
            g.setFont (font (11.5f, status.first));
            g.drawFittedText (status.second, right.reduced (8, 0), juce::Justification::centredRight, 2);
        }
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("PREPARED BY", authorLabel, juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (12.0f));
        g.drawFittedText (monitoring
                              ? "The sound level of the event against the limit, the monitoring mic and its calibration, where it was, and the traces "
                                "captured during the event. The cover lists the chapters; pages are numbered \"page x of y\"."
                              : "The system as found and as left, the venue plan on its own landscape page, the speakers and processor settings, "
                                "room EQ, the feedback test, the room's acoustics, the mics and the measurements. The cover lists the chapters; "
                                "pages are numbered \"page x of y\".",
                          infoArea, juce::Justification::topLeft, 6);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        optCard = r.removeFromRight (380);
        r.removeFromRight (10);
        listCard = r;
        for (size_t i = 0; i < toggles.size(); ++i)
        {
            auto row = rowBounds ((int) i);
            toggles[i]->setBounds (row.removeFromLeft (40).withSizeKeepingCentre (26, 26));
        }
        auto c = optCard.reduced (14, 0).withTrimmedTop (40);
        authorLabel = c.removeFromTop (18);
        authorEditor.setBounds (c.removeFromTop (30));
        c.removeFromTop (12);
        asFoundToggle.setBounds (c.removeFromTop (28));
        c.removeFromTop (12);
        exportButton.setBounds (c.removeFromTop (40));
        c.removeFromTop (14);
        infoArea = c.removeFromTop (110);
    }

private:
    struct Sec { juce::String id, title, what; };

    juce::Rectangle<int> rowBounds (int i) const { return listCard.reduced (14, 0).withTrimmedTop (40).withHeight (58).translated (0, i * 64); }

    void saveSkip()
    {
        juce::StringArray skip;
        for (size_t i = 0; i < sections.size(); ++i) if (! toggles[i]->getToggleState()) skip.add (sections[i].id);
        UserPrefs::set (monitoring ? "monitorReportSkip" : "tuningReportSkip", skip.joinIntoString (","));
    }

    // h:mm, e.g. 0:07 or 1:32
    static juce::String hoursMinutes (double seconds)
    {
        const int m = juce::jmax (0, juce::roundToInt (seconds / 60.0));
        return juce::String (m / 60) + ":" + juce::String (m % 60).paddedLeft ('0', 2);
    }

    // Whether there is anything for a chapter yet, and a word about it.
    std::pair<bool, juce::String> statusOf (const juce::String& id) const
    {
        const auto& room = proc.room;
        if (id == "summary") return { true, proc.asFound.taken() ? "As found " + proc.asFound.takenOn : juce::String ("Nothing changed yet") };
        if (id == "plan") return { true, proc.plan.drawn() ? juce::String ("Sketched") : juce::String ("The default sketch") };
        if (id == "speakers")
        {
            int measured = 0;
            for (int i = 0; i < (int) proc.system.sources.size(); ++i) measured += proc.system.covered (i) ? 1 : 0;
            return { measured > 0, juce::String (measured) + " of " + juce::String ((int) proc.system.sources.size()) + " measured" };
        }
        if (id == "room") return { room.count (false) > 0, room.count (false) > 0 ? juce::String (room.count (false)) + " positions" + (room.count (true) > 0 ? ", after EQ too" : "")
                                                                                  : juce::String ("Not measured") };
        if (id == "feedback") return { ! proc.feedback.rings.empty(), proc.feedback.rings.empty() ? juce::String ("Not done") : juce::String ((int) proc.feedback.rings.size()) + " rings" };
        if (id == "acoustics") return { room.acousticsCount() > 0, room.acousticsCount() > 0 ? juce::String (room.acousticsCount()) + " positions" : juce::String ("From Room EQ") };
        if (id == "mics") return { true, juce::String (proc.tuningMicCount()) + (proc.tuningMicCount() == 1 ? " mic" : " mics") };
        if (id == "measurements" || id == "traces")
        {
            int n = 0;
            for (const auto& t : proc.traces.all()) n += t.fromSession.isEmpty() ? 1 : 0;
            return { n > 0, juce::String (n) + (n == 1 ? " trace" : " traces") };
        }
        if (id == "level") return { ! proc.splLog.empty(), proc.splLog.empty() ? juce::String ("Not logged yet")
                                                                                : hoursMinutes (proc.splLog.end() - proc.splLog.start()) + " logged" };
        if (id == "setup") return { proc.monitorMic.cal.any() || proc.monitorMic.splCalibrated, proc.monitorMic.name };
        return { true, {} };
    }

    LiveAlignProcessor& proc;
    const bool monitoring;
    std::vector<Sec> sections;
    std::vector<std::unique_ptr<juce::ToggleButton>> toggles;
    juce::TextEditor authorEditor;
    juce::ToggleButton asFoundToggle;
    juce::TextButton exportButton;
    juce::Rectangle<int> listCard, optCard, authorLabel, infoArea;
};
} // namespace la::ui
