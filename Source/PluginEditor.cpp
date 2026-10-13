// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "PluginEditor.h"
#include "UI/SettingsPanel.h"
#include "UI/CalibrationPanel.h"
#include "StandaloneHooks.h"
#include "Report/MonitoringReport.h"

using namespace la::ui;

namespace
{
const char* pageSubtitle (int page)
{
    static const char* subs[] = {
        "The venue, its sessions, and where each step stands",
        "The tuning mics: inputs, calibration, loopback and room noise",
        "The speakers in the system: roles, channels, outputs and the settings for your processor",
        "A sketch of where the speakers and mics are (not to scale)",
        "Tuning step 1: measure each speaker on its own",
        "Tuning step 2: the crossover between the top and the sub",
        "Tuning step 3: line the speakers up in time where they meet",
        "Tuning step 4: measure the listening positions and EQ toward a target",
        "Tuning step 5: ring out the stage mics",
        "Tuning step 6: what the room does: reverberation, clarity, intelligibility, modes and noise",
        "The mic for monitoring the event, kept apart from the tuning mics",
        "RTA, transfer function, impulse response, waterfall and sound level, with the monitor mic",
        "A PDF of this session's tuning",
        "A PDF of the event: sound level against the limit, the monitor mic and the traces",
        "The measurement mics you own: model, serial number, calibration files and sound level" };
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
    nav.onLocker = [this] { openMicLocker(); };
    nav.onVenue = [this] { showPage ((int) Page::Home); };
    nav.onUpdate = [this] { showUpdate(); };
    processor.updates.onResult = [this] { refreshUpdate(); };
    nav.setLockerAvailable (standalone);
    if (! standalone)
        for (int i = 0; i < kNumPages; ++i) nav.setPageVisible (i, i == (int) Page::Monitor || i == (int) Page::MonitorMic);   // the VST3 only monitors

    // ---- pages ----
    homePage = std::make_unique<HomePage> (p);
    homePage->onGoTo = [this] (int pg) { showPage (pg); };
    homePage->onNewVenue = [this] { confirmDiscard ([this] { newProject(); }); };
    homePage->onNewVenueFromThis = [this]
    {
        confirmDiscard ([this]
        {
            askText ("New venue based on this one", "Keeps the mics, speakers, positions and plan. Measurements, delays, EQ and checks start fresh.",
                     processor.venueName + " (copy)", [this] (const juce::String& name)
            {
                processor.newVenueFrom (name);
                afterProjectLoaded();
            });
        });
    };
    homePage->onOpenVenue = [this] { confirmDiscard ([this] { openProject(); }); };
    homePage->onSave = [this] { saveProject (false); };
    homePage->onSaveAs = [this] { saveProject (true); };
    homePage->onReport = [this] { showPage ((int) Page::TuningReport); };
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
    micPage->onLoadCalFile = [this] (int m, int angle) { loadCalFile (m, angle); };
    micPage->onChanged = [this] { pickerMics = -1; refreshMicPicker(); if (roomPage != nullptr) roomPage->refresh(); };
    micPage->onManageLocker = [this] { openMicLocker(); };
    micPage->onSaveToLocker = [this] (int m)
    {
        askText ("Save to the mic locker", "A name you will recognise, e.g. the model and serial number. Its calibration file and "
                                           "sound level calibration are saved with it.",
                 processor.mics.nameOf (m),
                 [this, m] (const juce::String& name) { processor.saveMicToLocker (m, name); micPage->refreshChoices(); });
    };
    addChildComponent (*micPage);

    speakerPage = std::make_unique<SpeakersPage> (p);
    speakerPage->onImport = [this] (int i) { importSpeaker (i); };
    speakerPage->onChanged = [this] { generator.sync(); updateStatuses(); };
    speakerPage->onGuided = [this] (GuidedSource& s) { openGuided (s); };
    roomPage = std::make_unique<RoomPage> (p);
    roomPage->onChanged = [this] { generator.sync(); updateStatuses(); };
    roomPage->onGuided = [this] (GuidedSource& s) { openGuided (s); };
    roomPage->askText = [this] (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
    {
        askText (title, {}, initial, std::move (done));
    };
    addChildComponent (*speakerPage);

    planPage = std::make_unique<PlanPage> (p);
    planPage->onChanged = [this] { if (roomPage != nullptr) roomPage->refresh(); updateStatuses(); };
    planPage->onExport = [this] { exportPlan(); };
    planPage->onReport = [this] { showPage ((int) Page::TuningReport); };
    planPage->askText = [this] (const juce::String& title, const juce::String& initial, std::function<void (const juce::String&)> done)
    {
        askText (title, {}, initial, std::move (done));
    };
    addChildComponent (*planPage);

    lockerPage = std::make_unique<MicLockerPage> (p);
    lockerPage->onLoadCal = [this] (const juce::String& id, int angle) { loadLockerCal (id, angle); };
    lockerPage->onChanged = [this] { micPage->refreshChoices(); pickerMics = -1; refreshMicPicker(); };
    addChildComponent (*lockerPage);
    addChildComponent (*roomPage);

    feedbackPage = std::make_unique<FeedbackPage> (p);
    feedbackPage->onChanged = [this] { updateStatuses(); };
    addChildComponent (*feedbackPage);
    acousticsPage = std::make_unique<AcousticsPage> (p);
    acousticsPage->onChanged = [this] { updateStatuses(); };
    addChildComponent (*acousticsPage);
    monitorMicPage = std::make_unique<MonitorMicPage> (p);
    monitorMicPage->onChanged = [this] { updateStatuses(); tracesPanel.syncTarget(); for (auto& pane : panes) pane->sync(); };
    monitorMicPage->onCalibrate = [this] { openCalibration (kMonitorMic, nullptr); };
    monitorMicPage->onLoadCal = [this] (int angle) { loadCalFile (kMonitorMic, angle); };
    addChildComponent (*monitorMicPage);
    tuningReportPage = std::make_unique<ReportPage> (p, false);
    tuningReportPage->onExport = [this] { exportReport (false); };
    addChildComponent (*tuningReportPage);
    monitorReportPage = std::make_unique<ReportPage> (p, true);
    monitorReportPage->onExport = [this] { exportReport (true); };
    addChildComponent (*monitorReportPage);

    // ---- monitoring page ----
    for (int i = 0; i < 3; ++i)
    {
        panes[i] = std::make_unique<MeasurePane> (p.settings, p.traces, i);
        addChildComponent (*panes[i]);
    }
    for (auto* c : { (juce::Component*) &splPanel, (juce::Component*) &inputPanel, (juce::Component*) &delayPanel, (juce::Component*) &tracesPanel,
                     (juce::Component*) &eventPanel })
        addChildComponent (*c);

    micPicker.setTooltip ("Which mic the graphs and sound level show, or the average of all mics");
    micPicker.onChange = [this]
    {
        const int id = micPicker.getSelectedId();
        settings.selectedMic = id == 100 ? la::kAverageMic : juce::jmax (0, id - 1);
        monitorMicChosen = true;
        refreshMicPicker();
    };
    addChildComponent (micPicker);

    runButton.setTooltip ("Freeze the graphs to study them; the sound level keeps running (key: Space)");
    runButton.onClick = [this] { settings.running = ! settings.running.load(); };
    addChildComponent (runButton);
    resetButton.setTooltip ("Restart the transfer function and RTA averages (key: R)");
    resetButton.onClick = [this] { settings.requestResetAverages = true; };
    addChildComponent (resetButton);

    splPanel.onCalibrate = [this] { openCalibration (processor.isMonitorMode() ? kMonitorMic : settings.focusMic(), &splPanel.calibrateButton()); };
    tracesPanel.onCapture = [this] { capture(); };
    tracesPanel.onImport = [this] { importTrace(); };
    tracesPanel.onRename = [this] (int i) { renameTrace (i); };
    tracesPanel.onExport = [this] (int i) { exportTrace (i); };
    tracesPanel.onDelete = [this] (int i) { deleteTrace (i); };

    addChildComponent (generator);   // shown on the pages that play test signals

    nextButton.onClick = [this] { if (nextAction) nextAction(); };
    addChildComponent (nextButton);

    applyTheme (settings.theme.load());
    generator.setNumOutputs (p.numOutputs());
    generator.sync();
    refreshMicPicker();

    setResizable (true, true);
    setResizeLimits (1180, 740, 5120, 2880);
    setSize (1480, 920);
    showPage (standalone ? settings.currentTab.load() : (int) Page::Monitor);
    updateStatuses();

    refreshUpdate();
    processor.updates.checkAtStartIfWanted();

    // crash recovery: a running flag left behind means the last run did not close cleanly
    if (processor.isStandalone())
    {
        const auto flag = LiveAlignProcessor::runningFlag();
        const bool crashed = flag.existsAsFile() && LiveAlignProcessor::recoveryFile().existsAsFile();
        flag.getParentDirectory().createDirectory();
        flag.create();
        if (crashed)
        {
            juce::Component::SafePointer<LiveAlignEditor> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->offerRecovery(); });
        }
    }
    startTimerHz (30);
}

