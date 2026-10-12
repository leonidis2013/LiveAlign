#pragma once
// Pop-up from the Sound level box: SPL calibration with an acoustic calibrator, the manual offset,
// and the measurement mic's calibration file.
#include <functional>
#include "Theme.h"
#include "../Settings.h"

namespace la::ui
{
class CalibrationPanel : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onLoadFile, onClearFile;

    CalibrationPanel (Settings& s, std::function<float()> rawZSlowDbfs, std::function<juce::String()> calFileName)
        : settings (s), rawZ (std::move (rawZSlowDbfs)), fileName (std::move (calFileName))
    {
        levelBox.addItem ("94 dB", 1);
        levelBox.addItem ("114 dB", 2);
        levelBox.addItem ("Other", 3);
        const float cur = settings.calibratorDb.load();
        levelBox.setSelectedId (std::abs (cur - 94.0f) < 0.05f ? 1 : (std::abs (cur - 114.0f) < 0.05f ? 2 : 3), juce::dontSendNotification);
        levelBox.onChange = [this]
        {
            if (levelBox.getSelectedId() == 1) settings.calibratorDb = 94.0f;
            else if (levelBox.getSelectedId() == 2) settings.calibratorDb = 114.0f;
            otherSlider.setValue (settings.calibratorDb.load(), juce::dontSendNotification);
            otherSlider.setVisible (levelBox.getSelectedId() == 3);
        };
        addAndMakeVisible (levelBox);

        otherSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        otherSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        otherSlider.setRange (80.0, 130.0, 0.1);
        otherSlider.setTextValueSuffix (" dB");
        otherSlider.setValue (cur, juce::dontSendNotification);
        otherSlider.onValueChange = [this] { settings.calibratorDb = (float) otherSlider.getValue(); };
        addChildComponent (otherSlider);
        otherSlider.setVisible (levelBox.getSelectedId() == 3);

        calButton.setColour (juce::TextButton::buttonColourId, pal().accent);
        calButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
        calButton.onClick = [this] { calibrate(); };
        addAndMakeVisible (calButton);

        offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        offsetSlider.setRange (60.0, 160.0, 0.1);
        offsetSlider.setTextValueSuffix (" dB");
        offsetSlider.setTooltip ("dB SPL that a full-scale (0 dBFS) signal at the mic input would be. Set by Calibrate, or adjust by hand.");
        offsetSlider.onValueChange = [this] { settings.splOffset = (float) offsetSlider.getValue(); settings.splCalibrated = true; };
        addAndMakeVisible (offsetSlider);

        splToggle.onClick = [this] { settings.splCalibrated = splToggle.getToggleState(); };
        addAndMakeVisible (splToggle);

        loadButton.onClick = [this] { if (onLoadFile) onLoadFile(); };
        clearButton.onClick = [this] { if (onClearFile) onClearFile(); refresh(); };
        addAndMakeVisible (loadButton);
        addAndMakeVisible (clearButton);

        setSize (460, 404);
        refresh();
        startTimerHz (8);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.fillAll (p.panel);
        auto r = getLocalBounds().reduced (kPad, 0);

        sectionHeader (g, r.removeFromTop (34), "Sound level calibration");
        g.setColour (p.textDim);
        g.setFont (font (12.0f));
        g.drawFittedText ("Fit the calibrator to the mic and switch it on. When the reading below settles, press Calibrate.",
                          r.removeFromTop (34), juce::Justification::topLeft, 2);

        auto live = r.removeFromTop (40);
        g.setColour (p.text);
        g.setFont (font (12.0f));
        g.drawText ("Mic input now", live.removeFromLeft (kLabelW), juce::Justification::centredLeft);
        g.setFont (monoFont (17.0f, true));
        const float z = rawZ ? rawZ() : -200.0f;
        juce::String txt = z < -150.0f ? juce::String ("no signal") : juce::String (z, 1) + " dBFS";
        if (z > -150.0f && settings.splCalibrated.load()) txt << "  =  " << juce::String (z + settings.splOffset.load(), 1) << " dB SPL";
        g.drawText (txt, live, juce::Justification::centredLeft);

        g.setColour (p.text);
        g.setFont (font (12.0f));
        g.drawText ("Calibrator level", kPad, rowY (0), kLabelW, 28, juce::Justification::centredLeft);
        g.drawText ("SPL offset", kPad, rowY (2), kLabelW, 28, juce::Justification::centredLeft);
        g.drawText ("Show levels as", kPad, rowY (3), kLabelW, 28, juce::Justification::centredLeft);

        auto fileArea = juce::Rectangle<int> (kPad, rowY (4) + 6, getWidth() - 2 * kPad, 34);
        sectionHeader (g, fileArea, "Mic calibration file");
        g.setColour (p.text);
        g.drawText ("File", kPad, rowY (5) + 8, kLabelW, 28, juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (11.5f));
        const auto name = fileName ? fileName() : juce::String();
        g.drawText (name.isEmpty() ? "None loaded: the mic is treated as flat" : name,
                    kPad, rowY (6) + 8, getWidth() - 2 * kPad, 18, juce::Justification::centredLeft, true);

        g.setColour (message.startsWith ("!") ? p.warn : p.good);
        g.drawText (message.trimCharactersAtStart ("!"), kPad, getHeight() - 26, getWidth() - 2 * kPad, 18, juce::Justification::centredLeft);
    }

