// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The Settings pop-up: every option in one place, grouped into sections in two columns.
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
    std::function<void()> onThemeChanged, onLayoutChanged;

    explicit SettingsPanel (LiveAlignProcessor& p) : settings (p.settings)
    {
        const int nIn = juce::jmax (1, p.numInputs());

        // ================= left column =================
        header (0, "Inputs  (mics are set up on the Mic setup page)");
        if (StandaloneHooks::showAudioSettings())
        {
            deviceButton.onClick = [] { if (auto& f = StandaloneHooks::showAudioSettings()) f(); };
            deviceButton.setTooltip ("Choose the audio interface, sample rate, buffer size and which channels are used");
            row (0, "Audio device", deviceButton);
        }
        refBox.addItem ("Off", 1);
        refBox.addItem ("Generator (internal copy)", 2);
        for (int i = 0; i < nIn; ++i) refBox.addItem ("Input " + juce::String (i + 1) + " (loopback)", i + 3);
        refBox.setSelectedItemIndex (juce::jlimit (0, nIn + 1, settings.refSource.load()), juce::dontSendNotification);
        refBox.setTooltip ("What the mic is compared against. A loopback cable from the output that feeds the PA is the most "
                           "accurate. The internal generator copy works without a cable but only with the generator.");
        refBox.onChange = [this] { settings.refSource = refBox.getSelectedItemIndex(); settings.requestResetAverages = true; };
        row (0, "Reference", refBox);

        header (0, "Transfer function");
        static const int sizes[] = { 8192, 16384, 32768, 65536 };
        fftBox.addItemList ({ "8k   (5.9 Hz, fast)", "16k  (2.9 Hz)", "32k  (1.5 Hz)", "64k  (0.7 Hz, slow)" }, 1);
        for (int i = 0; i < 4; ++i) if (sizes[i] == settings.fftSize.load()) fftBox.setSelectedItemIndex (i, juce::dontSendNotification);
        fftBox.setTooltip ("Bigger resolves low frequencies better but reacts more slowly. Resolution shown at 48 kHz.");
        fftBox.onChange = [this] { settings.fftSize = sizes[juce::jlimit (0, 3, fftBox.getSelectedItemIndex())]; };
        row (0, "FFT size", fftBox);

        static const int avgs[] = { 1, 2, 4, 8, 16, 32, 64 };
        for (int i = 0; i < 7; ++i)
        {
            avgBox.addItem (juce::String (avgs[i]) + (avgs[i] == 1 ? " (none)" : " frames"), i + 1);
            if (avgs[i] == settings.averages.load()) avgBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }
        avgBox.setTooltip ("More averages are steadier but slower to react");
        avgBox.onChange = [this] { settings.averages = avgs[juce::jlimit (0, 6, avgBox.getSelectedItemIndex())]; };
        row (0, "Averaging", avgBox);

        static const int smooth[] = { 48, 24, 12, 6, 3 };
        for (int i = 0; i < 5; ++i)
        {
            smoothBox.addItem ("1/" + juce::String (smooth[i]) + " octave", i + 1);
            if (smooth[i] == settings.smoothing.load()) smoothBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }
        smoothBox.setTooltip ("1/24 or 1/12 octave for tuning, 1/3 for the overall tonal shape");
        smoothBox.onChange = [this] { settings.smoothing = smooth[juce::jlimit (0, 4, smoothBox.getSelectedItemIndex())]; };
        row (0, "Smoothing", smoothBox);

        blankBox.addItemList ({ "Off", "20 %", "40 % (default)", "60 %", "80 % (strict)" }, 1);
        blankBox.setSelectedItemIndex (juce::jlimit (0, 4, (int) std::lround (settings.coherenceBlank.load() * 5.0f)), juce::dontSendNotification);
        blankBox.setTooltip ("How reliable a measurement point must be to count. Points with less coherence are hidden on the "
                             "transfer function and are not trusted by the speaker measurements and the crossover search. "
                             "Raise it in a noisy room to be stricter.");
        blankBox.onChange = [this] { settings.coherenceBlank = (float) blankBox.getSelectedItemIndex() / 5.0f; };
        row (0, "Minimum coherence", blankBox);

        static const float ranges[] = { 12.0f, 20.0f, 30.0f, 40.0f };
        rangeBox.addItemList ({ "+/- 12 dB", "+/- 20 dB", "+/- 30 dB", "+/- 40 dB" }, 1);
        for (int i = 0; i < 4; ++i) if (std::abs (ranges[i] - settings.magRange.load()) < 0.1f) rangeBox.setSelectedItemIndex (i, juce::dontSendNotification);
        rangeBox.onChange = [this] { settings.magRange = ranges[juce::jlimit (0, 3, rangeBox.getSelectedItemIndex())]; };
        row (0, "Magnitude range", rangeBox);

        header (0, "RTA");
        static const int bands[] = { 1, 3, 6, 12, 24, 48 };
        for (int i = 0; i < 6; ++i)
        {
            bandsBox.addItem ("1/" + juce::String (bands[i]) + " octave", i + 1);
            if (bands[i] == settings.rtaBands.load()) bandsBox.setSelectedItemIndex (i, juce::dontSendNotification);
        }
        bandsBox.onChange = [this] { settings.rtaBands = bands[juce::jlimit (0, 5, bandsBox.getSelectedItemIndex())]; };
        row (0, "Resolution", bandsBox);

        speedBox.addItemList ({ "Fast", "Medium", "Slow" }, 1);
        speedBox.setSelectedItemIndex (settings.rtaSpeed.load(), juce::dontSendNotification);
        speedBox.onChange = [this] { settings.rtaSpeed = speedBox.getSelectedItemIndex(); };
        row (0, "Speed", speedBox);

        refRtaToggle.setToggleState (settings.rtaShowRef.load(), juce::dontSendNotification);
        refRtaToggle.onClick = [this] { settings.rtaShowRef = refRtaToggle.getToggleState(); };
        peakToggle.setToggleState (settings.rtaPeakHold.load(), juce::dontSendNotification);
        peakToggle.onClick = [this] { settings.rtaPeakHold = peakToggle.getToggleState(); settings.requestClearPeak = true; };
        row (0, "Overlays", refRtaToggle, &peakToggle);

        // ================= right column =================
        header (1, "Screen");
        countBox.addItemList ({ "1 graph", "2 graphs", "3 graphs" }, 1);
        countBox.setSelectedItemIndex (settings.paneCount.load() - 1, juce::dontSendNotification);
        countBox.setTooltip ("Split the screen to show the RTA, transfer function and impulse response together. "
                             "Pick what each graph shows with the buttons above it.");
        countBox.onChange = [this] { settings.paneCount = countBox.getSelectedItemIndex() + 1; if (onLayoutChanged) onLayoutChanged(); };
        row (1, "Graphs", countBox);

        layoutBox.addItemList ({ "Stacked", "Side by side" }, 1);
        layoutBox.setSelectedItemIndex (settings.paneLayout.load(), juce::dontSendNotification);
        layoutBox.onChange = [this] { settings.paneLayout = layoutBox.getSelectedItemIndex(); if (onLayoutChanged) onLayoutChanged(); };
        row (1, "Arrangement", layoutBox);

        themeBox.addItemList ({ "Dark", "Light" }, 1);
        themeBox.setSelectedItemIndex (settings.theme.load(), juce::dontSendNotification);
        themeBox.onChange = [this] { settings.theme = themeBox.getSelectedItemIndex(); if (onThemeChanged) onThemeChanged(); };
        row (1, "Theme", themeBox);

        header (1, "Impulse response");
        irModeBox.addItemList ({ "Linear", "Log (ETC)" }, 1);
        irModeBox.setSelectedItemIndex (settings.irMode.load(), juce::dontSendNotification);
        irModeBox.onChange = [this] { settings.irMode = irModeBox.getSelectedItemIndex(); };
        row (1, "Display", irModeBox);

        irRangeBox.addItemList ({ "20 ms", "100 ms", "500 ms" }, 1);
        irRangeBox.setSelectedItemIndex (settings.irRange.load(), juce::dontSendNotification);
        irRangeBox.onChange = [this] { settings.irRange = irRangeBox.getSelectedItemIndex(); };
        row (1, "Time range", irRangeBox);

        header (1, "Waterfall");
        wfSpanBox.addItemList ({ "10 seconds", "30 seconds", "60 seconds" }, 1);
        wfSpanBox.setSelectedItemIndex (settings.wfSeconds.load(), juce::dontSendNotification);
        wfSpanBox.onChange = [this] { settings.wfSeconds = wfSpanBox.getSelectedItemIndex(); };
        row (1, "History", wfSpanBox);
        wfRangeBox.addItemList ({ "40 dB", "60 dB", "80 dB" }, 1);
        wfRangeBox.setSelectedItemIndex (settings.wfRange.load(), juce::dontSendNotification);
        wfRangeBox.setTooltip ("Level range from the loudest band down to the darkest colour");
        wfRangeBox.onChange = [this] { settings.wfRange = wfRangeBox.getSelectedItemIndex(); };
        row (1, "Colour range", wfRangeBox);

        header (1, "Environment");
        tempUnitBox.addItemList ({ juce::String (juce::CharPointer_UTF8 ("\xc2\xb0" "C  Celsius")),
                                   juce::String (juce::CharPointer_UTF8 ("\xc2\xb0" "F  Fahrenheit")) }, 1);
        tempUnitBox.setSelectedItemIndex (settings.temperatureUnit.load(), juce::dontSendNotification);
        tempUnitBox.onChange = [this] { settings.temperatureUnit = tempUnitBox.getSelectedItemIndex(); setupTemperatureSlider(); };
        row (1, "Temperature unit", tempUnitBox);

        tempSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        tempSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
        tempSlider.setTooltip ("Air temperature, used to turn delay times into distances (speed of sound)");
        tempSlider.onValueChange = [this]
        {
            const double v = tempSlider.getValue();
            settings.temperatureC = (float) (settings.temperatureUnit.load() == 1 ? (v - 32.0) * 5.0 / 9.0 : v);
        };
        setupTemperatureSlider();
        row (1, "Temperature", tempSlider);

        unitBox.addItemList ({ "Feet", "Metres" }, 1);
        unitBox.setSelectedItemIndex (settings.distanceUnit.load(), juce::dontSendNotification);
        unitBox.onChange = [this] { settings.distanceUnit = unitBox.getSelectedItemIndex(); };
        row (1, "Distance unit", unitBox);

        setSize (2 * kColW + kColGap + 2 * kPad, juce::jmax (colY[0], colY[1]) + 40);
        startTimerHz (2);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.fillAll (p.panel);
        g.setColour (p.text);
        g.setFont (font (17.0f, true));
        g.drawText ("SETTINGS", kPad, 8, 200, 30, juce::Justification::centredLeft);

        // divider between the columns
        g.setColour (p.border);
        const int divX = kPad + kColW + kColGap / 2;
        g.drawVerticalLine (divX, (float) kTop, (float) getHeight() - 36.0f);

        for (const auto& h : headers)
        {
            const int x = colX (h.col);
            g.setColour (p.accent);
            g.fillRoundedRectangle ((float) x, (float) h.y + 7.0f, 3.0f, 12.0f, 1.5f);
            g.setColour (p.text);
            g.setFont (font (13.0f, true));
            g.drawText (h.text, x + 10, h.y, kColW - 10, 26, juce::Justification::centredLeft);
            g.setColour (p.border);
            g.drawHorizontalLine (h.y + 27, (float) x, (float) (x + kColW));
        }

        g.setFont (font (12.5f));
        for (const auto& r : rows)
        {
            g.setColour (p.text);
            g.drawText (r.label, colX (r.col), r.y, kLabelW - 8, 28, juce::Justification::centredLeft);
        }

        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawText (deviceText, kPad, getHeight() - 30, getWidth() / 2, 20, juce::Justification::centredLeft);
        g.drawText (juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString, getWidth() / 2, getHeight() - 30,
                    getWidth() / 2 - kPad, 20, juce::Justification::centredRight);
    }

    void resized() override
    {
        for (auto& r : rows)
        {
            auto area = juce::Rectangle<int> (colX (r.col) + kLabelW, r.y, kColW - kLabelW, 28);
            if (r.second != nullptr)
            {
                const int w = (area.getWidth() - 8) / 2;
                r.comp->setBounds (area.removeFromLeft (w));
                area.removeFromLeft (8);
                r.second->setBounds (area);
            }
            else
                r.comp->setBounds (area);
        }
    }

