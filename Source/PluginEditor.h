// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <memory>
#include "PluginProcessor.h"
#include "UI/LALookAndFeel.h"
#include "UI/NavRail.h"
#include "UI/HomePage.h"
#include "UI/MicSetupPage.h"
#include "UI/SpeakersPage.h"
#include "UI/PlanPage.h"
#include "UI/MicLockerPanel.h"
#include "UI/Pages.h"
#include "UI/MeasurePane.h"
#include "UI/SidePanels.h"
#include "UI/GeneratorStrip.h"

class LiveAlignEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LiveAlignEditor (LiveAlignProcessor&);
    ~LiveAlignEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    void showPage (int page);
    void setPaneViews (int a, int b, int c);   // used by the local screenshot tool
    la::ui::SpeakersPage& speakersPage() { return *speakerPage; }
    la::ui::PlanPage& planPageForPreview() { return *planPage; }

private:
    void timerCallback() override;
    void applyTheme (int theme);
    void layoutPanes();
    void updateStatuses();
    void refreshMicPicker();

    void openSettings();
    void refreshUpdate();
    void showUpdate();
    void openMicLocker();
    void loadLockerCal (const juce::String& id, int angle);
    void exportPlan();
    void exportReport();
    void openCalibration (int mic, juce::Component* anchor);
    void loadCalFile (int mic, int angle = -1);
    void capture();
    void importTrace();
    void importSpeaker (int source);
    void exportTrace (int index);
    void renameTrace (int index);
    void deleteTrace (int index);
    void toggleGenerator();
    void toggleFullScreen();
    void showMessage (const juce::String& title, const juce::String& text);
    void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                  std::function<void (const juce::String&)> done);

    // venue and sessions
    void saveProject (bool askForFile);
    void openProject();
    void newProject();
    void newSession();
    void afterProjectLoaded();
    void confirmDiscard (std::function<void()> then);

    LiveAlignProcessor& processor;
    la::Settings& settings;
    la::ui::LALookAndFeel laf;
    const bool standalone;

    la::Snapshot snap;
    juce::uint64 snapSerial = 0;

    la::ui::NavRail nav;
    std::unique_ptr<la::ui::HomePage> homePage;
    std::unique_ptr<la::ui::MicSetupPage> micPage;
    std::unique_ptr<la::ui::SpeakersPage> speakerPage;
    std::unique_ptr<la::ui::PlaceholderPage> roomPage;
    std::unique_ptr<la::ui::PlanPage> planPage;
    std::unique_ptr<la::ui::MicLockerPage> lockerPage;

    // Monitoring page
    std::unique_ptr<la::ui::MeasurePane> panes[3];
    la::ui::SplPanel splPanel;
    la::ui::InputPanel inputPanel;
    la::ui::DelayPanel delayPanel;
    la::ui::TracesPanel tracesPanel;
    juce::ComboBox micPicker;

    la::ui::GeneratorStrip generator;

    juce::TextButton runButton, resetButton { "RESET AVG" };
    juce::TooltipWindow tooltip { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> textWindow;

    int appliedTheme = -1, tick = 0, laidOutCount = -1, laidOutLayout = -1, shownPage = -1, pickerMics = -1;
    bool unsaved = false, monitorMicChosen = false;
    juce::uint32 overloadUntil = 0;
    juce::Rectangle<int> paneArea, pageArea, titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAlignEditor)
};
