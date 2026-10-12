// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// The Live series colour system (LiveVoice, LiveMeter, BroadcastMaster): same surfaces, accents, light/dark pairing,
// rounded cards with an accent bar beside the title, pill switches and slim sliders.
namespace la::ui
{
struct Palette
{
    bool dark;
    juce::Colour bg, panel, panelRaised, border, grid, text, textDim, accent, good, warn, bad, inputCol, outputCol, onAccent;
    juce::Colour bandCol[4];   // extra accents (violet, gold ...)
    juce::Colour reduction;    // coherence
    juce::Colour textFaint;
};

inline const Palette& darkPalette()
{
    static const Palette p { true,
        juce::Colour (0xff0d1015), juce::Colour (0xff151a21), juce::Colour (0xff1b2129), juce::Colour (0xff252c36),
        juce::Colour (0xff1e252e), juce::Colour (0xffd8dee6), juce::Colour (0xff7c8794), juce::Colour (0xff2ec4b6),
        juce::Colour (0xff3ecf6e), juce::Colour (0xfff2c94c), juce::Colour (0xffef4c4c),
        juce::Colour (0xff4c8dff), juce::Colour (0xffff8a3d), juce::Colour (0xff0d1015),
        { juce::Colour (0xff2ec4b6), juce::Colour (0xff4c8dff), juce::Colour (0xffa78bfa), juce::Colour (0xfff6c453) },
        juce::Colour (0xfff472b6), juce::Colour (0xff4c5561) };
    return p;
}

// Light: soft grey page, white cards, accents darkened for contrast on white.
inline const Palette& lightPalette()
{
    static const Palette p { false,
        juce::Colour (0xffe9edf2), juce::Colour (0xfffbfcfd), juce::Colour (0xfff0f3f6), juce::Colour (0xffd3dae3),
        juce::Colour (0xffe3e8ee), juce::Colour (0xff18202b), juce::Colour (0xff586474), juce::Colour (0xff0f9488),
        juce::Colour (0xff23a056), juce::Colour (0xffcf9a10), juce::Colour (0xffd63232),
        juce::Colour (0xff2563d6), juce::Colour (0xffe0661a), juce::Colour (0xffffffff),
        { juce::Colour (0xff0f9488), juce::Colour (0xff2563d6), juce::Colour (0xff7553e0), juce::Colour (0xffc98a0a) },
        juce::Colour (0xffd2478f), juce::Colour (0xff97a1ae) };
    return p;
}

// The editor sets this before it creates or repaints anything.
inline const Palette*& currentPalette() { static const Palette* p = &darkPalette(); return p; }
inline const Palette& pal() { return *currentPalette(); }

inline juce::Font font (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (height, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font monoFont (float height, bool bold = false)
{
    return juce::Font (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), height,
                                          bold ? juce::Font::bold : juce::Font::plain));
}

// Lighter for a positive amount, darker for a negative one.
inline juce::Colour shade (juce::Colour c, float amount) { return amount >= 0.0f ? c.brighter (amount) : c.darker (-amount); }

// Colours for saved traces, cycled in order. Chosen to stay apart from the live trace (accent) and coherence.
inline juce::Colour traceColour (int i)
{
    static const juce::uint32 darkCols[]  = { 0xfff6c453, 0xff4c8dff, 0xffa78bfa, 0xffff8a3d, 0xff3ecf6e, 0xfff472b6, 0xff9ad0ec, 0xffd8dee6 };
    static const juce::uint32 lightCols[] = { 0xffc98a0a, 0xff2563d6, 0xff7553e0, 0xffe0661a, 0xff23a056, 0xffd2478f, 0xff3b8fb8, 0xff434d5a };
    const int k = ((i % 8) + 8) % 8;
    return juce::Colour (pal().dark ? darkCols[k] : lightCols[k]);
}

// One colour per measurement mic (mic 1 is the accent).
inline juce::Colour micColour (int m)
{
    static const juce::uint32 darkCols[]  = { 0xff2ec4b6, 0xff4c8dff, 0xffa78bfa, 0xfff6c453, 0xffff8a3d, 0xfff472b6, 0xff8bd450, 0xff9ad0ec };
    static const juce::uint32 lightCols[] = { 0xff0f9488, 0xff2563d6, 0xff7553e0, 0xffc98a0a, 0xffe0661a, 0xffd2478f, 0xff4b9b25, 0xff3b8fb8 };
    const int k = ((m % 8) + 8) % 8;
    return juce::Colour (pal().dark ? darkCols[k] : lightCols[k]);
}

// Live series card: rounded panel, border, a short accent bar and the title beside it.
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title, juce::Colour accent = juce::Colour())
{
    g.setColour (pal().panel);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (pal().border);
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (accent.isTransparent() ? pal().accent : accent);
        g.fillRoundedRectangle (r.getX() + 12.0f, r.getY() + 11.0f, 3.0f, 12.0f, 1.5f);
        g.setColour (pal().text);
        g.setFont (font (13.0f, true));
        g.drawText (title, juce::Rectangle<float> (r.getX() + 22.0f, r.getY() + 5.0f, r.getWidth() - 30.0f, 24.0f),
                    juce::Justification::centredLeft, true);
    }
}

// Rounded readout tile, as in LiveMeter.
inline void drawTile (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, const juce::String& value,
                      const juce::String& unit, juce::Colour valueColour, float valueSize)
{
    const auto& p = pal();
    g.setColour (p.panelRaised);
    g.fillRoundedRectangle (r, 7.0f);
    g.setColour (p.border);
    g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    g.setColour (p.textDim);
    g.setFont (font (10.5f, true));
    g.drawText (label, r.reduced (10.0f, 6.0f).withHeight (14.0f), juce::Justification::centredLeft);
    g.setFont (font (10.5f));
    g.drawText (unit, r.reduced (10.0f, 6.0f).withHeight (14.0f), juce::Justification::centredRight);
    g.setColour (valueColour);
    g.setFont (monoFont (valueSize, true));
    g.drawText (value, r.reduced (10.0f, 0.0f).withTrimmedTop (18.0f).withTrimmedBottom (3.0f), juce::Justification::centredLeft);
}

inline juce::String dbText (float v, int decimals = 1, float floorDb = -119.0f)
{
    if (v <= floorDb) return juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e"));
    return decimals <= 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, decimals);
}

inline juce::String signedText (float v, int decimals = 1)
{
    return (v >= 0.0f ? "+" : "") + juce::String (v, decimals);
}
} // namespace la::ui