private:
    static constexpr int kColW = 400, kColGap = 48, kLabelW = 150, kRowH = 36, kTop = 44, kPad = 20;
    static int colX (int col) { return kPad + col * (kColW + kColGap); }

    struct Row { juce::String label; juce::Component* comp; juce::Component* second; int col, y; };
    struct Header { juce::String text; int col, y; };

    void header (int col, const juce::String& text)
    {
        if (colY[col] > kTop) colY[col] += 10;
        headers.push_back ({ text, col, colY[col] });
        colY[col] += 32;
    }

    void row (int col, const juce::String& label, juce::Component& c, juce::Component* second = nullptr)
    {
        addAndMakeVisible (c);
        if (second != nullptr) addAndMakeVisible (second);
        rows.push_back ({ label, &c, second, col, colY[col] });
        colY[col] += kRowH;
    }

    void setupTemperatureSlider()
    {
        const bool f = settings.temperatureUnit.load() == 1;
        const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        tempSlider.setRange (f ? 14.0 : -10.0, f ? 113.0 : 45.0, 1.0);
        tempSlider.setTextValueSuffix (" " + deg + (f ? "F" : "C"));
        const double c = settings.temperatureC.load();
        tempSlider.setValue (f ? c * 9.0 / 5.0 + 32.0 : c, juce::dontSendNotification);
    }

    void timerCallback() override
    {
        const auto d = StandaloneHooks::deviceDescription() ? StandaloneHooks::deviceDescription()() : juce::String ("Inputs and outputs come from the host");
        if (d != deviceText) { deviceText = d; repaint(); }
    }

    Settings& settings;
    std::vector<Row> rows;
    std::vector<Header> headers;
    int colY[2] { kTop, kTop };

    juce::TextButton deviceButton { "Audio device..." };
    juce::ComboBox refBox, fftBox, avgBox, smoothBox, blankBox, rangeBox, bandsBox, speedBox, countBox, layoutBox,
        themeBox, irModeBox, irRangeBox, wfSpanBox, wfRangeBox, tempUnitBox, unitBox;
    juce::ToggleButton refRtaToggle { "Reference" }, peakToggle { "Peak hold" };
    juce::Slider tempSlider;
    juce::String deviceText;
};
} // namespace la::ui
