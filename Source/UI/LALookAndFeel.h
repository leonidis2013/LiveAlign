#pragma once
#include "Theme.h"

namespace la::ui
{
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
        setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accent);
        setColour (juce::PopupMenu::highlightedTextColourId, p.onAccent);
        setColour (juce::TextButton::buttonColourId, p.panelRaised);
        setColour (juce::TextButton::buttonOnColourId, p.accent);
        setColour (juce::TextButton::textColourOffId, p.text);
        setColour (juce::TextButton::textColourOnId, p.onAccent);
        setColour (juce::ToggleButton::textColourId, p.text);
        setColour (juce::TooltipWindow::backgroundColourId, p.panelRaised);
        setColour (juce::TooltipWindow::textColourId, p.text);
        setColour (juce::TooltipWindow::outlineColourId, p.border);
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
        return font (juce::jmin (13.0f, (float) buttonHeight * 0.55f), true);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override { return font (13.0f); }
    juce::Font getPopupMenuFont() override { return font (13.0f); }
    juce::Font getLabelFont (juce::Label&) override { return font (12.0f); }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool hover, bool down) override
    {
        const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        const bool on = b.getToggleState();
        auto fill = on ? b.findColour (juce::TextButton::buttonOnColourId)
                     : (b.isColourSpecified (juce::TextButton::buttonColourId) ? b.findColour (juce::TextButton::buttonColourId) : pal().panelRaised);
        if (hover && ! on) fill = shade (fill, pal().dark ? 0.12f : -0.04f);
        if (down) fill = fill.darker (0.15f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 5.0f);
        g.setColour (on ? fill.brighter (0.25f) : pal().border);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
        g.setColour (pal().panelRaised);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (pal().border);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);

        juce::Path arrow;
        const float cx = (float) w - 14.0f, cy = (float) h * 0.5f;
        arrow.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.fillPath (arrow);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
        label.setFont (getComboBoxFont (box));
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
    {
        const auto r = b.getLocalBounds().toFloat();
        const float h = 16.0f, w = 30.0f;
        const auto track = juce::Rectangle<float> (r.getX() + 1.0f, r.getCentreY() - h * 0.5f, w, h);
        const bool on = b.getToggleState();
        g.setColour (on ? pal().accent : pal().border);
        g.fillRoundedRectangle (track, h * 0.5f);
        g.setColour (juce::Colours::white);
        g.fillEllipse (juce::Rectangle<float> (h - 4.0f, h - 4.0f).withCentre (
            { on ? track.getRight() - h * 0.5f : track.getX() + h * 0.5f, track.getCentreY() }));
        g.setColour (hover ? pal().text : pal().textDim);
        g.setFont (font (12.0f, true));
        g.drawText (b.getButtonText(), r.withTrimmedLeft (w + 8.0f), juce::Justification::centredLeft);
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
        const bool enabled = s.isEnabled();

        juce::Path bg;
        bg.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
        g.setColour (pal().border);
        g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // The value arc starts at the zero point of the range, so cut-only and boost/cut knobs both read clearly.
        const double lo = s.getMinimum(), hi = s.getMaximum();
        const float zeroPos = (lo < 0.0 && hi > 0.0) ? (float) ((0.0 - lo) / (hi - lo)) : 0.0f;
        const float zeroAngle = startAngle + zeroPos * (endAngle - startAngle);
        juce::Path value;
        value.addCentredArc (centre.x, centre.y, arcR, arcR, 0.0f, juce::jmin (zeroAngle, angle), juce::jmax (zeroAngle, angle), true);
        g.setColour (enabled ? s.findColour (juce::Slider::rotarySliderFillColourId) : pal().textDim.withAlpha (0.35f));
        g.strokePath (value, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float knobR = radius - track * 2.2f;
        g.setColour (pal().panelRaised);
        g.fillEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f);
        g.setColour (shade (pal().border, pal().dark ? 0.2f : -0.05f));
        g.drawEllipse (centre.x - knobR, centre.y - knobR, knobR * 2.0f, knobR * 2.0f, 1.0f);

        const auto tip = centre.getPointOnCircumference (knobR * 0.78f, angle);
        const auto base = centre.getPointOnCircumference (knobR * 0.25f, angle);
        g.setColour (enabled ? pal().text : pal().textDim.withAlpha (0.5f));
        g.drawLine ({ base, tip }, 2.0f);
    }
};
} // namespace la::ui