void LiveAlignEditor::offerRecovery()
{
    auto x = juce::XmlDocument::parse (LiveAlignProcessor::recoveryFile());
    if (x == nullptr) { processor.clearRecovery(); return; }
    const auto* v = x->getChildByName ("Venue");
    const auto name = v != nullptr ? v->getStringAttribute ("name", "Untitled venue") : juce::String ("Untitled venue");
    const auto when = x->getStringAttribute ("autosaved");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Restore unsaved work?",
                                      "LiveAlign did not close normally last time. \"" + name + "\" had changes that were not saved"
                                          + (when.isNotEmpty() ? " (autosaved " + when + ")" : juce::String()) + ".\n\nRestore them now? You can then save the venue.",
                                      "Restore", "Discard", this),
                                  [safe] (int r)
                                  {
                                      if (safe == nullptr) return;
                                      if (r == 1)
                                      {
                                          const auto err = safe->processor.restoreRecovery();
                                          if (err.isNotEmpty()) safe->showMessage ("Not restored", err);
                                          safe->afterProjectLoaded();
                                      }
                                      else safe->processor.clearRecovery();
                                  });
}

LiveAlignEditor::~LiveAlignEditor()
{
    stopTimer();
    if (processor.isStandalone())   // a clean close: nothing to recover next time
    {
        processor.clearRecovery();
        LiveAlignProcessor::runningFlag().deleteFile();
    }
    processor.updates.onResult = nullptr;
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

    const bool spk = page == (int) Page::Speakers || page == (int) Page::Response || page == (int) Page::Crossover || page == (int) Page::Align;
    const bool mon = page == (int) Page::Monitor;
    // monitoring uses its own mic: switch the analysis over on the monitoring pages, back on the others
    const bool monitoringPage = mon || page == (int) Page::MonitorMic;
    if (monitoringPage != processor.isMonitorMode())
    {
        processor.setMonitorMode (monitoringPage);
        pickerMics = -1;
        refreshMicPicker();
    }
    if (page != (int) Page::Feedback) feedbackPage->stopListening();

    homePage->setVisible (page == (int) Page::Home);
    micPage->setVisible (page == (int) Page::Mics);
    speakerPage->setVisible (spk);
    if (spk) speakerPage->setMode (page == (int) Page::Speakers ? 0 : page == (int) Page::Response ? 1 : page == (int) Page::Crossover ? 2 : 3);
    roomPage->setVisible (page == (int) Page::Room);
    planPage->setVisible (page == (int) Page::Plan);
    lockerPage->setVisible (page == (int) Page::Locker);
    feedbackPage->setVisible (page == (int) Page::Feedback);
    acousticsPage->setVisible (page == (int) Page::Acoustics);
    monitorMicPage->setVisible (page == (int) Page::MonitorMic);
    tuningReportPage->setVisible (page == (int) Page::TuningReport);
    monitorReportPage->setVisible (page == (int) Page::MonitorReport);
    nav.setLockerActive (page == (int) Page::Locker);
    if (page == (int) Page::Locker) lockerPage->refresh();
    if (page == (int) Page::Plan) planPage->refresh();
    for (int i = 0; i < 3; ++i) panes[i]->setVisible (mon && i < juce::jlimit (1, 3, settings.paneCount.load()));
    for (auto* c : { (juce::Component*) &splPanel, (juce::Component*) &inputPanel, (juce::Component*) &delayPanel, (juce::Component*) &tracesPanel,
                     (juce::Component*) &runButton, (juce::Component*) &resetButton, (juce::Component*) &eventPanel })
        c->setVisible (mon);
    micPicker.setVisible (false);   // monitoring has one mic
    // the generator is for tuning; monitoring listens to the programme
    generator.setVisible (page == (int) Page::Response || page == (int) Page::Crossover || page == (int) Page::Align || page == (int) Page::Room);
    if (! generator.isVisible() && page != (int) Page::Mics) { settings.genOn = false; generator.sync(); }

    if (page == (int) Page::Home) homePage->refresh();
    if (page == (int) Page::Mics) { micPage->refreshChoices(); if (snapSerial > 0) micPage->setSnapshot (snap); }
    if (spk) speakerPage->setSnapshot (snap);
    if (page == (int) Page::Room) { roomPage->refresh(); roomPage->setSnapshot (snap); }
    if (page == (int) Page::Feedback) { feedbackPage->refresh(); feedbackPage->setSnapshot (snap); }
    if (page == (int) Page::Acoustics) acousticsPage->refresh();
    if (page == (int) Page::MonitorMic) { monitorMicPage->refresh(); monitorMicPage->setSnapshot (snap); }
    if (page == (int) Page::TuningReport) tuningReportPage->refresh();
    if (page == (int) Page::MonitorReport) monitorReportPage->refresh();
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
    if (processor.isMonitorMode())
    {
        const auto& mm = processor.monitorMic;
        inputPanel.setMicLabel (mm.name + "  In " + juce::String (mm.input + 1));
        splPanel.setMicName ("From " + mm.name + (mm.position.isNotEmpty() ? "  -  " + mm.position : juce::String()) + "  (Monitor mic page)");
        return;
    }
    inputPanel.setMicLabel (processor.mics.nameOf (settings.focusMic()) + "  In " + juce::String (settings.micInput[settings.focusMic()].load() + 1));
    {
        const int fm = settings.focusMic();
        const bool avg = settings.selectedMic.load() >= la::kAverageMic && n > 1;
        juce::String who = avg ? "Average of " + juce::String (n) + " mics" : processor.mics.nameOf (fm);
        if (! avg && processor.mics.position[(size_t) fm].isNotEmpty()) who << "  -  " << processor.mics.position[(size_t) fm];
        splPanel.setMicName ("From " + who + "  (pick the mic at the top)");
    }
}

