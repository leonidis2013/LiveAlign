#include "PluginEditor.h"
#include "UI/SettingsPanel.h"

using namespace la::ui;

namespace
{
const int kAverages[] = { 1, 2, 4, 8, 16, 32, 64 };
const int kTfSmoothing[] = { 48, 24, 12, 6, 3 };
const int kRtaBands[] = { 1, 3, 6, 12, 24, 48 };
}

LiveAlignEditor::LiveAlignEditor (LiveAlignProcessor& p)
    : AudioProcessorEditor (&p), processor (p), settings (p.settings),
      transferView (p.settings, p.traces), rtaView (p.settings, p.traces), impulseView (p.settings),
      splPanel (p.settings), inputPanel (p.settings), delayPanel (p.settings), tracesPanel (p.traces), generator (p.settings)
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);

    addChildComponent (transferView);
    addChildComponent (rtaView);
    addChildComponent (impulseView);
    addAndMakeVisible (splPanel);
    addAndMakeVisible (inputPanel);
    addAndMakeVisible (delayPanel);
    addAndMakeVisible (tracesPanel);
    addAndMakeVisible (generator);

    const char* viewNames[] = { "RTA", "Transfer", "Impulse" };
    const char* viewTips[] = { "Real-time analyser of the mic (key: 1)",
                               "Transfer function: magnitude, phase and coherence of the system, mic against reference (key: 2)",
                               "Impulse response: arrival time, reflections and polarity (key: 3)" };
    for (int i = 0; i < 3; ++i)
    {
        viewButtons[i].setButtonText (viewNames[i]);
        viewButtons[i].setTooltip (viewTips[i]);
        viewButtons[i].setClickingTogglesState (true);
        viewButtons[i].setRadioGroupId (201);
        viewButtons[i].onClick = [this, i] { if (viewButtons[i].getToggleState()) setView (i); };
        addAndMakeVisible (viewButtons[i]);
    }

    runButton.setClickingTogglesState (true);
    runButton.setTooltip ("Freeze the graphs to study them; the sound level keeps running (key: Space)");
    runButton.onClick = [this] { settings.running = ! runButton.getToggleState(); };
    addAndMakeVisible (runButton);

    for (int i = 0; i < 7; ++i) avgBox.addItem (juce::String (kAverages[i]) + " avg", i + 1);
    avgBox.setTooltip ("Transfer function averaging: more averages are steadier but slower to react");
    avgBox.onChange = [this] { settings.averages = kAverages[juce::jlimit (0, 6, avgBox.getSelectedItemIndex())]; };
    addAndMakeVisible (avgBox);

    smoothBox.onChange = [this]
    {
        const int idx = smoothBox.getSelectedItemIndex();
        if (idx < 0) return;
        if (settings.view.load() == (int) la::View::Rta) settings.rtaBands = kRtaBands[juce::jlimit (0, 5, idx)];
        else settings.smoothing = kTfSmoothing[juce::jlimit (0, 4, idx)];
    };
    addAndMakeVisible (smoothBox);

    resetButton.setTooltip ("Restart the averages (key: R)");
    resetButton.onClick = [this] { settings.requestResetAverages = true; };
    addAndMakeVisible (resetButton);

    centreButton.setTooltip ("Move the transfer function to the 0 dB line (or double-click the graph). Drag the graph up and down to shift it.");
    centreButton.onClick = [this] { transferView.centreOnLive(); };
    addAndMakeVisible (centreButton);

    settingsButton.setTooltip ("Inputs, reference, mic calibration, SPL calibration, FFT size, units and theme");
    settingsButton.onClick = [this] { openSettings(); };
    addAndMakeVisible (settingsButton);

    themeButton.onClick = [this] { settings.theme = appliedTheme == 0 ? 1 : 0; };
    addAndMakeVisible (themeButton);

    tracesPanel.onCapture = [this] { capture(); };
    tracesPanel.onImport = [this] { importTrace(); };
    tracesPanel.onRename = [this] (int i) { renameTrace (i); };
    tracesPanel.onExport = [this] (int i) { exportTrace (i); };
    tracesPanel.onDelete = [this] (int i) { deleteTrace (i); };

    for (int i = 0; i < 7; ++i)
        if (kAverages[i] == settings.averages.load()) avgBox.setSelectedItemIndex (i, juce::dontSendNotification);

    applyTheme (settings.theme.load());
    setView (settings.view.load());
    generator.setNumOutputs (p.numOutputs());
    generator.sync();

    setResizable (true, true);
    setResizeLimits (1100, 720, 2400, 1600);
    setSize (1340, 860);

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
    themeButton.setButtonText (theme == 0 ? "Light" : "Dark");
    sendLookAndFeelChange();
    repaint();
}

