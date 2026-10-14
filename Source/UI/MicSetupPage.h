// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Step 1: Mic setup. A table of up to eight measurement mics (input, position, calibration file, sound level
// calibration, live level), a checklist of the steps that turns green as they are done, and a live RTA of every mic.
#include <functional>
#include "Views.h"
#include "NavRail.h"
#include "../PluginProcessor.h"
#include "../StandaloneHooks.h"

namespace la::ui
{
enum class StepState { Todo, Done, Warn, Skipped };

// One numbered step: status badge, title, explanation and a result line; its controls sit on the right.
class StepCard : public juce::Component
{
public:
    StepCard (int number, juce::String titleText, juce::String helpText)
        : num (number), title (std::move (titleText)), help (std::move (helpText)) {}

    void set (StepState s, const juce::String& resultText)
    {
        if (s == state && resultText == result) return;
        state = s;
        result = resultText;
        repaint();
    }

    StepState getState() const { return state; }
    const juce::String& getTitle() const { return title; }
    bool isComplete() const { return state != StepState::Todo; }
    juce::Rectangle<int> controlArea() const
    {
        if (compact) return getLocalBounds().withTrimmedLeft (48).withTrimmedRight (12).removeFromBottom (42).withSizeKeepingCentre (getWidth() - 60, 28);
        return getLocalBounds().reduced (12, 0).removeFromRight (kControlsW).withSizeKeepingCentre (kControlsW, 28);
    }
    // Narrow: the controls go under the text instead of beside it.
    void setCompact (bool c) { if (c != compact) { compact = c; repaint(); } }
    int preferredHeight() const { return compact ? (hasControls ? kCompactHeight : kCompactHeight - 34) : kHeight; }
    void setHasControls (bool h) { hasControls = h; }

    // Done: green.  Required but not done: amber border.  A problem: red.  Not used (opted out): greyed out.
    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto r = getLocalBounds().toFloat();
        const bool off = state == StepState::Skipped;
        g.setColour (p.panel);
        g.fillRoundedRectangle (r, 8.0f);
        juce::Colour edge = p.border;
        float width = 1.0f;
        if (state == StepState::Done) { edge = p.good.withAlpha (0.6f); width = 1.4f; }
        else if (state == StepState::Warn) { edge = p.bad.withAlpha (0.8f); width = 1.6f; }
        else if (state == StepState::Todo) { edge = p.warn.withAlpha (0.65f); width = 1.4f; }
        g.setColour (edge);
        g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, width);

        const auto badge = juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ 26.0f, compact ? 22.0f : r.getCentreY() });
        if (state == StepState::Done) drawStatusBadge (g, badge, StepStatus::Ready);
        else if (state == StepState::Warn)
        {
            g.setColour (p.bad);
            g.fillEllipse (badge);
            g.setColour (p.onAccent);
            g.setFont (font (15.0f, true));
            g.drawText ("!", badge, juce::Justification::centred);
        }
        else
        {
            g.setColour (off ? p.textFaint : p.warn);
            g.drawEllipse (badge.reduced (1.0f), 1.4f);
            g.setFont (font (11.5f, true));
            g.drawText (off ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) : juce::String (num), badge, juce::Justification::centred);
        }

        auto text = compact ? getLocalBounds().withTrimmedLeft (48).withTrimmedRight (12).withTrimmedBottom (hasControls ? 42 : 8).reduced (0, 8)
                            : getLocalBounds().withTrimmedLeft (48).withTrimmedRight (kControlsW + 22).reduced (0, 8);
        g.setColour (off ? p.textFaint : p.text);
        g.setFont (font (14.0f, true));
        g.drawText (title + (off ? "   (not used)" : ""), text.removeFromTop (20), juce::Justification::centredLeft, true);
        g.setColour (off ? p.textFaint : p.textDim);
        g.setFont (font (11.5f));
        g.drawText (help, text.removeFromTop (18), juce::Justification::centredLeft, true);
        juce::Colour rc = p.textDim;
        if (state == StepState::Done) rc = p.good;
        else if (state == StepState::Warn) rc = p.bad;
        else if (state == StepState::Todo) rc = p.warn;
        else rc = p.textFaint;
        g.setColour (rc);
        g.setFont (font (11.5f, state != StepState::Skipped));
        g.drawFittedText (result, text, juce::Justification::centredLeft, compact ? 2 : 1, 1.0f);
    }

    static constexpr int kHeight = 78, kCompactHeight = 118, kControlsW = 300;

private:
    int num;
    juce::String title, help, result;
    StepState state = StepState::Todo;
    bool compact = false, hasControls = true;
};

//==============================================================================
// One row of the mic table.
class MicRow : public juce::Component
{
public:
    std::function<void()> onChanged, onRemove, onCalibrate, onCalFileMenu, onLockerMenu;

    MicRow (LiveAlignProcessor& p, int micIndex) : proc (p), index (micIndex)
    {
        nameEditor.setFont (font (13.0f));
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setIndents (8, 0);
        nameEditor.setTextToShowWhenEmpty ("Mic " + juce::String (index + 1), pal().textFaint);
        nameEditor.onTextChange = [this] { proc.mics.name[(size_t) index] = nameEditor.getText().trim(); changed(); };
        addAndMakeVisible (nameEditor);

        inputBox.onChange = [this]
        {
            proc.settings.micInput[index] = inputBox.getSelectedItemIndex();
            proc.settings.requestResetAverages = true;
            changed();
        };
        inputBox.setTooltip ("The interface input this mic is plugged into");
        addAndMakeVisible (inputBox);

        positionBox.setEditableText (true);
        positionBox.setTextWhenNothingSelected ("Not attached");
        positionBox.setTooltip ("The position this mic is attached to now. It uses that position's aim (and so its calibration "
                                "file); Room EQ moves it between positions as it measures.");
        positionBox.onChange = [this]
        {
            proc.setMicPosition (index, positionBox.getText());
            changed();
        };
        addAndMakeVisible (positionBox);

        lockerButton.onClick = [this] { if (onLockerMenu) onLockerMenu(); };
        addAndMakeVisible (lockerButton);
        calFileButton.onClick = [this] { if (onCalFileMenu) onCalFileMenu(); };
        addAndMakeVisible (calFileButton);
        splButton.onClick = [this] { if (onCalibrate) onCalibrate(); };
        addAndMakeVisible (splButton);
        removeButton.setTooltip ("Remove this mic");
        removeButton.onClick = [this] { if (onRemove) onRemove(); };
        addAndMakeVisible (removeButton);
        refresh();
    }