// Readiness of each step, for the navigation pane and the home page.
void LiveAlignEditor::updateStatuses()
{
    std::array<StepInfo, kNumPages> steps {};
    for (auto& st : steps) st.status = StepStatus::None;
    const int done = micPage->completedSteps(), total = MicSetupPage::kSteps;
    const bool setupChanged = processor.checks.setup.isNotEmpty() && processor.checks.setup != processor.setupFingerprint();
    bool everCalibrated = false;
    for (int m = 0; m < processor.tuningMicCount(); ++m) everCalibrated = everCalibrated || (! processor.isMonitorMode() || m > 0 ? settings.micSplCalibrated[m].load() : false);
    auto set = [&steps] (Page pg, StepStatus s, const juce::String& shortText, const juce::String& detail, float fraction = 0.0f)
    {
        auto& st = steps[(size_t) pg];
        st.status = s; st.shortText = shortText; st.detail = detail; st.fraction = fraction;
    };

    // ---- setup ----
    if (setupChanged) set (Page::Mics, StepStatus::Attention, "Wiring changed", "The interface or mic wiring changed since this session's checks.");
    else if (done == total && ! micPage->hasWarning()) set (Page::Mics, StepStatus::Ready, "Ready", "All " + juce::String (total) + " checks done for this session.");
    else if (micPage->hasWarning()) set (Page::Mics, StepStatus::Attention, juce::String (done) + " of " + juce::String (total) + ", warning", "A check needs attention.");
    else set (Page::Mics, done == 0 ? StepStatus::NotStarted : StepStatus::InProgress, juce::String (done) + " of " + juce::String (total) + " done",
              everCalibrated ? "Set up before: re-check the sound level, loopback and noise for this session." : "Mics, calibration, loopback and room noise.",
              (float) done / (float) total);

    const auto& sys = processor.system;
    int measured = 0;
    for (int i = 0; i < (int) sys.sources.size(); ++i) measured += sys.covered (i) ? 1 : 0;
    const int nSrc = (int) sys.sources.size();
    if (processor.speakers.changed) set (Page::Speakers, StepStatus::Attention, "Marked changed", "The system was marked as changed: measure the speakers again.");
    else if (processor.speakers.configured) set (Page::Speakers, StepStatus::Ready, "Set up", "Set up " + processor.speakers.configuredOn + ". Nothing has changed since.");
    else set (Page::Speakers, StepStatus::None, juce::String (nSrc) + (nSrc == 1 ? " speaker" : " speakers"),
              juce::String (nSrc) + (nSrc == 1 ? " speaker" : " speakers") + " in the system. Mark them as set up once tuned.");
    set (Page::Plan, StepStatus::None, processor.plan.drawn() ? "Sketched" : "Optional sketch", processor.plan.drawn() ? "Sketched." : "An optional sketch of the setup.");

    // ---- tuning ----
    if (measured == 0) set (Page::Response, StepStatus::NotStarted, "Not started", "Measure each speaker on its own.");
    else if (measured < nSrc) set (Page::Response, StepStatus::InProgress, juce::String (measured) + " of " + juce::String (nSrc), "Measure the remaining speakers.",
                                   (float) measured / (float) juce::jmax (1, nSrc));
    else set (Page::Response, StepStatus::Ready, "All measured", "Every speaker measured.");

    if (sys.pairValid() && sys.pairInverted()) set (Page::Crossover, StepStatus::Attention, "Pair upside down", "HIGH is a sub and LOW a top: swap them.");
    else if (speakerPage->crossoverDone()) set (Page::Crossover, StepStatus::Ready, speakerPage->stepSummaryText (1), "Crossover set: " + speakerPage->stepSummaryText (1) + ".");
    else set (Page::Crossover, measured == nSrc && nSrc > 0 ? StepStatus::InProgress : StepStatus::NotStarted, "Not set", "Find the crossover between the top and the sub.", 0.3f);

    if (speakerPage->alignmentRequired())
    {
        if (speakerPage->alignmentsDone()) set (Page::Align, StepStatus::Ready, "Lined up", speakerPage->stepSummaryText (2) + ".");
        else set (Page::Align, sys.alignments.empty() ? StepStatus::NotStarted : StepStatus::InProgress, speakerPage->stepSummaryText (2),
                  "Line the speakers up where they meet.", 0.5f);
    }
    else set (Page::Align, sys.alignments.empty() ? StepStatus::None : speakerPage->alignmentsDone() ? StepStatus::Ready : StepStatus::InProgress,
              sys.alignments.empty() ? "Optional" : speakerPage->stepSummaryText (2), "Optional: left / right, fills and delays.");

    {
        const auto& rt = processor.room;
        const int before = rt.count (false), after = rt.count (true);
        if (rt.tuned) set (Page::Room, StepStatus::Ready, "Tuned", "Room tuned " + rt.tunedOn + ".");
        else if (before == 0) set (Page::Room, StepStatus::NotStarted, "Not started", "Measure the listening positions, then compare with the target.");
        else set (Page::Room, StepStatus::InProgress, juce::String (before) + (before == 1 ? " position" : " positions") + (after > 0 ? ", checking" : ""),
                  roomPage->nextHint(), after > 0 ? 0.85f : (rt.suggested.empty() ? 0.4f : 0.6f));
    }
    {
        const auto& fb = processor.feedback;
        if (fb.done) set (Page::Feedback, StepStatus::Ready, "Rung out", juce::String ((int) fb.rings.size()) + " rings on " + fb.micName + ".");
        else if (fb.rings.empty()) set (Page::Feedback, StepStatus::None, "Optional", "Ring out the stage mics.");
        else set (Page::Feedback, StepStatus::InProgress, juce::String ((int) fb.rings.size()) + " rings", "Rings found on " + fb.micName + ".", 0.5f);
    }
    {
        const int n = processor.room.acousticsCount();
        if (n == 0) set (Page::Acoustics, StepStatus::None, "From Room EQ", "Worked out from the Room EQ measurements.");
        else
        {
            const auto a = processor.room.acousticsAverage();
            set (Page::Acoustics, StepStatus::Ready, "RT " + juce::String (a.rtMid(), 1) + " s, STI " + juce::String (a.sti, 2),
                 "Reverberation " + juce::String (a.rtMid(), 2) + " s, speech " + la::acoustics::stiRating (a.sti) + " (STI " + juce::String (a.sti, 2) + ").");
        }
    }

    // ---- monitoring (no ticks: it runs during the event) ----
    {
        const auto& mm = processor.monitorMic;
        const bool calOk = mm.cal.any() || mm.noCalFile;
        set (Page::MonitorMic, calOk && mm.splCalibrated ? StepStatus::Ready : StepStatus::None, mm.name,
             mm.name + (mm.position.isNotEmpty() ? " at " + mm.position : juce::String()) + (mm.splCalibrated ? ", calibrated." : ", sound level not calibrated."));
        set (Page::Monitor, StepStatus::None, processor.splLog.empty() ? "" : "Logging", "Live RTA, transfer function and sound level.");
    }
    // ---- reports ----
    set (Page::TuningReport, StepStatus::None, "", "The tuning as a PDF.");
    set (Page::MonitorReport, StepStatus::None, "", processor.splLog.empty() ? "The event as a PDF." : "The event as a PDF, with the sound level log.");

    for (int i = 0; i < kNumPages; ++i) nav.setStatus (i, steps[(size_t) i].status, steps[(size_t) i].shortText, steps[(size_t) i].fraction);
    homePage->setSteps (steps, setupChanged);

    const auto& list = processor.getSessions();
    const int cur = processor.currentSession();
    nav.setVenue (processor.venueName, cur < (int) list.size() ? list[(size_t) cur].name : juce::String(), unsaved);
    refreshNext();
}

