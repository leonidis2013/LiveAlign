#include "PluginEditor.h"
#include "UI/SettingsPanel.h"
#include "UI/CalibrationPanel.h"
#include "StandaloneHooks.h"

using namespace la::ui;

LiveAlignEditor::LiveAlignEditor (LiveAlignProcessor& p)
    : AudioProcessorEditor (&p), processor (p), settings (p.settings),
      splPanel (p.settings), inputPanel (p.settings), delayPanel (p.settings), tracesPanel (p.traces, p.settings), generator (p.settings)
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (workflowTabs);
    for (int i = 0; i < 3; ++i)
    {
        panes[i] = std::make_unique<MeasurePane> (p.settings, p.traces, i);
        addChildComponent (*panes[i]);
    }
    addAndMakeVisible (splPanel);
    addAndMakeVisible (inputPanel);
    addAndMakeVisible (delayPanel);
    addAndMakeVisible (tracesPanel);
    addAndMakeVisible (generator);

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

    applyTheme (settings.theme.load());
    generator.setNumOutputs (p.numOutputs());
    generator.sync();

    setResizable (true, true);
    setResizeLimits (1100, 720, 5120, 2880);
    setSize (1400, 900);

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
        panes[i]->setVisible (i < count);
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
    if (key == juce::KeyPress::spaceKey) { settings.running = ! settings.running.load(); return true; }
    if (key == juce::KeyPress::escapeKey)
    {
        if (la::StandaloneHooks::isFullScreen() && la::StandaloneHooks::isFullScreen()()) toggleFullScreen();
        return true;
    }
    if (c == 'f' || key == juce::KeyPress (juce::KeyPress::F11Key)) { toggleFullScreen(); return true; }
    if (c == 'c') { capture(); return true; }
    if (c == 'd') { settings.requestFindDelay = true; return true; }
    if (c == 'g') { toggleGenerator(); return true; }
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

    if (processor.analyzer.readSnapshot (snap, snapSerial))
    {
        snapSerial = snap.serial;
        for (int i = 0; i < laidOutCount; ++i) panes[i]->update (snap);
        splPanel.update (snap);
    }

    inputPanel.setLevels (processor.takeMicPeak(), processor.micRmsDb(), processor.takeRefPeak(), processor.refRmsDb());
    delayPanel.setReport (processor.analyzer.readDelayReport());
    tracesPanel.refresh();

    const bool running = settings.running.load();
    runButton.setToggleState (! running, juce::dontSendNotification);
    runButton.setButtonText (running ? "Running" : "Frozen");

    if (processor.analyzer.hasOverflowed()) overloadUntil = juce::Time::getMillisecondCounter() + 3000;

    if (tick % 10 == 0)
    {
        generator.setNumOutputs (processor.numOutputs());
        generator.sync();
        tracesPanel.syncTarget();
        const bool fs = la::StandaloneHooks::isFullScreen() && la::StandaloneHooks::isFullScreen()();
        fullScreenButton.setButtonText (fs ? "Exit full screen" : "Full screen");

        juce::String s;
        s << juce::String (snap.sampleRate / 1000.0, 1) << " kHz   FFT " << snap.fftSize / 1024 << "k   "
          << juce::jmin (snap.tfFrames, settings.averages.load()) << "/" << settings.averages.load() << " avg   1/"
          << settings.smoothing.load() << " oct";
        if (juce::Time::getMillisecondCounter() < overloadUntil) s << "   ANALYSIS BEHIND";
        if (s != status) { status = s; repaint (statusArea); }
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

    g.setFont (monoFont (11.5f));
    g.setColour (status.contains ("BEHIND") ? p.warn : p.textDim);
    g.drawText (status, statusArea, juce::Justification::centredRight);
}

void LiveAlignEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto header = r.removeFromTop (40);
    r.removeFromTop (8);

    auto h = header.withTrimmedLeft (196);
    workflowTabs.setBounds (h.removeFromLeft (140));

    settingsButton.setBounds (h.removeFromRight (90).withSizeKeepingCentre (90, 28));
    h.removeFromRight (8);
    if (fullScreenButton.isVisible())
    {
        fullScreenButton.setBounds (h.removeFromRight (124).withSizeKeepingCentre (124, 28));
        h.removeFromRight (8);
    }
    resetButton.setBounds (h.removeFromRight (86).withSizeKeepingCentre (86, 28));
    h.removeFromRight (6);
    runButton.setBounds (h.removeFromRight (90).withSizeKeepingCentre (90, 28));
    h.removeFromRight (14);
    statusArea = h;

    generator.setBounds (r.removeFromBottom (54));
    r.removeFromBottom (8);

    auto side = r.removeFromRight (330);
    r.removeFromRight (8);
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