    juce::Component& calibrateButton() { return splButton; }
    juce::Component& calFileComponent() { return calFileButton; }
    juce::Component& lockerComponent() { return lockerButton; }

    void refresh()
    {
        const int nIn = juce::jmax (1, proc.numInputs());
        if (inputBox.getNumItems() != nIn)
        {
            inputBox.clear (juce::dontSendNotification);
            for (int i = 0; i < nIn; ++i) inputBox.addItem ("Input " + juce::String (i + 1), i + 1);
        }
        inputBox.setSelectedItemIndex (juce::jlimit (0, nIn - 1, proc.settings.micInput[index].load()), juce::dontSendNotification);

        if (! nameEditor.hasKeyboardFocus (true)) nameEditor.setText (proc.mics.name[(size_t) index], juce::dontSendNotification);

        if (! positionBox.hasKeyboardFocus (true) && ! positionBox.isPopupActive())
        {
            positionBox.clear (juce::dontSendNotification);
            for (int i = 0; i < proc.positions.size(); ++i) positionBox.addItem (proc.positions[i], i + 1);
            positionBox.setText (proc.mics.position[(size_t) index], juce::dontSendNotification);
        }

        const auto& cs = proc.calibrations (index);
        calFileButton.setButtonText (cs.any() ? cs.label() : (proc.mics.noCalFile[(size_t) index] ? "No file (flat)" : "Load cal file(s)..."));
        const int active = proc.activeCalAngle (index);
        calFileButton.setTooltip (cs.any() ? "Calibration: " + cs.names() + ". In use now: " + calAngleName (active) + "."
                                           : "Load this mic's calibration file(s): select the 0 and 90 degree files together if you have both");

        const auto* linked = proc.locker.find (proc.mics.lockerId[(size_t) index]);
        lockerButton.setColour (juce::TextButton::buttonColourId, linked != nullptr ? pal().accent.withAlpha (0.35f) : pal().panelRaised);
        lockerButton.setButtonText (linked != nullptr ? "Linked" : "Locker");
        lockerButton.setTooltip (linked != nullptr ? "From the mic locker: " + linked->label() + ". Its calibration is kept in sync with the locker."
                                                   : "Pick one of your mics from the mic locker (name, serial, calibration files, sound level), or save this one to it");

        const bool calibrated = proc.settings.micSplCalibrated[index].load();
        const bool verified = proc.checks.splVerified[(size_t) index];
        if (proc.mics.noCalibrator) splButton.setButtonText ("dBFS (no calibrator)");
        else if (! calibrated) splButton.setButtonText ("Calibrate...");
        else if (verified) splButton.setButtonText ("Calibrated");
        else splButton.setButtonText ("Re-check level");
        splButton.setColour (juce::TextButton::buttonColourId,
                             ! proc.mics.noCalibrator && calibrated && ! verified ? pal().warn.withAlpha (0.35f) : pal().panelRaised);
        removeButton.setVisible (proc.settings.micCount.load() > 1);
        repaint();
    }

    void setLevel (float peak, float rms)
    {
        const float pk = 20.0f * std::log10 (juce::jmax (1e-6f, peak));
        peakDb = juce::jmax (pk, peakDb - 1.5f);
        rmsDb = rms;
        repaint (meterArea);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds();
        g.setColour (micColour (index));
        g.fillRoundedRectangle (r.removeFromLeft (4).toFloat().reduced (0.0f, 4.0f), 2.0f);
        g.setColour (p.textDim);
        g.setFont (monoFont (12.0f, true));
        g.drawText (juce::String (index + 1), r.removeFromLeft (22), juce::Justification::centred);

        auto bar = meterArea.toFloat().withSizeKeepingCentre ((float) meterArea.getWidth() - 46.0f, 8.0f).withX ((float) meterArea.getX());
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (bar, 3.0f);
        auto pos = [] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour (micColour (index).withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pos (rmsDb)), 3.0f);
        g.setColour (peakDb > -1.0f ? p.bad : p.text);
        g.fillRect (bar.getX() + bar.getWidth() * pos (peakDb) - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
        g.setColour (p.textDim);
        g.setFont (monoFont (11.0f));
        g.drawText (dbText (rmsDb, 0, -99.0f), meterArea.withTrimmedLeft (meterArea.getWidth() - 42), juce::Justification::centredRight);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (30).reduced (0, 3);
        removeButton.setBounds (r.removeFromRight (30));
        r.removeFromRight (8);
        const int w = r.getWidth();
        auto take = [&r] (int px) { auto a = r.removeFromLeft (px); r.removeFromLeft (8); return a; };
        const auto cols = columnWidths (w);
        {
            auto nameCol = take (cols[0]);
            lockerButton.setBounds (nameCol.removeFromRight (64));
            nameCol.removeFromRight (4);
            nameEditor.setBounds (nameCol);
        }
        inputBox.setBounds (take (cols[1]));
        positionBox.setBounds (take (cols[2]));
        calFileButton.setBounds (take (cols[3]));
        splButton.setBounds (take (cols[4]));
        meterArea = r;
    }

    // Column positions for the table header.
    static juce::StringArray columnNames() { return { "Mic", "Input", "Attached to", "Calibration files", "Sound level", "Level" }; }
    static std::array<int, 5> columnWidths (int w) { return { w * 23 / 100, w * 11 / 100, w * 15 / 100, w * 17 / 100, w * 14 / 100 }; }

private:
    void changed() { if (onChanged) onChanged(); }

    LiveAlignProcessor& proc;
    const int index;
    juce::TextEditor nameEditor;
    juce::ComboBox inputBox, positionBox;
    juce::TextButton lockerButton { "Locker" }, calFileButton, splButton, removeButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
    juce::Rectangle<int> meterArea;
    float peakDb = -120.0f, rmsDb = -120.0f;
};

//==============================================================================
// One listening position: its name, how a mic there is aimed, and the mic attached to it (one of this venue's mics,
// or one from the mic locker, which then joins the venue's mics).
class PositionRow : public juce::Component
{
public:
    std::function<void()> onChanged, onRemove, onMicsChanged;
    std::function<int()> maxMics;