// The one next action for the shown page: finish what is left here, or go on to the next step.
void LiveAlignEditor::refreshNext()
{
    juce::String text;
    std::function<void()> action;
    auto goTo = [this] (Page pg) { return [this, pg] { showPage ((int) pg); }; };
    auto cont = [&] (Page pg) { text = "Continue to " + juce::String (pageName ((int) pg)); action = goTo (pg); };
    switch ((Page) shownPage)
    {
        case Page::Home:
        {
            if (micPage->nextHint().isNotEmpty()) { text = "Start: Mics"; action = goTo (Page::Mics); }
            else if (! speakerPage->responsesDone()) { text = "Next: Speaker response"; action = goTo (Page::Response); }
            else if (! speakerPage->crossoverDone()) { text = "Next: Crossover"; action = goTo (Page::Crossover); }
            else if (speakerPage->alignmentRequired() && ! speakerPage->alignmentsDone()) { text = "Next: Time alignment"; action = goTo (Page::Align); }
            else if (! processor.room.tuned) { text = "Next: Room EQ"; action = goTo (Page::Room); }
            else { text = "Tuning report"; action = goTo (Page::TuningReport); }
            break;
        }
        case Page::Mics:
        {
            const auto h = micPage->nextHint();
            if (h.isNotEmpty()) { text = "Next: " + h; action = [this] { micPage->showNextStep(); }; }
            else cont (Page::Speakers);
            break;
        }
        case Page::Speakers: cont (processor.plan.drawn() ? Page::Response : Page::Plan); break;
        case Page::Plan: cont (Page::Response); break;
        case Page::Response: if (speakerPage->busy()) text = "Measuring..."; else if (! speakerPage->responsesDone()) { text = "Next: measure each speaker"; action = [this] { speakerPage->requestMeasure(); }; } else cont (Page::Crossover); break;
        case Page::Crossover:
            if (speakerPage->busy()) text = "Searching...";
            else if (! speakerPage->crossoverDone()) { text = "Next: Find the best crossover"; action = [this] { speakerPage->findCrossover(); }; }
            else cont (speakerPage->alignmentRequired() ? Page::Align : Page::Room);
            break;
        case Page::Align: if (speakerPage->busy()) text = "Measuring..."; else cont (Page::Room); break;
        case Page::Room:
        {
            const auto h = roomPage->nextHint();
            if (roomPage->busy()) text = "Measuring...";
            else if (h.isNotEmpty()) { text = "Next: " + (h.startsWith ("Enter") ? juce::String ("Measure after EQ") : h); action = [this] { roomPage->doNext(); }; }
            else cont (Page::Feedback);
            break;
        }
        case Page::Feedback:
        {
            const auto h = feedbackPage->nextHint();
            if (h.isNotEmpty() && ! feedbackPage->isListening()) { text = "Next: " + h; action = [this] { feedbackPage->doNext(); }; }
            else if (! feedbackPage->isListening()) cont (Page::Acoustics);
            break;
        }
        case Page::Acoustics: text = "Tuning report"; action = goTo (Page::TuningReport); break;
        case Page::MonitorMic:
        {
            const auto h = monitorMicPage->nextHint();
            if (h.isNotEmpty()) { text = "Next: " + h; action = [this] { monitorMicPage->doNext(); }; }
            else cont (Page::Monitor);
            break;
        }
        case Page::TuningReport: text = "Export the PDF"; action = [this] { exportReport (false); }; break;
        case Page::MonitorReport: text = "Export the PDF"; action = [this] { exportReport (true); }; break;
        case Page::Monitor:
        case Page::Locker:
        default: break;
    }
    if (text.isNotEmpty() && action != nullptr) text << juce::String (juce::CharPointer_UTF8 ("  \xe2\x86\x92"));
    nextAction = std::move (action);
    const bool show = standalone && text.isNotEmpty();
    const bool layout = show != nextButton.isVisible() || text != nextButton.getButtonText();
    nextButton.setButtonText (text);
    nextButton.setEnabled (nextAction != nullptr);
    nextButton.setTooltip (shownPage == (int) Page::Room ? roomPage->nextHint() : juce::String());
    nextButton.setVisible (show);
    nextButton.setColour (juce::TextButton::buttonColourId, nextAction != nullptr ? pal().accent : pal().panelRaised);
    nextButton.setColour (juce::TextButton::textColourOffId, nextAction != nullptr ? pal().onAccent : pal().textDim);
    if (layout) resized();
}

