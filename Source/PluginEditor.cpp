// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "PluginEditor.h"
#include "UI/SettingsPanel.h"
#include "UI/CalibrationPanel.h"
#include "StandaloneHooks.h"

using namespace la::ui;

namespace
{
const char* pageSubtitle (int page)
{
    static const char* subs[] = {
        "The venue, its sessions, and where each step stands",
        "Mics, inputs, calibration, loopback and room noise",
        "Measure each speaker or group and work out the crossover",
        "Measure across the room and tune toward a target",
        "RTA, transfer function, impulse response and sound level" };
    return subs[juce::jlimit (0, kNumPages - 1, page)];
}
} // namespace

LiveAlignEditor::LiveAlignEditor (LiveAlignProcessor& p)
    : AudioProcessorEditor (&p), processor (p), settings (p.settings), standalone (p.isStandalone() || p.uiAsStandalone),
      splPanel (p.settings), inputPanel (p.settings), delayPanel (p.settings), tracesPanel (p.traces, p.settings), generator (p.settings)
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);

    // ---- navigation ----
    addAndMakeVisible (nav);
    nav.onSelect = [this] (int pg) { showPage (pg); };
    nav.onSettings = [this] { openSettings(); };
    nav.onFullScreen = [this] { toggleFullScreen(); };
    nav.onVenue = [this] { showPage ((int) Page::Home); };
    nav.setFullScreenAvailable ((bool) la::StandaloneHooks::toggleFullScreen());
    if (! standalone)
        for (int i = 0; i < 4; ++i) nav.setPageVisible (i, false);   // the VST3 shows Monitoring only

    // ---- pages ----
    homePage = std::make_unique<HomePage> (p);
    homePage->onGoTo = [this] (int pg) { showPage (pg); };
    homePage->onNewVenue = [this] { confirmDiscard ([this] { newProject(); }); };
    homePage->onOpenVenue = [this] { confirmDiscard ([this] { openProject(); }); };
    homePage->onSave = [this] { saveProject (false); };
    homePage->onSaveAs = [this] { saveProject (true); };
    homePage->onNewSession = [this] { newSession(); };
    homePage->onChanged = [this] { afterProjectLoaded(); };
    homePage->onRenameSession = [this] (int i)
    {
        if (i < 0 || i >= (int) processor.getSessions().size()) return;
        askText ("Rename session", {}, processor.getSessions()[(size_t) i].name,
                 [this, i] (const juce::String& t) { processor.renameSession (i, t); homePage->refresh(); updateStatuses(); });
    };
    homePage->onDeleteSession = [this] (int i)
    {
        if (i < 0 || i >= (int) processor.getSessions().size()) return;
        const auto name = processor.getSessions()[(size_t) i].name;
        juce::Component::SafePointer<LiveAlignEditor> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Delete session",
                                          "Delete \"" + name + "\" and all its traces and checks?", "Delete", "Cancel", this),
                                      [safe, i] (int r) { if (safe != nullptr && r == 1) { safe->processor.deleteSession (i); safe->afterProjectLoaded(); } });
    };
    addChildComponent (*homePage);

    micPage = std::make_unique<MicSetupPage> (p);
    micPage->onCalibrate = [this] (int m) { openCalibration (m, micPage->calibrateButtonFor (m)); };
    micPage->onLoadCalFile = [this] (int m) { loadCalFile (m); };
    micPage->onBack = [this] { showPage ((int) Page::Home); };
    micPage->onNext = [this] { showPage ((int) Page::Speakers); };
    micPage->onChanged = [this] { pickerMics = -1; refreshMicPicker(); };
    addChildComponent (*micPage);

    speakerPage = std::make_unique<PlaceholderPage> ((int) Page::Speakers, "Coming in the next stage",
        "Measure each speaker or group on its own, compare their responses, and get crossover settings for any speaker management system.",
        juce::StringArray { "Build the system: tops, subs, fills, delays, groups such as a line array or several subs",
                            "Speaker library: spec sheet, built-in DSP switches, EASE / FRD / CSV / CLF data",
                            "Measure and overlay each speaker or group",
                            "Crossover recommendation: frequency, filter type and slope, delay, polarity",
                            "Predicted combined response, then a settings summary for your processor",
                            "Set up once per venue: it stays valid until you mark the system as changed" });
    roomPage = std::make_unique<PlaceholderPage> ((int) Page::Room, "Coming in a later stage",
        "Measure across the listening area, compare with a target curve and tune the system.",
        juce::StringArray { "All mic positions, averaged", "Target curve with suggested EQ cuts", "Top and sub alignment checked at the seats",
                            "Feedback ring-out for the stage mics", "Room acoustics: reverb time, clarity, room modes" });
    speakerPage->onBack = [this] { showPage ((int) Page::Mics); };
    speakerPage->onNext = [this] { showPage ((int) Page::Room); };
    roomPage->onBack = [this] { showPage ((int) Page::Speakers); };
    roomPage->onNext = [this] { showPage ((int) Page::Monitor); };
    addChildComponent (*speakerPage);
    addChildComponent (*roomPage);

    // ---- monitoring page ----
    for (int i = 0; i < 3; ++i)
    {
        panes[i] = std::make_unique<MeasurePane> (p.settings, p.traces, i);
        addChildComponent (*panes[i]);
    }
    for (auto* c : { (juce::Component*) &splPanel, (juce::Component*) &inputPanel, (juce::Component*) &delayPanel, (juce::Component*) &tracesPanel })
        addChildComponent (*c);
    monitorBackButton.setButtonText (juce::String ("Back: ") + pageName ((int) Page::Room));
    monitorBackButton.onClick = [this] { showPage ((int) Page::Room); };
    addChildComponent (monitorBackButton);

    micPicker.setTooltip ("Which mic the graphs and sound level show, or the average of all mics");
    micPicker.onChange = [this]
    {
        const int id = micPicker.getSelectedId();
        settings.selectedMic = id == 100 ? la::kAverageMic : juce::jmax (0, id - 1);
        refreshMicPicker();
    };
    addChildComponent (micPicker);

    runButton.setTooltip ("Freeze the graphs to study them; the sound level keeps running (key: Space)");
    runButton.onClick = [this] { settings.running = ! settings.running.load(); };
    addChildComponent (runButton);
    resetButton.setTooltip ("Restart the transfer function and RTA averages (key: R)");
    resetButton.onClick = [this] { settings.requestResetAverages = true; };
    addChildComponent (resetButton);

    splPanel.onCalibrate = [this] { openCalibration (settings.focusMic(), &splPanel.calibrateButton()); };
    tracesPanel.onCapture = [this] { capture(); };
    tracesPanel.onImport = [this] { importTrace(); };
    tracesPanel.onRename = [this] (int i) { renameTrace (i); };
    tracesPanel.onExport = [this] (int i) { exportTrace (i); };
    tracesPanel.onDelete = [this] (int i) { deleteTrace (i); };

    addAndMakeVisible (generator);

    applyTheme (settings.theme.load());
    generator.setNumOutputs (p.numOutputs());
    generator.sync();
    refreshMicPicker();

    setResizable (true, true);
    setResizeLimits (1180, 740, 5120, 2880);
    setSize (1480, 920);
    showPage (standalone ? settings.currentTab.load() : (int) Page::Monitor);
    updateStatuses();

    startTimerHz (30);
}