    PositionRow (LiveAlignProcessor& p, const juce::String& positionName, int row) : proc (p), name (positionName), index (row)
    {
        nameEditor.setFont (font (13.0f, true));
        nameEditor.setJustification (juce::Justification::centredLeft);
        nameEditor.setIndents (8, 0);
        nameEditor.setText (name, juce::dontSendNotification);
        nameEditor.onReturnKey = [this] { commitName(); };
        nameEditor.onFocusLost = [this] { commitName(); };
        nameEditor.setTooltip ("The position's name (FOH, Row 8, Balcony ...). Renaming it renames it everywhere");
        addAndMakeVisible (nameEditor);
        fillAimBox (aimBox);
        aimBox.setTooltip ("How a mic at this position is aimed, which picks its calibration file: at the speaker (0 degrees), "
                           "pointing up at the ceiling (90 degrees, usual in the seats), or an angle in between");
        aimBox.onChange = [this] { proc.setPositionAim (name, aimFromBox (aimBox)); if (onChanged) onChanged(); };
        addAndMakeVisible (aimBox);
        micBox.setTooltip ("The mic at this position: one of this venue's mics, or one from your mic locker");
        micBox.onChange = [this] { attach(); };
        addAndMakeVisible (micBox);
        removeButton.setTooltip ("Remove this position everywhere (Room EQ, the venue plan) with its measurements");
        removeButton.onClick = [this] { if (onRemove) onRemove(); };
        addAndMakeVisible (removeButton);
        refresh();
    }

    const juce::String& positionName() const { return name; }

