#include "PluginEditor.h"
#include "UI/SettingsPanel.h"
#include "UI/CalibrationPanel.h"
#include "StandaloneHooks.h"

using namespace la::ui;

LiveAlignEditor::LiveAlignEditor (LiveAlignProcessor& p)
    : AudioProcessorEditor (&p), processor (p), settings (p.settings), standalone (p.isStandalone() || p.uiAsStandalone),
      splPanel (p.settings), inputPanel (p.settings), delayPanel (p.settings), tracesPanel (p.traces, p.settings), generator (p.settings)
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);

    // ---- workflow tabs and pages ----
    addAndMakeVisible (workflowTabs);
    workflowTabs.onSelect = [this] (int t) { showTab (t); };

    micPage = std::make_unique<MicSetupPage> (p);
    micPage->onLoadCalFile = [this] { loadCalFile(); };
    micPage->onNext = [this] { showTab (1); };
    addChildComponent (*micPage);

    speakerPage = std::make_unique<PlaceholderPage> (1,
        "Measure each speaker or group on its own, compare their responses, and get crossover settings for your speaker management system.",
        juce::StringArray { "Build the system: tops, subs, delays, groups such as a line array or several subs",
                            "Speaker library: spec sheet, built-in DSP switches, EASE / FRD / CSV / CLF data",
                            "Measure and overlay each speaker or group",
                            "Crossover recommendation: frequency, filter type and slope, delay, polarity",
                            "Predicted combined response, then a settings summary to enter in your processor" });
    roomPage = std::make_unique<PlaceholderPage> (2,
        "Measure across the listening area, compare with a target curve and tune the system.",
        juce::StringArray { "Several mic positions, averaged",
                            "Target curve with suggested EQ cuts",
                            "Top and sub alignment checked at the seats",
                            "Feedback ring-out for the stage mics",
                            "Room acoustics: reverb time, clarity, room modes" });
    speakerPage->onBack = [this] { showTab (0); };
    speakerPage->onNext = [this] { showTab (2); };
    roomPage->onBack = [this] { showTab (1); };
    roomPage->onNext = [this] { showTab (3); };
    addChildComponent (*speakerPage);
    addChildComponent (*roomPage);

    // ---- monitoring page ----
    for (int i = 0; i < 3; ++i)
    {
        panes[i] = std::make_unique<MeasurePane> (p.settings, p.traces, i);
        addChildComponent (*panes[i]);
    }
    addChildComponent (splPanel);
    addChildComponent (inputPanel);
    addChildComponent (delayPanel);
    addChildComponent (tracesPanel);
    monitorBackButton.setButtonText (juce::String ("Back: ") + tabInfo (2).name);
    monitorBackButton.onClick = [this] { showTab (2); };
    addChildComponent (monitorBackButton);

    addAndMakeVisible (generator);

    // ---- header ----
    projectButton.setTooltip ("New, open and save venue projects");
    projectButton.onClick = [this] { showProjectMenu(); };
    addAndMakeVisible (projectButton);

    runButton.setTooltip ("Freeze the graphs to study them; the sound level keeps running (key: Space)");
    runButton.onClick = [this] { settings.running = ! settings.running.load(); };
    addAndMakeVisible (runButton);

    resetButton.setTooltip ("Restart the transfer function and RTA averages (key: R)");
    resetButton.onClick = [this] { settings.requestResetAverages = true; };
    addAndMakeVisible (resetButton);

    fullScreenButton.setTooltip ("Fill the whole screen (key: F, Esc to leave)");
    fullScreenButton.onClick = [this] { toggleFullScreen(); };
    addChildComponent (fullScreenButton);
    fullScreenButton.setVisible ((bool) la::StandaloneHooks::toggleFullScreen());

    settingsButton.setTooltip ("Inputs, calibration, analysis, screen layout, units and theme");
    settingsButton.onClick = [this] { openSettings(); };
    addAndMakeVisible (settingsButton);

    splPanel.onCalibrate = [this] { openCalibration(); };

    tracesPanel.onCapture = [this] { capture(); };
    tracesPanel.onImport = [this] { importTrace(); };
    tracesPanel.onRename = [this] (int i) { renameTrace (i); };
    tracesPanel.onExport = [this] (int i) { exportTrace (i); };
    tracesPanel.onDelete = [this] (int i) { deleteTrace (i); };

    // The VST3 lives in a host during the event: it shows Monitoring only.
    if (! standalone)
        for (int i = 0; i < 3; ++i) workflowTabs.setTabVisible (i, false);

    applyTheme (settings.theme.load());
    generator.setNumOutputs (p.numOutputs());
    generator.sync();

    setResizable (true, true);
    setResizeLimits (1100, 720, 5120, 2880);
    setSize (1400, 900);
    showTab (standalone ? settings.currentTab.load() : 3);

    startTimerHz (30);
}

