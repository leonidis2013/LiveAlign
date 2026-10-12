#pragma once
// The three measurement views: transfer function (magnitude, phase, coherence), RTA, and impulse response.
#include <algorithm>
#include <cmath>
#include <optional>
#include "Graph.h"
#include "../Analyzer.h"
#include "../TraceStore.h"

namespace la::ui
{
inline size_t nearestByFreq (const std::vector<double>& f, double target)
{
    if (f.empty()) return 0;
    const auto it = std::lower_bound (f.begin(), f.end(), target);
    size_t i = (size_t) (it - f.begin());
    if (i >= f.size()) return f.size() - 1;
    if (i > 0 && std::abs (std::log (f[i - 1] / target)) < std::abs (std::log (f[i] / target))) --i;
    return i;
}

inline void drawWaiting (juce::Graphics& g, juce::Rectangle<float> area, const juce::String& title, const juce::String& hint)
{
    const auto& p = pal();
    g.setColour (p.text.withAlpha (0.85f));
    g.setFont (font (15.0f, true));
    g.drawText (title, area.withSizeKeepingCentre (area.getWidth(), 24.0f).translated (0.0f, -14.0f), juce::Justification::centred);
    g.setColour (p.textDim);
    g.setFont (font (12.5f));
    g.drawText (hint, area.withSizeKeepingCentre (area.getWidth(), 20.0f).translated (0.0f, 14.0f), juce::Justification::centred);
}

//==============================================================================
class TransferView : public juce::Component
{
public:
    TransferView (Settings& s, TraceStore& t) : settings (s), traces (t) {}

    void update (const Snapshot& snap) { data = &snap; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), {});
        layout();

        const float range = settings.magRange.load();
        const float top = centreDb + range, bottom = centreDb - range;
        auto yMag = [&] (float db) { return magArea.getY() + (top - db) / (top - bottom) * magArea.getHeight(); };

        // magnitude grid
        g.setFont (font (10.5f));
        const float step = range <= 12.0f ? 3.0f : (range <= 24.0f ? 6.0f : 10.0f);
        for (float db = std::ceil (bottom / step) * step; db <= top + 0.01f; db += step)
        {
            const float y = yMag (db);
            g.setColour (std::abs (db) < 0.01f ? shade (p.border, p.dark ? 0.3f : -0.1f) : p.grid);
            g.drawHorizontalLine ((int) y, magArea.getX(), magArea.getRight());
            g.setColour (p.textDim);
            g.drawText (signedText (db, 0), juce::Rectangle<float> (magArea.getX() - 42.0f, y - 7.0f, 36.0f, 14.0f),
                        juce::Justification::centredRight);
        }
        // phase grid
        for (int deg = -180; deg <= 180; deg += 90)
        {
            const float y = phaseArea.getY() + (180.0f - deg) / 360.0f * phaseArea.getHeight();
            g.setColour (deg == 0 ? shade (p.border, p.dark ? 0.3f : -0.1f) : p.grid);
            g.drawHorizontalLine ((int) y, phaseArea.getX(), phaseArea.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String (deg) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")),
                        juce::Rectangle<float> (phaseArea.getX() - 42.0f, y - 7.0f, 36.0f, 14.0f), juce::Justification::centredRight);
        }
        drawFreqGrid (g, axMag, magArea, juce::Rectangle<float> (magArea.getX(), magArea.getBottom() + 2.0f, magArea.getWidth(), 14.0f));
        drawFreqGrid (g, axPhase, phaseArea, juce::Rectangle<float> (phaseArea.getX(), phaseArea.getBottom() + 2.0f, phaseArea.getWidth(), 14.0f));

        // coherence scale on the right of the magnitude plot
        g.setColour (p.reduction.withAlpha (0.8f));
        g.drawText ("100%", juce::Rectangle<float> (magArea.getRight() + 4.0f, magArea.getY() - 6.0f, 36.0f, 12.0f), juce::Justification::centredLeft);
        g.drawText ("0", juce::Rectangle<float> (magArea.getRight() + 4.0f, cohBottom() - 6.0f, 36.0f, 12.0f), juce::Justification::centredLeft);

        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("MAGNITUDE", magArea.translated (8.0f, 4.0f).withHeight (14.0f), juce::Justification::centredLeft);
        g.drawText ("PHASE", phaseArea.translated (8.0f, 4.0f).withHeight (14.0f), juce::Justification::centredLeft);

