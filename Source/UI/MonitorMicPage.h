// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Monitoring: the monitor mic. Monitoring has different needs from tuning (one mic, usually at FOH, left there for
// the event), so it has its own mic: input, calibration files, sound level calibration and delay, kept with the venue
// and apart from the tuning mics. Also what the event is monitored against: the target curve and a sound level limit.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "../PluginProcessor.h"
#include "../Analysis/TargetCurves.h"

namespace la::ui
{
class MonitorMicPage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onChanged, onCalibrate;
    std::function<void (int angle)> onLoadCal;   // -1: one or both files, by their names

    explicit MonitorMicPage (LiveAlignProcessor& p) : proc (p), settings (p.settings), mic (p.monitorMic)
    {
        nameEditor.setFont (font (13.0f));
        addAndMakeVisible (nameEditor);
        nameEditor.onTextChange = [this] { mic.name = nameEditor.getText().trim(); mic.configured = true; changed(); };
        posBox.setTextWhenNothingSelected ("Pick a point");
        posBox.setTooltip ("Where the monitor mic stands: a point on the venue plan (usually FOH). Points are made on the venue plan.");
        posBox.onChange = [this]
        {
            const int id = posBox.getSelectedId();
            if (id > 0 && id - 1 < proc.positions.size()) { mic.position = proc.positions[id - 1]; mic.configured = true; changed(); }
        };
        addAndMakeVisible (posBox);

        sourceBox.setTextWhenNothingSelected ("Pick a mic...");
        sourceBox.setTooltip ("A mic from your mic locker (its calibration comes with it), or one of the tuning mics");
        sourceBox.onChange = [this] { pickSource(); };
        addAndMakeVisible (sourceBox);

        inputBox.setTooltip ("The interface input the monitor mic is on");
        inputBox.onChange = [this] { mic.input = juce::jmax (0, inputBox.getSelectedItemIndex()); mic.configured = true; proc.applyMonitorMic(); changed(); };
        addAndMakeVisible (inputBox);

        calButton.onClick = [this] { showCalMenu(); };
        calButton.setTooltip ("Load, clear or check the calibration files (0 and 90 degrees)");
        addAndMakeVisible (calButton);
        fillAimBox (aimBox);
        aimBox.setTooltip ("How the mic is aimed: pointing up (90 degrees) is usual at FOH; it picks which calibration file is used. "
                           "Only the angles this mic has a file for.");
        aimBox.onChange = [this] { mic.aim = aimFromBox (aimBox); mic.configured = true; proc.applyMonitorMic(); changed(); };
        addAndMakeVisible (aimBox);

        levelButton.onClick = [this] { if (onCalibrate) onCalibrate(); };
        levelButton.setTooltip ("Calibrate the sound level with an acoustic calibrator, or confirm it for this event");
        addAndMakeVisible (levelButton);
        delayButton.onClick = [this]
        {
            settings.requestFindDelay = true;
            note = "Finding the delay: play programme or pink noise through the PA.";
            repaint();
        };
        delayButton.setTooltip ("Find the delay between the reference (the console feed) and the mic, so the transfer function lines up");
        addAndMakeVisible (delayButton);

        for (int i = 0; i < targets::count(); ++i) targetBox.addItem (targets::list()[(size_t) i].name, i + 1);
        targetBox.onChange = [this] { settings.targetCurve = targetBox.getSelectedItemIndex(); settings.targetAnchorMask = 0; changed(); };
        targetBox.setTooltip ("The target the live transfer function and RTA are compared with");
        addAndMakeVisible (targetBox);
        sameAsRoomButton.onClick = [this] { settings.targetCurve = proc.room.target; settings.targetAnchorMask = 0; refresh(); changed(); };
        sameAsRoomButton.setTooltip ("Use the target the room was tuned to (Room EQ)");
        addAndMakeVisible (sameAsRoomButton);

        // A-weighted limits (LAeq), then C-weighted ones (LCeq, the bass included)
        limitBox.addItem ("No limit", 1);
        limitBox.addSectionHeading ("A-weighted (LAeq)");
        for (int l = 75; l <= 110; ++l) limitBox.addItem (juce::String (l) + " dBA", l + 1);
        limitBox.addSectionHeading ("C-weighted (LCeq)");
        for (int l = 85; l <= 125; ++l) limitBox.addItem (juce::String (l) + " dBC", 1000 + l);
        limitBox.setTooltip ("A sound level limit, as an LAeq (or LCeq) over the window beside it: the monitoring report shows the time over it. "
                             "The default for new sessions is in Settings, Monitoring.");
        limitBox.onChange = [this]
        {
            const int id = limitBox.getSelectedId();
            proc.splLog.limitC = id >= 1000;
            proc.splLog.limitLaeq = id >= 1000 ? id - 1000 : juce::jmax (0, id - 1);
            changed();
        };
        addAndMakeVisible (limitBox);
        windowBox.addItem ("over 1 minute", 1);
        windowBox.addItem ("over 5 minutes", 5);
        windowBox.addItem ("over 15 minutes", 15);
        windowBox.addItem ("over 10 minutes", 10);
        windowBox.addItem ("over 30 minutes", 30);
        windowBox.addItem ("over 60 minutes", 60);
        windowBox.onChange = [this] { proc.splLog.limitMinutes = juce::jmax (1, windowBox.getSelectedId()); changed(); };
        addAndMakeVisible (windowBox);
        clearLogButton.onClick = [this] { proc.splLog.clear(); changed(); };
        clearLogButton.setTooltip ("Start the sound level log again (for a new event in this session)");
        addAndMakeVisible (clearLogButton);

        refresh();
        startTimerHz (15);
    }