void LiveAlignEditor::setView (int v)
{
    v = juce::jlimit (0, 2, v);
    settings.view = v;
    shownView = v;
    for (int i = 0; i < 3; ++i) viewButtons[i].setToggleState (i == v, juce::dontSendNotification);
    rtaView.setVisible (v == (int) la::View::Rta);
    transferView.setVisible (v == (int) la::View::Transfer);
    impulseView.setVisible (v == (int) la::View::Impulse);
    centreButton.setVisible (v == (int) la::View::Transfer);
    smoothBox.setVisible (v != (int) la::View::Impulse);
    rebuildSmoothingBox();
    if (snapSerial > 0)
    {
        if (v == 0) rtaView.update (snap);
        else if (v == 1) transferView.update (snap);
        else impulseView.update (snap);
    }
}

void LiveAlignEditor::rebuildSmoothingBox()
{
    smoothBox.clear (juce::dontSendNotification);
    if (shownView == (int) la::View::Rta)
    {
        smoothBox.setTooltip ("RTA resolution: bands per octave");
        for (int i = 0; i < 6; ++i)
        {
            smoothBox.addItem ("1/" + juce::String (kRtaBands[i]) + " oct", i + 1);
            if (kRtaBands[i] == settings.rtaBands.load()) smoothBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }
    }
    else
    {
        smoothBox.setTooltip ("Transfer function smoothing. 1/24 or 1/12 octave for tuning, 1/3 for the overall tonal shape.");
        for (int i = 0; i < 5; ++i)
        {
            smoothBox.addItem ("1/" + juce::String (kTfSmoothing[i]) + " oct", i + 1);
            if (kTfSmoothing[i] == settings.smoothing.load()) smoothBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }
    }
}

void LiveAlignEditor::openSettings()
{
    auto panel = std::make_unique<SettingsPanel> (processor, [this] { return snap.rawZSlowDbfs; });
    panel->onThemeChanged = [this] { applyTheme (settings.theme.load()); };
    panel->onLoadCalFile = [this] { loadCalFile(); };
    juce::CallOutBox::launchAsynchronously (std::move (panel), settingsButton.getBounds(), this);
}

void LiveAlignEditor::toggleGenerator()
{
    settings.genOn = ! settings.genOn.load();
    generator.sync();
}

void LiveAlignEditor::toggleRunning()
{
    settings.running = ! settings.running.load();
}