LiveAlignEditor::~LiveAlignEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void LiveAlignEditor::applyTheme (int theme)
{
    appliedTheme = theme;
    currentPalette() = theme == 0 ? &darkPalette() : &lightPalette();
    laf.applyPalette();
    sendLookAndFeelChange();
    repaint();
}

void LiveAlignEditor::showPage (int page)
{
    page = standalone ? juce::jlimit (0, kNumPages - 1, page) : (int) Page::Monitor;
    settings.currentTab = page;
    shownPage = page;
    nav.setCurrent (page);

    homePage->setVisible (page == (int) Page::Home);
    micPage->setVisible (page == (int) Page::Mics);
    speakerPage->setVisible (page == (int) Page::Speakers);
    roomPage->setVisible (page == (int) Page::Room);
    const bool mon = page == (int) Page::Monitor;
    for (int i = 0; i < 3; ++i) panes[i]->setVisible (mon && i < juce::jlimit (1, 3, settings.paneCount.load()));
    for (auto* c : { (juce::Component*) &splPanel, (juce::Component*) &inputPanel, (juce::Component*) &delayPanel, (juce::Component*) &tracesPanel,
                     (juce::Component*) &runButton, (juce::Component*) &resetButton, (juce::Component*) &micPicker })
        c->setVisible (mon);
    monitorBackButton.setVisible (mon && standalone);

    if (page == (int) Page::Home) homePage->refresh();
    if (page == (int) Page::Mics) { micPage->refreshChoices(); if (snapSerial > 0) micPage->setSnapshot (snap); }
    updateStatuses();
    resized();
    repaint();
}