void LiveAlignEditor::showTab (int tab)
{
    tab = standalone ? juce::jlimit (0, 3, tab) : 3;
    settings.currentTab = tab;
    shownTab = tab;
    workflowTabs.setCurrent (tab);

    workflowTabs.setDone (0, micPage->completedSteps() == 6);
    workflowTabs.setProgress (0, micPage->completedSteps() == 6 ? juce::String() : juce::String (micPage->completedSteps()) + " of 6 done");
    micPage->setVisible (tab == 0);
    if (tab == 0 && snapSerial > 0) micPage->setSnapshot (snap);
    speakerPage->setVisible (tab == 1);
    roomPage->setVisible (tab == 2);
    const bool mon = tab == 3;
    for (int i = 0; i < 3; ++i) panes[i]->setVisible (mon && i < juce::jlimit (1, 3, settings.paneCount.load()));
    splPanel.setVisible (mon);
    inputPanel.setVisible (mon);
    delayPanel.setVisible (mon);
    tracesPanel.setVisible (mon);
    monitorBackButton.setVisible (mon && standalone);
    resetButton.setVisible (mon);
    runButton.setVisible (mon);
    resized();
}

// ---- venue projects ----
void LiveAlignEditor::showProjectMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "New venue");
    m.addItem (2, "Open venue...");
    m.addSeparator();
    m.addItem (3, "Save", true);
    m.addItem (4, "Save as...");
    m.addSeparator();
    m.addItem (5, "Venue name and notes...");
    if (processor.projectFile != juce::File())
        m.addItem (6, "Show file in folder");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&projectButton), [safe] (int r)
    {
        if (safe == nullptr) return;
        switch (r)
        {
            case 1: safe->confirmDiscard ([safe] { if (safe != nullptr) safe->newProject(); }); break;
            case 2: safe->confirmDiscard ([safe] { if (safe != nullptr) safe->openProject(); }); break;
            case 3: safe->saveProject (false); break;
            case 4: safe->saveProject (true); break;
            case 5: safe->editVenueDetails(); break;
            case 6: safe->processor.projectFile.revealToUser(); break;
            default: break;
        }
    });
}

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
        repaint (venueArea);
        return;
    }
    auto dir = processor.projectFile != juce::File() ? processor.projectFile.getParentDirectory()
                                                     : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    chooser = std::make_unique<juce::FileChooser> ("Save venue project",
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
        safe->repaint();
    });
}

void LiveAlignEditor::openProject()
{
    chooser = std::make_unique<juce::FileChooser> ("Open venue project", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory),
                                                   "*" + LiveAlignProcessor::projectExtension());
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto err = safe->processor.openProject (f);
        if (err.isNotEmpty()) { safe->showMessage ("Could not open the project", err); return; }
        safe->afterProjectLoaded();
    });
}

void LiveAlignEditor::newProject()
{
    processor.newProject();
    afterProjectLoaded();
    editVenueDetails();
}

void LiveAlignEditor::afterProjectLoaded()
{
    micPage->refreshChoices();
    generator.sync();
    tracesPanel.syncTarget();
    tracesPanel.refresh();
    for (auto& pane : panes) pane->sync();
    layoutPanes();
    showTab (standalone ? settings.currentTab.load() : 3);
    repaint();
}

