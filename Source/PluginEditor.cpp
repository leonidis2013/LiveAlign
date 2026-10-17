// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "PluginEditor.h"
#include "Clock.h"
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
        "The tuning mics: inputs, where each one is, calibration, the reference, loopback and room noise",
        "The speakers in the system: roles, channels, outputs, cabinets and how widely they cover",
        "The measurement points for tuning and monitoring, the mic at each, and the speakers, on a sketch of the room",
        "measure each speaker close up; its polarity comes from the same capture",
        "the crossover between the top and the sub",
        "each speaker's level against the others, where the audience hears them",
        "line the speakers up in time where they meet",
        "what the room does, and whether EQ can help: reverberation, clarity, intelligibility",
        "measure the listening points and EQ toward a target",
        "ring out the stage mics",
        "The mic for monitoring the event, kept apart from the tuning mics",
        "RTA, transfer function, impulse response, waterfall and sound level, with the monitor mic",
        "Every setting to enter in your processor, output by output",
        "A PDF of this session's tuning",
        "A PDF of the event: sound level against the limit, the monitor mic and the traces",
        "Every measurement mic you own: model, serial number, calibration files and sound level",
        "Your preferences for every venue, and this venue's own tuning values",
        "The venue: its name and notes, the room, the conditions of the day and the targets",
        "each speaker across the seats it covers: re-aim one that falls off before trimming or EQ",
        "pink noise in steps up to the show level: the headroom above it, and the speaker that runs out first",
        "music you know, at each point: how it sounds, with the EQ in and bypassed" };
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
    homePage->onOpenFile = [this] (const juce::File& f)
    {
        confirmDiscard ([this, f]
        {
            const auto err = processor.openProject (f);
            if (err.isNotEmpty()) { showMessage ("Could not open the venue", err); return; }
            afterProjectLoaded();
        });
    };
    homePage->onRenameFile = [this] (const la::VenueEntry& v)
    {
        askText ("Rename venue", "The venue's name and its file.", v.name, [this, v] (const juce::String& name)
        {
            const auto to = la::venues::freeFile (v.file.getParentDirectory(), name);
            if (v.file == processor.projectFile)
            {
                processor.venueName = name;
                const auto err = processor.saveProject (to);
                if (err.isNotEmpty()) { showMessage ("Not renamed", err); return; }
                if (to != v.file) v.file.deleteFile();
            }
            else
            {
                auto x = juce::XmlDocument::parse (v.file);
                if (x == nullptr) { showMessage ("Not renamed", v.file.getFileName() + " could not be read."); return; }
                if (auto* ve = x->getChildByName ("Venue")) ve->setAttribute ("name", name);
                if (! x->writeTo (to)) { showMessage ("Not renamed", "Could not write " + to.getFullPathName()); return; }
                if (to != v.file) v.file.deleteFile();
            }
            la::venues::renamed (v.file, to);
            unsaved = processor.projectChanged();
            updateStatuses();
            homePage->rescanVenues();
        });
    };
    homePage->onDuplicateFile = [this] (const la::VenueEntry& v)
    {
        askText ("Duplicate venue", "A copy of the venue with all its sessions, under a new name.", v.name + " (copy)", [this, v] (const juce::String& name)
        {
            auto x = juce::XmlDocument::parse (v.file);
            if (x == nullptr) { showMessage ("Not copied", v.file.getFileName() + " could not be read."); return; }
            if (auto* ve = x->getChildByName ("Venue")) ve->setAttribute ("name", name);
            const auto to = la::venues::freeFile (v.file.getParentDirectory(), name);
            if (! x->writeTo (to)) { showMessage ("Not copied", "Could not write " + to.getFullPathName()); return; }
            homePage->rescanVenues();
        });
    };
    homePage->onDeleteFile = [this] (const juce::File& f)
    {
        juce::Component::SafePointer<LiveAlignEditor> safe (this);
        const bool open = f == processor.projectFile;
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::WarningIcon, "Delete venue",
                                          "Move \"" + f.getFileNameWithoutExtension() + "\" to the trash, with all its sessions?"
                                              + (open ? juce::String ("\n\nIt stays open here, unsaved, until you save it again or open another venue.") : juce::String()),
                                          "Delete", "Cancel", this),
                                      [safe, f, open] (int r)
                                      {
                                          if (safe == nullptr || r != 1) return;
                                          if (! f.moveToTrash()) { safe->showMessage ("Not deleted", "Could not move " + f.getFullPathName() + " to the trash."); return; }
                                          la::venues::forget (f);
                                          if (open) safe->processor.projectFile = juce::File();
                                          safe->unsaved = safe->processor.projectChanged() || open;
                                          safe->updateStatuses();
                                          safe->homePage->rescanVenues();
                                      });
    };
    homePage->onSave = [this] { saveProject (false); };
    homePage->onSaveAs = [this] { saveProject (true); };
    homePage->onReport = [this] { showPage ((int) Page::TuningReport); };
    homePage->onNewSession = [this] { newSession(); };
    homePage->onRecheck = [this]
    {
        if (! (processor.isStandalone() || processor.uiAsStandalone)) return;
        recheck->begin();
        openGuided (*recheck);
    };
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
    micPage->onShowLockerMic = [this] (const juce::String& id) { showPage ((int) Page::Locker); lockerPage->selectId (id); };
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
    speakerPage->onEditSpeakers = [this] { showPage ((int) Page::Speakers); };
    speakerPage->onOpenSheet = [this] { showPage ((int) Page::Sheet); };
    roomPage = std::make_unique<RoomPage> (p);
    roomPage->onChanged = [this] { generator.sync(); updateStatuses(); };
    roomPage->onGuided = [this] (GuidedSource& s) { openGuided (s); };
    roomPage->onOpenPlan = [this] { showPage ((int) Page::Plan); };
    roomPage->onOpenVenue = [this] { showPage ((int) Page::Venue); };
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

    coveragePage = std::make_unique<CoveragePage> (p);
    coveragePage->onChanged = [this] { generator.sync(); levelPage->refresh(); updateStatuses(); };
    coveragePage->onGuided = [this] (GuidedSource& s) { openGuided (s); };
    coveragePage->onOpenLevel = [this] { showPage ((int) Page::Level); };
    coveragePage->onOpenPlan = [this] { showPage ((int) Page::Plan); };
    coveragePage->setSnapshot (snap);
    addChildComponent (*coveragePage);
    headroomPage = std::make_unique<HeadroomPage> (p);
    headroomPage->onChanged = [this] { generator.sync(); updateStatuses(); };
    headroomPage->onOpenVenue = [this] { showPage ((int) Page::Venue); };
    headroomPage->setSnapshot (snap);
    addChildComponent (*headroomPage);
    listeningPage = std::make_unique<ListeningPage> (p);
    listeningPage->onChanged = [this] { roomPage->refresh(); updateStatuses(); };
    listeningPage->onRemeasure = [this] (const juce::String& point) { showPage ((int) Page::Room); roomPage->remeasurePoint (point); };
    addChildComponent (*listeningPage);

    undoButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb6")));
    redoButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x86\xb7")));
    undoButton.setTooltip ("Undo the last change: EQ, trims, delays, speakers, points, venue values (Cmd/Ctrl+Z)");
    redoButton.setTooltip ("Redo (Cmd/Ctrl+Shift+Z)");
    undoButton.onClick = [this] { undo(); };
    redoButton.onClick = [this] { redo(); };
    addChildComponent (undoButton);
    addChildComponent (redoButton);
    helpButton.setTooltip ("Help: how to do this step, what anything on the screen is, the glossary, the tour");
    helpButton.onClick = [this] { showHelpMenu(); };
    addAndMakeVisible (helpButton);

    venuePage = std::make_unique<VenueDetailsPage> (p);
    venuePage->onChanged = [this] { if (roomPage != nullptr) roomPage->refresh(); acousticsPage->refresh(); updateStatuses(); };
    venuePage->onRecheck = [this] { if (homePage->onRecheck) homePage->onRecheck(); };
    addChildComponent (*venuePage);

    lockerPage = std::make_unique<MicLockerPage> (p);
    lockerPage->onLoadCal = [this] (const juce::String& id, int angle) { loadLockerCal (id, angle); };
    lockerPage->onChanged = [this] { micPage->refreshChoices(); pickerMics = -1; refreshMicPicker(); };
    addChildComponent (*lockerPage);
    settingsPage = std::make_unique<SettingsPage> (p);
    settingsPage->onThemeChanged = [this] { applyTheme (settings.theme.load()); settingsPage->refresh(); };
    settingsPage->onLayoutChanged = [this] { layoutPanes(); };
    settingsPage->onPrefsChanged = [this] { applyPrefs(); };
    settingsPage->onVenueChanged = [this] { roomPage->refresh(); levelPage->refresh(); acousticsPage->refresh(); updateStatuses(); };
    settingsPage->onShowTour = [this] { showTour (0); };
    addChildComponent (*settingsPage);
    addChildComponent (*roomPage);

    feedbackPage = std::make_unique<FeedbackPage> (p);
    feedbackPage->onChanged = [this] { updateStatuses(); };
    addChildComponent (*feedbackPage);
    acousticsPage = std::make_unique<AcousticsPage> (p);
    acousticsPage->onChanged = [this] { updateStatuses(); if (roomPage != nullptr) roomPage->refresh(); };
    acousticsPage->onGuided = [this] (GuidedSource& s) { openGuided (s); };
    acousticsPage->onOpenPlan = [this] { showPage ((int) Page::Plan); };
    acousticsPage->onOpenVenue = [this] { showPage ((int) Page::Venue); };
    acousticsPage->onEditSpeakers = [this] { showPage ((int) Page::Speakers); };
    addChildComponent (*acousticsPage);
    levelPage = std::make_unique<LevelPage> (p);
    levelPage->onChanged = [this] { generator.sync(); updateStatuses(); };
    levelPage->onGuided = [this] (GuidedSource& s) { openGuided (s); };
    levelPage->onOpenPlan = [this] { showPage ((int) Page::Plan); };
    levelPage->onOpenSheet = [this] { showPage ((int) Page::Sheet); };
    levelPage->setSnapshot (snap);
    addChildComponent (*levelPage);
    sheetPage = std::make_unique<SheetPage> (p);
    sheetPage->onChanged = [this] { updateStatuses(); };
    sheetPage->onExport = [this] (bool csv) { exportSheet (csv); };
    sheetPage->onRollBack = [this] (int i)
    {
        if (i < 0 || i >= (int) processor.history.entries.size()) return;
        const auto& e = processor.history.entries[(size_t) i];
        juce::Component::SafePointer<LiveAlignEditor> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Roll back",
                                          "Put every output's processing back as it was before " + e.output + ": " + e.what + " (" + e.when + ")?\n\n"
                                              "Undo (Cmd/Ctrl+Z) brings it back again.", "Roll back", "Cancel", this),
                                      [safe, i] (int r) { if (safe != nullptr && r == 1) safe->rollBackTo (i); });
    };
    addChildComponent (*sheetPage);
    recheck = std::make_unique<RecheckGuide> (p);
    recheck->setSnapshot (snap);
    recheck->onChanged = [this] { homePage->repaint(); updateStatuses(); };
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
                     (juce::Component*) &eventPanel, (juce::Component*) &fbWatch, (juce::Component*) &tunedPanel, (juce::Component*) &condPanel })
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

    fbWatch.onClear = [this] { la::ui::feedbackMarks().clear(); liveFb.reset(); for (auto& pane : panes) pane->repaint(); };
    splPanel.onCalibrate = [this] { openCalibration (processor.isMonitorMode() ? kMonitorMic : settings.focusMic(), &splPanel.calibrateButton()); };
    tracesPanel.onCapture = [this] { capture(); };
    tracesPanel.onImport = [this] { importTrace(); };
    tracesPanel.onRename = [this] (int i) { renameTrace (i); };
    tracesPanel.onExport = [this] (int i) { exportTrace (i); };
    tracesPanel.onDelete = [this] (int i) { deleteTrace (i); };

    addChildComponent (generator);   // shown on the pages that play test signals


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
    // the first time LiveAlign runs: the tour
    if (processor.isStandalone() && la::UserPrefs::get ("tourShown") != "1")
    {
        juce::Component::SafePointer<LiveAlignEditor> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->tour == nullptr) safe->showTour (0); });
    }
    juce::Desktop::getInstance().addGlobalMouseListener (&inputWatch);
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
    juce::Desktop::getInstance().removeGlobalMouseListener (&inputWatch);
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
    page = standalone ? juce::jlimit (0, kNumPages - 1, page) : page == (int) Page::Settings ? page : (int) Page::Monitor;
    settings.currentTab = page;
    if (shownPage != (int) Page::Settings && shownPage >= 0) lastPage = shownPage;
    shownPage = page;
    nav.setCurrent (page);

    const bool spk = page == (int) Page::Speakers || page == (int) Page::Response || page == (int) Page::Crossover || page == (int) Page::Align;
    const bool mon = page == (int) Page::Monitor;
    // monitoring uses its own mic: switch the analysis over on the monitoring pages, back on the others
    const bool monitoringPage = mon || page == (int) Page::MonitorMic;
    // Settings leaves the analysis as it was (monitoring or tuning)
    if (page != (int) Page::Settings && monitoringPage != processor.isMonitorMode())
    {
        processor.setMonitorMode (monitoringPage);
        pickerMics = -1;
        refreshMicPicker();
    }
    if (page != (int) Page::Feedback) feedbackPage->stopListening();

    homePage->setVisible (page == (int) Page::Home);
    levelPage->setVisible (page == (int) Page::Level);
    sheetPage->setVisible (page == (int) Page::Sheet);
    micPage->setVisible (page == (int) Page::Mics);
    speakerPage->setVisible (spk);
    if (spk) speakerPage->setMode (page == (int) Page::Speakers ? 0 : page == (int) Page::Response ? 1 : page == (int) Page::Crossover ? 2 : 3);
    roomPage->setVisible (page == (int) Page::Room);
    planPage->setVisible (page == (int) Page::Plan);
    venuePage->setVisible (page == (int) Page::Venue);
    coveragePage->setVisible (page == (int) Page::Coverage);
    headroomPage->setVisible (page == (int) Page::Headroom);
    listeningPage->setVisible (page == (int) Page::Listening);
    if (page == (int) Page::Coverage) coveragePage->refresh();
    if (page == (int) Page::Headroom) headroomPage->refresh();
    if (page == (int) Page::Listening) listeningPage->refresh();
    if (page == (int) Page::Venue) venuePage->refresh();
    lockerPage->setVisible (page == (int) Page::Locker);
    settingsPage->setVisible (page == (int) Page::Settings);
    nav.setSettingsActive (page == (int) Page::Settings);
    if (page == (int) Page::Settings) settingsPage->refresh();
    feedbackPage->setVisible (page == (int) Page::Feedback);
    acousticsPage->setVisible (page == (int) Page::Acoustics);
    monitorMicPage->setVisible (page == (int) Page::MonitorMic);
    tuningReportPage->setVisible (page == (int) Page::TuningReport);
    monitorReportPage->setVisible (page == (int) Page::MonitorReport);
    if (page == (int) Page::Locker) lockerPage->refresh();
    if (page == (int) Page::Plan) planPage->refresh();
    for (int i = 0; i < 3; ++i) panes[i]->setVisible (mon && i < juce::jlimit (1, 3, settings.paneCount.load()));
    for (auto* c : { (juce::Component*) &splPanel, (juce::Component*) &inputPanel, (juce::Component*) &delayPanel, (juce::Component*) &tracesPanel,
                     (juce::Component*) &runButton, (juce::Component*) &resetButton, (juce::Component*) &eventPanel, (juce::Component*) &fbWatch, (juce::Component*) &tunedPanel })
        c->setVisible (mon);
    condPanel.setVisible (mon && processor.climate.enabled());
    if (mon) updateConditionsPanel();
    micPicker.setVisible (false);   // monitoring has one mic
    // the generator strip: only on the pages that play test signals (monitoring listens to the programme)
    generator.setVisible (page == (int) Page::Response || page == (int) Page::Level || page == (int) Page::Coverage || page == (int) Page::Align || page == (int) Page::Room);
    if (! generator.isVisible() && page != (int) Page::Mics) { settings.genOn = false; generator.sync(); }

    if (page == (int) Page::Home) homePage->refresh();
    if (page == (int) Page::Mics) { micPage->refreshChoices(); if (snapSerial > 0) micPage->setSnapshot (snap); }
    if (spk) speakerPage->setSnapshot (snap);
    if (page == (int) Page::Room) { roomPage->refresh(); roomPage->setSnapshot (snap); }
    if (page == (int) Page::Feedback) { feedbackPage->refresh(); feedbackPage->setSnapshot (snap); }
    if (page == (int) Page::Acoustics) acousticsPage->refresh();
    if (page == (int) Page::Level) levelPage->refresh();
    if (page == (int) Page::Sheet) sheetPage->refresh();
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
    if (setupChanged)
    {
        const auto ch = processor.setupChanges();
        set (Page::Mics, StepStatus::Attention, processor.interfaceChanged() ? "Interface changed" : "Wiring changed",
             "Changed since this session's checks: " + ch.joinIntoString ("; ") + ". Measure the room noise again on Mics.");
    }
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
    else set (Page::Speakers, nSrc > 0 ? StepStatus::Ready : StepStatus::NotStarted, juce::String (nSrc) + (nSrc == 1 ? " speaker" : " speakers"),
              juce::String (nSrc) + (nSrc == 1 ? " speaker" : " speakers") + " in the system: roles, channels and outputs.");
    {
        const int pts = processor.positions.size();
        set (Page::Plan, pts > 0 ? StepStatus::Ready : StepStatus::NotStarted, juce::String (pts) + (pts == 1 ? " point" : " points"),
             pts > 0 ? juce::String (pts) + " tuning points: " + processor.positions.joinIntoString (", ") + "."
                     : juce::String ("Add the points (FOH, a few seats) where the mic goes."));
        // Venue: the speakers and the points together
        const bool sizeKnown = processor.plan.volume() > 0.0;
        if (processor.speakers.changed) set (Page::Venue, StepStatus::Attention, "Speakers changed", "The system was marked as changed: measure the speakers again.");
        else if (nSrc == 0) set (Page::Venue, StepStatus::NotStarted, "No speakers yet", "Add the speakers, the points and the room's details.");
        else if (pts == 0) set (Page::Venue, StepStatus::InProgress, juce::String (nSrc) + (nSrc == 1 ? " speaker, no points" : " speakers, no points"),
                                "Add the points where the mic goes (Plan and points).", 0.5f);
        else set (Page::Venue, StepStatus::Ready, juce::String (nSrc) + (nSrc == 1 ? " speaker, " : " speakers, ") + juce::String (pts) + (pts == 1 ? " point" : " points"),
                  juce::String (nSrc) + " speakers and " + juce::String (pts) + " tuning points" + (sizeKnown ? "." : "; the room's size is not entered (Details)."));
    }

    // ---- tuning ----
    if (measured == 0) set (Page::Response, StepStatus::NotStarted, "Not started", "Measure each speaker on its own.");
    else if (measured < nSrc) set (Page::Response, StepStatus::InProgress, juce::String (measured) + " of " + juce::String (nSrc), "Measure the remaining speakers.",
                                   (float) measured / (float) juce::jmax (1, nSrc));
    else set (Page::Response, StepStatus::Ready, "All measured", "Every speaker measured.");
    for (int i = 0; i < nSrc; ++i)
    {
        const auto& s = sys.sources[(size_t) i];
        if (s.polaritySummary() == la::polarity::Inverted)
        {
            const auto* pc = s.polarityOf (0);
            const bool opp = pc != nullptr && pc->against >= 0 && pc->against != i && pc->relative == la::polarity::Inverted;
            set (Page::Response, StepStatus::Attention, "Polarity", opp ? s.name + " is opposite to " + sys.nameOf (pc->against) + ": check the wiring."
                                                                        : s.name + "'s polarity looks inverted: check its wiring.");
            break;
        }
    }

    if (! sys.xoNeeded())
    {
        if (! sys.strayCrossoverFilters().isEmpty())
            set (Page::Crossover, StepStatus::Attention, "Remove filters", "No sub, so no crossover: remove the crossover filters left on "
                                                                             + sys.strayCrossoverFilters().joinIntoString (", ") + ".");
        else
        {
            bool hp = false;
            for (int i = 0; i < (int) sys.sources.size() && ! hp; ++i) { la::xo::Filter f; hp = sys.topHighPass (i, f); }
            if (hp) set (Page::Crossover, StepStatus::InProgress, "Tops: high-pass", "No sub: add a protective high-pass to the tops.", 0.5f);
            else set (Page::Crossover, StepStatus::None, "Not needed", "No sub in the system: no crossover to set.");
        }
    }
    else if (sys.pairValid() && sys.pairInverted()) set (Page::Crossover, StepStatus::Attention, "Pair upside down", "HIGH is a sub and LOW a top: swap them.");
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
        if (coveragePage->done()) set (Page::Coverage, StepStatus::Ready, processor.coverage.done && coveragePage->problems() > 0 ? "Accepted" : "Within spec",
                                       "Each speaker covers its area" + juce::String (processor.coverage.before.empty() ? "." : " (re-aimed).")); 
        else if (! coveragePage->measured()) set (Page::Coverage, StepStatus::NotStarted, "Needs Level balance", "Each speaker across the seats it covers, from Level balance.");
        else set (Page::Coverage, StepStatus::Attention, juce::String (coveragePage->problems()) + " to re-aim", coveragePage->nextHint());
    }
    {
        const int lv = headroomPage->verdictLevel();
        if (lv < 0) set (Page::Headroom, StepStatus::NotStarted, "Not run", "Pink noise in steps up to the show level at FOH.");
        else if (lv == 0) set (Page::Headroom, StepStatus::Ready, "Headroom OK", "Clean above the show level.");
        else if (lv == 1) set (Page::Headroom, StepStatus::InProgress, "Little headroom", "Less headroom than wanted, or not run to the end.", 0.6f);
        else set (Page::Headroom, StepStatus::Attention, "Short of the level", "The system cannot reach the show level cleanly.");
    }
    {
        const auto& l = processor.listening;
        const int heard = l.count (1) + l.count (2);
        if (l.done) set (Page::Listening, StepStatus::Ready, "Done", "Listened at " + juce::String (heard) + " points" + (l.count (2) > 0 ? ", " + juce::String (l.count (2)) + " with notes." : "."));
        else if (heard == 0) set (Page::Listening, StepStatus::NotStarted, "Not started", "Music you know, at each point, with the EQ in and bypassed.");
        else set (Page::Listening, StepStatus::InProgress, juce::String (heard) + " of " + juce::String (processor.positions.size()) + " points",
                  listeningPage->nextHint(), (float) heard / (float) juce::jmax (1, processor.positions.size()));
    }
    {
        const auto& fb = processor.feedback;
        if (fb.done) set (Page::Feedback, StepStatus::Ready, "Rung out", juce::String ((int) fb.rings.size()) + " rings on " + fb.micName + ".");
        else if (fb.rings.empty()) set (Page::Feedback, StepStatus::None, "Optional", "Ring out the stage mics.");
        else set (Page::Feedback, StepStatus::InProgress, juce::String ((int) fb.rings.size()) + " rings", "Rings found on " + fb.micName + ".", 0.5f);
    }
    {
        // green only from real sweeps; an estimate from the Room EQ pink noise is still to do
        const auto& rt = processor.room;
        juce::String why;
        const int verdict = rt.acousticsVerdict (why);
        if (rt.acousticsFromSweeps())
        {
            const auto a = rt.acousticsAverage();
            const bool all = acousticsPage->nextHint().isEmpty();
            set (Page::Acoustics, verdict >= 3 ? StepStatus::Attention : all ? StepStatus::Ready : StepStatus::InProgress,
                 verdict >= 3 ? juce::String ("Treat before EQ") : "RT " + juce::String (a.rtMid(), 1) + " s, STI " + juce::String (a.sti, 2), why, 0.6f);
        }
        else if (rt.acousticsCount() > 0) set (Page::Acoustics, StepStatus::InProgress, "Estimate only", "An estimate from the Room EQ pink noise: measure with a sweep.", 0.3f);
        else set (Page::Acoustics, StepStatus::NotStarted, "Not measured", "Sweep at the listening points: does the room let EQ help?");
    }
    {
        const int total = levelPage->tasksCount(), got = levelPage->readingsCount (false);
        if (levelPage->done()) set (Page::Level, StepStatus::Ready, processor.level.applied ? "Trimmed" : "Balanced", "The speakers' levels are balanced.");
        else if (got == 0) set (Page::Level, StepStatus::NotStarted, "Not started", "Each speaker alone at the listening points.");
        else if (got >= total && levelPage->attentionCount (false) > 0 && ! processor.level.applied)
            set (Page::Level, StepStatus::Attention, juce::String (levelPage->attentionCount (false)) + " to trim", "Put the trims on the processor sheet.");
        else if (got >= total && processor.level.applied)
            set (Page::Level, StepStatus::InProgress, levelPage->readingsCount (true) > 0 ? juce::String (levelPage->attentionCount (true)) + " still out" : juce::String ("Check the trims"),
                 levelPage->nextHint().isNotEmpty() ? levelPage->nextHint() : juce::String ("Measure again to check the trims."), 0.8f);
        else set (Page::Level, StepStatus::InProgress, juce::String (got) + " of " + juce::String (total), levelPage->nextHint(), (float) got / (float) juce::jmax (1, total));
    }
    {
        const int n = la::sheet::enteredCount (sys), nLines = la::sheet::lineCount (sys);
        set (Page::Sheet, n == nLines && nLines > 0 ? StepStatus::Ready : n > 0 ? StepStatus::InProgress : StepStatus::None,
             juce::String (n) + " of " + juce::String (nLines) + " entered", "Tick each line once its settings are in the processor.", (float) n / (float) juce::jmax (1, nLines));
        processor.speakers.configured = n == nLines && nLines > 0;
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

// The next step: the first one in the flow that is not done, with what to do there. The navigation marks it and
// Home shows it; the steps themselves are reached only from the navigation.
void LiveAlignEditor::refreshNext()
{
    int page = -1;
    juce::String what;
    auto pick = [&] (Page pg, const juce::String& w) { if (page < 0) { page = (int) pg; what = w; } };
    const auto& sys = processor.system;
    if (sys.sources.empty()) pick (Page::Speakers, "add the speakers");
    if (micPage->nextHint().isNotEmpty()) pick (Page::Mics, micPage->nextHint());
    if (processor.positions.isEmpty()) pick (Page::Plan, "add the listening points");
    if (! speakerPage->responsesDone()) pick (Page::Response, "measure each speaker close up");
    if (headroomPage->nextHint().isNotEmpty()) pick (Page::Headroom, headroomPage->nextHint().toLowerCase());
    for (const auto& s : sys.sources) if (s.polaritySummary() == la::polarity::Inverted) { pick (Page::Response, "fix " + s.name + "'s polarity and measure it again"); break; }
    if (! speakerPage->crossoverDone()) pick (Page::Crossover, "find the crossover");
    if (! levelPage->done()) pick (Page::Level, levelPage->nextHint().isNotEmpty() ? levelPage->nextHint().toLowerCase() : juce::String ("balance the levels"));
    if (coveragePage->nextHint().isNotEmpty() && levelPage->readingsCount (false) > 0) pick (Page::Coverage, coveragePage->nextHint().toLowerCase());
    if (speakerPage->alignmentRequired() && ! speakerPage->alignmentsDone()) pick (Page::Align, "line up " + juce::String (sys.alignments.empty() ? "the fills or delays" : "the speakers"));
    if (! processor.room.acousticsFromSweeps() || acousticsPage->nextHint().isNotEmpty())
        pick (Page::Acoustics, acousticsPage->nextHint().isNotEmpty() ? acousticsPage->nextHint().toLowerCase() : juce::String ("sweep the room"));
    if (! processor.room.tuned && roomPage->nextHint().startsWith ("Listen")) pick (Page::Listening, listeningPage->nextHint().toLowerCase());
    if (! processor.room.tuned) pick (Page::Room, roomPage->nextHint().isNotEmpty() ? roomPage->nextHint().toLowerCase() : juce::String ("EQ toward the target"));
    if (listeningPage->nextHint().isNotEmpty()) pick (Page::Listening, listeningPage->nextHint().toLowerCase());
    if (la::sheet::enteredCount (sys) < la::sheet::lineCount (sys)) pick (Page::Sheet, "enter the settings and tick them off");
    nav.setNext (page, what);
    homePage->setNext (page, what);
}

// Where venues and exports go by default: the folder in Settings (Reports and files), else Documents/LiveAlign Venues.
static juce::File homeFolder() { return la::venues::folder (true); }

// ---------------------------------------------------------------------------- pop-ups

void LiveAlignEditor::openSettings() { showPage (shownPage == (int) Page::Settings ? lastPage : (int) Page::Settings); }

// This computer's preferences that the editor uses (feedback watch, paper size) and the processor's (generator).
void LiveAlignEditor::applyPrefs()
{
    const auto& ap = la::AppPrefs::get();
    prefsSerial = ap.serial;
    processor.applyAppPrefs();
    const int sense = juce::roundToInt (ap.value ("fbSense"));
    // Normal is what monitoring always used; Low waits longer for a bigger peak, High takes a smaller, shorter one
    static const double prom[] = { 18.0, 15.0, 12.0 };
    static const int frames[] = { 24, 18, 12 };
    liveFb.setOptions ({ 80.0, 12000.0, prom[juce::jlimit (0, 2, sense)], frames[juce::jlimit (0, 2, sense)], 3, 1.0 / 12.0 });
    la::pdf::Document::useA4() = ap.value ("paperA4") > 0.5;
    generator.sync();
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
                                                   homeFolder().getChildFile (name + ".png"), "*.png");
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
                                                     : homeFolder();
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

// The processor sheet as a CSV (a spreadsheet) or a PDF.
void LiveAlignEditor::exportSheet (bool csv)
{
    const auto name = juce::File::createLegalFileName (processor.venueName + " - processor sheet");
    auto dir = processor.projectFile != juce::File() ? processor.projectFile.getParentDirectory() : homeFolder();
    chooser = std::make_unique<juce::FileChooser> (csv ? "Save the processor sheet as a CSV file" : "Save the processor sheet as a PDF",
                                                   dir.getChildFile (name + (csv ? ".csv" : ".pdf")), csv ? "*.csv" : "*.pdf");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe, csv] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        auto f = fc.getResult();
        if (f == juce::File()) return;
        f = f.withFileExtension (csv ? "csv" : "pdf");
        juce::String err;
        if (csv) { if (! f.replaceWithText (la::sheet::csv (safe->processor.system))) err = "Could not write " + f.getFullPathName(); }
        else err = safe->sheetPage->writePdf (f);
        if (err.isNotEmpty()) safe->showMessage ("Not saved", err);
        else if (! csv) f.startAsProcess();
    });
}