    ~MonitorMicPage() override { stopTimer(); }

    void setSnapshot (const Snapshot& s) { snap = &s; }

    void refresh()
    {
        proc.syncMonitorFromSlot();
        if (! nameEditor.hasKeyboardFocus (true)) nameEditor.setText (mic.name, juce::dontSendNotification);
        if (! posBox.isPopupActive())
        {
            posBox.clear (juce::dontSendNotification);
            for (int i = 0; i < proc.positions.size(); ++i) posBox.addItem (proc.positions[i], i + 1);
            const int at = proc.positions.indexOf (mic.position);
            if (at >= 0) posBox.setSelectedId (at + 1, juce::dontSendNotification);
            else posBox.setText (mic.position, juce::dontSendNotification);
        }
        sourceBox.clear (juce::dontSendNotification);
        int id = 1;
        sourceIds.clear();
        for (const auto& l : proc.locker.mics) { sourceBox.addItem ("Mic locker: " + l.label(), id++); sourceIds.add ("L" + l.id); }
        const int tuningMics = proc.tuningMicCount();
        for (int m = 0; m < tuningMics; ++m) { sourceBox.addItem ("Same as tuning " + proc.mics.nameOf (m), id++); sourceIds.add ("T" + juce::String (m)); }
        const int k = sourceIds.indexOf ("L" + mic.lockerId);
        if (mic.lockerId.isNotEmpty() && k >= 0) sourceBox.setSelectedItemIndex (k, juce::dontSendNotification);
        else sourceBox.setText (mic.name, juce::dontSendNotification);

        inputBox.clear (juce::dontSendNotification);
        for (int i = 0; i < juce::jmax (1, proc.numInputs()); ++i) inputBox.addItem ("Input " + juce::String (i + 1), i + 1);
        inputBox.setSelectedItemIndex (juce::jlimit (0, juce::jmax (0, proc.numInputs() - 1), mic.input), juce::dontSendNotification);
        fillAimBoxFor (aimBox, mic.cal, mic.aim);
        calButton.setButtonText (mic.cal.any() ? mic.cal.label() : mic.noCalFile ? juce::String ("None (flat)") : juce::String ("Load cal file(s)..."));
        levelButton.setButtonText (mic.splCalibrated ? "Calibrated: re-check" : "Calibrate");
        targetBox.setSelectedItemIndex (juce::jlimit (0, targets::count() - 1, settings.targetCurve.load()), juce::dontSendNotification);
        limitBox.setSelectedId (proc.splLog.limitLaeq <= 0.0 ? 1 : proc.splLog.limitC ? 1000 + (int) proc.splLog.limitLaeq : (int) proc.splLog.limitLaeq + 1, juce::dontSendNotification);
        if (limitBox.getSelectedId() == 0) limitBox.setSelectedId (1, juce::dontSendNotification);
        windowBox.setSelectedId (proc.splLog.limitMinutes, juce::dontSendNotification);
        repaint();
    }

    juce::String nextHint() const
    {
        if (! mic.cal.any() && ! mic.noCalFile) return "Load the monitor mic's calibration";
        if (! mic.splCalibrated && ! proc.mics.noCalibrator) return "Calibrate the monitor mic's sound level";
        return {};
    }