    void refresh()
    {
        const int aim = proc.positionAimOf (name);
        setAimBox (aimBox, aim < 0 ? Cal90 : aim);
        if (micBox.isPopupActive()) return;
        micBox.clear (juce::dontSendNotification);
        micBox.addItem ("No mic", 1);
        int here = -1;
        for (int m = 0; m < proc.settings.micCount.load(); ++m)
        {
            micBox.addItem (proc.mics.nameOf (m), 100 + m);
            if (here < 0 && proc.mics.position[(size_t) m] == name) here = m;
        }
        bool header = false;
        for (int k = 0; k < (int) proc.locker.mics.size(); ++k)
        {
            const auto& e = proc.locker.mics[(size_t) k];
            bool inVenue = false;
            for (int m = 0; m < proc.settings.micCount.load(); ++m) inVenue = inVenue || proc.mics.lockerId[(size_t) m] == e.id;
            if (inVenue) continue;
            if (! header) { micBox.addSectionHeading ("From the mic locker"); header = true; }
            micBox.addItem (e.label(), 1000 + k);
        }
        micBox.setSelectedId (here >= 0 ? 100 + here : 1, juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds();
        g.setColour (traceColour (index + 2));
        g.fillRoundedRectangle (r.removeFromLeft (4).toFloat().reduced (0.0f, 5.0f), 2.0f);
        // what has been measured here
        juce::String st;
        if (proc.room.hasAt (name, false)) st << "measured as found";
        if (proc.room.hasAt (name, true)) st << (st.isEmpty() ? "measured in" : ",") << " check " << proc.room.checkRound;
        if (proc.room.skipped.contains (name)) st = "left out of Room EQ";
        g.setColour (st.isEmpty() ? p.textFaint : p.textDim);
        g.setFont (font (11.0f));
        g.drawText (st.isEmpty() ? juce::String ("not measured yet") : st, statusArea, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (14).reduced (0, 3);
        removeButton.setBounds (r.removeFromRight (30));
        r.removeFromRight (8);
        const auto cols = columnWidths (r.getWidth());
        auto take = [&r] (int px) { auto a = r.removeFromLeft (px); r.removeFromLeft (8); return a; };
        nameEditor.setBounds (take (cols[0]));
        aimBox.setBounds (take (cols[1]));
        micBox.setBounds (take (cols[2]));
        statusArea = r;
    }

    static juce::StringArray columnNames() { return { "Position", "Aim", "Mic attached", "Measured" }; }
    static std::array<int, 3> columnWidths (int w) { return { w * 22 / 100, w * 22 / 100, w * 28 / 100 }; }

private:
    void commitName()
    {
        const auto t = nameEditor.getText().trim();
        if (t.isEmpty() || t == name) { nameEditor.setText (name, juce::dontSendNotification); return; }
        proc.renamePosition (name, t);
        name = t;
        if (onChanged) onChanged();
    }

    void attach()
    {
        const int id = micBox.getSelectedId();
        if (id == 1)
        {
            for (int m = 0; m < proc.settings.micCount.load(); ++m) if (proc.mics.position[(size_t) m] == name) proc.setMicPosition (m, {});
        }
        else if (id >= 100 && id < 100 + kMaxMics) proc.setMicPosition (id - 100, name);
        else if (id >= 1000 && id - 1000 < (int) proc.locker.mics.size())
        {
            // a mic from the locker joins the venue's mics, attached here
            const int n = proc.settings.micCount.load();
            if (maxMics && n >= maxMics()) { refresh(); if (onChanged) onChanged(); return; }
            const bool reuseFirst = n == 1 && proc.mics.lockerId[0].isEmpty() && ! proc.calibrations (0).any() && proc.mics.name[0].isEmpty();
            const int m = reuseFirst ? 0 : (proc.addMic(), proc.settings.micCount.load() - 1);
            proc.assignLockerMic (m, proc.locker.mics[(size_t) (id - 1000)].id);
            proc.setMicPosition (m, name);
            if (onMicsChanged) onMicsChanged();
            return;
        }
        if (onChanged) onChanged();
    }

    LiveAlignProcessor& proc;
    juce::String name;
    const int index;
    juce::TextEditor nameEditor;
    juce::ComboBox aimBox, micBox;
    juce::TextButton removeButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
    juce::Rectangle<int> statusArea;
};

//==============================================================================
class MicSetupPage : public juce::Component, private juce::Timer
{
public:
    std::function<void (int)> onCalibrate, onSaveToLocker;
    std::function<void (int, int)> onLoadCalFile;   // mic, angle (-1: from the file names)
    std::function<void()> onManageLocker;
    std::function<void()> onChanged;

    explicit MicSetupPage (LiveAlignProcessor& p)
        : proc (p), settings (p.settings), checks (p.checks),
          cards { { 1, "Audio interface", "The interface your mics and the loopback are connected to." },
                  { 2, "Reference", "A loopback of the signal feeding the PA, compared with every mic." },
                  { 3, "Mics connected", "Every mic in the table is live on its own input." },
                  { 4, "Calibration files", "Each mic's own response is removed using its calibration file." },
                  { 5, "Sound level", "Each mic calibrated with an acoustic calibrator, or confirmed for this session." },
                  { 6, "Loopback check", "Pink noise for 2 seconds from the generator: round trip, polarity and level." },
                  { 7, "Room noise floor", "5 seconds of background noise on every mic, with the PA silent." } }
    {
        addPositionButton.onClick = [this] { addPosition(); };
        addPositionButton.setTooltip ("Add a listening position (a row, a seat, the balcony ...); it is placed on the venue plan for you");
        addAndMakeVisible (addPositionButton);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        for (auto& c : cards) content.addAndMakeVisible (c);

        addMicButton.onClick = [this] { showAddMicMenu(); };
        addMicButton.setTooltip ("Add a mic: one from your mic locker, or a new one");
        addAndMakeVisible (addMicButton);


        deviceButton.onClick = [] { if (auto& f = StandaloneHooks::showAudioSettings()) f(); };
        cards[0].addAndMakeVisible (deviceButton);
        acceptButton.setButtonText ("Keep my checks");
        acceptButton.setTooltip ("The change does not matter for the checks (the same interface, renamed): keep them as they are");
        acceptButton.onClick = [this] { checks.setup = proc.setupFingerprint(); changed(); };
        cards[0].addChildComponent (acceptButton);

        refBox.onChange = [this]
        {
            settings.refSource = refBox.getSelectedItemIndex();
            checks.loopbackDone = false;
            settings.requestResetAverages = true;
            changed();
        };
        cards[1].addAndMakeVisible (refBox);

        noFilesToggle.setTooltip ("None of the mics has a calibration file: they are treated as flat. "
                                  "A single mic can also be marked from its Calibration file button in the table.");
        noFilesToggle.onClick = [this]
        {
            for (int m = 0; m < kMaxMics; ++m) proc.mics.noCalFile[(size_t) m] = noFilesToggle.getToggleState();
            changed();
        };
        cards[3].addAndMakeVisible (noFilesToggle);

        noCalButton.setTooltip ("No acoustic calibrator: levels stay in dBFS and this step is not used");
        noCalButton.onClick = [this] { proc.mics.noCalibrator = noCalButton.getToggleState(); changed(); };
        cards[4].addAndMakeVisible (noCalButton);

        loopButton.onClick = [this] { startLoopbackCheck(); };
        skipLoopButton.onClick = [this] { checks.loopbackSkipped = skipLoopButton.getToggleState(); changed(); };
        cards[5].addAndMakeVisible (loopButton);
        cards[5].addAndMakeVisible (skipLoopButton);

        noiseButton.onClick = [this] { startNoise(); };
        cards[6].addAndMakeVisible (noiseButton);


        rebuildRows();
        refreshChoices();
        update();
        startTimerHz (15);
    }

    ~MicSetupPage() override { stopTimer(); }

    int completedSteps() const
    {
        int n = 0;
        for (const auto& c : cards) n += c.isComplete() ? 1 : 0;
        return n;
    }
    static constexpr int kSteps = 7;
    // The first step not done yet (empty when all are), and scrolling it into view.
    juce::String nextHint() const
    {
        for (const auto& c : cards) if (! c.isComplete()) return c.getTitle();
        return {};
    }
    void showNextStep()
    {
        for (const auto& c : cards)
            if (! c.isComplete()) { viewport.setViewPosition (0, juce::jmax (0, c.getY() - 8)); return; }
    }
    bool hasWarning() const
    {
        for (const auto& c : cards) if (c.getState() == StepState::Warn) return true;
        return false;
    }

    // A message under the mic table for a few seconds.
    void setNote (int mic, const juce::String& text)
    {
        note = mic >= 0 ? proc.mics.nameOf (mic) + ": " + text : text;
        noteUntil = juce::Time::getMillisecondCounter() + 10000;
        repaint();
    }

    void setSnapshot (const Snapshot& s) { snap = &s; }
    juce::Component* calibrateButtonFor (int mic) { return mic < (int) rows.size() ? &rows[(size_t) mic]->calibrateButton() : nullptr; }
    juce::Component* calFileButtonFor (int mic) { return mic < (int) rows.size() ? &rows[(size_t) mic]->calFileComponent() : nullptr; }

    // After a venue or session is opened.
    void refreshChoices()
    {
        if ((int) rows.size() != settings.micCount.load()) rebuildRows();
        proc.syncPositionsWithPlan();
        rebuildPositionRows();
        const int nIn = juce::jmax (1, proc.numInputs());
        refBox.clear (juce::dontSendNotification);
        refBox.addItem ("Off", 1);
        refBox.addItem ("Generator (internal copy)", 2);
        for (int i = 0; i < nIn; ++i) refBox.addItem ("Input " + juce::String (i + 1) + " (loopback)", i + 3);
        // a reference input this interface does not have shows as nothing chosen (the step says why)
        if (settings.refSource.load() <= nIn + 1) refBox.setSelectedItemIndex (settings.refSource.load(), juce::dontSendNotification);
        else refBox.setSelectedId (0, juce::dontSendNotification);
        refBox.setTextWhenNothingSelected ("Input " + juce::String (settings.refSource.load() - 1) + " (not on this interface)");
        noCalButton.setToggleState (proc.mics.noCalibrator, juce::dontSendNotification);
        bool allNoFile = true;
        for (int m = 0; m < settings.micCount.load(); ++m) allNoFile = allNoFile && proc.mics.noCalFile[(size_t) m];
        noFilesToggle.setToggleState (allNoFile, juce::dontSendNotification);
        skipLoopButton.setToggleState (checks.loopbackSkipped, juce::dontSendNotification);
        addMicButton.setEnabled (settings.micCount.load() < maxMics());
        for (auto& r : rows) r->refresh();
        nIns = nIn;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, posCard.toFloat(), "Mic positions");
        drawPanel (g, tableCard.toFloat(), "Mics");
        auto header = [&g, &p] (juce::Rectangle<int> hdr, const juce::StringArray& names, const int* widths, int n)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            for (int i = 0; i < n; ++i)
            {
                auto cell = i < n - 1 ? hdr.removeFromLeft (widths[i]) : hdr;
                if (i < n - 1) hdr.removeFromLeft (8);
                g.drawText (names[i].toUpperCase(), cell, juce::Justification::centredLeft);
            }
        };
        if (! posRows.empty())
        {
            auto hdr = posHeader.withTrimmedLeft (14).withTrimmedRight (38);
            const auto c = PositionRow::columnWidths (hdr.getWidth());
            const int widths[] = { c[0], c[1], c[2], 0 };
            header (hdr, PositionRow::columnNames(), widths, 4);
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawText (posRows.empty() ? juce::String ("No positions yet: add where you will measure (FOH, Row 8, Balcony ...).")
                                    : juce::String ("Each position is on the venue plan. Attach a mic from your locker and it joins the mics below."),
                    posHint, juce::Justification::centredLeft, true);
        if (! rows.empty())
        {
            auto hdr = headerRow.withTrimmedLeft (30).withTrimmedRight (38);
            const auto c = MicRow::columnWidths (hdr.getWidth());
            const int widths[] = { c[0], c[1], c[2], c[3], c[4], 0 };
            header (hdr, MicRow::columnNames(), widths, 6);
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        if (note.isNotEmpty())
        {
            g.setColour (p.accent);
            g.drawText (note, tableHint, juce::Justification::centredLeft, true);
        }
        else
            g.drawText (juce::String (settings.micCount.load()) + " of " + juce::String (maxMics()) + " mics (the inputs this interface has free). "
                            "The sound level is re-checked each session.",
                        tableHint, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();

        // top: the positions; below: the mics, with the checklist beside them
        const int posH = 36 + 22 + (int) posRows.size() * 38 + 44;
        posCard = r.removeFromTop (juce::jmin (posH, r.getHeight() / 2));
        r.removeFromTop (10);
        {
            auto t = posCard.reduced (14, 0).withTrimmedTop (34);
            auto foot = t.removeFromBottom (44).withSizeKeepingCentre (t.getWidth(), 30);
            posHeader = t.removeFromTop (20);
            for (auto& row : posRows) row->setBounds (t.removeFromTop (38));
            addPositionButton.setBounds (foot.removeFromLeft (130));
            foot.removeFromLeft (14);
            posHint = foot;
        }
        auto right = r.removeFromRight (juce::jmax (420, r.getWidth() * 40 / 100));
        r.removeFromRight (10);
        {
            const int tableH = 36 + 24 + (int) rows.size() * 40 + 44;
            tableCard = r.removeFromTop (juce::jmin (tableH, r.getHeight()));
            auto t = tableCard.reduced (14, 0).withTrimmedTop (34);
            headerRow = t.removeFromTop (20);
            for (auto& row : rows) row->setBounds (t.removeFromTop (40));
            auto foot = t.removeFromTop (40).withSizeKeepingCentre (t.getWidth(), 30);
            addMicButton.setBounds (foot.removeFromLeft (130));
            foot.removeFromLeft (14);
            tableHint = foot;
        }
        r = right;
        viewport.setBounds (r);
        const int w = viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : r.getWidth();
        const bool compact = w < 680;
        int y = 0;
        for (int i = 0; i < kSteps; ++i)
        {
            cards[i].setCompact (compact);
            cards[i].setHasControls (i != 2);   // Mics connected has nothing to press
            cards[i].setBounds (0, y, w - 4, cards[i].preferredHeight());
            y += cards[i].preferredHeight() + 8;
            layoutControls (i);
        }
        content.setSize (w - 4, y);
    }

private:
    void rebuildPositionRows()
    {
        bool same = (int) posRows.size() == proc.positions.size();
        for (int i = 0; same && i < (int) posRows.size(); ++i) same = posRows[(size_t) i]->positionName() == proc.positions[i];
        if (same) { for (auto& r : posRows) r->refresh(); return; }
        posRows.clear();
        for (int i = 0; i < proc.positions.size(); ++i)
        {
            auto row = std::make_unique<PositionRow> (proc, proc.positions[i], i);
            row->maxMics = [this] { return maxMics(); };
            row->onChanged = [this] { later ([] (MicSetupPage& pg) { pg.refreshChoices(); pg.changed(); }); };
            row->onMicsChanged = [this] { later ([] (MicSetupPage& pg) { pg.rebuildRows(); pg.refreshChoices(); pg.changed(); }); };
            const auto nm = proc.positions[i];
            row->onRemove = [this, nm] { later ([nm] (MicSetupPage& pg) { pg.proc.removePosition (nm); pg.refreshChoices(); pg.changed(); }); };
            addAndMakeVisible (*row);
            posRows.push_back (std::move (row));
        }
        resized();
        repaint();
    }

    // After the click or edit that asked for it has finished (it may delete the row it came from).
    void later (std::function<void (MicSetupPage&)> f)
    {
        juce::Component::SafePointer<MicSetupPage> safe (this);
        juce::MessageManager::callAsync ([safe, f] { if (safe != nullptr) f (*safe); });
    }

    void addPosition()
    {
        int n = proc.positions.size() + 1;
        while (proc.positions.contains ("Position " + juce::String (n))) ++n;
        const auto nm = "Position " + juce::String (n);
        proc.positions.add (nm);
        proc.syncPositionsWithPlan();
        refreshChoices();
        changed();
        if (! posRows.empty()) setNote (-1, "Added " + nm + ": click its name to rename it. It is on the venue plan; drag it where it is.");
    }

    // + Add mic: one of your locker's mics not in this venue yet, or a new one.
    void showAddMicMenu()
    {
        juce::PopupMenu menu;
        const auto& lk = proc.locker.mics;
        bool any = false;
        for (int k = 0; k < (int) lk.size(); ++k)
        {
            bool inVenue = false;
            for (int m = 0; m < settings.micCount.load(); ++m) inVenue = inVenue || proc.mics.lockerId[(size_t) m] == lk[(size_t) k].id;
            if (inVenue) continue;
            if (! any) menu.addSectionHeader ("From the mic locker");
            any = true;
            menu.addItem (100 + k, lk[(size_t) k].label());
        }
        if (any) menu.addSeparator();
        menu.addItem (1, "A new mic (not in the locker)");
        juce::Component::SafePointer<MicSetupPage> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&addMicButton), [safe] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& p = safe->proc;
            if (p.settings.micCount.load() >= safe->maxMics()) { safe->setNote (-1, "Every input of this interface is in use."); return; }
            p.addMic();
            const int m = p.settings.micCount.load() - 1;
            if (r >= 100 && r - 100 < (int) p.locker.mics.size()) p.assignLockerMic (m, p.locker.mics[(size_t) (r - 100)].id);
            safe->rebuildRows();
            safe->refreshChoices();
            safe->changed();
        });
    }

