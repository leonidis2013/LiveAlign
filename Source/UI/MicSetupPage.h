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
    bool isComplete() const { return state != StepState::Todo; }
    juce::Rectangle<int> controlArea() const { return getLocalBounds().reduced (12, 0).removeFromRight (kControlsW).withSizeKeepingCentre (kControlsW, 28); }

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

        const auto badge = juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ 26.0f, r.getCentreY() });
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

        auto text = getLocalBounds().withTrimmedLeft (48).withTrimmedRight (kControlsW + 22).reduced (0, 8);
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
        g.drawText (result, text, juce::Justification::centredLeft, true);
    }

    static constexpr int kHeight = 78, kControlsW = 300;

private:
    int num;
    juce::String title, help, result;
    StepState state = StepState::Todo;
};

//==============================================================================
// One row of the mic table.
class MicRow : public juce::Component
{
public:
    std::function<void()> onChanged, onRemove, onCalibrate, onCalFileMenu;

    MicRow (LiveAlignProcessor& p, int micIndex) : proc (p), index (micIndex)
    {
        nameEditor.setFont (font (13.0f));
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
        positionBox.setTextWhenNothingSelected ("Position");
        positionBox.setTooltip ("Where the mic is. Type a new name to add a position to the venue.");
        positionBox.onChange = [this]
        {
            const auto t = positionBox.getText().trim();
            proc.mics.position[(size_t) index] = t;
            if (t.isNotEmpty() && ! proc.positions.contains (t)) proc.positions.add (t);
            changed();
        };
        addAndMakeVisible (positionBox);

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

        const auto cal = proc.analyzer.getCalibrationName (index);
        calFileButton.setButtonText (cal.isNotEmpty() ? cal : (proc.mics.noCalFile[(size_t) index] ? "No file (flat)" : "Load cal file..."));
        calFileButton.setTooltip (cal.isNotEmpty() ? "Calibration file: " + cal : "Load this mic's calibration file, or mark that it has none");

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
        nameEditor.setBounds (take (w * 16 / 100));
        inputBox.setBounds (take (w * 11 / 100));
        positionBox.setBounds (take (w * 16 / 100));
        calFileButton.setBounds (take (w * 19 / 100));
        splButton.setBounds (take (w * 15 / 100));
        meterArea = r;
    }

    // Column positions for the table header.
    static juce::StringArray columnNames() { return { "Name", "Input", "Position", "Calibration file", "Sound level", "Level" }; }

private:
    void changed() { if (onChanged) onChanged(); }

    LiveAlignProcessor& proc;
    const int index;
    juce::TextEditor nameEditor;
    juce::ComboBox inputBox, positionBox;
    juce::TextButton calFileButton, splButton, removeButton { juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) };
    juce::Rectangle<int> meterArea;
    float peakDb = -120.0f, rmsDb = -120.0f;
};

//==============================================================================
class MicSetupPage : public juce::Component, private juce::Timer
{
public:
    std::function<void (int)> onCalibrate, onLoadCalFile;
    std::function<void()> onChanged;