void LiveAlignEditor::setPaneViews (int a, int b, int c)
{
    settings.view = a;
    settings.paneView2 = b;
    settings.paneView3 = c;
    for (auto& pane : panes) pane->sync();
}

void LiveAlignEditor::layoutPanes()
{
    const int count = juce::jlimit (1, 3, settings.paneCount.load());
    const bool columns = settings.paneLayout.load() == 1;
    laidOutCount = count;
    laidOutLayout = columns ? 1 : 0;
    auto r = paneArea;
    const int gap = 8;
    for (int i = 0; i < 3; ++i)
    {
        panes[i]->setVisible (i < count && shownPage == (int) Page::Monitor);
        if (i >= count) continue;
        const int remaining = count - i;
        if (columns) { const int w = (r.getWidth() - gap * (remaining - 1)) / remaining; panes[i]->setBounds (r.removeFromLeft (w)); r.removeFromLeft (gap); }
        else { const int h = (r.getHeight() - gap * (remaining - 1)) / remaining; panes[i]->setBounds (r.removeFromTop (h)); r.removeFromTop (gap); }
        if (snapSerial > 0) panes[i]->update (snap);
    }
}

void LiveAlignEditor::refreshMicPicker()
{
    const int n = settings.micCount.load();
    juce::String sig;
    for (int m = 0; m < n; ++m) sig << processor.mics.nameOf (m) << "|" << processor.mics.position[(size_t) m] << "|";
    const int hash = sig.hashCode() + n;
    if (hash != pickerMics)
    {
        pickerMics = hash;
        micPicker.clear (juce::dontSendNotification);
        for (int m = 0; m < n; ++m)
        {
            juce::String label = processor.mics.nameOf (m);
            if (processor.mics.position[(size_t) m].isNotEmpty()) label << "  -  " << processor.mics.position[(size_t) m];
            micPicker.addItem (label, m + 1);
        }
        if (n > 1) micPicker.addItem ("Average of " + juce::String (n) + " mics", 100);
    }
    const int sel = settings.selectedMic.load();
    micPicker.setSelectedId (sel >= la::kAverageMic && n > 1 ? 100 : juce::jlimit (0, n - 1, sel) + 1, juce::dontSendNotification);
    inputPanel.setMicLabel (processor.mics.nameOf (settings.focusMic()) + "  In " + juce::String (settings.micInput[settings.focusMic()].load() + 1));
}