// ---------------------------------------------------------------------------- pop-ups

void LiveAlignEditor::openSettings()
{
    auto panel = std::make_unique<SettingsPanel> (processor);
    panel->onThemeChanged = [this] { applyTheme (settings.theme.load()); };
    panel->onLayoutChanged = [this] { layoutPanes(); };
    juce::CallOutBox::launchAsynchronously (std::move (panel), nav.getBounds().withTrimmedTop (nav.getHeight() - 60), this);
}

void LiveAlignEditor::refreshUpdate()
{
    const auto& u = processor.updates.info();
    nav.setUpdate (u.state == la::UpdateInfo::Available ? u.latest : juce::String());
}

void LiveAlignEditor::showUpdate()
{
    const auto& u = processor.updates.info();
    if (u.state != la::UpdateInfo::Available) return;
    juce::String msg = "LiveAlign " + u.latest + " is available. You have " + juce::String (JucePlugin_VersionString) + ".";
    if (u.notes.isNotEmpty()) msg << "\n\n" << u.notes;
    msg << "\n\nThe download page opens in your browser. Your venues, traces and mic locker are kept when you install it.";
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::InfoIcon, "Update available", msg,
                                                                                "Download", "Later", this),
                                  [safe] (int r) { if (safe != nullptr && r == 1) safe->processor.updates.openReleasePage(); });
}

