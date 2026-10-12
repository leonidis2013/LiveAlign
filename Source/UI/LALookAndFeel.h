// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include "Theme.h"

namespace la::ui
{
// Look-and-feel shared with the Live series: rounded 5 px buttons and boxes, pill switches, slim sliders.
class LALookAndFeel : public juce::LookAndFeel_V4
{
public:
    LALookAndFeel() { applyPalette(); }

    void applyPalette()
    {
        const auto& p = pal();
        setColour (juce::ResizableWindow::backgroundColourId, p.bg);
        setColour (juce::Slider::rotarySliderFillColourId, p.accent);
        setColour (juce::Slider::rotarySliderOutlineColourId, p.border);
        setColour (juce::Slider::thumbColourId, p.text);
        setColour (juce::Slider::textBoxTextColourId, p.text);
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour (juce::Slider::backgroundColourId, p.border);
        setColour (juce::Slider::trackColourId, p.accent);
        setColour (juce::Label::textColourId, p.text);
        setColour (juce::ComboBox::backgroundColourId, p.panelRaised);
        setColour (juce::ComboBox::textColourId, p.text);
        setColour (juce::ComboBox::outlineColourId, p.border);
        setColour (juce::ComboBox::arrowColourId, p.textDim);
        setColour (juce::PopupMenu::backgroundColourId, p.panelRaised);
        setColour (juce::PopupMenu::textColourId, p.text);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.border);
        setColour (juce::PopupMenu::highlightedTextColourId, p.text);
        setColour (juce::TextButton::buttonColourId, p.panelRaised);
        setColour (juce::TextButton::buttonOnColourId, p.accent);
        setColour (juce::TextButton::textColourOffId, p.text);
        setColour (juce::TextButton::textColourOnId, p.onAccent);
        setColour (juce::ToggleButton::textColourId, p.text);
        setColour (juce::TextEditor::backgroundColourId, p.panelRaised);
        setColour (juce::TextEditor::outlineColourId, p.border);
        setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
        setColour (juce::TextEditor::textColourId, p.text);
        setColour (juce::TextEditor::highlightColourId, p.accent.withAlpha (0.35f));
        setColour (juce::CaretComponent::caretColourId, p.text);
        setColour (juce::AlertWindow::backgroundColourId, p.panel);
        setColour (juce::AlertWindow::textColourId, p.text);
        setColour (juce::AlertWindow::outlineColourId, p.border);
        setColour (juce::TooltipWindow::backgroundColourId, p.panelRaised);
        setColour (juce::TooltipWindow::textColourId, p.text);
        setColour (juce::TooltipWindow::outlineColourId, p.border);
        setColour (juce::ScrollBar::thumbColourId, p.border);
        setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    }

    void drawCallOutBoxBackground (juce::CallOutBox& box, juce::Graphics& g, const juce::Path& path, juce::Image& cachedImage) override
    {
        if (cachedImage.isNull())
        {
            cachedImage = juce::Image (juce::Image::ARGB, box.getWidth(), box.getHeight(), true);
            juce::Graphics shadow (cachedImage);
            juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 8, { 0, 2 }).drawForPath (shadow, path);
        }
        g.setColour (juce::Colours::black);
        g.drawImageAt (cachedImage, 0, 0);
        g.setColour (pal().panel);
        g.fillPath (path);
        g.setColour (pal().border);
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return font (juce::jmin (13.0f, (float) buttonHeight * 0.48f), true);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getPopupMenuFont() override { return font (13.5f); }
    juce::Font getLabelFont (juce::Label&) override { return font (12.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgc, bool over, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId) : bgc;
        if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.5f);
        else if (down) fill = fill.brighter (0.15f);
        else if (over) fill = fill.brighter (0.07f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (on ? fill.brighter (0.3f) : pal().border);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool) override
    {
        g.setFont (getTextButtonFont (b, b.getHeight()));
        auto c = b.findColour (b.getToggleState() ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId);
        if (! b.isEnabled()) c = c.withMultipliedAlpha (0.45f);
        g.setColour (c);
        g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (6, 2), juce::Justification::centred, 1);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (box.hasKeyboardFocus (true) && box.isTextEditable() ? pal().accent : box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
        juce::Path arrow;
        const float ax = (float) w - 14.0f, ay = (float) h * 0.5f;
        arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (pal().textDim);
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 26, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    // Pill switch: track on the left, label beside it.
    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool) override
    {
        const auto r = b.getLocalBounds().toFloat();
        const auto track = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - 11.0f, 40.0f, 22.0f);
        const bool on = b.getToggleState();
        auto fill = on ? pal().accent : pal().border;
        if (! b.isEnabled()) fill = fill.withAlpha (0.4f);
        else if (over) fill = fill.brighter (0.08f);
        g.setColour (fill);
        g.fillRoundedRectangle (track, 11.0f);
        const float d = 16.0f;
        const float x = on ? track.getRight() - d - 3.0f : track.getX() + 3.0f;
        g.setColour (on ? pal().onAccent : pal().textDim);
        g.fillEllipse (x, track.getCentreY() - d * 0.5f, d, d);
        g.setColour (b.isEnabled() ? (on ? pal().text : pal().textDim) : pal().textFaint);
        g.setFont (font (12.0f, true));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (track.getWidth() + 10.0f), juce::Justification::centredLeft, true);
    }

    // Slim track and a round thumb, filled from zero for bipolar ranges.
    void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                           juce::Slider::SliderStyle, juce::Slider& s) override
    {
        const auto& p = pal();
        const float cy = (float) y + (float) h * 0.5f;
        const float x0 = (float) x + 6.0f, x1 = (float) (x + w) - 6.0f;
        g.setColour (p.border);
        g.fillRoundedRectangle (x0, cy - 2.0f, x1 - x0, 4.0f, 2.0f);
        const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0 && std::abs (s.getMinimum() + s.getMaximum()) < 1.0e-3;
        const float from = bipolar ? (x0 + x1) * 0.5f : x0;
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.fillRoundedRectangle (juce::jmin (from, pos), cy - 2.0f, std::abs (pos - from), 4.0f, 2.0f);
        g.setColour (p.text);
        g.fillEllipse (pos - 6.0f, cy - 6.0f, 12.0f, 12.0f);
        g.setColour (p.bg);
        g.drawEllipse (pos - 6.0f, cy - 6.0f, 12.0f, 12.0f, 1.5f);
    }

    juce::Label* createSliderTextBox (juce::Slider& s) override
    {
        auto* l = LookAndFeel_V4::createSliderTextBox (s);
        l->setFont (font (11.5f));
        l->setJustificationType (juce::Justification::centred);
        return l;
    }

    void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                        int thumbStart, int thumbSize, bool over, bool) override
    {
        const auto thumb = vertical ? juce::Rectangle<int> (x + w / 2 - 3, thumbStart, 6, thumbSize)
                                    : juce::Rectangle<int> (thumbStart, y + h / 2 - 3, thumbSize, 6);
        g.setColour (over ? pal().textDim : pal().border);
        g.fillRoundedRectangle (thumb.toFloat(), 3.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const float track = juce::jmax (2.5f, radius * 0.14f);
        const float arcR = radius - track;
        const float angle = startAngle + pos * (endAngle - startAngle);
        juce::Path bgArc;
        bgArc.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (pal().border);
        g.strokePath (bgArc, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, angle, true);
        g.setColour (s.findColour (juce::Slider::rotarySliderFillColourId));
        g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
};
} // namespace la::ui