void LiveAlignEditor::editVenueDetails()
{
    venueWindow = std::make_unique<juce::AlertWindow> ("Venue", "Name and notes for this venue project.", juce::MessageBoxIconType::NoIcon, this);
    venueWindow->addTextEditor ("name", processor.venueName, "Venue name");
    venueWindow->addTextEditor ("notes", processor.venueNotes, "Notes");
    if (auto* notes = venueWindow->getTextEditor ("notes"))
    {
        notes->setMultiLine (true, true);
        notes->setReturnKeyStartsNewLine (true);
        notes->setSize (notes->getWidth(), 90);
    }
    venueWindow->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    venueWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    venueWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int result)
    {
        if (safe == nullptr || safe->venueWindow == nullptr) return;
        if (result == 1)
        {
            const auto name = safe->venueWindow->getTextEditorContents ("name").trim();
            safe->processor.venueName = name.isEmpty() ? juce::String ("Untitled venue") : name;
            safe->processor.venueNotes = safe->venueWindow->getTextEditorContents ("notes");
        }
        safe->venueWindow->setVisible (false);
        safe->repaint();
    }), false);
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
        panes[i]->setVisible (i < count && shownTab == 3);
        if (i >= count) continue;
        const int remaining = count - i;
        if (columns)
        {
            const int w = (r.getWidth() - gap * (remaining - 1)) / remaining;
            panes[i]->setBounds (r.removeFromLeft (w));
            r.removeFromLeft (gap);
        }
        else
        {
            const int h = (r.getHeight() - gap * (remaining - 1)) / remaining;
            panes[i]->setBounds (r.removeFromTop (h));
            r.removeFromTop (gap);
        }
        if (snapSerial > 0) panes[i]->update (snap);
    }
}

void LiveAlignEditor::openSettings()
{
    auto panel = std::make_unique<SettingsPanel> (processor);
    panel->onThemeChanged = [this] { applyTheme (settings.theme.load()); };
    panel->onLayoutChanged = [this] { layoutPanes(); };
    panel->onOpenCalibration = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<LiveAlignEditor> (this)]
                                                                         { if (safe != nullptr) safe->openCalibration(); }); };
    juce::CallOutBox::launchAsynchronously (std::move (panel), settingsButton.getBounds(), this);
}

void LiveAlignEditor::openCalibration()
{
    auto panel = std::make_unique<CalibrationPanel> (settings, [this] { return snap.rawZSlowDbfs; },
                                                     [this] { return processor.analyzer.getCalibrationName(); });
    panel->onLoadFile = [this] { loadCalFile(); };
    panel->onClearFile = [this] { processor.clearCalibration(); };
    const auto anchor = splPanel.calibrateButton().getBounds() + splPanel.getPosition();
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor, this);
}

void LiveAlignEditor::toggleGenerator()
{
    settings.genOn = ! settings.genOn.load();
    generator.sync();
}

void LiveAlignEditor::toggleFullScreen()
{
    if (auto& f = la::StandaloneHooks::toggleFullScreen()) f();
}

// Captures what is on screen: the transfer function and / or the RTA, whichever the graphs show.
void LiveAlignEditor::capture()
{
    bool wantTf = false, wantRta = false;
    for (int i = 0; i < settings.paneCount.load(); ++i)
    {
        const int v = settings.paneViewRef (i).load();
        wantTf |= v != (int) la::View::Rta;   // the impulse view captures the transfer function behind it
        wantRta |= v == (int) la::View::Rta;
    }
    const auto time = juce::Time::getCurrentTime().formatted ("%H:%M:%S");
    bool added = false;

    if (wantRta && ! snap.rtaFreq.empty())
    {
        la::Trace t;
        t.kind = la::Trace::Rta;
        t.name = "RTA " + time;
        for (size_t i = 0; i < snap.rtaFreq.size(); ++i)
        {
            t.data.freq.push_back (snap.rtaFreq[i]);
            t.data.db.push_back (snap.rtaMic[i]);
        }
        processor.traces.add (std::move (t));
        added = true;
    }

    if (wantTf)
    {
        la::Trace t;
        t.kind = la::Trace::Transfer;
        t.name = "TF " + time;
        for (size_t i = 0; i < snap.tfFreq.size() && snap.tfFrames > 0; ++i)
        {
            if (! std::isfinite (snap.tfMag[i])) continue;
            t.data.freq.push_back (snap.tfFreq[i]);
            t.data.db.push_back (snap.tfMag[i]);
            t.data.phase.push_back (snap.tfPhase[i]);
            t.data.extra.push_back (snap.tfCoh[i]);
        }
        if (! t.data.freq.empty()) { processor.traces.add (std::move (t)); added = true; }
        else if (! added)
            showMessage ("Nothing to capture", "There is no transfer function yet. Start the generator or program audio with a reference connected.");
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
        safe->repaint();
    });
}