    void resized() override
    {
        const int x = kPad + kLabelW, w = getWidth() - kPad - x;
        levelBox.setBounds (x, rowY (0), 110, 28);
        otherSlider.setBounds (x + 118, rowY (0), w - 118, 28);
        calButton.setBounds (x, rowY (1), w, 30);
        offsetSlider.setBounds (x, rowY (2), w, 28);
        splToggle.setBounds (x, rowY (3), w, 28);
        loadButton.setBounds (x, rowY (5) + 8, (w - 6) / 2, 28);
        clearButton.setBounds (x + (w - 6) / 2 + 6, rowY (5) + 8, (w - 6) / 2, 28);
    }

    void refresh()
    {
        if (! offsetSlider.isMouseButtonDown()) offsetSlider.setValue (settings.splOffset.load(), juce::dontSendNotification);
        splToggle.setToggleState (settings.splCalibrated.load(), juce::dontSendNotification);
        splToggle.setButtonText (settings.splCalibrated.load() ? "dB SPL (calibrated)" : "dBFS (not calibrated)");
        repaint();
    }

private:
    static constexpr int kPad = 18, kLabelW = 130;
    static int rowY (int row) { return 114 + row * 38; }

    static void sectionHeader (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title)
    {
        g.setColour (pal().accent);
        g.setFont (font (11.0f, true));
        g.drawText (title.toUpperCase(), r, juce::Justification::centredLeft);
        g.setColour (pal().border);
        g.drawHorizontalLine (r.getBottom() - 4, (float) r.getX(), (float) r.getRight());
    }

    void timerCallback() override { refresh(); }

    void calibrate()
    {
        const float level = rawZ ? rawZ() : -200.0f;
        const float target = settings.calibratorDb.load();
        if (level < -80.0f)
        {
            message = "!No signal on the mic input. Check the calibrator is on and fitted.";
        }
        else
        {
            settings.splOffset = target - level;
            settings.splCalibrated = true;
            settings.requestResetLeq = true;
            message = "Calibrated: " + juce::String (level, 1) + " dBFS = " + juce::String (target, 1) + " dB SPL";
        }
        refresh();
    }

    Settings& settings;
    std::function<float()> rawZ;
    std::function<juce::String()> fileName;
    juce::ComboBox levelBox;
    juce::Slider otherSlider, offsetSlider;
    juce::TextButton calButton { "Calibrate" }, loadButton { "Load..." }, clearButton { "Clear" };
    juce::ToggleButton splToggle;
    juce::String message;
};
} // namespace la::ui