// Calibration file(s) for a mic in the locker; angle -1 = one or both, placed by their names.
void LiveAlignEditor::loadLockerCal (const juce::String& id, int angle)
{
    chooser = std::make_unique<juce::FileChooser> (angle < 0 ? juce::String ("Load the calibration file(s): select every angle's file together (0, 30, 90 degrees ...)")
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
        while (files.size() > la::kCalSlots) files.remove (la::kCalSlots);
        std::vector<bool> guessed;
        const auto angles = safe->anglesFor (files, angle, &e->cals, &guessed);
        juce::StringArray errors;
        for (int i = 0; i < files.size(); ++i)
        {
            auto t = la::parseFreqTable (files[i].loadFileAsString().toStdString());
            if (t.freq.size() < 3) { errors.add ("No calibration data found in " + files[i].getFileName()); continue; }
            e->cals.table[angles[(size_t) i]] = std::move (t);
            e->cals.name[angles[(size_t) i]] = files[i].getFileName();
            e->cals.guessed[angles[(size_t) i]] = guessed[(size_t) i];
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

// The angle of each file chosen: `angle` when one was picked, otherwise from its name ("_90deg", "30deg" ...). Two
// files whose names say nothing different go to 0 and 90 degrees, in the order picked.
std::vector<int> LiveAlignEditor::anglesFor (const juce::Array<juce::File>& files, int angle, const la::MicCalSet* existing, std::vector<bool>* guessed)
{
    std::vector<int> a;
    std::vector<bool> g;
    bool taken[la::kCalSlots] {};
    for (const auto& f : files)
    {
        const int n = angle >= 0 ? angle : la::MicCalSet::angleInName (f.getFileName());
        a.push_back (n);
        g.push_back (n < 0);
        if (n >= 0) taken[n] = true;
    }
    // a name with no angle: the first angle still free (0, then 90 ...), flagged so it can be checked
    la::MicCalSet none;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i] < 0)
        {
            const int f = (existing != nullptr ? *existing : none).firstFree (taken);
            a[i] = f >= 0 ? f : la::Cal0;
            taken[a[i]] = true;
        }
    if (guessed != nullptr) *guessed = g;
    return a;
}

// angle -1: one or both files at once; each goes to 0 or 90 degrees from its name (e.g. miniDSP's "_90deg").
void LiveAlignEditor::loadCalFile (int mic, int angle)
{
    const auto who = mic == kMonitorMic ? processor.monitorMic.name : processor.mics.nameOf (mic);
    const auto title = angle < 0 ? "Load the calibration file(s) for " + who + " (select every angle's file together: 0, 30, 90 degrees ...)"
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
        while (files.size() > la::kCalSlots) files.remove (la::kCalSlots);
        const auto angles = safe->anglesFor (files, angle, mic == kMonitorMic ? &safe->processor.monitorMic.cal : &safe->processor.calibrations (mic));
        juce::StringArray done, errors;
        for (int i = 0; i < files.size(); ++i)
        {
            const auto err = mic == kMonitorMic ? safe->processor.loadMonitorCalibration (files[i], angles[(size_t) i])
                                                : safe->processor.loadCalibrationFile (mic, files[i], angles[(size_t) i]);
            if (err.isNotEmpty()) errors.add (err);
            else done.add (la::calAngleName (angles[(size_t) i]) + ": " + files[i].getFileName());
        }
        if (! errors.isEmpty()) safe->showMessage ("Calibration file not loaded", errors.joinIntoString ("\n"));
        else if (angle < 0 && mic >= 0) safe->micPage->setNote (mic, "Loaded " + done.joinIntoString (", ") + ". Wrong angle? Use the file menu to load it for another angle.");
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
                                                   homeFolder().getChildFile (name + ".frd"), "*.frd");
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
    if (! processor.projectChangedNow()) { then(); return; }
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsYesNoCancel (juce::MessageBoxIconType::QuestionIcon, "Unsaved changes",
                                      "Save the changes to \"" + processor.venueName + "\" first?", "Save", "Don't save", "Cancel", this),
                                  [safe, then] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1)
        {
            if (safe->processor.projectFile == juce::File()) { safe->saveProject (true, then); return; }   // carries on once the file is chosen
            safe->saveProject (false);
            then();
        }
        else if (r == 2) then();
    });
}