        // saved traces
        for (const auto& t : traces.all())
        {
            if (! t.visible || t.kind != Trace::Transfer) continue;
            const auto& d = t.data;
            const auto col = traceColour (t.colour);
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (magArea.toNearestInt());
            strokeTrace (g, tracePath (d.freq.size(), [&] (size_t i) { return axMag.x (d.freq[i]); },
                                       [&] (size_t i) { return yMag ((float) d.db[i]); }, [] (size_t) { return true; }),
                         col.withAlpha (0.9f), 1.4f);
            if (d.hasPhase())
            {
                juce::Graphics::ScopedSaveState s2 (g);
                g.reduceClipRegion (phaseArea.toNearestInt());
                std::vector<float> ph (d.phase.begin(), d.phase.end());
                strokeTrace (g, phasePath (ph, [&] (size_t i) { return axPhase.x (d.freq[i]); }, [] (size_t) { return true; }, phaseArea),
                             col.withAlpha (0.8f), 1.2f);
            }
        }

        // target curve, lined up with the live trace's 500 Hz to 2 kHz average
        {
            float level = centreDb;
            if (data != nullptr && data->tfFrames > 0) bandAverage (data->tfFreq, data->tfMag, 500.0, 2000.0, level);
            drawTarget (g, settings.targetCurve.load(), axMag, magArea, yMag, level);
        }

        // live trace
        if (data == nullptr || data->tfFrames == 0 || data->tfFreq.empty())
        {
            drawWaiting (g, magArea, "Waiting for signal",
                         "Turn on the generator, or play program audio, with a reference connected (Settings).");
            return;
        }

        const auto& f = data->tfFreq;
        const float blank = settings.coherenceBlank.load();
        auto keep = [&] (size_t i) { return blank <= 0.0f || (std::isfinite (data->tfCoh[i]) && data->tfCoh[i] >= blank); };

        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (magArea.toNearestInt());
            // coherence across the top third
            strokeTrace (g, tracePath (f.size(), [&] (size_t i) { return axMag.x (f[i]); },
                                       [&] (size_t i) { return cohBottom() - data->tfCoh[i] * (cohBottom() - magArea.getY()); },
                                       [] (size_t) { return true; }),
                         p.reduction.withAlpha (0.75f), 1.2f);
            strokeTrace (g, tracePath (f.size(), [&] (size_t i) { return axMag.x (f[i]); },
                                       [&] (size_t i) { return yMag (data->tfMag[i]); }, keep),
                         p.accent, 2.0f);
        }
        {
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (phaseArea.toNearestInt());
            strokeTrace (g, phasePath (data->tfPhase, [&] (size_t i) { return axPhase.x (f[i]); }, keep, phaseArea), p.accent, 1.6f);
        }
        if (data->shownMic >= kAverageMic)
        {
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawText ("Phase is not averaged across mics. Pick one mic to see its phase.", phaseArea, juce::Justification::centred);
        }

        // hover readout
        if (hover.has_value())
        {
            const auto pt = *hover;
            const bool inMag = magArea.contains (pt), inPh = phaseArea.contains (pt);
            if (inMag || inPh)
            {
                const auto& ax = inMag ? axMag : axPhase;
                const size_t i = nearestByFreq (f, ax.freqAt (pt.x));
                const float x = ax.x (f[i]);
                g.setColour (p.textDim.withAlpha (0.5f));
                g.drawVerticalLine ((int) x, magArea.getY(), magArea.getBottom());
                g.drawVerticalLine ((int) x, phaseArea.getY(), phaseArea.getBottom());
                juce::String txt = freqLabel (f[i]) + " Hz   ";
                if (std::isfinite (data->tfMag[i]))
                    txt << signedText (data->tfMag[i], 1) << " dB   " << juce::String (juce::roundToInt (data->tfPhase[i]))
                        << juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) << "   coh " << juce::String (juce::roundToInt (data->tfCoh[i] * 100.0f)) << "%";
                else
                    txt << "no data";
                drawReadout (g, inMag ? magArea : phaseArea, pt, txt);
            }
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hover = e.position; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover.reset(); repaint(); }

    void mouseDown (const juce::MouseEvent& e) override { dragStartDb = centreDb; dragging = magArea.contains (e.position); }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        hover = e.position;
        if (! dragging) return;
        const float range = settings.magRange.load();
        centreDb = juce::jlimit (-60.0f, 60.0f, dragStartDb + (float) e.getDistanceFromDragStartY() / magArea.getHeight() * 2.0f * range);
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent&) override { centreOnLive(); }

    // Puts the live trace's average level (200 Hz to 5 kHz) on the 0 line.
    void centreOnLive()
    {
        if (data == nullptr) { centreDb = 0.0f; repaint(); return; }
        double sum = 0.0;
        int n = 0;
        for (size_t i = 0; i < data->tfFreq.size(); ++i)
            if (data->tfFreq[i] >= 200.0 && data->tfFreq[i] <= 5000.0 && std::isfinite (data->tfMag[i])) { sum += data->tfMag[i]; ++n; }
        centreDb = n > 0 ? (float) std::round (sum / n) : 0.0f;
        repaint();
    }

    void resized() override { layout(); }