    void rebuildRows()
    {
        rows.clear();
        for (int m = 0; m < settings.micCount.load(); ++m)
        {
            auto row = std::make_unique<MicRow> (proc, m);
            row->onChanged = [this] { changed(); };
            row->onRemove = [this, m]
            {
                // rebuild after the click has finished: the row being clicked is deleted
                juce::Component::SafePointer<MicSetupPage> safe (this);
                juce::MessageManager::callAsync ([safe, m] { if (safe != nullptr) { safe->proc.removeMic (m); safe->rebuildRows(); safe->changed(); } });
            };
            row->onCalibrate = [this, m] { if (onCalibrate) onCalibrate (m); };
            row->onCalFileMenu = [this, m] { showCalFileMenu (m); };
            row->onLockerMenu = [this, m] { showLockerMenu (m); };
            addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        addMicButton.setEnabled (settings.micCount.load() < maxMics());
        addMicButton.setTooltip (settings.micCount.load() < maxMics() ? "Add a measurement mic"
                                                                      : "Every input of this interface is in use (the reference takes one)");
        resized();
    }

    // As many mics as the interface has inputs, less the one the reference uses (up to 8).
    int maxMics() const
    {
        const int nIn = juce::jmax (1, proc.numInputs());
        const int ref = settings.refSource.load();
        const bool refUsesInput = ref >= 2 && ref - 2 < nIn;
        return juce::jlimit (1, kMaxMics, nIn - (refUsesInput ? 1 : 0));
    }

    void showLockerMenu (int m)
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Mic locker");
        const auto& lk = proc.locker.mics;
        const auto linkedId = proc.mics.lockerId[(size_t) m];
        for (int i = 0; i < (int) lk.size(); ++i)
        {
            juce::String usedBy;
            for (int k = 0; k < settings.micCount.load(); ++k)
                if (k != m && proc.mics.lockerId[(size_t) k] == lk[(size_t) i].id) usedBy = " (now in row " + juce::String (k + 1) + ")";
            juce::PopupMenu::Item it (lk[(size_t) i].label() + usedBy);
            it.setID (100 + i);
            it.setTicked (lk[(size_t) i].id == linkedId);
            menu.addItem (it);
        }
        if (lk.empty()) menu.addItem (1, "No mics saved yet", false);
        menu.addSeparator();
        const auto* linked = proc.locker.find (linkedId);
        menu.addItem (2, linked != nullptr ? "Save changes to \"" + linked->label() + "\" now" : juce::String ("Save this mic to the locker..."));
        if (linked != nullptr) menu.addItem (3, "Unlink from the locker");
        menu.addItem (4, "Open the mic locker");
        juce::Component::SafePointer<MicSetupPage> safe (this);
        auto* target = m < (int) rows.size() ? &rows[(size_t) m]->lockerComponent() : nullptr;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (target), [safe, m] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& p = safe->proc;
            if (r >= 100 && r - 100 < (int) p.locker.mics.size()) p.assignLockerMic (m, p.locker.mics[(size_t) (r - 100)].id);
            else if (r == 2)
            {
                if (p.locker.find (p.mics.lockerId[(size_t) m]) != nullptr) p.saveMicToLocker (m, p.mics.nameOf (m));
                else if (safe->onSaveToLocker) safe->onSaveToLocker (m);
            }
            else if (r == 3) p.unlinkLockerMic (m);
            else if (r == 4 && safe->onManageLocker) safe->onManageLocker();
            safe->refreshChoices();
            safe->changed();
        });
    }

    void showCalFileMenu (int m)
    {
        const auto& cs = proc.calibrations (m);
        juce::PopupMenu menu;
        menu.addItem (1, "Load calibration file(s)...  (every angle's file together)");
        menu.addSeparator();
        // the angles loaded, each with its file; then a file for another angle
        for (int a : calSlotsInOrder())
        {
            if (! cs.has (a)) continue;
            menu.addSectionHeader (calAngleName (a) + ":  " + cs.name[a]);
            menu.addItem (10 + a, "Load another " + calAngleName (a) + " file...");
            menu.addItem (20 + a, "Clear the " + calAngleName (a) + " file");
        }
        juce::PopupMenu other;
        for (int a : calSlotsInOrder())
            if (! cs.has (a)) other.addItem (10 + a, calAngleName (a) + "  " + calAimText (a).upToFirstOccurrenceOf (" (", false, false) + "...");
        menu.addSubMenu ("Load a file for another angle", other);
        if (cs.count() == 1 || cs.count() == 2) menu.addItem (30, cs.count() == 1 ? "This file is really the " + calAngleName (cs.has (Cal0) ? Cal90 : Cal0) + " one"
                                                                                : juce::String ("Swap the two files' angles"));
        menu.addSeparator();
        menu.addItem (3, "This mic has no file (treat as flat)", true, proc.mics.noCalFile[(size_t) m]);
        juce::Component::SafePointer<MicSetupPage> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (calFileButtonFor (m)), [safe, m] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& p = safe->proc;
            if (r == 1 && safe->onLoadCalFile) safe->onLoadCalFile (m, -1);
            if (r >= 10 && r < 10 + kCalSlots && safe->onLoadCalFile) safe->onLoadCalFile (m, r - 10);
            if (r >= 20 && r < 20 + kCalSlots) p.clearCalibration (m, r - 20);
            if (r == 30) p.swapCalibrationAngle (m);
            if (r == 3) p.mics.noCalFile[(size_t) m] = ! p.mics.noCalFile[(size_t) m];
            safe->changed();
        });
    }

    void layoutControls (int card)
    {
        auto a = cards[card].controlArea();
        auto put = [&a] (juce::Component& c, int w) { c.setBounds (a.removeFromLeft (w)); a.removeFromLeft (8); };
        switch (card)
        {
            case 0: put (deviceButton, 150); put (acceptButton, 140); break;
            case 1: put (refBox, 300); break;
            case 3: put (noFilesToggle, 220); break;
            case 4: put (noCalButton, 240); break;
            case 5: put (loopButton, 130); put (skipLoopButton, 120); break;
            case 6: put (noiseButton, 170); break;
            default: break;
        }
    }

    void changed() { for (auto& r : rows) r->refresh(); update(); if (onChanged) onChanged(); }

    void timerCallback() override
    {
        if (proc.numInputs() != nIns || (int) rows.size() != settings.micCount.load()) refreshChoices();
        for (int m = 0; m < (int) rows.size(); ++m)
        {
            lastMicRms[(size_t) m] = proc.micRmsDb (m);
            rows[(size_t) m]->setLevel (proc.takeMicPeak (m), lastMicRms[(size_t) m]);
        }
        pollLoopback();
        pollNoise();
        if (++tick % 4 == 0) { for (auto& r : rows) r->refresh(); update(); }
        if (note.isNotEmpty() && juce::Time::getMillisecondCounter() > noteUntil) { note.clear(); repaint(); }
    }

    // ---- step logic ----
    void update()
    {
        const bool standalone = (bool) StandaloneHooks::showAudioSettings();
        const int count = settings.micCount.load();
        deviceButton.setVisible (standalone);

        // 1 interface
        if (! standalone) cards[0].set (StepState::Done, "Inside a host: it provides the inputs and outputs.");
        else
        {
            const auto d = StandaloneHooks::deviceDescription() ? StandaloneHooks::deviceDescription()() : juce::String();
            const bool ok = d.isNotEmpty() && d != "No audio device";
            cards[0].set (ok ? StepState::Done : StepState::Todo, ok ? d : juce::String ("No audio device is open yet."));
        }
        // changed since this session's checks: say what, and which checks to do again
        const auto changes = proc.setupChanges();
        const bool rewired = ! changes.isEmpty();
        acceptButton.setVisible (rewired);
        if (rewired)
            cards[0].set (StepState::Todo, "Changed since this session's checks: " + changes.joinIntoString ("; ") + ". Measure the room noise again"
                                               + (settings.refSource.load() >= 2 ? " and run the loopback check" : juce::String())
                                               + (proc.interfaceChanged() ? ", and re-check each mic's sound level" : juce::String()) + ".");

        // 2 reference
        {
            const int ref = settings.refSource.load();
            const int nIn = juce::jmax (1, proc.numInputs());
            bool clash = false;
            for (int m = 0; m < count; ++m) clash = clash || settings.micInput[m].load() == ref - 2;
            if (ref == 0) cards[1].set (StepState::Todo, "Choose a reference. Without one, only the RTA and sound level work.");
            else if (ref >= 2 && ref - 2 >= nIn)
                cards[1].set (StepState::Warn, "Input " + juce::String (ref - 1) + " is not on this interface (it has " + juce::String (nIn)
                                                   + (nIn == 1 ? " input" : " inputs") + "). Choose another reference.");
            else if (clash) cards[1].set (StepState::Warn, "A mic is on the reference input (Input " + juce::String (ref - 1) + "): use separate inputs.");
            else cards[1].set (StepState::Done, ref == 1 ? "Generator copy (works only with the generator)." : "Input " + juce::String (ref - 1) + ".");
        }

        // 3 mics connected
        {
            int live = 0;
            bool dup = false;
            for (int m = 0; m < count; ++m)
            {
                live += lastMicRms[(size_t) m] > -100.0f ? 1 : 0;
                for (int k = 0; k < m; ++k) dup = dup || settings.micInput[k].load() == settings.micInput[m].load();
            }
            if (dup) cards[2].set (StepState::Warn, "Two mics are set to the same input.");
            else if (live == count) cards[2].set (StepState::Done, juce::String (count) + (count == 1 ? " mic is live." : " mics are live."));
            else cards[2].set (StepState::Todo, juce::String (live) + " of " + juce::String (count) + " mics show signal. Check cables and phantom power.");
        }

        // 4 calibration files
        {
            int loaded = 0, none = 0;
            for (int m = 0; m < count; ++m)
            {
                if (proc.calibrations (m).any()) ++loaded;
                else if (proc.mics.noCalFile[(size_t) m]) ++none;
            }
            if (none == count) cards[3].set (StepState::Skipped, "No calibration files: the mics are treated as flat.");
            else if (loaded + none == count) cards[3].set (StepState::Done, juce::String (loaded) + " file(s) loaded" + (none > 0 ? ", " + juce::String (none) + " mic(s) without." : "."));
            else cards[3].set (StepState::Todo, juce::String (count - loaded - none) + " mic(s) need a file: use the Calibration file button in the table.");
            noFilesToggle.setToggleState (none == count, juce::dontSendNotification);
        }

        // 5 sound level
        {
            int ok = 0, stale = 0;
            for (int m = 0; m < count; ++m)
            {
                const bool cal = settings.micSplCalibrated[m].load();
                ok += cal && checks.splVerified[(size_t) m] ? 1 : 0;
                stale += cal && ! checks.splVerified[(size_t) m] ? 1 : 0;
            }
            if (proc.mics.noCalibrator) cards[4].set (StepState::Skipped, "No calibrator: levels show as dBFS.");
            else if (ok == count) cards[4].set (StepState::Done, "Every mic calibrated or confirmed this session.");
            else if (stale > 0) cards[4].set (StepState::Todo, juce::String (stale) + " mic(s) need a quick re-check: Sound level button in the table, then Still correct.");
            else cards[4].set (StepState::Todo, juce::String (count - ok) + " mic(s) to calibrate: Sound level button in the table.");
        }

        // 6 loopback
        {
            const int ref = settings.refSource.load();
            loopButton.setEnabled (ref >= 2 && loopPhase == 0);
            loopButton.setButtonText (loopPhase != 0 ? "Checking..." : "Run check");
            if (ref == 1) cards[5].set (StepState::Done, "Not needed: the reference is the generator's internal copy.");
            else if (ref == 0) cards[5].set (StepState::Todo, "Choose a reference input first.");
            else if (loopPhase != 0) cards[5].set (StepState::Todo, "Playing pink noise and listening on the reference input...");
            else if (checks.loopbackDone)
            {
                const float lvl = checks.loopbackLevelDbfs;
                const bool levelOk = lvl > -36.0f && lvl < -3.0f;
                juce::String t = juce::String (checks.loopbackLatencyMs, 1) + " ms round trip, polarity "
                                 + (checks.loopbackInverted ? "INVERTED" : "normal") + ", level " + juce::String (juce::roundToInt (lvl)) + " dBFS";
                if (checks.loopbackInverted) t << ". Swap the loopback wiring or flip polarity.";
                else if (! levelOk) t << (lvl <= -36.0f ? ". Low: raise the input gain." : ". Hot: lower the input gain.");
                cards[5].set (checks.loopbackInverted || ! levelOk ? StepState::Warn : StepState::Done, t);
            }
            else if (checks.loopbackSkipped) cards[5].set (StepState::Skipped, "Not checked this session.");
            else cards[5].set (StepState::Todo, loopMessage.isNotEmpty() ? loopMessage : juce::String ("Turn the amps down if you don't want to hear it."));
            if (rewired && settings.refSource.load() >= 2 && loopPhase == 0)
                cards[5].set (StepState::Todo, "Run it again: " + changes[0] + ".");
        }

        // 7 noise
        {
            if (noiseActive) cards[6].set (StepState::Todo, "Measuring... " + juce::String (juce::roundToInt (noiseProgress * 100.0f)) + " %   Keep the room quiet.");
            else if (checks.noiseDone)
            {
                float worst = -200.0f;
                bool spl = true;
                for (int m = 0; m < checks.noiseMics; ++m) { worst = juce::jmax (worst, checks.noiseA[(size_t) m]); spl = spl && checks.noiseIsSpl[(size_t) m]; }
                const juce::String unit = spl ? " dBA" : " dBFS(A)";
                cards[6].set (StepState::Done, "Loudest mic: " + juce::String (worst, 1) + unit + ". Measure at "
                                                   + juce::String (juce::roundToInt (worst + 15.0f)) + unit + " or louder.");
            }
            else cards[6].set (StepState::Todo, "The generator is switched off while this runs.");
            if (rewired && ! noiseActive) cards[6].set (StepState::Todo, "Measure it again: " + changes[0] + ".");
            noiseButton.setEnabled (! noiseActive);
            noiseButton.setButtonText (noiseActive ? "Measuring..." : "Measure (5 seconds)");
        }

    }

    void startLoopbackCheck()
    {
        saved = { settings.genOn.load(), settings.genType.load(), settings.genLevelDb.load() };
        settings.genType = 0;
        settings.genLevelDb = -20.0f;
        settings.genOn = true;
        loopPhase = 1;
        loopStart = juce::Time::getMillisecondCounter();
        loopSerial = proc.analyzer.readLoopbackReport().serial;
        loopMessage.clear();
        update();
    }

    void pollLoopback()
    {
        if (loopPhase == 0) return;
        const auto now = juce::Time::getMillisecondCounter();
        if (loopPhase == 1 && now - loopStart > 2000)
        {
            proc.analyzer.requestLoopbackCheck();
            loopPhase = 2;
        }
        else if (loopPhase == 2)
        {
            const auto rep = proc.analyzer.readLoopbackReport();
            if (rep.serial != loopSerial || now - loopStart > 6000)
            {
                settings.genOn = saved.on;
                settings.genType = saved.type;
                settings.genLevelDb = saved.level;
                loopPhase = 0;
                if (rep.serial != loopSerial && rep.valid)
                {
                    checks.loopbackDone = true;
                    checks.loopbackLatencyMs = rep.latencyMs;
                    checks.loopbackInverted = rep.inverted;
                    checks.loopbackLevelDbfs = rep.refLevelDbfs;
                    checks.setup = proc.setupFingerprint();
                }
                else
                {
                    checks.loopbackDone = false;
                    loopMessage = rep.refLevelDbfs < -90.0f ? "Nothing arrived on the reference input. Check the loopback cable and output."
                                                            : "Signal on the reference input, but not the generator. Is the loopback on the generator's output?";
                }
                changed();
            }
        }
    }

    void startNoise()
    {
        settings.genOn = false;
        noiseSerial = proc.analyzer.readNoiseReport().serial;
        proc.analyzer.requestNoiseMeasurement (5.0);
        noiseActive = true;
        noiseProgress = 0.0f;
        update();
    }

    void pollNoise()
    {
        if (! noiseActive) return;
        const auto rep = proc.analyzer.readNoiseReport();
        noiseProgress = rep.progress;
        if (! rep.done || rep.serial <= noiseSerial + 1) return;

        noiseActive = false;
        checks.noiseDone = true;
        checks.noiseMics = rep.micCount;
        checks.setup = proc.setupFingerprint();
        for (int m = 0; m < rep.micCount; ++m)
        {
            checks.noiseA[(size_t) m] = rep.laeq[(size_t) m];
            checks.noiseC[(size_t) m] = rep.lceq[(size_t) m];
            checks.noiseIsSpl[(size_t) m] = rep.isSpl[(size_t) m];
            if (m < (int) rep.bandDb.size()) checks.setNoiseOctaves (m, rep.bandFreq, rep.bandDb[(size_t) m]);

            // keep each mic's noise spectrum as an RTA trace (replacing an older one)
            const auto name = "Noise floor: " + proc.mics.nameOf (m);
            for (int i = proc.traces.size() - 1; i >= 0; --i)
                if (proc.traces.all()[(size_t) i].name == name) proc.traces.remove (i);
            Trace t;
            t.kind = Trace::Rta;
            t.name = name;
            for (size_t i = 0; i < rep.bandFreq.size(); ++i) { t.data.freq.push_back (rep.bandFreq[i]); t.data.db.push_back (rep.bandDb[(size_t) m][i]); }
            proc.traces.add (std::move (t));
        }
        changed();
    }

    LiveAlignProcessor& proc;
    Settings& settings;
    SessionChecks& checks;
    const Snapshot* snap = nullptr;

    std::vector<std::unique_ptr<PositionRow>> posRows;
    juce::TextButton addPositionButton { "+ Position" };
    juce::Rectangle<int> posCard, posHeader, posHint;
    juce::Viewport viewport;
    juce::Component content;
    StepCard cards[kSteps];
    std::vector<std::unique_ptr<MicRow>> rows;
    juce::Rectangle<int> tableCard, headerRow, tableHint;
    juce::String note;
    juce::uint32 noteUntil = 0;

    juce::TextButton acceptButton;
    juce::TextButton addMicButton { "+ Add mic" }, deviceButton { "Audio device..." }, loopButton { "Run check" },
        noiseButton { "Measure (5 seconds)" };
    juce::ToggleButton noFilesToggle { "No calibration files" }, noCalButton { "No calibrator (dBFS)" }, skipLoopButton { "Skip" };
    juce::ComboBox refBox;

    struct SavedGen { bool on = false; int type = 0; float level = -20.0f; } saved;
    int loopPhase = 0, nIns = 0, tick = 0;
    juce::uint32 loopStart = 0;
    juce::uint64 loopSerial = 0, noiseSerial = 0;
    bool noiseActive = false;
    float noiseProgress = 0.0f;
    std::array<float, kMaxMics> lastMicRms { -120, -120, -120, -120, -120, -120, -120, -120 };
    juce::String loopMessage;
};
} // namespace la::ui