void LiveAlignEditor::saveProject (bool askForFile, std::function<void()> then)
{
    if (! askForFile && processor.projectFile != juce::File())
    {
        const auto err = processor.saveProject (processor.projectFile);
        if (err.isNotEmpty()) showMessage ("Save failed", err);
        unsaved = false;
        updateStatuses();
        homePage->rescanVenues();
        if (then) then();
        return;
    }
    auto dir = processor.projectFile != juce::File() ? processor.projectFile.getParentDirectory()
                                                     : homeFolder();
    chooser = std::make_unique<juce::FileChooser> ("Save venue",
                                                   dir.getChildFile (juce::File::createLegalFileName (processor.venueName) + LiveAlignProcessor::projectExtension()),
                                                   "*" + LiveAlignProcessor::projectExtension());
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [safe, then] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto err = safe->processor.saveProject (f);
        if (err.isNotEmpty()) { safe->showMessage ("Save failed", err); return; }
        safe->unsaved = false;
        safe->updateStatuses();
        safe->homePage->rescanVenues();
        if (then) then();
    });
}

void LiveAlignEditor::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Open venue", homeFolder(),
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
    refreshAllPages();
    resetUndo();
}

void LiveAlignEditor::refreshAllPages()
{
    micPage->refreshChoices();
    speakerPage->refreshChoices();
    planPage->refresh();
    roomPage->refresh();
    feedbackPage->refresh();
    acousticsPage->refresh();
    monitorMicPage->refresh();
    homePage->refresh();
    settingsPage->refresh();
    venuePage->refresh();
    levelPage->refresh();
    coveragePage->refresh();
    headroomPage->refresh();
    listeningPage->refresh();
    sheetPage->refresh();
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
    if (dynamic_cast<juce::TextEditor*> (getCurrentlyFocusedComponent()) == nullptr && standalone)
    {
        // undo and redo: Cmd/Ctrl+Z, Cmd/Ctrl+Shift+Z (or Ctrl+Y)
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { undo(); return true; }
        if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
            || key == juce::KeyPress ('y', juce::ModifierKeys::commandModifier, 0)) { redo(); return true; }
    }
    if (key == juce::KeyPress (juce::KeyPress::F1Key)) { showHowTo(); return true; }
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
    if (tick % 10 == 0 && standalone)
    {
        const bool typing = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent()) != nullptr;
        if (pendingFp.isNotEmpty() || typing || inputWatch.recent() || tick % 60 == 0) trackChanges();
        undoButton.setEnabled (canUndo() || pendingFp.isNotEmpty());
        redoButton.setEnabled (canRedo());
    }
    if (toastText.isNotEmpty() && la::clock::ms32() > toastUntil) { toastText.clear(); repaint(); }
    if (la::AppPrefs::get().serial != prefsSerial) applyPrefs();
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
            if (snap.serial % 8 == 0) updateTunedPanel();
            // feedback watch: the newest spectrum of the monitoring mic
            if (fbWatch.watching() && snap.wfFrame != liveFbFrame && ! snap.wfLine.empty())
            {
                liveFbFrame = snap.wfFrame;
                for (const auto& r : liveFb.push (snap.wfFreq, snap.wfLine))
                {
                    fbWatch.add ({ r.freq, r.prominenceDb, r.cutDb, juce::Time::getCurrentTime().formatted ("%H:%M:%S") });
                    auto& marks = la::ui::feedbackMarks();
                    marks.push_back (r.freq);
                    if (marks.size() > 6) marks.erase (marks.begin());
                }
            }
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
        // delay tracking: find the delay on the programme every 5 seconds
        if (delayPanel.tracking() && settings.refSource.load() >= 1 && la::clock::ms32() - lastTrackRequest > (juce::uint32) (1000.0 * la::AppPrefs::get().value ("trackSec")))
        {
            lastTrackRequest = la::clock::ms32();
            settings.requestFindDelay = true;
        }
        tracesPanel.refresh();
        if (tick % 15 == 0) { eventPanel.setLog (processor.splLog); updateConditionsPanel(); }
    }

    const bool running = settings.running.load();
    runButton.setButtonText (running ? "RUNNING" : "FROZEN");
    runButton.setColour (juce::TextButton::buttonColourId, running ? pal().good : pal().warn);
    runButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);

    if (processor.analyzer.hasOverflowed()) overloadUntil = la::clock::ms32() + 3000;

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
            if (la::clock::ms32() < overloadUntil) s << "   ANALYSIS BEHIND";
        }
        panes[0]->setStatus (s);
    }

    if (tick % 15 == 0 && shownPage != (int) Page::Monitor) refreshNext();
    if (processor.isStandalone() && tick % juce::jmax (30 * 60, (int) (30 * 60 * la::AppPrefs::get().value ("autosaveMin"))) == 0) processor.autosave();
    if (tick % 60 == 0 && processor.syncLocker() && shownPage == (int) Page::Mics) micPage->refreshChoices();

    if (tick % 15 == 0)
    {
        // every half second: has anything changed since the save, and where does each step stand
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
    g.drawText ((isTuningStep (shownPage) ? "Tuning step " + juce::String (tuningStepNumber (shownPage)) + ": " : juce::String()) + pageSubtitle (shownPage),
                t, juce::Justification::topLeft, true);
    g.setColour (p.border);
    g.drawHorizontalLine (titleArea.getBottom() + 6, (float) titleArea.getX(), (float) getWidth() - 12.0f);
}

