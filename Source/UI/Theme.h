#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace la::ui
{
struct Palette
{
    bool dark;
    juce::Colour bg, panel, panelRaised, border, grid, text, textDim, accent, good, warn, bad, inputCol, outputCol, onAccent;
    juce::Colour bandCol[4];   // multiband compressor bands
    juce::Colour reduction;    // gain reduction
};

// Same surfaces, accents and light/dark pairing as the LiveVoice dynamic EQ: a softer, lighter look.
inline const Palette& darkPalette()
{
    static const Palette p { true,
        juce::Colour (0xff0d1015), juce::Colour (0xff151a21), juce::Colour (0xff1b2129), juce::Colour (0xff252c36),
        juce::Colour (0xff1e252e), juce::Colour (0xffd8dee6), juce::Colour (0xff7c8794), juce::Colour (0xff2ec4b6),
        juce::Colour (0xff3ecf6e), juce::Colour (0xfff2c94c), juce::Colour (0xffef4c4c),
        juce::Colour (0xff4c8dff), juce::Colour (0xffff8a3d), juce::Colour (0xff0d1015),
        { juce::Colour (0xff2ec4b6), juce::Colour (0xff4c8dff), juce::Colour (0xffa78bfa), juce::Colour (0xfff6c453) },
        juce::Colour (0xfff472b6) };
    return p;
}

inline const Palette& lightPalette()
{
    static const Palette p { false,
        juce::Colour (0xffe9edf2), juce::Colour (0xfffbfcfd), juce::Colour (0xfff0f3f6), juce::Colour (0xffd3dae3),
        juce::Colour (0xffe3e8ee), juce::Colour (0xff18202b), juce::Colour (0xff586474), juce::Colour (0xff0f9488),
        juce::Colour (0xff23a056), juce::Colour (0xffcf9a10), juce::Colour (0xffd63232),
        juce::Colour (0xff2563d6), juce::Colour (0xffe0661a), juce::Colour (0xffffffff),
        { juce::Colour (0xff0f9488), juce::Colour (0xff2563d6), juce::Colour (0xff7553e0), juce::Colour (0xffc98a0a) },
        juce::Colour (0xffd2478f) };
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

// Shared drawing helpers
inline void drawPanel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& title)
{
    g.setColour (pal().panel);
    g.fillRoundedRectangle (r, 10.0f);
    g.setColour (pal().border);
    g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);
    if (title.isNotEmpty())
    {
        g.setColour (pal().textDim);
        g.setFont (font (11.0f, true));
        g.drawText (title.toUpperCase(), r.withHeight (22.0f).withTrimmedLeft (12.0f), juce::Justification::centredLeft);
    }
}

inline juce::String dbText (float v, int decimals = 1, float floorDb = -119.0f)
{
    return v <= floorDb ? juce::String (juce::CharPointer_UTF8 ("-\xe2\x88\x9e")) : juce::String (v, decimals);
}

inline juce::String signedText (float v, int decimals = 1)
{
    return (v >= 0.0f ? "+" : "") + juce::String (v, decimals);
}
} // namespace la::ui