private:
    void layout()
    {
        auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        r.removeFromLeft (40.0f);
        r.removeFromRight (34.0f);
        const float h = r.getHeight();
        magArea = r.removeFromTop (h * 0.62f - 16.0f);
        r.removeFromTop (22.0f);
        phaseArea = r.withTrimmedBottom (16.0f);
        const double fMax = data != nullptr ? juce::jmin (20000.0, data->sampleRate * 0.48) : 20000.0;
        axMag = { 20.0, fMax, magArea };
        axPhase = { 20.0, fMax, phaseArea };
    }

    float cohBottom() const { return magArea.getY() + magArea.getHeight() * 0.33f; }

    Settings& settings;
    TraceStore& traces;
    const Snapshot* data = nullptr;
    juce::Rectangle<float> magArea, phaseArea;
    FreqAxis axMag, axPhase;
    float centreDb = 0.0f, dragStartDb = 0.0f;
    bool dragging = false;
    std::optional<juce::Point<float>> hover;
};

//==============================================================================
class RtaView : public juce::Component
{
public:
    RtaView (Settings& s, TraceStore& t) : settings (s), traces (t) {}

    // Draw every mic as its own coloured line (Mic setup page) instead of the selected mic's bars.
    void setShowAllMics (bool b) { showAll = b; repaint(); }