// Readiness of each step, for the navigation pane and the home page.
void LiveAlignEditor::updateStatuses()
{
    StepInfo steps[4];
    const int done = micPage->completedSteps(), total = MicSetupPage::kSteps;
    const bool setupChanged = processor.checks.setup.isNotEmpty() && processor.checks.setup != processor.setupFingerprint();
    bool everCalibrated = false;
    for (int m = 0; m < settings.micCount.load(); ++m) everCalibrated = everCalibrated || settings.micSplCalibrated[m].load();

    auto& mic = steps[0];
    if (setupChanged) { mic.status = StepStatus::Attention; mic.shortText = "Wiring changed"; mic.detail = "The interface or mic wiring changed since this session's checks."; }
    else if (done == total && ! micPage->hasWarning()) { mic.status = StepStatus::Ready; mic.shortText = "Ready"; mic.detail = "All " + juce::String (total) + " checks done for this session."; }
    else if (micPage->hasWarning()) { mic.status = StepStatus::Attention; mic.shortText = juce::String (done) + " of " + juce::String (total) + ", warning"; mic.detail = "A check needs attention."; }
    else
    {
        mic.status = done == 0 ? StepStatus::NotStarted : StepStatus::InProgress;
        mic.fraction = (float) done / (float) total;
        mic.shortText = juce::String (done) + " of " + juce::String (total) + " done";
        mic.detail = everCalibrated ? "Set up before: re-check the sound level, loopback and noise for this session."
                                    : "Not set up yet: mics, calibration, loopback and room noise.";
    }

    auto& spk = steps[1];
    if (processor.speakers.changed) { spk.status = StepStatus::Attention; spk.shortText = "Marked changed"; spk.detail = "The system was marked as changed: measure the speakers again."; }
    else if (processor.speakers.configured) { spk.status = StepStatus::Ready; spk.shortText = "Unchanged"; spk.detail = "Set up " + processor.speakers.configuredOn + ". Nothing has changed since."; }
    else { spk.status = StepStatus::Later; spk.shortText = "Next stage"; spk.detail = "Speaker measurement and crossover come in the next stage."; }

    auto& room = steps[2];
    room.status = StepStatus::Later;
    room.shortText = "Later stage";
    room.detail = "Room tuning comes in a later stage.";

    auto& mon = steps[3];
    if (mic.status == StepStatus::Ready) { mon.status = StepStatus::Ready; mon.shortText = "Ready"; mon.detail = "Mics checked: ready to monitor."; }
    else { mon.status = StepStatus::InProgress; mon.fraction = mic.fraction; mon.shortText = "Usable"; mon.detail = "Works now; finish the mic setup for calibrated, aligned readings."; }

    for (int i = 0; i < 4; ++i) nav.setStatus (i + 1, steps[i].status, steps[i].shortText, steps[i].fraction);
    homePage->setSteps (steps, setupChanged);

    const auto& list = processor.getSessions();
    const int cur = processor.currentSession();
    nav.setVenue (processor.venueName, cur < (int) list.size() ? list[(size_t) cur].name : juce::String(), unsaved);
}

// ---------------------------------------------------------------------------- pop-ups

void LiveAlignEditor::openSettings()
{
    auto panel = std::make_unique<SettingsPanel> (processor);
    panel->onThemeChanged = [this] { applyTheme (settings.theme.load()); };
    panel->onLayoutChanged = [this] { layoutPanes(); };
    juce::CallOutBox::launchAsynchronously (std::move (panel), nav.getBounds().withTrimmedTop (nav.getHeight() - 60), this);
}

void LiveAlignEditor::openCalibration (int mic, juce::Component* anchor)
{
    auto panel = std::make_unique<CalibrationPanel> (settings, mic, processor.mics.nameOf (mic),
        [this, mic] { return mic < (int) snap.mics.size() ? snap.mics[(size_t) mic].rawZSlowDbfs : -200.0f; },
        [this, mic] { return processor.analyzer.getCalibrationName (mic); });
    panel->onLoadFile = [this, mic] { loadCalFile (mic); };
    panel->onClearFile = [this, mic] { processor.clearCalibration (mic); };
    panel->onVerified = [this, mic] { processor.checks.splVerified[(size_t) mic] = true; };
    auto area = anchor != nullptr ? getLocalArea (anchor, anchor->getLocalBounds()) : getLocalBounds().withSizeKeepingCentre (10, 10);
    juce::CallOutBox::launchAsynchronously (std::move (panel), area, this);
}

void LiveAlignEditor::loadCalFile (int mic)
{
    chooser = std::make_unique<juce::FileChooser> ("Load the calibration file for " + processor.mics.nameOf (mic), juce::File(), "*.txt;*.cal;*.frd;*.csv");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, mic] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto err = safe->processor.loadCalibrationFile (mic, f);
        if (err.isNotEmpty()) safe->showMessage ("Calibration file not loaded", err);
        safe->micPage->refreshChoices();
    });
}

void LiveAlignEditor::showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon).withTitle (title)
                                      .withMessage (text).withButton ("OK").withAssociatedComponent (this), nullptr);
}

void LiveAlignEditor::askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                               std::function<void (const juce::String&)> done)
{
    textWindow = std::make_unique<juce::AlertWindow> (title, message, juce::MessageBoxIconType::NoIcon, this);
    textWindow->addTextEditor ("text", initial);
    textWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    textWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    textWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe, done] (int result)
    {
        if (safe == nullptr || safe->textWindow == nullptr) return;
        const auto text = safe->textWindow->getTextEditorContents ("text").trim();
        safe->textWindow->setVisible (false);
        if (result == 1 && text.isNotEmpty()) done (text);
        safe->repaint();
    }), false);
}