void LiveAlignEditor::paintOverChildren (juce::Graphics& g)
{
    if (toastText.isEmpty() || la::clock::ms32() > toastUntil) return;
    const auto& p = pal();
    const auto f = font (13.0f, true);
    const int w = juce::GlyphArrangement::getStringWidthInt (f, toastText) + 40;
    auto r = juce::Rectangle<int> (w, 36).withCentre ({ NavRail::kWidth + (getWidth() - NavRail::kWidth) / 2, getHeight() - 96 });
    g.setColour (p.panelRaised.withAlpha (0.97f));
    g.fillRoundedRectangle (r.toFloat(), 8.0f);
    g.setColour (p.accent);
    g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.4f);
    g.setColour (p.text);
    g.setFont (f);
    g.drawText (toastText, r, juce::Justification::centred);
}

void LiveAlignEditor::showToast (const juce::String& text)
{
    toastText = text;
    toastUntil = la::clock::ms32() + 2600;
    repaint();
}

void LiveAlignEditor::openGuided (GuidedSource& source)
{
    guidedOverlay = std::make_unique<GuidedOverlay> (processor, source);
    guidedOverlay->onChanged = [this] { planPage->refresh(); updateStatuses(); };
    juce::Component::SafePointer<LiveAlignEditor> self (this);
    guidedOverlay->onClose = [self]
    {
        // closed from inside its own callback: let it go once that has returned
        juce::MessageManager::callAsync ([safe = self]
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
    if (guidedOverlay->isShowing()) guidedOverlay->grabKeyboardFocus();
}

void LiveAlignEditor::resized()
{
    if (guidedOverlay != nullptr) guidedOverlay->setBounds (getLocalBounds());
    if (tour != nullptr) tour->setBounds (getLocalBounds());
    if (whatIsThis != nullptr) whatIsThis->setBounds (getLocalBounds());
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
        h.removeFromRight (8);
        helpButton.setBounds (h.removeFromRight (32).withSizeKeepingCentre (32, 30));
        h.removeFromRight (14);
        micPicker.setBounds (h.removeFromRight (240).withSizeKeepingCentre (240, 30));
        titleArea = h;
    }
    else
    {
        titleArea = header;
    }
    {
        // undo and redo, at the right of the title (the monitoring page has its own controls there)
        if (shownPage != (int) Page::Monitor)
        {
            auto hr = titleArea.removeFromRight (36).withSizeKeepingCentre (32, 30);
            helpButton.setBounds (hr);
            titleArea.removeFromRight (8);
        }
        const bool showUndo = standalone && shownPage != (int) Page::Monitor;
        undoButton.setVisible (showUndo);
        redoButton.setVisible (showUndo);
        if (showUndo)
        {
            auto ur = titleArea.removeFromRight (76).withSizeKeepingCentre (76, 30);
            redoButton.setBounds (ur.removeFromRight (36));
            ur.removeFromRight (4);
            undoButton.setBounds (ur.removeFromRight (36));
            titleArea.removeFromRight (10);
        }
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
    venuePage->setBounds (r);
    coveragePage->setBounds (r);
    headroomPage->setBounds (r);
    listeningPage->setBounds (r);
    lockerPage->setBounds (r);
    settingsPage->setBounds (r);
    for (auto* pg : std::initializer_list<juce::Component*> { feedbackPage.get(), acousticsPage.get(), monitorMicPage.get(), tuningReportPage.get(), monitorReportPage.get(),
                                                              levelPage.get(), sheetPage.get() })
        pg->setBounds (r);

    auto side = r.removeFromRight (330);
    r.removeFromRight (8);
    paneArea = r;
    layoutPanes();

    splPanel.setBounds (side.removeFromTop (184));
    side.removeFromTop (8);
    inputPanel.setBounds (side.removeFromTop (98));
    side.removeFromTop (8);
    delayPanel.setBounds (side.removeFromTop (160));
    side.removeFromTop (8);
    // the panels below share what is left: their usual heights when it fits, shrinking toward a minimum when not
    struct Slot { juce::Component* c; int want, min; };
    std::vector<Slot> slots;
    if (condPanel.isVisible()) slots.push_back ({ &condPanel, 96, 96 });
    slots.push_back ({ &eventPanel, 90, 80 });
    slots.push_back ({ &fbWatch, 96, 68 });
    slots.push_back ({ &tunedPanel, 80, 72 });
    int want = 0, min = 0;
    for (const auto& s : slots) { want += s.want; min += s.min; }
    const int avail = side.getHeight() - 8 * (int) slots.size() - 60;   // the traces keep a little
    for (const auto& s : slots)
    {
        const int h = avail >= want ? s.want : s.min + (want > min ? (s.want - s.min) * juce::jmax (0, avail - min) / (want - min) : 0);
        s.c->setBounds (side.removeFromTop (h));
        side.removeFromTop (8);
    }
    tracesPanel.setBounds (side);
}

// ---- undo, redo and the change history ----
// Every third of a second the editable state is fingerprinted. A change that has settled (the same for two looks)
// becomes an undo step, and what it did to the outputs' processing goes into the change history with the step it
// was made on.
void LiveAlignEditor::resetUndo()
{
    undoStack.clear();
    redoStack.clear();
    committedState = processor.undoState();
    committedProc = la::ChangeLog::snapshot (processor.system);
    committedFp = processor.undoFingerprint();
    pendingFp.clear();
    pendingTicks = 0;
}

void LiveAlignEditor::updateConditionsPanel()
{
    auto& f = processor.climate;
    const bool want = f.enabled();
    if (condPanel.isVisible() != want) { condPanel.setVisible (want); resized(); }
    if (! want) return;
    la::ui::ConditionsPanel::State s;
    s.live = f.live();
    s.connected = f.connected.load();
    s.status = f.status();
    s.age = f.ageText();
    s.tC = f.tempC.load();
    s.rh = f.humidity.load();
    const auto& ch = processor.checks;
    if (std::isfinite (ch.tunedTempC))
    {
        s.refC = ch.tunedTempC;
        s.refRh = ch.tunedHumidity;
        s.refLabel = "Tuned";
    }
    else
    {
        s.refC = settings.temperatureC.load();
        s.refRh = settings.humidity.load();
        s.refLabel = "Session";
    }
    for (int i = 0; i < (int) processor.system.sources.size(); ++i)
        if (processor.system.sources[(size_t) i].proc.delayMs > s.maxDelayMs)
        {
            s.maxDelayMs = processor.system.sources[(size_t) i].proc.delayMs;
            s.maxDelayName = processor.system.nameOf (i);
        }
    s.recheckMs = processor.pref ("recheckDelay");
    s.farIsRoom = processor.plan.lengthM > 1.0;
    s.farM = s.farIsRoom ? processor.plan.lengthM : 30.0;
    s.feet = settings.distanceUnit.load() == 0;
    s.fahrenheit = settings.temperatureUnit.load() == 1;
    condPanel.setState (s);
}

void LiveAlignEditor::trackChanges()
{
    if (committedState == nullptr) { resetUndo(); return; }
    if (guidedOverlay != nullptr) return;   // measuring: wait until it closes
    const auto fp = processor.undoFingerprint();
    if (fp == committedFp) { pendingFp.clear(); return; }
    if (fp != pendingFp) { if (pendingFp.isEmpty()) pendingPage = shownPage; pendingFp = fp; pendingTicks = 0; return; }
    if (++pendingTicks < 2) return;
    // settled: the old state is an undo step
    undoStack.push_back (std::move (committedState));
    if (undoStack.size() > 40) undoStack.erase (undoStack.begin());
    redoStack.clear();
    logChanges (*committedProc, {});
    committedState = processor.undoState();
    committedProc = la::ChangeLog::snapshot (processor.system);
    committedFp = fp;
    pendingFp.clear();
    pendingPage = -1;
}

void LiveAlignEditor::logChanges (const juce::XmlElement& procBefore, const juce::String& whyGiven)
{
    la::SpeakerSystem before = processor.system;
    la::ChangeLog::restore (before, procBefore);
    const auto changes = la::ChangeLog::diff (before, processor.system);
    if (changes.empty()) return;
    const int on = pendingPage >= 0 ? pendingPage : shownPage;   // the page the change was made on
    const auto step = juce::String (pageName (on == (int) Page::Settings ? lastPage : on));
    auto why = whyGiven;
    if (why.isEmpty())
    {
        switch ((Page) on)
        {
            case Page::Level: why = "Level balance trim"; break;
            case Page::Room: why = "Room EQ entered"; break;
            case Page::Align: why = "Time alignment"; break;
            case Page::Crossover: why = "Crossover set"; break;
            case Page::Response: why = "Speaker response (polarity, protection)"; break;
            case Page::Sheet: why = "Edited on the processor sheet"; break;
            case Page::Speakers: why = "Speaker set-up"; break;
            case Page::Coverage: why = "Re-aimed"; break;
            default: why = "Edited"; break;
        }
    }
    const auto when = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
    for (const auto& c : changes) processor.history.add (when, step, c.first, c.second, why, &procBefore);
    sheetPage->refresh();
}

void LiveAlignEditor::restoreState (const juce::XmlElement& state, const juce::String& why)
{
    const auto procBefore = la::ChangeLog::snapshot (processor.system);
    processor.restoreUndoState (state);
    logChanges (*procBefore, why);
    committedState = processor.undoState();
    committedProc = la::ChangeLog::snapshot (processor.system);
    committedFp = processor.undoFingerprint();
    pendingFp.clear();
    refreshAllPages();
    speakerPage->refreshChoices();
}

void LiveAlignEditor::undo()
{
    trackChanges();
    if (! pendingFp.isEmpty())   // a change still settling: make it a step first
    {
        pendingTicks = 2;
        trackChanges();
    }
    if (undoStack.empty()) { showToast ("Nothing to undo."); return; }
    auto state = std::move (undoStack.back());
    undoStack.pop_back();
    redoStack.push_back (processor.undoState());
    restoreState (*state, "Undone");
    showToast ("Undone.");
}

void LiveAlignEditor::redo()
{
    if (redoStack.empty()) { showToast ("Nothing to redo."); return; }
    auto state = std::move (redoStack.back());
    redoStack.pop_back();
    undoStack.push_back (processor.undoState());
    restoreState (*state, "Redone");
    showToast ("Redone.");
}

// The processing as it was before a change in the history: every output put back (an undo step, so it can be undone).
void LiveAlignEditor::rollBackTo (int i)
{
    auto& h = processor.history.entries;
    if (i < 0 || i >= (int) h.size() || h[(size_t) i].before == nullptr) return;
    undoStack.push_back (processor.undoState());
    redoStack.clear();
    const auto procBefore = la::ChangeLog::snapshot (processor.system);
    const auto when = h[(size_t) i].when;
    la::ChangeLog::restore (processor.system, *h[(size_t) i].before);
    processor.system.syncLinks();
    logChanges (*procBefore, "Rolled back to before " + when);
    committedState = processor.undoState();
    committedProc = la::ChangeLog::snapshot (processor.system);
    committedFp = processor.undoFingerprint();
    refreshAllPages();
    speakerPage->refreshChoices();
    showToast ("Rolled back to before the change of " + when + ".");
}

// The tuned response where the monitor mic stands: the latest Room EQ check there (else the average of the latest
// round), for the comparison with the show.
void LiveAlignEditor::updateTunedPanel()
{
    const auto& room = processor.room;
    const bool after = room.count (true) > 0;
    const auto& at = processor.monitorMic.position;
    la::FreqTable ref;
    juce::String name;
    for (const auto& m : room.meas)
        if (m.after == after && m.position == at && m.tf.freq.size() > 4) { ref = m.tf; name = at; }
    if (ref.freq.empty() && room.count (after) > 0)
    {
        const auto avg = room.average (after);
        const auto g = la::RoomTuning::grid();
        for (size_t i = 0; i < g.size(); ++i) if (std::isfinite (avg[i])) { ref.freq.push_back (g[i]); ref.db.push_back (avg[i]); }
        name = "the average of the points";
    }
    if (ref.freq.empty()) { tunedPanel.clear(); return; }
    tunedPanel.update (snap, ref, name);
}


// ---- help ----
void LiveAlignEditor::showHelpMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "How to: " + juce::String (pageName (shownPage)) + "   (F1)");
    m.addItem (2, "What is this? (click anything)");
    m.addItem (3, "Glossary...");
    m.addSeparator();
    m.addItem (4, "The tour");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&helpButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        if (r == 1) safe->showHowTo();
        if (r == 2) safe->startWhatIsThis();
        if (r == 3) safe->showGlossary();
        if (r == 4) safe->showTour (0);
    });
}