// The venue plan as a PNG picture, to print or share.
void LiveAlignEditor::exportPlan()
{
    const auto name = juce::File::createLegalFileName (processor.venueName + " plan");
    chooser = std::make_unique<juce::FileChooser> ("Save the venue plan as a picture",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".png"), "*.png");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension ("png");
        const auto img = safe->planPage->getCanvas().exportImage();
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat png;
        if (! os.openedOk() || ! png.writeImageToStream (img, os)) safe->showMessage ("Not saved", "Could not write " + f.getFullPathName());
    });
}

// The tuning report as a PDF, opened when written.
// The venue plan picture for a report: the tuning layer or the monitoring layer.
juce::Image LiveAlignEditor::planImage (int layer)
{
    processor.plan.placeMissing (juce::jmin ((int) processor.system.sources.size(), la::VenuePlan::kMaxSpeakers), processor.tuningMicCount());
    return planPage->getCanvas().exportImage (2200, layer);
}

void LiveAlignEditor::exportReportTo (bool monitoring, const juce::File& f)
{
    processor.syncMonitorFromSlot();
    const auto opt = (monitoring ? monitorReportPage : tuningReportPage)->options();
    const auto err = monitoring ? la::report::writeMonitoringReport (processor, f, planImage (1), opt)
                                : la::report::writeTuningReport (processor, f, planImage (0), opt);
    if (err.isNotEmpty()) showMessage ("Report not saved", err);
}

void LiveAlignEditor::exportReport (bool monitoring)
{
    const auto& sessions = processor.getSessions();
    const int cur = processor.currentSession();
    const auto name = juce::File::createLegalFileName (processor.venueName + " - " + (cur < (int) sessions.size() ? sessions[(size_t) cur].name : juce::String ("session"))
                                                       + (monitoring ? " - monitoring report" : " - tuning report"));
    auto dir = processor.projectFile != juce::File() ? processor.projectFile.getParentDirectory()
                                                     : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> (monitoring ? "Save the monitoring report" : "Save the tuning report", dir.getChildFile (name + ".pdf"), "*.pdf");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe, monitoring] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension ("pdf");
        safe->exportReportTo (monitoring, f);
        if (f.existsAsFile()) f.startAsProcess();
    });
}

void LiveAlignEditor::openMicLocker() { showPage ((int) Page::Locker); }

// Calibration file(s) for a mic in the locker; angle -1 = one or both, placed by their names.
void LiveAlignEditor::loadLockerCal (const juce::String& id, int angle)
{
    chooser = std::make_unique<juce::FileChooser> (angle < 0 ? juce::String ("Load the calibration file(s): select the 0 and 90 degree files together")
                                                              : "Load the " + la::calAngleName (angle) + " calibration file",
                                                   juce::File(), "*.txt;*.cal;*.frd;*.csv");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    const int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                             | (angle < 0 ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
    chooser->launchAsync (chooserFlags, [safe, id, angle] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto files = fc.getResults();
        auto* e = safe->processor.locker.find (id);
        if (files.isEmpty() || e == nullptr) return;
        while (files.size() > 2) files.remove (2);
        int angles[2] { angle, angle };
        if (angle < 0)
        {
            for (int i = 0; i < files.size(); ++i) angles[i] = la::MicCalSet::angleFromName (files[i].getFileName());
            if (files.size() == 2 && angles[0] == angles[1]) { angles[0] = la::Cal0; angles[1] = la::Cal90; }
        }
        juce::StringArray errors;
        for (int i = 0; i < files.size(); ++i)
        {
            auto t = la::parseFreqTable (files[i].loadFileAsString().toStdString());
            if (t.freq.size() < 3) { errors.add ("No calibration data found in " + files[i].getFileName()); continue; }
            e->cals.table[angles[i]] = std::move (t);
            e->cals.name[angles[i]] = files[i].getFileName();
            e->noCalFile = false;
        }
        safe->processor.locker.save();
        safe->processor.lockerEntryChanged (id);
        if (! errors.isEmpty()) safe->showMessage ("Calibration file not loaded", errors.joinIntoString ("\n"));
        safe->lockerPage->refresh();
        safe->micPage->refreshChoices();
    });
}