// ---------------------------------------------------------------------------- traces

// Captures what is on screen: the transfer function and / or the RTA, whichever the graphs show.
void LiveAlignEditor::capture()
{
    bool wantTf = false, wantRta = false;
    for (int i = 0; i < settings.paneCount.load(); ++i)
    {
        const int v = settings.paneViewRef (i).load();
        wantTf |= v != (int) la::View::Rta;
        wantRta |= v == (int) la::View::Rta;
    }
    const auto time = juce::Time::getCurrentTime().formatted ("%H:%M:%S");
    const auto who = settings.selectedMic.load() >= la::kAverageMic && settings.micCount.load() > 1
                         ? juce::String ("Average") : processor.mics.nameOf (settings.focusMic());
    bool added = false;

    if (wantRta && ! snap.rtaFreq.empty() && snap.rtaMic.size() == snap.rtaFreq.size())
    {
        la::Trace t;
        t.kind = la::Trace::Rta;
        t.name = "RTA " + who + " " + time;
        for (size_t i = 0; i < snap.rtaFreq.size(); ++i) { t.data.freq.push_back (snap.rtaFreq[i]); t.data.db.push_back (snap.rtaMic[i]); }
        processor.traces.add (std::move (t));
        added = true;
    }
    if (wantTf)
    {
        la::Trace t;
        t.kind = la::Trace::Transfer;
        t.name = "TF " + who + " " + time;
        for (size_t i = 0; i < snap.tfFreq.size() && i < snap.tfMag.size() && snap.tfFrames > 0; ++i)
        {
            if (! std::isfinite (snap.tfMag[i])) continue;
            t.data.freq.push_back (snap.tfFreq[i]);
            t.data.db.push_back (snap.tfMag[i]);
            t.data.phase.push_back (std::isfinite (snap.tfPhase[i]) ? snap.tfPhase[i] : 0.0f);
            t.data.extra.push_back (snap.tfCoh[i]);
        }
        if (! t.data.freq.empty()) { processor.traces.add (std::move (t)); added = true; }
        else if (! added) showMessage ("Nothing to capture", "There is no transfer function yet. Start the generator or program audio with a reference connected.");
    }
    tracesPanel.refresh();
    repaint();
}

void LiveAlignEditor::importTrace()
{
    chooser = std::make_unique<juce::FileChooser> ("Import a trace or speaker response", juce::File(), "*.frd;*.txt;*.csv;*.latrace");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto err = safe->processor.traces.importFile (f);
        if (err.isNotEmpty()) safe->showMessage ("Import failed", err);
        safe->tracesPanel.refresh();
    });
}

void LiveAlignEditor::exportTrace (int index)
{
    if (! processor.traces.valid (index)) return;
    const auto name = juce::File::createLegalFileName (processor.traces.all()[(size_t) index].name);
    chooser = std::make_unique<juce::FileChooser> ("Export trace as FRD",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".frd"), "*.frd");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe, index] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        if (! safe->processor.traces.exportFrd (index, f.withFileExtension ("frd")))
            safe->showMessage ("Export failed", "Could not write " + f.getFullPathName());
    });
}

void LiveAlignEditor::renameTrace (int index)
{
    if (! processor.traces.valid (index)) return;
    askText ("Rename trace", {}, processor.traces.all()[(size_t) index].name,
             [this, index] (const juce::String& t) { processor.traces.rename (index, t); tracesPanel.refresh(); });
}

void LiveAlignEditor::deleteTrace (int index)
{
    if (! processor.traces.valid (index)) return;
    const auto name = processor.traces.all()[(size_t) index].name;
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Delete trace",
                                      "Delete \"" + name + "\"?", "Delete", "Cancel", this),
                                  [safe, index, name] (int result)
    {
        if (safe == nullptr || result != 1) return;
        if (safe->processor.traces.valid (index) && safe->processor.traces.all()[(size_t) index].name == name)
            safe->processor.traces.remove (index);
        safe->tracesPanel.refresh();
    });
}

void LiveAlignEditor::toggleGenerator() { settings.genOn = ! settings.genOn.load(); generator.sync(); }
void LiveAlignEditor::toggleFullScreen() { if (auto& f = la::StandaloneHooks::toggleFullScreen()) f(); }

