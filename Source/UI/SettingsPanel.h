#pragma once
// The pop-up box for setup and rarely changed options, so the main window stays free for the graphs.
#include <functional>
#include <memory>
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../StandaloneHooks.h"

namespace la::ui
{
class SettingsPanel : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onThemeChanged, onLoadCalFile;

    SettingsPanel (LiveAlignProcessor& p, std::function<float()> rawZSlowDbfs) : proc (p), settings (p.settings), rawZ (std::move (rawZSlowDbfs))
    {
        const int nIn = juce::jmax (1, p.numInputs());

        // ---- left column: routing and analysis ----
        if (StandaloneHooks::showAudioSettings())
        {
            deviceButton.onClick = [] { if (auto& f = StandaloneHooks::showAudioSettings()) f(); };
            deviceButton.setTooltip ("Choose the audio interface, sample rate, buffer size and which channels are used");
            addRow (0, "Audio device", deviceButton);
        }

        for (int i = 0; i < nIn; ++i) micBox.addItem ("Input " + juce::String (i + 1), i + 1);
        micBox.setSelectedItemIndex (juce::jlimit (0, nIn - 1, settings.micChannel.load()), juce::dontSendNotification);
        micBox.onChange = [this] { settings.micChannel = micBox.getSelectedItemIndex(); settings.requestResetAverages = true; };
        addRow (0, "Measurement mic", micBox);

        refBox.addItem ("Off", 1);
        refBox.addItem ("Generator (internal copy)", 2);
        for (int i = 0; i < nIn; ++i) refBox.addItem ("Input " + juce::String (i + 1) + " (loopback)", i + 3);
        refBox.setSelectedItemIndex (juce::jlimit (0, nIn + 1, settings.refSource.load()), juce::dontSendNotification);
        refBox.setTooltip ("What the mic is compared against. A loopback cable from the output that feeds the PA is the most "
                           "accurate. The internal generator copy works without a cable but only with the generator.");
        refBox.onChange = [this] { settings.refSource = refBox.getSelectedItemIndex(); settings.requestResetAverages = true; };
        addRow (0, "Reference", refBox);

        fftBox.addItem ("8k   (5.9 Hz, fast)", 1);
        fftBox.addItem ("16k  (2.9 Hz)", 2);
        fftBox.addItem ("32k  (1.5 Hz)", 3);
        fftBox.addItem ("64k  (0.7 Hz, slow)", 4);
        static const int sizes[] = { 8192, 16384, 32768, 65536 };
        for (int i = 0; i < 4; ++i) if (sizes[i] == settings.fftSize.load()) fftBox.setSelectedItemIndex (i, juce::dontSendNotification);
        fftBox.setTooltip ("Transfer function FFT size. Bigger resolves low frequencies better but reacts more slowly. "
                           "Resolution shown at 48 kHz.");
        fftBox.onChange = [this] { settings.fftSize = sizes[juce::jlimit (0, 3, fftBox.getSelectedItemIndex())]; };
        addRow (0, "FFT size", fftBox);

        blankBox.addItemList ({ "Off", "20 %", "40 %", "60 %", "80 %" }, 1);
        blankBox.setSelectedItemIndex (juce::jlimit (0, 4, (int) std::lround (settings.coherenceBlank.load() * 5.0f)), juce::dontSendNotification);
        blankBox.setTooltip ("Hide parts of the transfer function where coherence is below this, so noise and reflections "
                             "do not look like real response");
        blankBox.onChange = [this] { settings.coherenceBlank = (float) blankBox.getSelectedItemIndex() / 5.0f; };
        addRow (0, "Coherence blanking", blankBox);

        rangeBox.addItemList ({ "+/- 12 dB", "+/- 20 dB", "+/- 30 dB", "+/- 40 dB" }, 1);
        static const float ranges[] = { 12.0f, 20.0f, 30.0f, 40.0f };
        for (int i = 0; i < 4; ++i) if (std::abs (ranges[i] - settings.magRange.load()) < 0.1f) rangeBox.setSelectedItemIndex (i, juce::dontSendNotification);
        rangeBox.onChange = [this] { settings.magRange = ranges[juce::jlimit (0, 3, rangeBox.getSelectedItemIndex())]; };
        addRow (0, "Magnitude range", rangeBox);

        speedBox.addItemList ({ "Fast", "Medium", "Slow" }, 1);
        speedBox.setSelectedItemIndex (settings.rtaSpeed.load(), juce::dontSendNotification);
        speedBox.onChange = [this] { settings.rtaSpeed = speedBox.getSelectedItemIndex(); };
        addRow (0, "RTA speed", speedBox);

        refRtaToggle.setToggleState (settings.rtaShowRef.load(), juce::dontSendNotification);
        refRtaToggle.onClick = [this] { settings.rtaShowRef = refRtaToggle.getToggleState(); };
        addRow (0, "RTA overlay", refRtaToggle);

        peakToggle.setToggleState (settings.rtaPeakHold.load(), juce::dontSendNotification);
        peakToggle.onClick = [this] { settings.rtaPeakHold = peakToggle.getToggleState(); settings.requestClearPeak = true; };
        addRow (0, "RTA peak hold", peakToggle);

        // ---- right column: calibration, environment, display ----
        calLoad.onClick = [this] { if (onLoadCalFile) onLoadCalFile(); };
        calLoad.setTooltip ("Load your measurement mic's calibration file (frequency and dB per line, as supplied with the mic)");
        calClear.onClick = [this] { proc.clearCalibration(); refreshText(); };
        addRow (1, "Mic calibration file", calLoad, &calClear);

        cal94.onClick = [this] { calibrateTo (94.0f); };
        cal114.onClick = [this] { calibrateTo (114.0f); };
        cal94.setTooltip ("Put a 94 dB calibrator on the mic, then press this");
        cal114.setTooltip ("Put a 114 dB calibrator on the mic, then press this");
        addRow (1, "SPL calibrator", cal94, &cal114);

        offsetSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        offsetSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        offsetSlider.setRange (60.0, 160.0, 0.1);
        offsetSlider.setTextValueSuffix (" dB");
        offsetSlider.setValue (settings.splOffset.load(), juce::dontSendNotification);
        offsetSlider.setTooltip ("dB SPL that a full-scale (0 dBFS) signal at the mic input would be. Set by the calibrator "
                                 "buttons, or type it in.");
        offsetSlider.onValueChange = [this] { settings.splOffset = (float) offsetSlider.getValue(); settings.splCalibrated = true; splToggle.setToggleState (true, juce::dontSendNotification); };
        addRow (1, "SPL offset", offsetSlider);

        splToggle.setToggleState (settings.splCalibrated.load(), juce::dontSendNotification);
        splToggle.onClick = [this] { settings.splCalibrated = splToggle.getToggleState(); };
        addRow (1, "Show levels as", splToggle);

        tempSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        tempSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        tempSlider.setRange (-10.0, 45.0, 1.0);
        tempSlider.setTextValueSuffix (juce::String (juce::CharPointer_UTF8 (" \xc2\xb0" "C")));
        tempSlider.setValue (settings.temperatureC.load(), juce::dontSendNotification);
        tempSlider.setTooltip ("Air temperature, for converting delay to distance (speed of sound)");
        tempSlider.onValueChange = [this] { settings.temperatureC = (float) tempSlider.getValue(); };
        addRow (1, "Temperature", tempSlider);

        unitBox.addItemList ({ "Feet", "Metres" }, 1);
        unitBox.setSelectedItemIndex (settings.distanceUnit.load(), juce::dontSendNotification);
        unitBox.onChange = [this] { settings.distanceUnit = unitBox.getSelectedItemIndex(); };
        addRow (1, "Distance units", unitBox);

        themeBox.addItemList ({ "Dark", "Light" }, 1);
        themeBox.setSelectedItemIndex (settings.theme.load(), juce::dontSendNotification);
        themeBox.onChange = [this] { settings.theme = themeBox.getSelectedItemIndex(); if (onThemeChanged) onThemeChanged(); };
        addRow (1, "Theme", themeBox);

        folderButton.onClick = [] { TraceStore::traceFolder().createDirectory(); TraceStore::traceFolder().revealToUser(); };
        addRow (1, "Saved traces", folderButton);

        const int numRows = juce::jmax (rowCount[0], rowCount[1]);
        setSize (2 * kColW + 3 * kPad, kTop + numRows * kRowH + 64);
        refreshText();
        startTimerHz (4);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.fillAll (p.panel);
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("SETTINGS", kPad, 0, 200, kTop - 6, juce::Justification::centredLeft);

        g.setFont (font (12.0f));
        for (const auto& r : rows)
        {
            g.setColour (p.text);
            g.drawText (r.label, colX (r.col), kTop + r.row * kRowH, kLabelW - 6, 28, juce::Justification::centredLeft);
        }

        auto bottom = getLocalBounds().reduced (kPad, 8).removeFromBottom (48);
        g.setFont (font (11.5f));
        g.setColour (p.textDim);
        g.drawText ("Calibration file: " + calText, bottom.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (message.startsWith ("!") ? p.warn : p.textDim);
        g.drawText (message.trimCharactersAtStart ("!"), bottom.removeFromTop (16), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.drawText (deviceText + "      " + JucePlugin_Name + " " + JucePlugin_VersionString, bottom, juce::Justification::centredLeft);
    }

    void resized() override
    {
        for (auto& r : rows)
        {
            auto area = juce::Rectangle<int> (colX (r.col) + kLabelW, kTop + r.row * kRowH, kColW - kLabelW, 28);
            if (r.second != nullptr)
            {
                const int w = (area.getWidth() - 6) / 2;
                r.comp->setBounds (area.removeFromLeft (w));
                area.removeFromLeft (6);
                r.second->setBounds (area);
            }
            else
                r.comp->setBounds (area);
        }
    }

private:
    static constexpr int kColW = 380, kLabelW = 150, kRowH = 36, kTop = 34, kPad = 14;
    static int colX (int col) { return kPad + col * (kColW + kPad); }

    struct Row { juce::String label; juce::Component* comp; juce::Component* second; int col, row; };

    void addRow (int col, const juce::String& label, juce::Component& c, juce::Component* second = nullptr)
    {
        addAndMakeVisible (c);
        if (second != nullptr) addAndMakeVisible (second);
        rows.push_back ({ label, &c, second, col, rowCount[col]++ });
    }

    void timerCallback() override { refreshText(); }

    void refreshText()
    {
        const auto n = proc.analyzer.getCalibrationName();
        calText = n.isEmpty() ? "none (flat)" : n;
        deviceText = StandaloneHooks::deviceDescription() ? StandaloneHooks::deviceDescription()() : "Inputs and outputs come from the host";
        splToggle.setButtonText (settings.splCalibrated.load() ? "dB SPL" : "dBFS");
        splToggle.setToggleState (settings.splCalibrated.load(), juce::dontSendNotification);
        if (! offsetSlider.isMouseButtonDown()) offsetSlider.setValue (settings.splOffset.load(), juce::dontSendNotification);
        repaint();
    }

    void calibrateTo (float target)
    {
        const float level = rawZ ? rawZ() : -200.0f;
        if (level < -80.0f)
        {
            message = "!No signal on the mic input. Fit the calibrator, switch it on, wait a second, then press again.";
            repaint();
            return;
        }
        settings.splOffset = target - level;
        settings.splCalibrated = true;
        message = "Calibrated: " + juce::String (level, 1) + " dBFS = " + juce::String (juce::roundToInt (target)) + " dB SPL";
        refreshText();
    }

    LiveAlignProcessor& proc;
    Settings& settings;
    std::function<float()> rawZ;
    std::vector<Row> rows;
    int rowCount[2] {};

    juce::TextButton deviceButton { "Audio device..." }, calLoad { "Load..." }, calClear { "Clear" },
        cal94 { "Set 94 dB" }, cal114 { "Set 114 dB" }, folderButton { "Open folder" };
    juce::ComboBox micBox, refBox, fftBox, blankBox, rangeBox, speedBox, unitBox, themeBox;
    juce::ToggleButton refRtaToggle { "Show reference" }, peakToggle { "Show peaks" }, splToggle { "dB SPL" };
    juce::Slider offsetSlider, tempSlider;
    juce::String calText, deviceText, message;
};
} // namespace la::ui