    void doNext()
    {
        if (! mic.cal.any() && ! mic.noCalFile) { if (onLoadCal) onLoadCal (-1); }
        else if (! mic.splCalibrated && onCalibrate) onCalibrate();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, micCard.toFloat(), "The monitoring mic");
        drawPanel (g, liveCard.toFloat(), "Live");
        drawPanel (g, eventCard.toFloat(), "The event");
        drawPanel (g, whyCard.toFloat(), "Why a separate mic");
        for (const auto& l : labels)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            g.drawText (l.first.toUpperCase(), l.second, juce::Justification::centredLeft);
        }
        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        juce::String info;
        info << "Sound level: " << (mic.splCalibrated ? juce::String (proc.isMonitorMode() ? settings.micSplOffset[0].load() : mic.splOffset, 2) + " dB at 0 dBFS"
                                                       + (mic.splDate.isNotEmpty() ? " (" + mic.splDate + ")" : juce::String())
                                                     : juce::String ("not calibrated (levels in dBFS)"))
             << "\nDelay: " << juce::String (proc.isMonitorMode() ? settings.micDelayMs[0].load() : mic.delayMs, 2) << " ms";
        if (note.isNotEmpty()) info << "\n" << note;
        g.drawFittedText (info, infoArea, juce::Justification::topLeft, 4);

        // live level and sound level
        {
            auto r = liveCard.reduced (14, 0).withTrimmedTop (38);
            auto meter = r.removeFromTop (14).toFloat();
            g.setColour (p.border);
            g.fillRoundedRectangle (meter, 4.0f);
            const float db = juce::jlimit (-60.0f, 0.0f, peakDb);
            g.setColour (db > -6.0f ? p.bad : db > -20.0f ? p.warn : p.good);
            g.fillRoundedRectangle (meter.withWidth (meter.getWidth() * (db + 60.0f) / 60.0f), 4.0f);
            r.removeFromTop (10);
            const bool live = snap != nullptr && proc.isMonitorMode();
            const auto tiles = r.removeFromTop (64);
            const int tw = (tiles.getWidth() - 16) / 3;
            auto t = tiles;
            drawTile (g, t.removeFromLeft (tw).toFloat(), "LA SLOW", live ? dbText (snap->splAS) : juce::String ("-"), snap != nullptr && snap->splIsSpl ? "dB" : "dBFS", p.text, 22.0f);
            t.removeFromLeft (8);
            drawTile (g, t.removeFromLeft (tw).toFloat(), "LC SLOW", live ? dbText (snap->splCS) : juce::String ("-"), snap != nullptr && snap->splIsSpl ? "dB" : "dBFS", p.text, 22.0f);
            t.removeFromLeft (8);
            const double leq = proc.splLog.leq (false);
            drawTile (g, t.toFloat(), "LAeq (LOGGED)", std::isfinite (leq) ? juce::String (leq, 1) : juce::String ("-"), "dB", p.accent, 22.0f);
            r.removeFromTop (10);
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawFittedText ("Input " + juce::String (mic.input + 1) + (proc.isMonitorMode() ? "" : " (shown while on a Monitoring page)"), r.removeFromTop (20),
                              juce::Justification::topLeft, 1);
        }

        // the event log
        {
            auto r = logInfo;
            const auto& log = proc.splLog;
            juce::String t;
            if (log.empty()) t = "The sound level is logged every 5 seconds while a Monitoring page is open, for the monitoring report.";
            else
            {
                t << "Logged " << juce::Time ((juce::int64) (log.start() * 1000.0)).formatted ("%H:%M") << " - "
                  << juce::Time ((juce::int64) (log.end() * 1000.0)).formatted ("%H:%M") << ": LAeq " << juce::String (log.leq (false), 1)
                  << " dB, LCeq " << juce::String (log.leq (true), 1) << " dB, max LAF " << juce::String (log.maxLaf(), 1) << " dB.";
                if (log.limitLaeq > 0.0) t << "\nOver the limit for " << juce::String (juce::roundToInt (log.minutesOver())) << " min.";
            }
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawFittedText (t, r, juce::Justification::topLeft, 4);
        }

        // why
        g.setColour (p.text);
        g.setFont (font (12.0f));
        g.drawFittedText ("Tuning moves several mics around the room; monitoring keeps one mic at FOH for the whole event. This mic, "
                          "its calibration, sound level and delay are kept apart, so tuning never changes what you monitor with. "
                          "The VST3 in your host uses this mic too.", whyCard.reduced (14, 0).withTrimmedTop (38), juce::Justification::topLeft, 6);
    }

    void resized() override
    {
        labels.clear();
        auto r = getLocalBounds();
        auto left = r.removeFromLeft (juce::jmin (520, r.getWidth() / 2));
        r.removeFromLeft (10);
        micCard = left;
        liveCard = r.removeFromTop (190);
        r.removeFromTop (10);
        eventCard = r.removeFromTop (260);
        r.removeFromTop (10);
        whyCard = r;
        {
            auto c = micCard.reduced (14, 0).withTrimmedTop (40);
            auto row = [&] (const juce::String& name, juce::Component& comp, juce::Component* second = nullptr)
            {
                auto rr = c.removeFromTop (30);
                labels.push_back ({ name, rr.removeFromLeft (120) });
                if (second != nullptr)
                {
                    second->setBounds (rr.removeFromRight (rr.getWidth() / 2 - 4));
                    rr.removeFromRight (8);
                }
                comp.setBounds (rr);
                c.removeFromTop (10);
            };
            row ("Mic", sourceBox);
            row ("Name", nameEditor);
            row ("Position", posBox);
            row ("Input", inputBox);
            row ("Calibration", calButton);
            row ("Aim", aimBox);
            row ("Sound level", levelButton, &delayButton);
            c.removeFromTop (6);
            infoArea = c.removeFromTop (80);
        }
        {
            auto c = eventCard.reduced (14, 0).withTrimmedTop (40);
            auto row = [&] (const juce::String& name, juce::Component& comp, juce::Component* second, int secondW)
            {
                auto rr = c.removeFromTop (30);
                labels.push_back ({ name, rr.removeFromLeft (110) });
                if (second != nullptr) { second->setBounds (rr.removeFromRight (secondW)); rr.removeFromRight (8); }
                comp.setBounds (rr);
                c.removeFromTop (10);
            };
            row ("Target", targetBox, &sameAsRoomButton, 150);
            row ("Level limit", limitBox, &windowBox, 160);
            auto lr = c.removeFromTop (30);
            clearLogButton.setBounds (lr.removeFromRight (150));
            c.removeFromTop (8);
            logInfo = c;
        }
    }

