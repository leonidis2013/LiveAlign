#pragma once
// Bottom strip: the signal generator. It always starts off, and the on button turns red while it is sounding.
#include "Theme.h"
#include "../Settings.h"

namespace la::ui
{
class GeneratorStrip : public juce::Component
{
public:
    explicit GeneratorStrip (Settings& s) : settings (s)
    {
        onButton.setClickingTogglesState (true);
        onButton.setTooltip ("Start or stop the generator (key: G). It ramps in and out over 20 ms.");
        onButton.onClick = [this] { settings.genOn = onButton.getToggleState(); refreshColours(); };
        addAndMakeVisible (onButton);

        typeBox.addItemList ({ "Pink noise", "White noise", "Sine", "Log sweep", "Polarity pulse" }, 1);
        typeBox.setTooltip ("Pink noise for tuning, a sweep for quiet rooms, sine for checking a frequency, "
                            "and a positive pulse for polarity checkers");
        typeBox.onChange = [this] { settings.genType = typeBox.getSelectedItemIndex(); updateVisibility(); };
        addAndMakeVisible (typeBox);

        setupSlider (levelSlider, -60.0, 0.0, 0.5, " dBFS");
        levelSlider.setTooltip ("Generator level (RMS; a full-scale sine is 0 dBFS). Start low and bring the PA up slowly.");
        levelSlider.onValueChange = [this] { settings.genLevelDb = (float) levelSlider.getValue(); };

        setupSlider (freqSlider, 20.0, 20000.0, 1.0, " Hz");
        freqSlider.setSkewFactorFromMidPoint (630.0);
        freqSlider.onValueChange = [this] { settings.genFreq = (float) freqSlider.getValue(); };

        sweepBox.addItemList ({ "1 s sweep", "2 s sweep", "3 s sweep", "5 s sweep", "10 s sweep" }, 1);
        sweepBox.onChange = [this]
        {
            static const float secs[] = { 1.0f, 2.0f, 3.0f, 5.0f, 10.0f };
            settings.genSweepSec = secs[juce::jlimit (0, 4, sweepBox.getSelectedItemIndex())];
        };
        addAndMakeVisible (sweepBox);

        outputBox.setTooltip ("Which output the generator plays from");
        outputBox.onChange = [this] { settings.genOutput = outputBox.getSelectedItemIndex(); };
        addAndMakeVisible (outputBox);

        sync();
    }

    void setNumOutputs (int n)
    {
        if (n == numOutputs) return;
        numOutputs = n;
        outputBox.clear (juce::dontSendNotification);
        outputBox.addItem ("All outputs", 1);
        for (int i = 0; i < n; ++i) outputBox.addItem ("Output " + juce::String (i + 1), i + 2);
        outputBox.setSelectedItemIndex (juce::jlimit (0, n, settings.genOutput.load()), juce::dontSendNotification);
    }

    // Follows the settings (keyboard shortcut, state recall).
    void sync()
    {
        onButton.setToggleState (settings.genOn.load(), juce::dontSendNotification);
        typeBox.setSelectedItemIndex (settings.genType.load(), juce::dontSendNotification);
        {
            // the ceiling from Settings, Generator and safety
            const double ceiling = juce::jlimit (-40.0, 0.0, (double) settings.genMaxDb.load());
            if (std::abs (levelSlider.getMaximum() - ceiling) > 0.01) levelSlider.setRange (-60.0, ceiling, 0.5);
            if (settings.genLevelDb.load() > ceiling) settings.genLevelDb = (float) ceiling;
        }
        if (! levelSlider.isMouseButtonDown()) levelSlider.setValue (settings.genLevelDb.load(), juce::dontSendNotification);
        if (! freqSlider.isMouseButtonDown()) freqSlider.setValue (settings.genFreq.load(), juce::dontSendNotification);
        static const float secs[] = { 1.0f, 2.0f, 3.0f, 5.0f, 10.0f };
        int si = 2;
        for (int i = 0; i < 5; ++i) if (std::abs (secs[i] - settings.genSweepSec.load()) < 0.01f) si = i;
        sweepBox.setSelectedItemIndex (si, juce::dontSendNotification);
        if (numOutputs > 0) outputBox.setSelectedItemIndex (juce::jlimit (0, numOutputs, settings.genOutput.load()), juce::dontSendNotification);
        updateVisibility();
        refreshColours();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), {});
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("GENERATOR", getLocalBounds().withTrimmedLeft (14).withWidth (90), juce::Justification::centredLeft);
        if (settings.genOn.load())
        {
            g.setColour (p.bad);
            g.setFont (font (11.5f, true));
            g.drawText ("SOUNDING", getLocalBounds().withTrimmedRight (14).removeFromRight (80), juce::Justification::centredRight);
        }
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 0).withTrimmedLeft (90).withTrimmedRight (90);
        auto row = r.withSizeKeepingCentre (r.getWidth(), 30);
        onButton.setBounds (row.removeFromLeft (70));
        row.removeFromLeft (10);
        typeBox.setBounds (row.removeFromLeft (150));
        row.removeFromLeft (10);
        levelSlider.setBounds (row.removeFromLeft (260));
        row.removeFromLeft (10);
        auto extra = row.removeFromLeft (260);
        freqSlider.setBounds (extra);
        sweepBox.setBounds (extra.withWidth (130));
        row.removeFromLeft (10);
        outputBox.setBounds (row.removeFromLeft (140));
    }

    void lookAndFeelChanged() override { refreshColours(); }

private:
    void setupSlider (juce::Slider& s, double lo, double hi, double step, const juce::String& suffix)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 84, 24);
        s.setRange (lo, hi, step);
        s.setTextValueSuffix (suffix);
        addAndMakeVisible (s);
    }

    void updateVisibility()
    {
        const int t = settings.genType.load();
        freqSlider.setVisible (t == 2);
        sweepBox.setVisible (t == 3);
    }

    void refreshColours()
    {
        onButton.setButtonText (settings.genOn.load() ? "ON" : "OFF");
        onButton.setColour (juce::TextButton::buttonOnColourId, pal().bad);
        repaint();
    }

    Settings& settings;
    juce::TextButton onButton { "OFF" };
    juce::ComboBox typeBox, sweepBox, outputBox;
    juce::Slider levelSlider, freqSlider;
    int numOutputs = 0;
};
} // namespace la::ui