    void update (const Snapshot& snap) { data = &snap; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), {});
        auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        r.removeFromLeft (44.0f);
        r.removeFromRight (16.0f);
        plot = r.withTrimmedBottom (16.0f);
        const double fMax = data != nullptr ? juce::jmin (20000.0, data->sampleRate * 0.45) : 20000.0;
        ax = { 16.0, fMax * 1.12, plot };

        const bool spl = data != nullptr && data->rtaIsSpl;
        const float top = spl ? 120.0f : 0.0f, bottom = spl ? 20.0f : -100.0f;
        auto y = [&] (float db) { return plot.getY() + (top - juce::jlimit (bottom - 5.0f, top + 5.0f, db)) / (top - bottom) * plot.getHeight(); };

        g.setFont (font (10.5f));
        for (float db = bottom; db <= top + 0.1f; db += 10.0f)
        {
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) y (db), plot.getX(), plot.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 44.0f, y (db) - 7.0f, 38.0f, 14.0f),
                        juce::Justification::centredRight);
        }
        drawFreqGrid (g, ax, plot, juce::Rectangle<float> (plot.getX(), plot.getBottom() + 2.0f, plot.getWidth(), 14.0f));
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        const int bands = juce::jmax (1, settings.rtaBands.load());
        g.drawText ("RTA  1/" + juce::String (bands) + " OCTAVE   " + (spl ? "dB SPL" : "dBFS (not calibrated)"),
                    plot.translated (8.0f, 4.0f).withHeight (14.0f), juce::Justification::centredLeft);

        for (const auto& t : traces.all())
        {
            if (! t.visible || t.kind != Trace::Rta) continue;
            const auto& d = t.data;
            strokeTrace (g, tracePath (d.freq.size(), [&] (size_t i) { return ax.x (d.freq[i]); },
                                       [&] (size_t i) { return y ((float) d.db[i]); }, [] (size_t) { return true; }),
                         traceColour (t.colour).withAlpha (0.9f), 1.5f);
        }

        if (data == nullptr || data->rtaFreq.empty()) return;
        const auto& f = data->rtaFreq;
        {
            float level = 0.0f;
            if (bandAverage (f, data->rtaMic, 500.0, 2000.0, level))
                drawTarget (g, settings.targetCurve.load(), ax, plot, y, level);
        }
        const double half = std::pow (2.0, 0.5 / bands);

        const bool multi = showAll && data->mics.size() > 1;
        if (multi)
        {
            for (size_t m = 0; m < data->mics.size(); ++m)
            {
                const auto& v = data->mics[m].rta;
                if (v.size() != f.size()) continue;
                strokeTrace (g, tracePath (f.size(), [&] (size_t i) { return ax.x (f[i]); }, [&] (size_t i) { return y (v[i]); },
                                           [] (size_t) { return true; }), micColour ((int) m), 1.6f);
            }
        }
        else if (bands <= 6)
        {
            for (size_t i = 0; i < f.size(); ++i)
            {
                const float x0 = ax.x (f[i] / half) + 1.0f, x1 = ax.x (f[i] * half) - 1.0f;
                const float yy = y (data->rtaMic[i]);
                juce::ColourGradient grad (p.accent.withAlpha (0.9f), 0.0f, plot.getY(), p.accent.withAlpha (0.35f), 0.0f, plot.getBottom(), false);
                g.setGradientFill (grad);
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x0, yy, juce::jmax (x0 + 1.0f, x1), plot.getBottom()));
            }
        }
        else
        {
            strokeTrace (g, tracePath (f.size(), [&] (size_t i) { return ax.x (f[i]); }, [&] (size_t i) { return y (data->rtaMic[i]); },
                                       [] (size_t) { return true; }), p.accent, 1.8f);
        }

        if (settings.rtaShowRef.load())
            strokeTrace (g, tracePath (f.size(), [&] (size_t i) { return ax.x (f[i]); }, [&] (size_t i) { return y (data->rtaRef[i]); },
                                       [] (size_t) { return true; }), p.inputCol.withAlpha (0.85f), 1.4f);

        if (settings.rtaPeakHold.load())
            strokeTrace (g, tracePath (f.size(), [&] (size_t i) { return ax.x (f[i]); }, [&] (size_t i) { return y (data->rtaPeak[i]); },
                                       [] (size_t) { return true; }), p.warn.withAlpha (0.85f), 1.2f);

        if (hover.has_value() && plot.contains (*hover))
        {
            const size_t i = nearestByFreq (f, ax.freqAt (hover->x));
            const juce::String unit = spl ? " dB SPL" : " dBFS";
            drawReadout (g, plot, *hover, freqLabel (nominalFrequency (f[i])) + " Hz   " + juce::String (data->rtaMic[i], 1) + unit);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override { hover = e.position; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover.reset(); repaint(); }

private:
    Settings& settings;
    TraceStore& traces;
    const Snapshot* data = nullptr;
    juce::Rectangle<float> plot;
    FreqAxis ax;
    bool showAll = false;
    std::optional<juce::Point<float>> hover;
};

//==============================================================================
class ImpulseView : public juce::Component
{
public:
    explicit ImpulseView (Settings& s) : settings (s) {}