    explicit MicSetupPage (LiveAlignProcessor& p)
        : proc (p), settings (p.settings), checks (p.checks), rta (p.settings, p.traces),
          cards { { 1, "Audio interface", "The interface your mics and the loopback are connected to." },
                  { 2, "Reference", "A loopback of the signal feeding the PA, compared with every mic." },
                  { 3, "Mics connected", "Every mic in the table is live on its own input." },
                  { 4, "Calibration files", "Each mic's own response is removed using its calibration file." },
                  { 5, "Sound level", "Each mic calibrated with an acoustic calibrator, or confirmed for this session." },
                  { 6, "Loopback check", "Pink noise for 2 seconds from the generator: round trip, polarity and level." },
                  { 7, "Room noise floor", "5 seconds of background noise on every mic, with the PA silent." } }
    {
        rta.setShowAllMics (true);
        addAndMakeVisible (rta);
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        for (auto& c : cards) content.addAndMakeVisible (c);

        addMicButton.onClick = [this] { proc.addMic(); rebuildRows(); changed(); };   // rows are rebuilt; the add button is not one of them
        addMicButton.setTooltip ("Add a measurement mic (up to 8)");
        addAndMakeVisible (addMicButton);

        deviceButton.onClick = [] { if (auto& f = StandaloneHooks::showAudioSettings()) f(); };
        cards[0].addAndMakeVisible (deviceButton);

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
    bool hasWarning() const
    {
        for (const auto& c : cards) if (c.getState() == StepState::Warn) return true;
        return false;
    }

    void setSnapshot (const Snapshot& s) { snap = &s; rta.update (s); }
    juce::Component* calibrateButtonFor (int mic) { return mic < (int) rows.size() ? &rows[(size_t) mic]->calibrateButton() : nullptr; }
    juce::Component* calFileButtonFor (int mic) { return mic < (int) rows.size() ? &rows[(size_t) mic]->calFileComponent() : nullptr; }

    // After a venue or session is opened.
    void refreshChoices()
    {
        if ((int) rows.size() != settings.micCount.load()) rebuildRows();
        const int nIn = juce::jmax (1, proc.numInputs());
        refBox.clear (juce::dontSendNotification);
        refBox.addItem ("Off", 1);
        refBox.addItem ("Generator (internal copy)", 2);
        for (int i = 0; i < nIn; ++i) refBox.addItem ("Input " + juce::String (i + 1) + " (loopback)", i + 3);
        refBox.setSelectedItemIndex (juce::jlimit (0, nIn + 1, settings.refSource.load()), juce::dontSendNotification);
        noCalButton.setToggleState (proc.mics.noCalibrator, juce::dontSendNotification);
        bool allNoFile = true;
        for (int m = 0; m < settings.micCount.load(); ++m) allNoFile = allNoFile && proc.mics.noCalFile[(size_t) m];
        noFilesToggle.setToggleState (allNoFile, juce::dontSendNotification);
        skipLoopButton.setToggleState (checks.loopbackSkipped, juce::dontSendNotification);
        for (auto& r : rows) r->refresh();
        nIns = nIn;
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, tableCard.toFloat(), "Microphones");
        const auto& p = pal();
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        if (! rows.empty())
        {
            const auto names = MicRow::columnNames();
            auto hdr = headerRow.withTrimmedLeft (30).withTrimmedRight (38);
            const int w = hdr.getWidth();
            const int widths[] = { w * 16 / 100, w * 11 / 100, w * 16 / 100, w * 19 / 100, w * 15 / 100, 0 };
            for (int i = 0; i < 6; ++i)
            {
                auto cell = i < 5 ? hdr.removeFromLeft (widths[i]) : hdr;
                if (i < 5) hdr.removeFromLeft (8);
                g.drawText (names[i].toUpperCase(), cell, juce::Justification::centredLeft);
            }
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawText (juce::String (settings.micCount.load()) + " of " + juce::String (kMaxMics) + " mics. Mics, positions and calibration belong to the venue; "
                        "the sound level is re-checked each session.",
                    tableHint, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto r = getLocalBounds();

        const int tableH = 36 + 24 + (int) rows.size() * 40 + 44;
        tableCard = r.removeFromTop (tableH);
        r.removeFromTop (10);
        auto t = tableCard.reduced (14, 0).withTrimmedTop (34);
        headerRow = t.removeFromTop (20);
        for (auto& row : rows) row->setBounds (t.removeFromTop (40));
        auto foot = t.removeFromTop (40).withSizeKeepingCentre (t.getWidth(), 30);
        addMicButton.setBounds (foot.removeFromLeft (130));
        foot.removeFromLeft (14);
        tableHint = foot;

        auto right = r.removeFromRight (juce::jmax (360, r.getWidth() * 38 / 100));
        r.removeFromRight (10);
        rta.setBounds (right);
        viewport.setBounds (r);
        const int w = viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : r.getWidth();
        content.setSize (w - 4, kSteps * (StepCard::kHeight + 8));
        for (int i = 0; i < kSteps; ++i)
        {
            cards[i].setBounds (0, i * (StepCard::kHeight + 8), content.getWidth(), StepCard::kHeight);
            layoutControls (i);
        }
    }

private:
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
            addAndMakeVisible (*row);
            rows.push_back (std::move (row));
        }
        addMicButton.setEnabled (settings.micCount.load() < kMaxMics);
        resized();
    }

    void showCalFileMenu (int m)
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Load calibration file...");
        menu.addItem (2, "Clear", proc.analyzer.getCalibrationName (m).isNotEmpty());
        menu.addItem (3, "This mic has no file (treat as flat)", true, proc.mics.noCalFile[(size_t) m]);
        juce::Component::SafePointer<MicSetupPage> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (calFileButtonFor (m)), [safe, m] (int r)
        {
            if (safe == nullptr) return;
            if (r == 1 && safe->onLoadCalFile) safe->onLoadCalFile (m);
            if (r == 2) safe->proc.clearCalibration (m);
            if (r == 3) safe->proc.mics.noCalFile[(size_t) m] = ! safe->proc.mics.noCalFile[(size_t) m];
            safe->changed();
        });
    }

    void layoutControls (int card)
    {
        auto a = cards[card].controlArea();
        auto put = [&a] (juce::Component& c, int w) { c.setBounds (a.removeFromLeft (w)); a.removeFromLeft (8); };
        switch (card)
        {
            case 0: put (deviceButton, 150); break;
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

        // 2 reference
        {
            const int ref = settings.refSource.load();
            bool clash = false;
            for (int m = 0; m < count; ++m) clash = clash || settings.micInput[m].load() == ref - 2;
            if (ref == 0) cards[1].set (StepState::Todo, "Choose a reference. Without one, only the RTA and sound level work.");
            else if (clash) cards[1].set (StepState::Warn, "A mic is on the reference input.");
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
                if (proc.analyzer.getCalibrationName (m).isNotEmpty()) ++loaded;
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

    RtaView rta;
    juce::Viewport viewport;
    juce::Component content;
    StepCard cards[kSteps];
    std::vector<std::unique_ptr<MicRow>> rows;
    juce::Rectangle<int> tableCard, headerRow, tableHint;

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