void LiveAlignEditor::exportTrace (int index)
{
    if (! processor.traces.valid (index)) return;
    const auto name = juce::File::createLegalFileName (processor.traces.all()[(size_t) index].name);
    chooser = std::make_unique<juce::FileChooser> ("Export trace as FRD",
                                                   juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name + ".frd"),
                                                   "*.frd");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [safe, index] (const juce::FileChooser& fc)
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
    renameWindow = std::make_unique<juce::AlertWindow> ("Rename trace", "", juce::MessageBoxIconType::NoIcon, this);
    renameWindow->addTextEditor ("name", processor.traces.all()[(size_t) index].name);
    renameWindow->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
    renameWindow->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    renameWindow->enterModalState (true, juce::ModalCallbackFunction::create ([safe, index] (int result)
    {
        if (safe == nullptr || safe->renameWindow == nullptr) return;
        const auto text = safe->renameWindow->getTextEditorContents ("name");
        safe->renameWindow->setVisible (false);
        if (result == 1) safe->processor.traces.rename (index, text);
        safe->tracesPanel.refresh();
        safe->repaint();
    }), false);
}

void LiveAlignEditor::deleteTrace (int index)
{
    if (! processor.traces.valid (index)) return;
    const auto name = processor.traces.all()[(size_t) index].name;
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    juce::AlertWindow::showAsync (juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "Delete trace",
                                                                                  "Delete \"" + name + "\"? This removes its file.",
                                                                                  "Delete", "Cancel", this),
                                  [safe, index, name] (int result)
    {
        if (safe == nullptr || result != 1) return;
        if (safe->processor.traces.valid (index) && safe->processor.traces.all()[(size_t) index].name == name)
            safe->processor.traces.remove (index);
        safe->tracesPanel.refresh();
        safe->repaint();
    });
}

void LiveAlignEditor::loadCalFile()
{
    chooser = std::make_unique<juce::FileChooser> ("Load mic calibration file", juce::File(), "*.txt;*.cal;*.frd;*.csv");
    juce::Component::SafePointer<LiveAlignEditor> safe (this);
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
    {
        if (safe == nullptr) return;
        const auto f = fc.getResult();
        if (f == juce::File()) return;
        const auto err = safe->processor.loadCalibrationFile (f);
        safe->showMessage (err.isEmpty() ? "Calibration loaded" : "Calibration file not loaded",
                           err.isEmpty() ? f.getFileName() + " is now applied to the RTA, sound level and transfer function." : err);
    });
}

void LiveAlignEditor::showMessage (const juce::String& title, const juce::String& text)
{
    juce::AlertWindow::showAsync (juce::MessageBoxOptions().withIconType (juce::MessageBoxIconType::InfoIcon).withTitle (title)
                                      .withMessage (text).withButton ("OK").withAssociatedComponent (this), nullptr);
}

bool LiveAlignEditor::keyPressed (const juce::KeyPress& key)
{
    const auto c = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    if (key == juce::KeyPress ('s', juce::ModifierKeys::commandModifier, 0)) { saveProject (false); return true; }
    if (key == juce::KeyPress::escapeKey)
    {
        if (la::StandaloneHooks::isFullScreen() && la::StandaloneHooks::isFullScreen()()) toggleFullScreen();
        return true;
    }
    if (c == 'f' || key == juce::KeyPress (juce::KeyPress::F11Key)) { toggleFullScreen(); return true; }
    if (c == 'g') { toggleGenerator(); return true; }
    if (shownTab != 3) return false;
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

    const bool monitoring = shownTab == 3;
    if (processor.analyzer.readSnapshot (snap, snapSerial))
    {
        snapSerial = snap.serial;
        if (monitoring)
        {
            for (int i = 0; i < laidOutCount; ++i) panes[i]->update (snap);
            splPanel.update (snap);
        }
        else if (shownTab == 0)
            micPage->setSnapshot (snap);
    }

    const float mp = processor.takeMicPeak(), rp = processor.takeRefPeak();
    if (monitoring) inputPanel.setLevels (mp, processor.micRmsDb(), rp, processor.refRmsDb());
    else micPage->setLevels (mp, processor.micRmsDb(), rp, processor.refRmsDb());
    if (monitoring)
    {
        delayPanel.setReport (processor.analyzer.readDelayReport());
        tracesPanel.refresh();
    }

    // Running is green; Frozen is amber so a frozen screen is never mistaken for live data.
    const bool running = settings.running.load();
    runButton.setButtonText (running ? "Running" : "Frozen");
    runButton.setColour (juce::TextButton::buttonColourId, running ? pal().good : pal().warn);
    runButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);

    if (processor.analyzer.hasOverflowed()) overloadUntil = juce::Time::getMillisecondCounter() + 3000;

    if (tick % 10 == 0)
    {
        generator.setNumOutputs (processor.numOutputs());
        generator.sync();
        tracesPanel.syncTarget();
        const bool fs = la::StandaloneHooks::isFullScreen() && la::StandaloneHooks::isFullScreen()();
        fullScreenButton.setButtonText (fs ? "Exit full screen" : "Full screen");

        const int micDone = micPage->completedSteps();
        workflowTabs.setDone (0, micDone == 6);
        workflowTabs.setProgress (0, micDone == 6 ? juce::String() : juce::String (micDone) + " of 6 done");

        juce::String s;
        if (monitoring)
        {
            s << juce::String (snap.sampleRate / 1000.0, 1) << " kHz   FFT " << snap.fftSize / 1024 << "k   "
              << juce::jmin (snap.tfFrames, settings.averages.load()) << "/" << settings.averages.load() << " avg   1/"
              << settings.smoothing.load() << " oct";
            if (juce::Time::getMillisecondCounter() < overloadUntil) s << "   ANALYSIS BEHIND";
        }
        status = s;
        panes[0]->setStatus (s);
    }

    if (tick % 30 == 0)
    {
        const bool changed = processor.projectChanged();
        if (changed != unsaved) { unsaved = changed; repaint (venueArea); }
    }
}