void LiveAlignEditor::capture()
{
    const bool rta = settings.view.load() == (int) la::View::Rta;
    la::Trace t;
    const auto time = juce::Time::getCurrentTime().formatted ("%H:%M:%S");
    if (rta)
    {
        if (snap.rtaFreq.empty()) return;
        t.kind = la::Trace::Rta;
        t.name = "RTA " + time;
        for (size_t i = 0; i < snap.rtaFreq.size(); ++i)
        {
            t.data.freq.push_back (snap.rtaFreq[i]);
            t.data.db.push_back (snap.rtaMic[i]);
        }
    }
    else
    {
        if (snap.tfFrames == 0)
        {
            showMessage ("Nothing to capture", "There is no transfer function yet. Start the generator or program audio with a reference connected.");
            return;
        }
        t.kind = la::Trace::Transfer;
        t.name = "TF " + time;
        for (size_t i = 0; i < snap.tfFreq.size(); ++i)
        {
            if (! std::isfinite (snap.tfMag[i])) continue;
            t.data.freq.push_back (snap.tfFreq[i]);
            t.data.db.push_back (snap.tfMag[i]);
            t.data.phase.push_back (snap.tfPhase[i]);
            t.data.extra.push_back (snap.tfCoh[i]);
        }
        if (t.data.freq.empty()) return;
    }
    processor.traces.add (std::move (t));
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
    if (key == juce::KeyPress::spaceKey) { toggleRunning(); return true; }
    if (c == 'c') { capture(); return true; }
    if (c == 'd') { settings.requestFindDelay = true; return true; }
    if (c == 'g') { toggleGenerator(); return true; }
    if (c == 'r') { settings.requestResetAverages = true; return true; }
    if (c == '1') { setView (0); return true; }
    if (c == '2') { setView (1); return true; }
    if (c == '3') { setView (2); return true; }
    return false;
}

void LiveAlignEditor::timerCallback()
{
    ++tick;
    if (settings.theme.load() != appliedTheme) applyTheme (settings.theme.load());
    if (settings.view.load() != shownView) setView (settings.view.load());

    if (processor.analyzer.readSnapshot (snap, snapSerial))
    {
        snapSerial = snap.serial;
        if (shownView == 0) rtaView.update (snap);
        else if (shownView == 1) transferView.update (snap);
        else impulseView.update (snap);
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
        for (int i = 0; i < 7; ++i)
            if (kAverages[i] == settings.averages.load()) avgBox.setSelectedItemIndex (i, juce::dontSendNotification);

        juce::String s;
        s << juce::String (snap.sampleRate / 1000.0, 1) << " kHz";
        if (shownView != 0)
            s << "   FFT " << snap.fftSize / 1024 << "k   " << juce::jmin (snap.tfFrames, settings.averages.load()) << "/"
              << settings.averages.load() << " avg";
        if (juce::Time::getMillisecondCounter() < overloadUntil) s << "   ANALYSIS BEHIND";
        if (s != status) { status = s; repaint (0, 0, getWidth(), 56); }
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

    auto h = header.withTrimmedLeft (190);
    for (auto& b : viewButtons) { b.setBounds (h.removeFromLeft (84).withSizeKeepingCentre (84, 28)); h.removeFromLeft (2); }
    h.removeFromLeft (14);
    runButton.setBounds (h.removeFromLeft (84).withSizeKeepingCentre (84, 28));
    h.removeFromLeft (8);
    avgBox.setBounds (h.removeFromLeft (92).withSizeKeepingCentre (92, 28));
    h.removeFromLeft (6);
    smoothBox.setBounds (h.removeFromLeft (100).withSizeKeepingCentre (100, 28));
    h.removeFromLeft (6);
    resetButton.setBounds (h.removeFromLeft (62).withSizeKeepingCentre (62, 28));
    h.removeFromLeft (6);
    centreButton.setBounds (h.removeFromLeft (66).withSizeKeepingCentre (66, 28));

    settingsButton.setBounds (h.removeFromRight (86).withSizeKeepingCentre (86, 28));
    h.removeFromRight (8);
    themeButton.setBounds (h.removeFromRight (62).withSizeKeepingCentre (62, 28));
    h.removeFromRight (12);
    statusArea = h;

    generator.setBounds (r.removeFromBottom (54));
    r.removeFromBottom (8);

    auto side = r.removeFromRight (330);
    r.removeFromRight (8);
    transferView.setBounds (r);
    rtaView.setBounds (r);
    impulseView.setBounds (r);

    splPanel.setBounds (side.removeFromTop (186));
    side.removeFromTop (8);
    inputPanel.setBounds (side.removeFromTop (98));
    side.removeFromTop (8);
    delayPanel.setBounds (side.removeFromTop (160));
    side.removeFromTop (8);
    tracesPanel.setBounds (side);
}