    void update (const Snapshot& snap) { data = &snap; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), {});

        auto r = getLocalBounds().toFloat().reduced (10.0f, 8.0f);
        r.removeFromTop (22.0f);
        r.removeFromLeft (44.0f);
        r.removeFromRight (16.0f);
        const auto plot = r.withTrimmedBottom (16.0f);

        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        static const char* rangeNames[] = { "20 ms", "100 ms", "500 ms" };
        g.drawText (juce::String ("IMPULSE RESPONSE   ") + (settings.irMode.load() == 1 ? "LOG (ETC)" : "LINEAR") + "   "
                        + rangeNames[juce::jlimit (0, 2, settings.irRange.load())],
                    getLocalBounds().toFloat().reduced (14.0f, 8.0f).withHeight (16.0f), juce::Justification::centredLeft);

        static const double rangesMs[] = { 20.0, 100.0, 500.0 };
        const double sr = data != nullptr ? data->sampleRate : 48000.0;
        const double tMin = -5.0, tMax = rangesMs[juce::jlimit (0, 2, settings.irRange.load())];
        const bool log = settings.irMode.load() == 1;
        auto x = [&] (double ms) { return plot.getX() + (float) ((ms - tMin) / (tMax - tMin)) * plot.getWidth(); };

        // time grid
        const double step = tMax <= 20.0 ? 2.0 : (tMax <= 100.0 ? 10.0 : 50.0);
        g.setFont (font (10.5f));
        for (double ms = 0.0; ms <= tMax + 1e-9; ms += step)
        {
            g.setColour (std::abs (ms) < 1e-9 ? shade (p.border, p.dark ? 0.3f : -0.1f) : p.grid);
            g.drawVerticalLine ((int) x (ms), plot.getY(), plot.getBottom());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) ms) + " ms", juce::Rectangle<float> (x (ms) - 30.0f, plot.getBottom() + 2.0f, 60.0f, 14.0f),
                        juce::Justification::centred);
        }
        // level grid
        if (log)
        {
            for (int db = 0; db >= -60; db -= 10)
            {
                const float yy = plot.getY() + (float) -db / 60.0f * plot.getHeight();
                g.setColour (p.grid);
                g.drawHorizontalLine ((int) yy, plot.getX(), plot.getRight());
                g.setColour (p.textDim);
                g.drawText (juce::String (db), juce::Rectangle<float> (plot.getX() - 44.0f, yy - 7.0f, 38.0f, 14.0f), juce::Justification::centredRight);
            }
        }
        else
        {
            for (float v : { 1.0f, 0.5f, 0.0f, -0.5f, -1.0f })
            {
                const float yy = plot.getCentreY() - v * plot.getHeight() * 0.5f;
                g.setColour (std::abs (v) < 1e-6f ? p.border : p.grid);
                g.drawHorizontalLine ((int) yy, plot.getX(), plot.getRight());
            }
        }

        if (data == nullptr || data->ir.empty() || data->tfFrames == 0)
        {
            drawWaiting (g, plot, "Waiting for signal", "The impulse response needs the generator or program audio and a reference.");
            return;
        }

        const auto& ir = data->ir;
        const int i0 = juce::jmax (0, data->irZero + (int) std::floor (tMin * 0.001 * sr));
        const int i1 = juce::jmin ((int) ir.size() - 1, data->irZero + (int) std::ceil (tMax * 0.001 * sr));
        float peak = 1e-12f;
        int peakIdx = data->irZero;
        for (int i = 0; i < (int) ir.size(); ++i)
            if (std::abs (ir[(size_t) i]) > peak) { peak = std::abs (ir[(size_t) i]); peakIdx = i; }

        // Draw min/max per pixel column so long ranges stay accurate.
        juce::Path path;
        const int cols = juce::jmax (1, (int) plot.getWidth());
        const double perCol = (double) (i1 - i0) / cols;
        for (int c = 0; c < cols; ++c)
        {
            const int a = i0 + (int) (c * perCol), b = juce::jmax (a + 1, i0 + (int) ((c + 1) * perCol));
            float lo = 1e9f, hi = -1e9f;
            for (int i = a; i < b && i <= i1; ++i)
            {
                float v = ir[(size_t) i] / peak;
                if (log) v = 20.0f * std::log10 (juce::jmax (1e-6f, std::abs (v)));
                lo = juce::jmin (lo, v);
                hi = juce::jmax (hi, v);
            }
            if (lo > hi) continue;
            auto yv = [&] (float v) { return log ? plot.getY() + juce::jlimit (0.0f, 1.0f, -v / 60.0f) * plot.getHeight()
                                                 : plot.getCentreY() - juce::jlimit (-1.0f, 1.0f, v) * plot.getHeight() * 0.5f; };
            const float px = plot.getX() + (float) c;
            if (c == 0) path.startNewSubPath (px, yv (hi));
            path.lineTo (px, yv (hi));
            path.lineTo (px, yv (lo));
        }
        strokeTrace (g, path, p.accent, 1.2f);

        const double peakMs = (peakIdx - data->irZero) * 1000.0 / sr;
        if (peakMs >= tMin && peakMs <= tMax)
        {
            g.setColour (p.warn);
            g.drawVerticalLine ((int) x (peakMs), plot.getY(), plot.getBottom());
        }
        const double total = settings.delayMsValue() + peakMs;
        g.setColour (p.text);
        g.setFont (monoFont (12.0f));
        g.drawText ("Peak " + signedText ((float) peakMs, 2) + " ms after compensation   (arrival " + juce::String (total, 2) + " ms, "
                        + settings.distanceText (total) + ")" + (ir[(size_t) peakIdx] < 0.0f ? "   POLARITY INVERTED" : ""),
                    plot.reduced (10.0f, 6.0f).removeFromBottom (18.0f), juce::Justification::centredRight);
    }

private:
    Settings& settings;
    const Snapshot* data = nullptr;
};
} // namespace la::ui