private:
    void pickSource()
    {
        const int i = sourceBox.getSelectedItemIndex();
        if (i < 0 || i >= sourceIds.size()) return;
        const auto id = sourceIds[i];
        if (id.startsWith ("L")) proc.assignMonitorFromLocker (id.substring (1));
        else
        {
            proc.takeMonitorMicFromTuning (id.substring (1).getIntValue());
            mic.configured = true;
        }
        refresh();
        changed();
    }

    void showCalMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, "Load file(s)...  (0 and 90 degrees, by their names)");
        m.addItem (2, "Load the 0 degree file...");
        m.addItem (3, "Load the 90 degree file...");
        m.addSeparator();
        m.addItem (4, "Clear the files", mic.cal.any());
        m.addItem (5, "This mic has no calibration file", true, mic.noCalFile);
        juce::Component::SafePointer<MonitorMicPage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&calButton), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& mm = safe->mic;
            if (r <= 3) { if (safe->onLoadCal) safe->onLoadCal (r == 1 ? -1 : r == 2 ? Cal0 : Cal90); return; }
            if (r == 4) mm.cal.clearAll();
            if (r == 5) mm.noCalFile = ! mm.noCalFile;
            mm.configured = true;
            safe->proc.applyMonitorMic();
            safe->refresh();
            safe->changed();
        });
    }

    void changed()
    {
        if (onChanged) onChanged();
        repaint();
    }

    void timerCallback() override
    {
        if (proc.isMonitorMode())
        {
            const float pk = proc.takeMicPeak (0);
            peakDb = juce::jmax (peakDb - 1.5f, pk > 0.0f ? juce::Decibels::gainToDecibels (pk) : -100.0f);
            const auto rep = proc.analyzer.readDelayReport();
            if (rep.serial != delaySerial)
            {
                if (delaySerial != 0 && rep.micCount > 0)
                    note = rep.valid[0] ? "Delay found: " + juce::String (rep.ms[0], 2) + " ms." : juce::String ("No clear delay: play more through the PA and try again.");
                delaySerial = rep.serial;
                proc.syncMonitorFromSlot();
            }
        }
        repaint (liveCard);
        repaint (infoArea);
    }

    LiveAlignProcessor& proc;
    Settings& settings;
    MonitorMic& mic;
    const Snapshot* snap = nullptr;
    juce::TextEditor nameEditor;
    juce::ComboBox posBox;
    juce::ComboBox sourceBox, inputBox, aimBox, targetBox, limitBox, windowBox;
    juce::TextButton calButton, levelButton { "Calibrate" }, delayButton { "Find delay" }, sameAsRoomButton { "Same as Room EQ" },
        clearLogButton { "Clear the log" };
    juce::StringArray sourceIds;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
    juce::Rectangle<int> micCard, liveCard, eventCard, whyCard, infoArea, logInfo;
    juce::String note;
    float peakDb = -100.0f;
    juce::uint64 delaySerial = 0;
};
} // namespace la::ui