void LiveAlignEditor::openCalibration (int mic, juce::Component* anchor)
{
    std::unique_ptr<CalibrationPanel> panel;
    if (mic == kMonitorMic)
    {
        // the monitoring mic: analysis slot 1 while monitoring
        if (! processor.isMonitorMode()) processor.setMonitorMode (true);
        panel = std::make_unique<CalibrationPanel> (settings, 0, processor.monitorMic.name,
            [this] { return ! snap.mics.empty() ? snap.mics[0].rawZSlowDbfs : -200.0f; },
            [this]
            {
                const auto& c = processor.monitorMic.cal;
                const int a = c.pick (processor.monitorMic.aim);
                return a < 0 ? juce::String() : la::calAngleName (a) + ": " + c.name[a];
            });
        panel->onLoadFile = [this] { loadCalFile (kMonitorMic); };
        panel->onClearFile = [this] { processor.monitorMic.cal.clearAll(); processor.applyMonitorMic(); monitorMicPage->refresh(); };
        panel->onVerified = [this]
        {
            processor.syncMonitorFromSlot();
            processor.monitorMic.splDate = LiveAlignProcessor::today();
            processor.monitorMic.configured = true;
            monitorMicPage->refresh();
        };
    }
    else
    {
        panel = std::make_unique<CalibrationPanel> (settings, mic, processor.mics.nameOf (mic),
            [this, mic] { return mic < (int) snap.mics.size() ? snap.mics[(size_t) mic].rawZSlowDbfs : -200.0f; },
            [this, mic]
            {
                const int a = processor.activeCalAngle (mic);
                return a < 0 ? juce::String() : la::calAngleName (a) + ": " + processor.calibrations (mic).name[a];
            });
        panel->onLoadFile = [this, mic] { loadCalFile (mic); };
        panel->onClearFile = [this, mic] { processor.clearCalibration (mic); };
        panel->onVerified = [this, mic] { processor.checks.splVerified[(size_t) mic] = true; };
    }
    auto area = anchor != nullptr ? getLocalArea (anchor, anchor->getLocalBounds()) : getLocalBounds().withSizeKeepingCentre (10, 10);
    juce::CallOutBox::launchAsynchronously (std::move (panel), area, this);
}

// angle -1: one or both files at once; each goes to 0 or 90 degrees from its name (e.g. miniDSP's "_90deg").
void LiveAlignEditor::loadCalFile (int mic, int angle)
{
    const auto who = mic == kMonitorMic ? processor.monitorMic.name : processor.mics.nameOf (mic);
    const auto title = angle < 0 ? "Load the calibration file(s) for " + who + " (select both 0 and 90 degree files if you have them)"
                                 : "Load the " + la::calAngleName (angle) + " calibration file for " + who;
    chooser = std::make_unique<juce::FileChooser> (title, juce::File(), "*.txt;*.cal;*.frd;*.csv");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    const int chooserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                      | (angle < 0 ? juce::FileBrowserComponent::canSelectMultipleItems : 0);
    chooser->launchAsync (chooserFlags, [safe, mic, angle] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto files = fc.getResults();
        if (files.isEmpty()) return;
        while (files.size() > 2) files.remove (2);
        // two files: the one named for 90 degrees goes there, the other to 0 (if neither or both say 90, the order picked)
        int angles[2] { angle, angle };
        if (angle < 0)
        {
            for (int i = 0; i < files.size(); ++i) angles[i] = la::MicCalSet::angleFromName (files[i].getFileName());
            if (files.size() == 2 && angles[0] == angles[1]) { angles[0] = la::Cal0; angles[1] = la::Cal90; }
        }
        juce::StringArray done, errors;
        for (int i = 0; i < files.size(); ++i)
        {
            const auto err = mic == kMonitorMic ? safe->processor.loadMonitorCalibration (files[i], angles[i])
                                                : safe->processor.loadCalibrationFile (mic, files[i], angles[i]);
            if (err.isNotEmpty()) errors.add (err);
            else done.add (la::calAngleName (angles[i]) + ": " + files[i].getFileName());
        }
        if (! errors.isEmpty()) safe->showMessage ("Calibration file not loaded", errors.joinIntoString ("\n"));
        else if (angle < 0 && mic >= 0) safe->micPage->setNote (mic, "Loaded " + done.joinIntoString (", ") + ". Wrong angle? Use the file menu to load it as the other one.");
        if (mic == kMonitorMic) safe->monitorMicPage->refresh();
        else safe->micPage->refreshChoices();
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

// A speaker's response from a file: manufacturer data or a measurement from another analyser.
void LiveAlignEditor::importSpeaker (int source)
{
    if (source < 0 || source >= (int) processor.system.sources.size()) return;
    chooser = std::make_unique<juce::FileChooser> ("Import the response of " + processor.system.sources[(size_t) source].name, juce::File(), "*.frd;*.txt;*.csv");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, source] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        auto& sys = safe->processor.system;
        if (source >= (int) sys.sources.size()) return;
        auto t = la::parseFreqTable (f.loadFileAsString().toStdString());
        if (t.freq.size() < 4) { safe->showMessage ("Import failed", "No frequency response found in " + f.getFileName() + "."); return; }
        juce::String note;
        if (! t.hasPhase()) { la::xo::addMinimumPhase (t); note = " (no phase in the file: minimum phase used)"; }
        auto& s = sys.sources[(size_t) source];
        s.response = std::move (t);
        s.imported = true;
        s.hasArrival = false;
        s.source = f.getFileName() + note;
        safe->speakerPage->sourceImported();
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
    speakerPage->refreshChoices();
    planPage->refresh();
    roomPage->refresh();
    feedbackPage->refresh();
    acousticsPage->refresh();
    monitorMicPage->refresh();
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
    if (c == 'g' && generator.isVisible()) { toggleGenerator(); return true; }
    if (shownPage != (int) Page::Monitor) return false;
    if (key == juce::KeyPress::spaceKey) { settings.running = ! settings.running.load(); return true; }
    if (c == 'c') { capture(); return true; }
    if (c == 'd') { settings.requestFindDelay = true; return true; }
    if (c == 'r') { settings.requestResetAverages = true; return true; }
    if (c >= '1' && c <= '4') { settings.view = (int) (c - '1'); panes[0]->sync(); return true; }
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
        else if (speakerPage->isVisible())
            speakerPage->setSnapshot (snap);
        else if (shownPage == (int) Page::Room)
            roomPage->setSnapshot (snap);
        else if (shownPage == (int) Page::Feedback)
            feedbackPage->setSnapshot (snap);
        else if (shownPage == (int) Page::MonitorMic)
            monitorMicPage->setSnapshot (snap);
    }

    // the sound level log of the event, about once a second while monitoring
    if (processor.isMonitorMode() && snapSerial > 0)
    {
        const double now = (double) juce::Time::currentTimeMillis() / 1000.0;
        if (now - lastLogSecond >= 1.0)
        {
            lastLogSecond = now;
            processor.splLog.add (now, snap.splAS, snap.splCS, snap.splAF);
        }
    }

    if (monitoring)
    {
        const int m = settings.focusMic();
        inputPanel.setLevels (processor.takeMicPeak (m), processor.micRmsDb (m), processor.takeRefPeak(), processor.refRmsDb());
        delayPanel.setReport (processor.analyzer.readDelayReport());
        tracesPanel.refresh();
        if (tick % 15 == 0) eventPanel.setLog (processor.splLog);
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

        juce::String s;
        if (monitoring)
        {
            s << juce::String (snap.sampleRate / 1000.0, 1) << " kHz   FFT " << snap.fftSize / 1024 << "k   "
              << juce::jmin (snap.tfFrames, settings.averages.load()) << "/" << settings.averages.load() << " avg   1/"
              << settings.smoothing.load() << " oct";
            // signal quality: how much of the transfer function is above the minimum coherence
            if (snap.tfFrames > 0 && snap.tfCoh.size() == snap.tfFreq.size())
            {
                int n = 0, good = 0;
                for (size_t i = 0; i < snap.tfFreq.size(); ++i)
                {
                    if (snap.tfFreq[i] < 40.0 || snap.tfFreq[i] > 16000.0 || ! std::isfinite (snap.tfCoh[i])) continue;
                    ++n;
                    good += snap.tfCoh[i] >= juce::jmax (0.2f, settings.coherenceBlank.load()) ? 1 : 0;
                }
                if (n > 0) s << "   RELIABLE " << juce::roundToInt (100.0 * good / n) << "%";
            }
            {
                const int fm = settings.focusMic();
                const int a = processor.activeCalAngle (fm);
                if (a >= 0 && settings.selectedMic.load() < la::kAverageMic) s << "   CAL " << la::calAngleName (a);
            }
            if (juce::Time::getMillisecondCounter() < overloadUntil) s << "   ANALYSIS BEHIND";
        }
        panes[0]->setStatus (s);
    }

    if (tick % 15 == 0 && shownPage != (int) Page::Monitor) { refreshNext(); if (shownPage == (int) Page::Home || tick % 60 == 0) updateStatuses(); }
    if (processor.isStandalone() && tick % (30 * 120) == 0) processor.autosave();   // every two minutes
    if (tick % 60 == 0 && processor.syncLocker() && shownPage == (int) Page::Mics) micPage->refreshChoices();

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

void LiveAlignEditor::openGuided (GuidedSource& source)
{
    guidedOverlay = std::make_unique<GuidedOverlay> (processor, source);
    guidedOverlay->onChanged = [this] { planPage->refresh(); updateStatuses(); };
    guidedOverlay->onClose = [this]
    {
        // closed from inside its own callback: let it go once that has returned
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LiveAlignEditor> (this)]
        {
            if (safe == nullptr || safe->guidedOverlay == nullptr) return;
            safe->removeChildComponent (safe->guidedOverlay.get());
            safe->guidedOverlay.reset();
            safe->planPage->refresh();
            safe->updateStatuses();
        });
    };
    addAndMakeVisible (*guidedOverlay);
    guidedOverlay->setBounds (getLocalBounds());
    guidedOverlay->toFront (true);
    guidedOverlay->grabKeyboardFocus();
}

