#pragma once
// Shared plotting helpers: log frequency axis, grids, and NaN-aware trace paths.
#include <cmath>
#include <vector>
#include "Theme.h"
#include "../Analysis/TargetCurves.h"

namespace la::ui
{
struct FreqAxis
{
    double fMin = 20.0, fMax = 20000.0;
    juce::Rectangle<float> area;

    float x (double f) const
    {
        const double t = std::log (f / fMin) / std::log (fMax / fMin);
        return area.getX() + (float) t * area.getWidth();
    }

    double freqAt (float px) const
    {
        const double t = (px - area.getX()) / juce::jmax (1.0f, area.getWidth());
        return fMin * std::pow (fMax / fMin, t);
    }
};

inline juce::String freqLabel (double f)
{
    if (f >= 1000.0)
    {
        // the nominal third-octave names (1.25k, 3.15k, 6.3k), not a rounding of the exact centre
        struct Nominal { double v; const char* name; };
        static const Nominal nominal[] = { { 1.25, "1.25k" }, { 1.6, "1.6k" }, { 2.5, "2.5k" }, { 3.15, "3.15k" }, { 6.3, "6.3k" }, { 12.5, "12.5k" } };
        const double k = f / 1000.0;
        for (const auto& n : nominal) if (std::abs (k / n.v - 1.0) < 0.02) return n.name;
        return std::abs (k - std::round (k)) < 0.05 ? juce::String ((int) std::round (k)) + "k" : juce::String (k, 1) + "k";
    }
    return f < 100.0 && std::abs (f - std::round (f)) > 0.05 ? juce::String (f, 1) : juce::String ((int) std::round (f));
}

// Vertical grid lines with labels along the bottom edge of `labelArea`.
inline void drawFreqGrid (juce::Graphics& g, const FreqAxis& ax, juce::Rectangle<float> plot, juce::Rectangle<float> labelArea)
{
    const auto& p = pal();
    static const double major[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (int decade = 1; decade <= 10000; decade *= 10)
    {
        for (int m = 1; m <= 9; ++m)
        {
            const double f = (double) decade * m;
            if (f < ax.fMin || f > ax.fMax) continue;
            g.setColour (p.grid.withAlpha (0.6f));
            g.drawVerticalLine ((int) ax.x (f), plot.getY(), plot.getBottom());
        }
    }
    g.setFont (font (10.5f));
    for (double f : major)
    {
        if (f < ax.fMin || f > ax.fMax) continue;
        const float px = ax.x (f);
        g.setColour (shade (p.border, p.dark ? 0.15f : -0.05f));
        g.drawVerticalLine ((int) px, plot.getY(), plot.getBottom());
        g.setColour (p.textDim);
        g.drawText (freqLabel (f), juce::Rectangle<float> (px - 20.0f, labelArea.getY(), 40.0f, labelArea.getHeight()),
                    juce::Justification::centred);
    }
}

// A path through (x, y) points, broken wherever a value is NaN or `keep` says no.
template <typename XFn, typename YFn, typename KeepFn>
juce::Path tracePath (size_t count, XFn xAt, YFn yAt, KeepFn keep)
{
    juce::Path path;
    bool pen = false;
    for (size_t i = 0; i < count; ++i)
    {
        const float y = yAt (i);
        if (! std::isfinite (y) || ! keep (i)) { pen = false; continue; }
        const float x = xAt (i);
        if (! pen) { path.startNewSubPath (x, y); pen = true; }
        else path.lineTo (x, y);
    }
    return path;
}

// Wrapped phase: lines are broken where the trace jumps across +/-180 so it never draws a vertical streak.
template <typename XFn, typename KeepFn>
juce::Path phasePath (const std::vector<float>& phase, XFn xAt, KeepFn keep, juce::Rectangle<float> area)
{
    juce::Path path;
    bool pen = false;
    float prev = 0.0f;
    for (size_t i = 0; i < phase.size(); ++i)
    {
        const float ph = phase[i];
        if (! std::isfinite (ph) || ! keep (i)) { pen = false; continue; }
        const float y = area.getY() + (180.0f - ph) / 360.0f * area.getHeight();
        const float x = xAt (i);
        if (pen && std::abs (ph - prev) > 180.0f) pen = false;
        if (! pen) { path.startNewSubPath (x, y); pen = true; }
        else path.lineTo (x, y);
        prev = ph;
    }
    return path;
}

inline void strokeTrace (juce::Graphics& g, const juce::Path& path, juce::Colour c, float width)
{
    g.setColour (c);
    g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// Target curve: dashed line with a +/-3 dB tolerance band. `level` is where the curve's 1 kHz point sits.
template <typename YFn>
void drawTarget (juce::Graphics& g, int id, const FreqAxis& ax, juce::Rectangle<float> area, YFn yAt, float level)
{
    if (id <= 0 || id >= targets::count()) return;
    const auto& p = pal();
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (area.toNearestInt());

    juce::Path line, band;
    std::vector<juce::Point<float>> lower;
    const int n = 160;
    for (int i = 0; i <= n; ++i)
    {
        const double f = ax.fMin * std::pow (ax.fMax / ax.fMin, (double) i / n);
        const float x = ax.x (f);
        const float db = level + (float) targets::db (id, f);
        if (i == 0) { line.startNewSubPath (x, yAt (db)); band.startNewSubPath (x, yAt (db + 3.0f)); }
        else { line.lineTo (x, yAt (db)); band.lineTo (x, yAt (db + 3.0f)); }
        lower.push_back ({ x, yAt (db - 3.0f) });
    }
    for (auto it = lower.rbegin(); it != lower.rend(); ++it) band.lineTo (*it);
    band.closeSubPath();
    g.setColour (p.good.withAlpha (p.dark ? 0.08f : 0.10f));
    g.fillPath (band);

    juce::Path dashed;
    const float dashes[] = { 7.0f, 5.0f };
    juce::PathStrokeType (1.6f).createDashedStroke (dashed, line, dashes, 2);
    g.setColour (p.good.withAlpha (0.9f));
    g.fillPath (dashed);

    g.setFont (font (10.5f, true));
    g.drawText (juce::String ("TARGET: ") + targets::list()[(size_t) id].name,
                area.reduced (8.0f, 4.0f).removeFromBottom (14.0f), juce::Justification::centredRight);
}

// Average of finite values between fLo and fHi (used to line a target up with the measurement).
inline bool bandAverage (const std::vector<double>& f, const std::vector<float>& v, double fLo, double fHi, float& out)
{
    double sum = 0.0;
    int n = 0;
    for (size_t i = 0; i < f.size() && i < v.size(); ++i)
        if (f[i] >= fLo && f[i] <= fHi && std::isfinite (v[i])) { sum += v[i]; ++n; }
    if (n == 0) return false;
    out = (float) (sum / n);
    return true;
}

inline void drawReadout (juce::Graphics& g, juce::Rectangle<float> plot, juce::Point<float> at, const juce::String& text)
{
    const auto& p = pal();
    g.setFont (monoFont (11.0f));
    const float w = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 16.0f;
    auto box = juce::Rectangle<float> (w, 22.0f).withPosition (at.x + 12.0f, at.y - 30.0f);
    if (box.getRight() > plot.getRight()) box.setX (at.x - 12.0f - w);
    if (box.getY() < plot.getY()) box.setY (at.y + 10.0f);
    g.setColour (p.panelRaised.withAlpha (0.94f));
    g.fillRoundedRectangle (box, 5.0f);
    g.setColour (p.border);
    g.drawRoundedRectangle (box, 5.0f, 1.0f);
    g.setColour (p.text);
    g.drawText (text, box, juce::Justification::centred);
}
} // namespace la::ui