// ---------------------------------------------------------------------------- venue and sessions

void LiveAlignEditor::confirmDiscard (std::function<void()> then)
{
    if (! processor.projectChanged()) { then(); return; }
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon, "Unsaved changes",
                                      "Save the changes to \"" + processor.venueName + "\" first?", "Save", "Don't save", "Cancel", this),
                                  [safe, then] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)
        {
            if (safe->processor.projectFile == juce::File()) { safe->saveProject (true); return; }
            safe->saveProject (false);
            then();
        }
        else if (r == 2) then();
    });
}

void LiveAlignEditor::saveProject (bool askForFile)
{
    if (! askForFile && processor.projectFile != juce::File())
    {
        const auto err = processor.saveProject (processor.projectFile);
        if (err.isNotEmpty()) showMessage ("Save failed", err);
        unsaved = false;
        updateStatuses();
        homePage->repaint();
        return;
    }
    auto dir = processor.projectFile != juce::File() ? processor.projectFile.getParentDirectory()
                                                     : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Save venue",
                                                   dir.getChildFile (juce::File::createLegalFileName (processor.venueName) + LiveAlignProcessor::projectExtension()),
                                                   "*" + LiveAlignProcessor::projectExtension());
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto err = safe->processor.saveProject (f);
        if (err.isNotEmpty()) safe->showMessage ("Save failed", err);
        safe->unsaved = false;
        safe->updateStatuses();
        safe->homePage->repaint();
    });
}

void LiveAlignEditor::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Open venue", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*" + LiveAlignProcessor::projectExtension());
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto err = safe->processor.openProject (f);
        if (err.isNotEmpty()) { safe->showMessage ("Could not open the venue", err); return; }
        safe->afterProjectLoaded();
    });
}

void LiveAlignEditor::newProject()
{
    processor.newProject();
    afterProjectLoaded();
    showPage ((int) Page::Home);
}

void LiveAlignEditor::newSession()
{
    askText ("New session", "A new visit to " + processor.venueName + ". Mics, positions and the speaker setup carry over.",
             "Session " + juce::String (processor.getSessions().size() + 1) + "  " + LiveAlignProcessor::today(),
             [this] (const juce::String& name) { processor.newSession (name); afterProjectLoaded(); });
}

void LiveAlignEditor::afterProjectLoaded()
{
    micPage->refreshChoices();
    homePage->refresh();
    generator.sync();
    tracesPanel.syncTarget();
    tracesPanel.refresh();
    for (auto& pane : panes) pane->sync();
    pickerMics = -1;
    refreshMicPicker();
    layoutPanes();
    unsaved = processor.projectChanged();
    updateStatuses();
    repaint();
}

// ---------------------------------------------------------------------------- keys, timer, layout

bool LiveAlignEditor::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { saveProject (false); return true; }
    if (key == juce::KeyPress::escapeKey)
    {
        if (la::StandaloneHooks::isFullScreen() && la::StandaloneHooks::isFullScreen()()) toggleFullScreen();
        return true;
    }
    if (dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) != nullptr) return false;
    if (c == 'f' || key == juce::KeyPress (juce::KeyPress::F11Key)) { toggleFullScreen(); return true; }
    if (c == 'g') { toggleGenerator(); return true; }
    if (shownPage != (int) Page::Monitor) return false;
    if (key == juce::KeyPress::spaceKey) { settings.running = ! settings.running.load(); return true; }
    if (c == 'c') { capture(); return true; }
    if (c == 'd') { settings.requestFindDelay = true; return true; }
    if (c == 'r') { settings.requestResetAverages = true; return true; }
    if (c == '1' || c == '2' || c == '3') { settings.view = (int) (c - '1'); panes[0]->sync(); return true; }
    return false;
}