void LiveAlignEditor::resized()
{
    if (guidedOverlay != nullptr) guidedOverlay->setBounds (getLocalBounds());
    auto r = getLocalBounds();
    nav.setBounds (r.removeFromLeft (NavRail::kWidth));
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
    {
        auto h = header;
        if (nextButton.isVisible())
        {
            const int w = juce::jlimit (160, 420, juce::GlyphArrangement::getStringWidthInt (font (12.5f, true), nextButton.getButtonText()) + 44);
            nextButton.setBounds (h.removeFromRight (w).withSizeKeepingCentre (w, 32));
        }
        titleArea = h;
    }

    if (generator.isVisible())
    {
        generator.setBounds (r.removeFromBottom (54));
        r.removeFromBottom (10);
    }
    pageArea = r;

    homePage->setBounds (r);
    micPage->setBounds (r);
    speakerPage->setBounds (r);
    roomPage->setBounds (r);
    planPage->setBounds (r);
    lockerPage->setBounds (r);
    for (auto* pg : std::initializer_list<juce::Component*> { feedbackPage.get(), acousticsPage.get(), monitorMicPage.get(), tuningReportPage.get(), monitorReportPage.get() })
        pg->setBounds (r);

    auto side = r.removeFromRight (330);
    r.removeFromRight (8);
    paneArea = r;
    layoutPanes();

    splPanel.setBounds (side.removeFromTop (196));
    side.removeFromTop (8);
    inputPanel.setBounds (side.removeFromTop (98));
    side.removeFromTop (8);
    delayPanel.setBounds (side.removeFromTop (160));
    side.removeFromTop (8);
    eventPanel.setBounds (side.removeFromTop (90));
    side.removeFromTop (8);
    tracesPanel.setBounds (side);
}
