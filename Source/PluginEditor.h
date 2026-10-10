#pragma once
#include <memory>
#include "PluginProcessor.h"
#include "UI/LALookAndFeel.h"
#include "UI/Views.h"
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

private:
    void timerCallback() override;
    void applyTheme (int theme);
    void setView (int v);
    void rebuildSmoothingBox();
    void openSettings();
    void capture();
    void importTrace();
    void exportTrace (int index);
    void renameTrace (int index);
    void deleteTrace (int index);
    void loadCalFile();
    void toggleGenerator();
    void toggleRunning();
    void showMessage (const juce::String& title, const juce::String& text);

    LiveAlignProcessor& processor;
    la::Settings& settings;
    la::ui::LALookAndFeel laf;

    la::Snapshot snap;
    juce::uint64 snapSerial = 0;

    la::ui::TransferView transferView;
    la::ui::RtaView rtaView;
    la::ui::ImpulseView impulseView;
    la::ui::SplPanel splPanel;
    la::ui::InputPanel inputPanel;
    la::ui::DelayPanel delayPanel;
    la::ui::TracesPanel tracesPanel;
    la::ui::GeneratorStrip generator;

    juce::TextButton viewButtons[3], runButton, resetButton { "Reset" }, settingsButton { "Settings" }, themeButton, centreButton { "Centre" };
    juce::ComboBox avgBox, smoothBox;
    juce::TooltipWindow tooltip { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> renameWindow;

    int appliedTheme = -1, shownView = -1, tick = 0;
    juce::uint32 overloadUntil = 0;
    juce::String status;
    juce::Rectangle<int> statusArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAlignEditor)
};