void LiveAlignEditor::timerCallback()
{
    ++tick;
    if (settings.theme.load() != appliedTheme) applyTheme (settings.theme.load());
    if (settings.paneCount.load() != laidOutCount || settings.paneLayout.load() != laidOutLayout) layoutPanes();
    for (auto& pane : panes) pane->sync();

    const bool monitoring = shownPage == (int) Page::Monitor;
    if (processor.analyzer.readSnapshot (snap, snapSerial))
    {
        snapSerial = snap.serial;
        if (monitoring)
        {
            for (int i = 0; i < laidOutCount; ++i) panes[i]->update (snap);
            splPanel.update (snap);
        }
        else if (shownPage == (int) Page::Mics)
            micPage->setSnapshot (snap);
    }

    if (monitoring)
    {
        const int m = settings.focusMic();
        inputPanel.setLevels (processor.takeMicPeak (m), processor.micRmsDb (m), processor.takeRefPeak(), processor.refRmsDb());
        delayPanel.setReport (processor.analyzer.readDelayReport());
        tracesPanel.refresh();
    }

    const bool running = settings.running.load();
    runButton.setButtonText (running ? "RUNNING" : "FROZEN");
    runButton.setColour (juce::TextButton::buttonColourId, running ? pal().good : pal().warn);
    runButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);

    if (processor.analyzer.hasOverflowed()) overloadUntil = juce::Time::getMillisecondCounter() + 3000;

    if (tick % 10 == 0)
    {
        generator.setNumOutputs (processor.numOutputs());
        generator.sync();
        tracesPanel.syncTarget();
        refreshMicPicker();
        const bool fs = la::StandaloneHooks::isFullScreen() && la::StandaloneHooks::isFullScreen()();
        nav.setFullScreenText (fs ? "EXIT FULL SCREEN" : "FULL SCREEN");

        juce::String s;
        if (monitoring)
        {
            s << juce::String (snap.sampleRate / 1000.0, 1) << " kHz   FFT " << snap.fftSize / 1024 << "k   "
              << juce::jmin (snap.tfFrames, settings.averages.load()) << "/" << settings.averages.load() << " avg   1/"
              << settings.smoothing.load() << " oct";
            if (juce::Time::getMillisecondCounter() < overloadUntil) s << "   ANALYSIS BEHIND";
        }
        panes[0]->setStatus (s);
    }

    if (tick % 15 == 0)
    {
        unsaved = processor.projectChanged();
        updateStatuses();
    }
}

void LiveAlignEditor::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.bg);

    // page title, in the Live series header style
    auto t = titleArea;
    g.setColour (p.text);
    g.setFont (font (19.0f, true));
    g.drawText (pageName (shownPage), t.removeFromTop (26), juce::Justification::bottomLeft);
    g.setColour (p.textFaint);
    g.setFont (font (11.5f));
    g.drawText (pageSubtitle (shownPage), t, juce::Justification::topLeft, true);
    g.setColour (p.border);
    g.drawHorizontalLine (titleArea.getBottom() + 6, (float) titleArea.getX(), (float) getWidth() - 12.0f);
}

void LiveAlignEditor::resized()
{
    auto r = getLocalBounds();
    nav.setBounds (r.removeFromLeft (238));
    r.reduce (12, 10);

    auto header = r.removeFromTop (46);
    r.removeFromTop (14);
    if (shownPage == (int) Page::Monitor)
    {
        auto h = header;
        resetButton.setBounds (h.removeFromRight (100).withSizeKeepingCentre (100, 30));
        h.removeFromRight (8);
        runButton.setBounds (h.removeFromRight (100).withSizeKeepingCentre (100, 30));
        h.removeFromRight (14);
        micPicker.setBounds (h.removeFromRight (240).withSizeKeepingCentre (240, 30));
        titleArea = h;
    }
    else
        titleArea = header;

    generator.setBounds (r.removeFromBottom (54));
    r.removeFromBottom (10);
    pageArea = r;

    homePage->setBounds (r);
    micPage->setBounds (r);
    speakerPage->setBounds (r);
    roomPage->setBounds (r);

    auto side = r.removeFromRight (330);
    r.removeFromRight (8);
    if (standalone)
    {
        auto navRow = side.removeFromBottom (34);
        monitorBackButton.setBounds (navRow.removeFromLeft (220));
        side.removeFromBottom (8);
    }
    paneArea = r;
    layoutPanes();

    splPanel.setBounds (side.removeFromTop (186));
    side.removeFromTop (8);
    inputPanel.setBounds (side.removeFromTop (98));
    side.removeFromTop (8);
    delayPanel.setBounds (side.removeFromTop (160));
    side.removeFromTop (8);
    tracesPanel.setBounds (side);
}