void LiveAlignEditor::paint (juce::Graphics& g)
{
    const auto& p = pal();
    g.fillAll (p.bg);
    auto header = getLocalBounds().reduced (14, 10).removeFromTop (36);

    g.setColour (p.accent);
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ (float) header.getX() + 8.0f, (float) header.getCentreY() }));
    g.setColour (p.text);
    g.setFont (font (19.0f, true));
    g.drawText ("LiveAlign", header.withTrimmedLeft (24).withWidth (110), juce::Justification::centredLeft);
    g.setColour (p.textDim);
    g.setFont (monoFont (11.0f));
    g.drawText ("v" + juce::String (JucePlugin_VersionString), header.withTrimmedLeft (124).withWidth (60), juce::Justification::centredLeft);

    // venue name, with a dot while there are unsaved changes
    auto v = venueArea;
    g.setColour (p.text);
    g.setFont (font (13.0f, true));
    g.drawText (processor.venueName, v.removeFromTop (v.getHeight() / 2 + 2).withTrimmedTop (2), juce::Justification::bottomLeft, true);
    g.setColour (unsaved ? p.warn : p.textDim);
    g.setFont (font (10.5f));
    g.drawText (unsaved ? juce::String ("Unsaved changes") : (processor.projectFile != juce::File() ? processor.projectFile.getFileName() : juce::String ("Not saved yet")),
                v, juce::Justification::topLeft, true);

}

void LiveAlignEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto header = r.removeFromTop (44);
    r.removeFromTop (8);

    auto h = header.withTrimmedLeft (186);
    workflowTabs.setBounds (h.removeFromLeft (workflowTabs.preferredWidth()));
    h.removeFromLeft (16);

    settingsButton.setBounds (h.removeFromRight (90).withSizeKeepingCentre (90, 28));
    h.removeFromRight (8);
    if (fullScreenButton.isVisible())
    {
        fullScreenButton.setBounds (h.removeFromRight (124).withSizeKeepingCentre (124, 28));
        h.removeFromRight (8);
    }
    if (resetButton.isVisible())
    {
        resetButton.setBounds (h.removeFromRight (86).withSizeKeepingCentre (86, 28));
        h.removeFromRight (6);
        runButton.setBounds (h.removeFromRight (90).withSizeKeepingCentre (90, 28));
        h.removeFromRight (14);
    }
    projectButton.setBounds (h.removeFromLeft (76).withSizeKeepingCentre (76, 28));
    h.removeFromLeft (8);
    venueArea = h.removeFromLeft (juce::jmin (220, h.getWidth() / 2));
    h.removeFromLeft (8);
    statusArea = h;

    generator.setBounds (r.removeFromBottom (54));
    r.removeFromBottom (8);
    pageArea = r;

    micPage->setBounds (r);
    speakerPage->setBounds (r);
    roomPage->setBounds (r);

    auto side = r.removeFromRight (330);
    r.removeFromRight (8);
    if (standalone)
    {
        auto nav = side.removeFromBottom (36);
        monitorBackButton.setBounds (nav.removeFromLeft (220));
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