void LiveAlignEditor::showHowTo()
{
    juce::StringArray lines;
    const auto steps = la::help::howTo (shownPage);
    for (int i = 0; i < steps.size(); ++i) lines.add (juce::String (i + 1) + ".  " + steps[i]);
    auto card = std::make_unique<HelpCard> ("How to: " + juce::String (pageName (shownPage)), lines.joinIntoString ("\n"), la::help::termsFor (shownPage), 460);
    juce::CallOutBox::launchAsynchronously (std::move (card), helpButton.getScreenBounds(), nullptr);
}

void LiveAlignEditor::showGlossary()
{
    juce::CallOutBox::launchAsynchronously (std::make_unique<GlossaryView>(), helpButton.getScreenBounds(), nullptr);
}

void LiveAlignEditor::startWhatIsThis()
{
    whatIsThis = std::make_unique<WhatIsThisOverlay>();
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    whatIsThis->onPick = [safe] (juce::Point<int> pt)
    {
        juce::MessageManager::callAsync ([safe, pt]
        {
            if (safe == nullptr) return;
            if (safe->whatIsThis != nullptr) { safe->removeChildComponent (safe->whatIsThis.get()); safe->whatIsThis.reset(); }
            safe->helpAt (pt);
        });
    };
    whatIsThis->onCancel = [safe]
    {
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->whatIsThis != nullptr) { safe->removeChildComponent (safe->whatIsThis.get()); safe->whatIsThis.reset(); } });
    };
    addAndMakeVisible (*whatIsThis);
    whatIsThis->setBounds (getLocalBounds());
    whatIsThis->toFront (true);
    if (whatIsThis->isShowing()) whatIsThis->grabKeyboardFocus();
}

