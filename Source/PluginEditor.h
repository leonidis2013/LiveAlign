#pragma once
#include <memory>
#include "PluginProcessor.h"
#include "UI/LALookAndFeel.h"
#include "UI/MeasurePane.h"
#include "UI/SidePanels.h"
#include "UI/GeneratorStrip.h"
#include "UI/Pages.h"
#include "UI/MicSetupPage.h"

class LiveAlignEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LiveAlignEditor (LiveAlignProcessor&);
    ~LiveAlignEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    // Used by the local screenshot tool.
    void setPaneViews (int a, int b, int c);
    void showTab (int tab);

private:
    void timerCallback() override;
    void applyTheme (int theme);
    void layoutPanes();
    void openSettings();
    void openCalibration();
    void capture();
    void importTrace();
    void exportTrace (int index);
    void renameTrace (int index);
    void deleteTrace (int index);
    void loadCalFile();
    void toggleGenerator();
    void toggleFullScreen();
    void showMessage (const juce::String& title, const juce::String& text);

    // venue projects
    void showProjectMenu();
    void saveProject (bool askForFile);
    void openProject();
    void newProject();
    void editVenueDetails();
    void afterProjectLoaded();
    void confirmDiscard (std::function<void()> then);

    LiveAlignProcessor& processor;
    la::Settings& settings;
    la::ui::LALookAndFeel laf;
    const bool standalone;

    la::Snapshot snap;
    juce::uint64 snapSerial = 0;

    la::ui::WorkflowTabs workflowTabs;
    std::unique_ptr<la::ui::MicSetupPage> micPage;
    std::unique_ptr<la::ui::PlaceholderPage> speakerPage, roomPage;

    // Monitoring page
    std::unique_ptr<la::ui::MeasurePane> panes[3];
    la::ui::SplPanel splPanel;
    la::ui::InputPanel inputPanel;
    la::ui::DelayPanel delayPanel;
    la::ui::TracesPanel tracesPanel;
    juce::TextButton monitorBackButton;

    la::ui::GeneratorStrip generator;

    juce::TextButton projectButton { "Project" }, runButton, resetButton { "Reset avg" }, fullScreenButton { "Full screen" },
        settingsButton { "Settings" };
    juce::TooltipWindow tooltip { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> renameWindow, venueWindow;

    int appliedTheme = -1, tick = 0, laidOutCount = -1, laidOutLayout = -1, shownTab = -1;
    bool unsaved = false;
    juce::uint32 overloadUntil = 0;
    juce::String status;
    juce::Rectangle<int> statusArea, paneArea, pageArea, venueArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAlignEditor)
};
