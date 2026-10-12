#pragma once
#include <memory>
#include "PluginProcessor.h"
#include "UI/LALookAndFeel.h"
#include "UI/MeasurePane.h"
#include "UI/SidePanels.h"
#include "UI/GeneratorStrip.h"

namespace la::ui
{
// Workflow tabs across the top. Each step of the tuning process becomes a tab; stage 1 has Metering only.
class WorkflowTabs : public juce::Component, public juce::SettableTooltipClient
{
public:
    WorkflowTabs() { setTooltip ("Metering: RTA, transfer function, impulse response, sound level and delay"); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const juce::StringArray names { "Metering" };
        auto r = getLocalBounds();
        for (int i = 0; i < names.size(); ++i)
        {
            auto tab = r.removeFromLeft (128);
            const bool active = i == current;
            g.setColour (active ? p.panel : juce::Colours::transparentBlack);
            g.fillRoundedRectangle (tab.toFloat().reduced (0.0f, 2.0f), 6.0f);
            g.setColour (active ? p.accent : p.textDim);
            g.fillRoundedRectangle (juce::Rectangle<float> ((float) tab.getX() + 14.0f, (float) tab.getBottom() - 4.0f, (float) tab.getWidth() - 28.0f, 2.5f), 1.0f);

            auto label = tab.reduced (12, 0);
            g.setColour (active ? p.accent : p.textDim);
            g.setFont (monoFont (11.0f, true));
            g.drawText (juce::String (i + 1), label.removeFromLeft (16), juce::Justification::centredLeft);
            g.setColour (active ? p.text : p.textDim);
            g.setFont (font (13.5f, true));
            g.drawText (names[i], label, juce::Justification::centredLeft);
        }
    }

private:
    int current = 0;
};
} // namespace la::ui

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

    LiveAlignProcessor& processor;
    la::Settings& settings;
    la::ui::LALookAndFeel laf;

    la::Snapshot snap;
    juce::uint64 snapSerial = 0;

    la::ui::WorkflowTabs workflowTabs;
    std::unique_ptr<la::ui::MeasurePane> panes[3];
    la::ui::SplPanel splPanel;
    la::ui::InputPanel inputPanel;
    la::ui::DelayPanel delayPanel;
    la::ui::TracesPanel tracesPanel;
    la::ui::GeneratorStrip generator;

    juce::TextButton runButton, resetButton { "Reset avg" }, fullScreenButton { "Full screen" }, settingsButton { "Settings" };
    juce::TooltipWindow tooltip { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> renameWindow;

    int appliedTheme = -1, tick = 0, laidOutCount = -1, laidOutLayout = -1;
    juce::uint32 overloadUntil = 0;
    juce::String status;
    juce::Rectangle<int> statusArea, paneArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAlignEditor)
};