// Help for whatever is at `pt`: its tooltip and the terms in it; on a page (a number or a graph), the page's terms.
void LiveAlignEditor::helpAt (juce::Point<int> pt)
{
    juce::String title, body;
    juce::StringArray terms;
    for (auto* c = getComponentAt (pt); c != nullptr && c != this; c = c->getParentComponent())
    {
        juce::String tip;
        if (auto* tc = dynamic_cast<juce::TooltipClient*> (c)) tip = tc->getTooltip();
        if (tip.isEmpty()) continue;
        body = tip;
        if (auto* b = dynamic_cast<juce::Button*> (c)) title = b->getButtonText();
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (c)) title = cb->getText();
        terms = la::help::termsIn (tip + " " + title);
        break;
    }
    if (body.isEmpty())
    {
        // a number or a graph on a page: the page's own terms
        title = juce::String (pageName (shownPage));
        if (nav.getBounds().contains (pt)) body = "The steps, top to bottom: each shows where it stands, and NEXT marks the one to do now. Click one to go there.";
        else body = juce::String (pageSubtitle (shownPage)).substring (0, 1).toUpperCase() + juce::String (pageSubtitle (shownPage)).substring (1) + ".";
        terms = la::help::termsFor (shownPage);
    }
    if (title.isEmpty()) title = "What is this?";
    auto card = std::make_unique<HelpCard> (title, body, terms, 440);
    juce::CallOutBox::launchAsynchronously (std::move (card), localAreaToGlobal (juce::Rectangle<int> (pt.x - 6, pt.y - 6, 12, 12)), nullptr);
}

