// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The LiveAlign mark: sound arriving from two sides and meeting, in time, on one line. Drawn as vectors so it is
// sharp at every size: the navigation pane, the report cover and the app icon.
#include <juce_gui_basics/juce_gui_basics.h>

namespace la::ui
{
struct LogoColours
{
    juce::Colour left { 0xff2dd4bf }, right { 0xff60a5fa }, line { 0xffffffff };
    juce::Colour bgTop { 0xff16263a }, bgBottom { 0xff0b1220 };
};

// The mark in `r` (square works best). `tile` draws the rounded background used for the app icon.
inline void drawLogo (juce::Graphics& g, juce::Rectangle<float> r, bool tile, LogoColours c = {})
{
    r = r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight()));
    const float s = r.getWidth();
    if (tile)
    {
        juce::ColourGradient bg (c.bgTop, r.getCentreX(), r.getY(), c.bgBottom, r.getCentreX(), r.getBottom(), false);
        g.setGradientFill (bg);
        g.fillRoundedRectangle (r, s * 0.225f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawRoundedRectangle (r.reduced (s * 0.01f), s * 0.22f, s * 0.012f);
        r = r.reduced (s * 0.16f);
    }
    const float w = r.getWidth();
    const float cx = r.getCentreX(), cy = r.getCentreY();
    const float stroke = w * (tile ? 0.085f : 0.1f);

    // arcs: the wavefronts from each side, the nearest one strongest
    auto arcs = [&] (bool fromLeft, juce::Colour col)
    {
        for (int k = 0; k < 3; ++k)
        {
            const float radius = w * (0.26f + 0.08f * (float) k);
            const float apex = w * (0.1f + 0.135f * (float) k);   // distance of the arc's middle from the line
            const float ox = fromLeft ? cx - apex - radius : cx + apex + radius;   // arcs bulge toward the centre
            juce::Path p;
            const float span = 0.62f - 0.1f * (float) k;
            const float a0 = fromLeft ? juce::MathConstants<float>::halfPi - span : -juce::MathConstants<float>::halfPi - span;
            p.addCentredArc (ox, cy, radius, radius, 0.0f, a0, a0 + 2.0f * span, true);
            g.setColour (col.withAlpha (1.0f - 0.28f * (float) k));
            g.strokePath (p, juce::PathStrokeType (stroke * (1.0f - 0.12f * (float) k), juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    };
    arcs (true, c.left);
    arcs (false, c.right);

    // the alignment line
    const float lh = w * 0.84f;
    g.setColour (c.line);
    g.fillRoundedRectangle (juce::Rectangle<float> (stroke * 0.8f, lh).withCentre ({ cx, cy }), stroke * 0.4f);
}

inline juce::Image logoImage (int size)
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    drawLogo (g, juce::Rectangle<float> ((float) size, (float) size).reduced ((float) size * 0.04f), true);
    return img;
}
} // namespace la::ui