void LiveAlignEditor::showTour (int step)
{
    using Step = TourOverlay::Step;
    auto navArea = [this] (int page) { return std::function<juce::Rectangle<int>()> ([this, page] { return nav.entryBounds (page).translated (nav.getX(), nav.getY()); }); };
    std::vector<Step> steps;
    steps.push_back ({ "Welcome to LiveAlign", "LiveAlign walks you through tuning a sound system: set up the venue and the mics, the tuning steps in the order a "
                                               "system is tuned, then monitoring the show. It measures; you enter the settings in your own processor.", {} });
    steps.push_back ({ "The steps", "Top to bottom, the way a system is tuned. Each step shows where it stands; NEXT marks the one to do now. Any step can be "
                                    "done again at any time.", [this] { return nav.getBounds().withTrimmedTop (50).withTrimmedBottom (190); } });
    steps.push_back ({ "Venue", "The room (what it is used for, its size, the temperature and humidity of the day, the targets), the speakers, and the plan "
                                "with the points you measure at.", navArea ((int) Page::Venue) });
    steps.push_back ({ "Mics", "Your measurement mics: inputs, calibration files, the reference (a loopback cable) and the pre-flight checks. The locker keeps "
                               "every mic you own.", navArea ((int) Page::Mics) });
    steps.push_back ({ "Tuning", "Speaker response, gain and headroom, crossover, level balance, aim and coverage, time alignment, room acoustics, Room EQ, the "
                                 "listening check, the feedback test. Each measurement opens a guided pop-up that shows where the mic goes.", navArea ((int) Page::Response) });
    steps.push_back ({ "The processor sheet and reports", "Every setting to enter in your processor, output by output, with the history of every change. The "
                                                          "reports are PDFs of the tuning and the show.", navArea ((int) Page::Sheet) });
    steps.push_back ({ "Help and undo", "The ? has how to do each step (F1), What is this? for anything on the screen, and the glossary. Cmd/Ctrl+Z undoes a "
                                        "change; so do the arrows beside the ?.",
                       [this]
                       {
                           auto r = helpButton.getBounds();
                           if (undoButton.isVisible()) r = r.getUnion (undoButton.getBounds()).getUnion (redoButton.getBounds());
                           return r;
                       } });
    steps.push_back ({ "Your venues", "The open venue and session. Click it for Home: the list of venues, the sessions, and where this venue stands.",
                       [this] { return nav.venueArea().translated (nav.getX(), nav.getY()); } });
    tour = std::make_unique<TourOverlay> (std::move (steps));
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    tour->onClose = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr) safe->closeTour(); }); };
    addAndMakeVisible (*tour);
    tour->setBounds (getLocalBounds());
    tour->setStep (step);
    tour->toFront (true);
    if (tour->isShowing()) tour->grabKeyboardFocus();
}

void LiveAlignEditor::closeTour()
{
    if (tour == nullptr) return;
    removeChildComponent (tour.get());
    tour.reset();
    la::UserPrefs::set ("tourShown", "1");
}
