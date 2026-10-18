#pragma once
#include "../Clock.h"
// Right-hand column: sound level, input meters, delay finder and saved traces.
#include <functional>
#include "Graph.h"
#include "../Analyzer.h"
#include "../TraceStore.h"
#include "../Analysis/TargetCurves.h"
#include "../Monitoring.h"
#include "../Analysis/AirAbsorption.h"

namespace la::ui
{
inline juce::String refSourceName (int refSource)
{
    if (refSource == 0) return "Off";
    if (refSource == 1) return "Generator";
    return "In " + juce::String (refSource - 1);
}

//==============================================================================
class SplPanel : public juce::Component
{
public:
    explicit SplPanel (Settings& s) : settings (s)
    {
        resetButton.setTooltip ("Restart the Leq and maximum readings");
        resetButton.onClick = [this] { settings.requestResetLeq = true; };
        addAndMakeVisible (resetButton);
        calButton.setTooltip ("Calibrate the sound level with an acoustic calibrator, and load the mic's calibration file");
        calButton.onClick = [this] { if (onCalibrate) onCalibrate(); };
        addAndMakeVisible (calButton);
    }

    void update (const Snapshot& snap) { data = &snap; repaint(); }
    void setMicName (const juce::String& n) { if (n != micName) { micName = n; repaint(); } }

    void resized() override
    {
        resetButton.setBounds (getWidth() - 84, 6, 74, 24);
        calButton.setBounds (getWidth() - 84 - 86, 6, 82, 24);
    }

    std::function<void()> onCalibrate;
    juce::Component& calibrateButton() { return calButton; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Sound level");
        // which mic the level comes from
        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawText (micName, juce::Rectangle<int> (12, 26, getWidth() - 24, 14), juce::Justification::centredLeft, true);
        if (data == nullptr) return;
        const bool spl = data->splIsSpl;
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromTop (42);

        auto big = r.removeFromTop (44);
        g.setColour (p.text);
        g.setFont (monoFont (34.0f, true));
        g.drawText (dbText (data->splAS, 1, spl ? -50.0f : -150.0f), big.removeFromLeft (140), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (13.0f, true));
        g.drawText (spl ? "dBA  slow" : "dBFS A  slow", big, juce::Justification::centredLeft);

        struct Item { const char* name; float v; };
        const Item items[] = { { "A fast", data->splAF }, { "C slow", data->splCS }, { "Z slow", data->splZS },
                               { "LAeq", data->leqA }, { "LCeq", data->leqC }, { "LAF max", data->maxAF } };
        const int colW = r.getWidth() / 3;
        for (int i = 0; i < 6; ++i)
        {
            auto cell = juce::Rectangle<int> (r.getX() + (i % 3) * colW, r.getY() + (i / 3) * 36, colW, 34);
            g.setColour (p.textDim);
            g.setFont (font (10.5f, true));
            g.drawText (items[i].name, cell.removeFromTop (14), juce::Justification::centredLeft);
            g.setColour (p.text);
            g.setFont (monoFont (14.0f));
            g.drawText (dbText (items[i].v, 1, spl ? -50.0f : -150.0f), cell, juce::Justification::centredLeft);
        }

        g.setColour (spl ? p.textDim : p.warn);
        g.setFont (font (11.0f));
        const int mins = (int) (data->leqSeconds / 60.0);
        g.drawText (spl ? "Leq over " + juce::String (mins) + " min " + juce::String ((int) data->leqSeconds % 60) + " s"
                        : "Not calibrated: levels are dBFS. Use Calibrate above, or Sound level on Mic setup.",
                    getLocalBounds().reduced (12, 6).removeFromBottom (16), juce::Justification::centredLeft);
    }

private:
    juce::String micName;
    Settings& settings;
    const Snapshot* data = nullptr;
    juce::TextButton resetButton { "Reset Leq" }, calButton { "Calibrate" };
};

//==============================================================================
class InputPanel : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit InputPanel (Settings& s) : settings (s) {}

    void setMicLabel (const juce::String& l) { if (l != micLabel) { micLabel = l; repaint(); } }

    // The audio's health, on the last line (level: 0 fine, 1 a warning, 2 a fault); the detail is the tooltip.
    void setHealth (const juce::String& text, int level, const juce::String& detail)
    {
        if (text == healthText && level == healthLevel) return;
        healthText = text;
        healthLevel = level;
        setTooltip (detail);
        repaint();
    }

    void setLevels (float micPeak, float micRmsDb, float refPeak, float refRmsDb)
    {
        const auto now = la::clock::ms32();
        mic.update (micPeak, micRmsDb, now);
        ref.update (refPeak, refRmsDb, now);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        drawPanel (g, getLocalBounds().toFloat(), "Inputs");
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromTop (30);
        drawMeter (g, r.removeFromTop (26), micLabel, mic, micColour (settings.focusMic()));
        r.removeFromTop (6);
        drawMeter (g, r.removeFromTop (26), "Ref  " + refSourceName (settings.refSource.load()), ref, pal().inputCol);
        if (healthText.isNotEmpty())
        {
            r.removeFromTop (4);
            const auto& p = pal();
            auto line = r.removeFromTop (18);
            g.setColour (healthLevel == 2 ? p.bad : healthLevel == 1 ? p.warn : p.good);
            g.fillEllipse (line.removeFromLeft (10).toFloat().withSizeKeepingCentre (7.0f, 7.0f));
            line.removeFromLeft (6);
            g.setColour (healthLevel == 0 ? p.textDim : healthLevel == 2 ? p.bad : p.warn);
            g.setFont (font (11.0f, healthLevel > 0));
            g.drawFittedText (healthText, line, juce::Justification::centredLeft, 1, 0.85f);
        }
    }

private:
    struct Meter
    {
        float rmsDb = -120.0f, peakDb = -120.0f, holdDb = -120.0f;
        juce::uint32 holdUntil = 0, clipUntil = 0;
        void update (float peak, float rms, juce::uint32 now)
        {
            const float pk = 20.0f * std::log10 (juce::jmax (1e-6f, peak));
            peakDb = juce::jmax (pk, peakDb - 1.5f);
            rmsDb = rms;
            if (pk >= holdDb || now > holdUntil) { holdDb = pk; holdUntil = now + 1500; }
            if (pk >= -0.5f) clipUntil = now + 2000;
        }
    };

    void drawMeter (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& label, const Meter& m, juce::Colour c)
    {
        const auto& p = pal();
        g.setColour (p.text);
        g.setFont (font (11.5f, true));
        g.drawText (label, r.removeFromLeft (92), juce::Justification::centredLeft);
        auto led = r.removeFromRight (16).toFloat().withSizeKeepingCentre (10.0f, 10.0f);
        const bool clip = la::clock::ms32() < m.clipUntil;
        g.setColour (clip ? p.bad : p.border);
        g.fillEllipse (led);
        r.removeFromRight (6);
        auto valueArea = r.removeFromRight (48);
        auto bar = r.toFloat().withSizeKeepingCentre ((float) r.getWidth(), 10.0f);
        g.setColour (p.panelRaised);
        g.fillRoundedRectangle (bar, 3.0f);
        auto pos = [&] (float db) { return juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour (c.withAlpha (0.85f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pos (m.rmsDb)), 3.0f);
        g.setColour (c.withAlpha (0.35f));
        g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * pos (m.peakDb)), 3.0f);
        g.setColour (m.holdDb > -3.0f ? p.warn : p.text);
        g.fillRect (bar.getX() + bar.getWidth() * pos (m.holdDb) - 1.0f, bar.getY() - 2.0f, 2.0f, bar.getHeight() + 4.0f);
        g.setColour (p.textDim);
        g.setFont (monoFont (11.0f));
        g.drawText (dbText (m.rmsDb, 1, -99.0f), valueArea, juce::Justification::centredRight);
    }

    Settings& settings;
    Meter mic, ref;
    juce::String micLabel { "Mic 1" };
    juce::String healthText;
    int healthLevel = 0;
};

//==============================================================================
class DelayPanel : public juce::Component
{
public:
    explicit DelayPanel (Settings& s) : settings (s)
    {
        findButton.setTooltip ("Measure how late the mic hears the reference and compensate for it (key: D). "
                               "Needs the generator or program audio running for about 3 seconds.");
        findButton.onClick = [this] { settings.requestFindDelay = true; waiting = true; repaint(); };
        addAndMakeVisible (findButton);

        const double steps[] = { -1.0, -0.1, 0.1, 1.0 };
        const char* names[] = { "-1", "-0.1", "+0.1", "+1" };
        for (int i = 0; i < 4; ++i)
        {
            nudge[i].setButtonText (names[i]);
            nudge[i].setTooltip ("Nudge the delay compensation by " + juce::String (names[i]) + " ms");
            const double st = steps[i];
            nudge[i].onClick = [this, st] { settings.delayMs() = (float) juce::jlimit (0.0, 2000.0, settings.delayMsValue() + st); };
            addAndMakeVisible (nudge[i]);
        }
        zeroButton.setTooltip ("Remove the delay compensation");
        zeroButton.onClick = [this] { settings.delayMs() = 0.0f; };
        addAndMakeVisible (zeroButton);
        trackToggle.setButtonText ("Track");
        trackToggle.setTooltip ("Keep finding the delay on the programme (the console's feed as the reference) every few seconds and follow it, "
                                "so the transfer function stays lined up; shows how far it has drifted (temperature, a moved mic)");
        trackToggle.onClick = [this] { history.clear(); repaint(); };
        addAndMakeVisible (trackToggle);
    }

    bool tracking() const { return trackToggle.getToggleState(); }

    // While tracking: how fast the delay to the console feed moves (ms a minute) over the last few minutes, 0 until
    // there is enough. Temperature moves it a little over an hour; a steady fraction of a millisecond a minute, or a
    // sawtooth, is two clocks not locked.
    double driftMsPerMin() const
    {
        if (! tracking() || history.size() < 8) return 0.0;
        const auto end = history.back().first;
        std::vector<std::pair<double, double>> pts;
        for (const auto& h : history)
        {
            const double ago = (end - h.first).inSeconds();
            if (ago <= 300.0) pts.push_back ({ -ago / 60.0, h.second });
        }
        if (pts.size() < 8 || pts.front().first > -2.0) return 0.0;   // at least two minutes of it
        // the sawtooth: each jump back is a slip; take the movement between jumps
        double moved = 0.0, minutes = pts.back().first - pts.front().first;
        for (size_t i = 1; i < pts.size(); ++i)
        {
            const double d = pts[i].second - pts[i - 1].second;
            if (std::abs (d) < 0.5) moved += d;   // a jump of half a millisecond or more is a slip (or a moved mic): left out
        }
        return minutes > 0.0 ? moved / minutes : 0.0;
    }

    void setReport (const DelayReport& r)
    {
        const int m = settings.focusMic();
        if (r.serial != report.serial && tracking() && m < r.micCount && r.valid[(size_t) m] && r.confidenceDb[(size_t) m] >= 10.0)
        {
            history.push_back ({ juce::Time::getCurrentTime(), r.ms[(size_t) m] });
            if (history.size() > 720) history.erase (history.begin());   // an hour at one every 5 s
        }
        if (r.serial == report.serial && ! tracking()) return;   // nothing new: no repaint
        if (r.serial != report.serial) waiting = false;
        report = r;
        repaint();
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromTop (30);
        {
            auto row = r.removeFromTop (28);
            trackToggle.setBounds (row.removeFromRight (92));
            row.removeFromRight (6);
            findButton.setBounds (row);
        }
        r.removeFromTop (44);
        auto row = r.removeFromTop (24);
        const int w = (row.getWidth() - 4 * 4) / 5;
        for (auto& b : nudge) { b.setBounds (row.removeFromLeft (w)); row.removeFromLeft (4); }
        zeroButton.setBounds (row);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Delay compensation");
        auto r = getLocalBounds().reduced (12, 0);
        r.removeFromTop (62);

        const double ms = settings.delayMsValue();
        g.setColour (p.text);
        g.setFont (monoFont (20.0f, true));
        g.drawText (juce::String (ms, 2) + " ms", r.removeFromTop (28).removeFromLeft (140), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (12.0f));
        g.drawText (settings.distanceText (ms) + "  at " + settings.temperatureText(),
                    getLocalBounds().reduced (12, 0).withTrimmedTop (62).withHeight (28).withTrimmedLeft (140), juce::Justification::centredLeft);

        juce::String status;
        juce::Colour col = p.textDim;
        if (waiting) status = "Listening...";
        else if (report.serial == 0) status = "Press Find with the signal running.";
        else
        {
            const int m = settings.focusMic();
            int found = 0;
            for (int i = 0; i < report.micCount; ++i) found += report.valid[(size_t) i] ? 1 : 0;
            if (m >= report.micCount || ! report.valid[(size_t) m])
            {
                status = "No clear arrival on this mic. Is the signal on?";
                col = p.warn;
            }
            else
            {
                // (no polarity here: while monitoring the programme it says nothing about the system)
                status = "Found " + juce::String (report.ms[(size_t) m], 2) + " ms (" + juce::String (juce::roundToInt (report.confidenceDb[(size_t) m])) + " dB clear)";
                col = p.good;
            }
            if (report.micCount > 1) status << "   " << found << "/" << report.micCount << " mics";
        }
        if (tracking() && ! history.empty())
        {
            const double first = history.front().second, last = history.back().second;
            status = "Tracking " + juce::String (last, 2) + " ms, " + (last - first >= 0.0 ? "+" : "") + juce::String (last - first, 2) + " ms since "
                     + history.front().first.formatted ("%H:%M");
            col = std::abs (last - first) > 0.5 ? p.warn : p.good;
        }
        else if (tracking() && ! waiting) { status = "Tracking: waiting for a clear arrival in the programme..."; col = p.textDim; }
        g.setColour (col);
        g.setFont (font (11.5f));
        g.drawText (status, getLocalBounds().reduced (12, 6).removeFromBottom (18), juce::Justification::centredLeft);
    }

private:
    Settings& settings;
    DelayReport report;
    bool waiting = false;
    juce::ToggleButton trackToggle;
    std::vector<std::pair<juce::Time, double>> history;
    juce::TextButton findButton { "Find delay" }, zeroButton { "Zero" };
    juce::TextButton nudge[4];
};

//==============================================================================
// The event so far: the sound level log against the limit (set on the Monitor mic page).
class EventPanel : public juce::Component
{
public:
    std::function<void()> onMark;   // mark the start of a part of the event (Worship, Sermon ...)

    EventPanel()
    {
        markButton.setTooltip ("Mark where a part of the event starts (Worship, Sermon ...): the log and the report sum up each part");
        markButton.onClick = [this] { if (onMark) onMark(); };
        addAndMakeVisible (markButton);
    }

    void resized() override
    {
        auto top = getLocalBounds().reduced (12, 0).removeFromTop (30).withTrimmedTop (4);
        markButton.setBounds (top.removeFromRight (90).withSizeKeepingCentre (90, 22));
    }

    void setLog (const SplLog& l)
    {
        const auto rolling = l.rollingLaeq();
        const double now = rolling.empty() ? NAN : rolling.back();
        if (l.points.size() == shownPoints && std::abs (l.limitLaeq - shownLimit) < 0.01 && name == l.limitName() && l.parts.size() == shownParts) return;
        shownPoints = l.points.size();
        shownParts = l.parts.size();
        part = l.parts.empty() ? juce::String() : l.parts.back().name;
        partSince = l.parts.empty() ? juce::String() : juce::Time ((juce::int64) (l.parts.back().t * 1000.0)).formatted ("%H:%M");
        shownLimit = l.limitLaeq;
        leq = l.leq (false);
        roll = now;
        limit = l.limitLaeq;
        minutes = l.limitMinutes;
        over = l.minutesOver();
        name = l.limitName();
        unit = l.limitUnit();
        interval = l.interval;
        since = l.empty() ? juce::String() : juce::Time ((juce::int64) (l.start() * 1000.0)).formatted ("%H:%M");
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds().toFloat();
        drawPanel (g, r, "Event");
        auto c = r.reduced (12.0f, 0.0f).withTrimmedTop (32.0f);
        if (! std::isfinite (leq))
        {
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawFittedText ("The sound level is logged every " + juce::String (juce::roundToInt (interval)) + " seconds for the monitoring report.", c.toNearestInt(), juce::Justification::topLeft, 2);
            return;
        }
        auto line = c.removeFromTop (20.0f);
        g.setColour (p.text);
        g.setFont (monoFont (13.0f, true));
        g.drawText ("LAeq " + juce::String (leq, 1), line, juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawText ("since " + since, line, juce::Justification::centredRight);
        c.removeFromTop (4.0f);
        auto bar = c.removeFromTop (8.0f);
        g.setColour (p.border);
        g.fillRoundedRectangle (bar, 4.0f);
        if (limit > 0.0 && std::isfinite (roll))
        {
            const float f = (float) juce::jlimit (0.0, 1.0, (roll - (limit - 20.0)) / 22.0);
            g.setColour (roll > limit ? p.bad : roll > limit - 3.0 ? p.warn : p.good);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * f), 4.0f);
            g.setColour (p.text);
            const float lx = bar.getX() + bar.getWidth() * 20.0f / 22.0f;
            g.fillRect (lx - 1.0f, bar.getY() - 3.0f, 2.0f, bar.getHeight() + 6.0f);
        }
        c.removeFromTop (5.0f);
        g.setColour (limit > 0.0 && roll > limit ? p.bad : p.textDim);
        g.setFont (font (11.0f));
        g.drawText (limit > 0.0 ? juce::String (minutes) + " min " + name + " " + juce::String (roll, 1) + " of " + juce::String (juce::roundToInt (limit)) + " " + unit
                                      + (over > 0.0 ? ", over for " + juce::String (juce::roundToInt (over)) + " min" : juce::String())
                                : juce::String (minutes) + " min " + name + " " + juce::String (roll, 1) + " (no limit set)",
                    c.removeFromTop (16.0f), juce::Justification::centredLeft, true);
        c.removeFromTop (2.0f);
        g.setColour (part.isNotEmpty() ? p.text : p.textFaint);
        g.drawText (part.isNotEmpty() ? "Now: " + part + " (since " + partSince + ")" : juce::String ("No part marked yet"), c.removeFromTop (16.0f),
                    juce::Justification::centredLeft, true);
    }

private:
    juce::TextButton markButton { "Mark part" };
    juce::String part, partSince;
    size_t shownParts = (size_t) -1;
    size_t shownPoints = (size_t) -1;
    double shownLimit = -1.0, leq = NAN, roll = NAN, limit = 0.0, over = 0.0, interval = 5.0;
    int minutes = 15;
    juce::String name { "LAeq" }, unit { "dBA" };
    juce::String since;
};

// Live monitoring against the tuning: the programme's transfer function at the monitor mic, octave by octave, against
// what was measured there when the room was tuned (both levelled at 500 Hz - 4 kHz). A difference of 2 dB or more
// where the music has energy suggests a small change (half of it, never more than 3 dB): the room fills up, the air
// dries out.
class TunedPanel : public juce::Component
{
public:
    static constexpr int kBands = 8;
    static inline const double kFreq[kBands] = { 63, 125, 250, 500, 1000, 2000, 4000, 8000 };

    // `ref`: the tuned response at the monitor mic's point (empty: nothing to compare with).
    void update (const Snapshot& s, const FreqTable& ref, const juce::String& refName)
    {
        refText = refName;
        std::array<double, kBands> live {}, tuned {};
        std::array<int, kBands> nLive {}, nTuned {};
        auto band = [] (double f) { for (int b = 0; b < kBands; ++b) if (f >= kFreq[b] / 1.414 && f < kFreq[b] * 1.414) return b; return -1; };
        double midL = 0.0, midT = 0.0;
        int nmL = 0, nmT = 0;
        for (size_t i = 0; i < s.tfFreq.size() && i < s.tfMag.size(); ++i)
        {
            const double f = s.tfFreq[i];
            const float c = i < s.tfCoh.size() ? s.tfCoh[i] : 1.0f;
            if (! std::isfinite (s.tfMag[i]) || c < 0.6f) continue;
            if (const int b = band (f); b >= 0) { live[(size_t) b] += s.tfMag[i]; ++nLive[(size_t) b]; }
            if (f >= 500.0 && f <= 4000.0) { midL += s.tfMag[i]; ++nmL; }
        }
        for (size_t i = 0; i < ref.freq.size(); ++i)
        {
            if (! std::isfinite (ref.db[i])) continue;
            if (const int b = band (ref.freq[i]); b >= 0) { tuned[(size_t) b] += ref.db[i]; ++nTuned[(size_t) b]; }
            if (ref.freq[i] >= 500.0 && ref.freq[i] <= 4000.0) { midT += ref.db[i]; ++nmT; }
        }
        valid = nmL > 10 && nmT > 3;
        int worst = -1;
        for (int b = 0; b < kBands; ++b)
        {
            diff[(size_t) b] = NAN;
            if (! valid || nLive[(size_t) b] < 3 || nTuned[(size_t) b] < 1) continue;
            diff[(size_t) b] = (live[(size_t) b] / nLive[(size_t) b] - midL / nmL) - (tuned[(size_t) b] / nTuned[(size_t) b] - midT / nmT);
            if (worst < 0 || std::abs (diff[(size_t) b]) > std::abs (diff[(size_t) worst])) worst = b;
        }
        // a suggestion only when it has held for a while (the music changes from moment to moment)
        if (worst >= 0 && std::abs (diff[(size_t) worst]) >= 2.0)
        {
            if (worst == heldBand) ++held; else { heldBand = worst; held = 0; }
        }
        else { heldBand = -1; held = 0; }
        if (held >= 20)
        {
            const double d = diff[(size_t) heldBand];
            const double change = -juce::jlimit (-3.0, 3.0, d * 0.5);
            suggestion = freqWords (kFreq[heldBand]) + " is " + juce::String (std::abs (d), 1) + (d > 0.0 ? " dB up" : " dB down") + " on the tuning: try "
                         + (change > 0.0 ? "+" : "") + juce::String (change, 1) + " dB there"
                         + (heldBand <= 1 ? " (an audience soaks up the low end)." : heldBand >= 6 ? " (dry air or a full room takes the top off)." : ".");
        }
        else if (! valid || worst < 0 || std::abs (diff[(size_t) worst]) < 2.0) suggestion.clear();
        repaint();
    }
    void clear() { valid = false; refText.clear(); suggestion.clear(); repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds().toFloat();
        drawPanel (g, r, "Against the tuning");
        auto c = r.reduced (12.0f, 0.0f).withTrimmedTop (30.0f);
        if (refText.isEmpty() || ! valid)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f));
            g.drawFittedText (refText.isEmpty() ? "Tune the room (Room EQ) to compare the show with it here." : "Waiting for the programme (the reference and the mic need music).",
                              c.toNearestInt(), juce::Justification::topLeft, 3);
            return;
        }
        // a small bar per octave: up or down against the tuning
        auto bars = c.removeFromTop (30.0f);
        const float w = bars.getWidth() / kBands;
        for (int b = 0; b < kBands; ++b)
        {
            auto cell = bars.removeFromLeft (w).reduced (2.0f, 0.0f);
            const double d = diff[(size_t) b];
            const float mid = cell.getY() + 10.0f;
            g.setColour (p.border);
            g.drawHorizontalLine ((int) mid, cell.getX(), cell.getRight());
            if (std::isfinite (d))
            {
                const float h = (float) juce::jlimit (-9.0, 9.0, d) * 1.0f;
                g.setColour (std::abs (d) >= 2.0 ? p.warn : p.accent);
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (cell.getX() + 3.0f, std::min (mid, mid - h), cell.getRight() - 3.0f, std::max (mid, mid - h) + 1.0f));
            }
            g.setColour (p.textFaint);
            g.setFont (font (9.0f));
            g.drawText (kFreq[b] >= 1000.0 ? juce::String ((int) (kFreq[b] / 1000.0)) + "k" : juce::String ((int) kFreq[b]), cell.withTrimmedTop (20.0f), juce::Justification::centred);
        }
        c.removeFromTop (2.0f);
        g.setColour (suggestion.isNotEmpty() ? p.warn : p.textDim);
        g.setFont (font (11.0f, suggestion.isNotEmpty()));
        g.drawFittedText (suggestion.isNotEmpty() ? suggestion : "Close to the tuning at " + refText + ".", c.toNearestInt(), juce::Justification::topLeft, 2);
    }

private:
    static juce::String freqWords (double f) { return f >= 1000.0 ? juce::String ((int) (f / 1000.0)) + " kHz" : juce::String ((int) f) + " Hz"; }
    std::array<double, kBands> diff {};
    bool valid = false;
    juce::String refText, suggestion;
    int heldBand = -1, held = 0;
};

//==============================================================================
// Room conditions from the room sensor (MQTT), and what they do to the sound against the conditions the system was
// tuned in: the delays (the speed of sound) and the top end at the back of the room (the air's absorption, ISO 9613-1).
class ConditionsPanel : public juce::Component
{
public:
    struct State
    {
        bool live = false, connected = false;
        juce::String status, age;
        float tC = NAN, rh = NAN;                 // the sensor
        double refC = NAN, refRh = NAN;           // the tuning's conditions (else the session's)
        juce::String refLabel;                    // "when tuned (Oct 16)" / "this session"
        double maxDelayMs = 0.0;
        juce::String maxDelayName;
        double recheckMs = 0.5;                   // the quick re-check's delay tolerance
        double farM = 30.0;                       // the back of the room
        bool farIsRoom = false, feet = true, fahrenheit = true;
    };

    void setState (const State& s) { st = s; repaint(); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds().toFloat();
        drawPanel (g, r, "Room conditions (sensor)");
        const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        auto c = r.reduced (12.0f, 0.0f).withTrimmedTop (30.0f).withTrimmedBottom (6.0f);
        g.setFont (font (10.5f));
        g.setColour (st.live ? p.textDim : p.warn);
        g.drawText (st.live ? st.age : (st.connected ? "no reading yet" : "not connected"), r.reduced (12.0f, 0.0f).withHeight (28.0f), juce::Justification::centredRight, true);
        if (! st.live)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f));
            g.drawFittedText ((st.status.isNotEmpty() ? st.status + ". " : juce::String()) + "The sensor is set up in Settings, Room sensor.", c.toNearestInt(),
                              juce::Justification::topLeft, 3);
            return;
        }
        const bool haveT = std::isfinite (st.tC), haveH = std::isfinite (st.rh);
        auto temp = [&] (double tc, int d) { return juce::String (st.fahrenheit ? tc * 9.0 / 5.0 + 32.0 : tc, d) + deg + (st.fahrenheit ? "F" : "C"); };

        // the reading in the title row (with its age), what it is compared with on the first line
        {
            juce::String now;
            if (haveT) now << temp (st.tC, 1);
            if (haveT && haveH) now << ", ";
            if (haveH) now << juce::roundToInt (st.rh) << " %";
            g.setColour (p.text);
            g.setFont (monoFont (13.0f, true));
            g.drawText (now, r.reduced (12.0f, 0.0f).withHeight (28.0f).withTrimmedLeft (150.0f).withTrimmedRight (56.0f), juce::Justification::centredRight, true);
        }
        const double tc = haveT ? (double) st.tC : st.refC, rh = haveH ? (double) st.rh : st.refRh;
        const bool haveRef = std::isfinite (st.refC);
        juce::String refText;
        if (haveRef)
        {
            refText = st.refLabel + " " + temp (st.refC, 0);
            if (std::isfinite (st.refRh)) refText << ", " << juce::roundToInt (st.refRh) << " %";
            refText << ". ";
        }

        auto line = [&] (const juce::String& t, juce::Colour col)
        {
            g.setColour (col);
            g.setFont (font (11.0f));
            g.drawFittedText (t, c.removeFromTop (28.0f).toNearestInt(), juce::Justification::topLeft, 2, 0.85f);
            c.removeFromTop (2.0f);
        };

        // the delays: set for the speed of sound when tuned, the sound is now faster or slower
        const double c0 = air::speedOfSound (haveRef ? st.refC : tc), c1 = air::speedOfSound (tc);
        const juce::String speed = st.feet ? juce::String (juce::roundToInt (c1 * 3.28084)) + " ft/s" : juce::String (juce::roundToInt (c1)) + " m/s";
        const double dSpeed = st.feet ? (c1 - c0) * 3.28084 : c1 - c0;
        if (st.maxDelayMs > 0.05 && haveRef && haveT)
        {
            const double drift = air::delayDriftMs (st.maxDelayMs, st.refC, tc);
            const bool off = std::abs (drift) >= st.recheckMs * 0.5;
            line (refText + "Delays (" + st.maxDelayName + " " + juce::String (st.maxDelayMs, 1) + " ms): "
                      + (std::abs (drift) < 0.01 ? juce::String ("still right.")
                                                 : juce::String (std::abs (drift), 2) + " ms too " + (drift > 0.0 ? "short" : "long")
                                                       + (off ? ", worth the quick re-check." : ", within the tuning."))
                      + " Sound at " + speed + " (" + (dSpeed >= 0.0 ? "+" : "") + juce::String (juce::roundToInt (dSpeed)) + ").",
                  off ? p.warn : p.textDim);
        }
        else line (refText + "Sound at " + speed + "; no delays set, nothing to drift.", p.textDim);

        // the top end at the back: the air takes more off in dry or cold air
        if (haveRef)
        {
            const double refH = std::isfinite (st.refRh) ? st.refRh : rh;
            const double a8 = air::absorptionDbPerM (8000.0, tc, rh) * st.farM, a8r = air::absorptionDbPerM (8000.0, st.refC, refH) * st.farM;
            const double a16 = air::absorptionDbPerM (16000.0, tc, rh) * st.farM, a16r = air::absorptionDbPerM (16000.0, st.refC, refH) * st.farM;
            const double d8 = a8 - a8r, d16 = a16 - a16r;   // positive: duller than when tuned
            const auto far = st.feet ? juce::String (juce::roundToInt (st.farM * 3.28084)) + " ft" : juce::String (juce::roundToInt (st.farM)) + " m";
            juce::String t = "Air at " + far + (st.farIsRoom ? "" : " (typical)") + ": 8 kHz " + juce::String (-a8, 1) + " dB, 16 kHz " + juce::String (-a16, 1) + " dB";
            const bool notable = std::abs (d8) >= 1.0;
            if (std::abs (d8) < 0.3 && std::abs (d16) < 1.0) t << ", as when tuned.";
            else t << ", " << juce::String (std::abs (d8), 1) << " dB " << (d8 > 0.0 ? "duller" : "brighter") << " at 8k than tuned"
                   << (notable ? (d8 > 0.0 ? ": far seats lose air." : ": far seats brighter.") : ".");
            line (t, notable ? p.warn : p.textDim);
        }
    }

private:
    State st;
};

class TracesPanel : public juce::Component
{
public:
    std::function<void()> onCapture, onImport;
    std::function<void (int)> onRename, onExport, onDelete;

    TracesPanel (TraceStore& t, Settings& s) : traces (t), settings (s)
    {
        for (int i = 0; i < targets::count(); ++i) targetBox.addItem (targets::list()[(size_t) i].name, i + 1);
        targetBox.setSelectedItemIndex (settings.targetCurve.load(), juce::dontSendNotification);
        targetBox.onChange = [this]
        {
            settings.targetCurve = targetBox.getSelectedItemIndex();
            settings.targetAnchorMask = 0;   // line the new target up with the live trace once
            targetBox.setTooltip (targets::list()[(size_t) juce::jmax (0, targetBox.getSelectedItemIndex())].description);
        };
        targetBox.setTooltip (targets::list()[(size_t) settings.targetCurve.load()].description);
        addAndMakeVisible (targetBox);
        relevelButton.setTooltip ("Line the target up with the live trace again (it stays where it is otherwise)");
        relevelButton.onClick = [this] { settings.targetAnchorMask = 0; };
        addAndMakeVisible (relevelButton);

        captureButton.setTooltip ("Save the current transfer function or RTA as a trace (key: C)");
        importButton.setTooltip ("Load a trace or speaker response from an FRD, TXT or CSV file");
        captureButton.onClick = [this] { if (onCapture) onCapture(); };
        importButton.onClick = [this] { if (onImport) onImport(); };
        addAndMakeVisible (captureButton);
        addAndMakeVisible (importButton);
    }

    void refresh() { if (traces.getVersion() != seenVersion) { seenVersion = traces.getVersion(); repaint(); } }
    void forceRepaint() { repaint(); }

    void resized() override
    {
        auto r = getLocalBounds().reduced (12, 0).withTrimmedTop (30).removeFromTop (26);
        const int w = (r.getWidth() - 6) / 2;
        captureButton.setBounds (r.removeFromLeft (w));
        r.removeFromLeft (6);
        importButton.setBounds (r);
        {
            auto tr = getLocalBounds().reduced (12, 0).withTrimmedTop (62).removeFromTop (26).withTrimmedLeft (54);
            relevelButton.setBounds (tr.removeFromRight (72));
            tr.removeFromRight (6);
            targetBox.setBounds (tr);
        }
    }

    void syncTarget() { targetBox.setSelectedItemIndex (settings.targetCurve.load(), juce::dontSendNotification); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Traces");
        g.setColour (p.text);
        g.setFont (font (12.0f));
        g.drawText ("Target", getLocalBounds().reduced (12, 0).withTrimmedTop (62).removeFromTop (26).withWidth (50),
                    juce::Justification::centredLeft);
        const auto list = listArea();
        const auto& all = traces.all();
        if (all.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawFittedText ("No traces yet. Capture saves what is on screen so you can compare it with later changes.",
                              list.reduced (4, 6), juce::Justification::topLeft, 3);
            return;
        }

        scroll = juce::jlimit (0, juce::jmax (0, (int) all.size() - visibleRows()), scroll);
        for (int row = 0; row < visibleRows(); ++row)
        {
            const int i = scroll + row;
            if (i >= (int) all.size()) break;
            const auto& t = all[(size_t) i];
            auto r = rowBounds (row);
            if (i == hoverRow)
            {
                g.setColour (p.panelRaised);
                g.fillRoundedRectangle (r.toFloat(), 5.0f);
            }
            auto sw = r.removeFromLeft (26).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            const auto col = traceColour (t.colour);
            if (t.visible) { g.setColour (col); g.fillRoundedRectangle (sw, 3.0f); }
            g.setColour (col);
            g.drawRoundedRectangle (sw, 3.0f, 1.5f);

            auto menu = r.removeFromRight (24);
            g.setColour (p.textDim);
            g.setFont (font (14.0f, true));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x8b\xaf")), menu, juce::Justification::centred);

            auto tag = r.removeFromRight (34);
            g.setFont (font (9.5f, true));
            g.drawText (t.kind == Trace::Rta ? "RTA" : "TF", tag, juce::Justification::centred);

            g.setColour (t.visible ? p.text : p.textDim);
            g.setFont (font (12.0f));
            g.drawText (t.name, r.reduced (2, 0), juce::Justification::centredLeft, true);
        }

        if ((int) all.size() > visibleRows())
        {
            g.setColour (p.textDim);
            g.setFont (font (10.5f));
            g.drawText (juce::String (scroll + 1) + "-" + juce::String (juce::jmin ((int) all.size(), scroll + visibleRows())) + " of "
                            + juce::String (all.size()) + "  (scroll for more)",
                        list.withTop (list.getBottom() - 16), juce::Justification::centredRight);
        }
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int r = rowAt (e.getPosition());
        if (r != hoverRow) { hoverRow = r; repaint(); }
    }

    void mouseExit (const juce::MouseEvent&) override { hoverRow = -1; repaint(); }

    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
    {
        scroll -= w.deltaY > 0 ? 1 : (w.deltaY < 0 ? -1 : 0);
        repaint();
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());
        if (! traces.valid (i)) return;
        const int row = i - scroll;
        auto r = rowBounds (row);
        if (e.x < r.getX() + 26)
        {
            traces.setVisible (i, ! traces.all()[(size_t) i].visible);
            repaint();
            return;
        }
        if (e.x > r.getRight() - 24 || e.mods.isPopupMenu())
            showMenu (i);
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int i = rowAt (e.getPosition());
        if (traces.valid (i) && onRename) onRename (i);
    }

private:
    void showMenu (int i)
    {
        juce::PopupMenu m;
        m.addItem (1, traces.all()[(size_t) i].visible ? "Hide" : "Show");
        m.addItem (2, "Rename...");
        m.addItem (3, "Export as FRD...");
        m.addSeparator();
        m.addItem (4, "Delete");
        juce::Component::SafePointer<TracesPanel> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [safe, i] (int result)
        {
            if (safe == nullptr || ! safe->traces.valid (i)) return;
            switch (result)
            {
                case 1: safe->traces.setVisible (i, ! safe->traces.all()[(size_t) i].visible); safe->repaint(); break;
                case 2: if (safe->onRename) safe->onRename (i); break;
                case 3: if (safe->onExport) safe->onExport (i); break;
                case 4: if (safe->onDelete) safe->onDelete (i); break;
                default: break;
            }
        });
    }

    juce::Rectangle<int> listArea() const { return getLocalBounds().reduced (8, 0).withTrimmedTop (96).withTrimmedBottom (8); }
    int visibleRows() const { return juce::jmax (1, listArea().getHeight() / kRowH); }
    juce::Rectangle<int> rowBounds (int row) const { return listArea().withHeight (kRowH).translated (0, row * kRowH); }

    int rowAt (juce::Point<int> pt) const
    {
        if (! listArea().contains (pt)) return -1;
        const int row = (pt.y - listArea().getY()) / kRowH;
        const int i = scroll + row;
        return traces.valid (i) && row < visibleRows() ? i : -1;
    }

    static constexpr int kRowH = 26;
    TraceStore& traces;
    Settings& settings;
    juce::TextButton captureButton { "Capture" }, importButton { "Import..." };
    juce::ComboBox targetBox;
    juce::TextButton relevelButton { "Re-level" };
    int seenVersion = -1, hoverRow = -1, scroll = 0;
};
//==============================================================================
// Feedback watch while monitoring: frequencies that start to ring in the programme (a narrow peak that stands well
// above its neighbours and holds for more than a second), newest first, with the cut that would tame it. Marked on
// the RTA and the waterfall too. Sustained notes can look like this, so it says "possible" and lets you judge.
class FeedbackWatchPanel : public juce::Component
{
public:
    struct Hit { double freq = 0.0, prominenceDb = 0.0, cutDb = 0.0; juce::String time; };
    std::function<void()> onClear;
    std::function<void (bool)> onWatchChanged;

    FeedbackWatchPanel()
    {
        watchToggle.setButtonText ("Watch");
        watchToggle.onClick = [this] { if (onWatchChanged) onWatchChanged (watching()); repaint(); };
        watchToggle.setTooltip ("Watch the monitoring mic for frequencies starting to ring");
        watchToggle.setToggleState (true, juce::dontSendNotification);
        addAndMakeVisible (watchToggle);
        clearButton.setTooltip ("Clear the list and the marks on the graphs");
        clearButton.onClick = [this] { hits.clear(); if (onClear) onClear(); repaint(); };
        addAndMakeVisible (clearButton);
    }

    bool watching() const { return watchToggle.getToggleState(); }
    void add (const Hit& h) { hits.insert (hits.begin(), h); if (hits.size() > 12) hits.pop_back(); repaint(); }
    const std::vector<Hit>& all() const { return hits; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, getLocalBounds().toFloat(), "Feedback watch");
        auto r = getLocalBounds().reduced (12, 0).withTrimmedTop (32);
        if (hits.empty())
        {
            g.setColour (p.textFaint);
            g.setFont (font (11.5f));
            g.drawFittedText (watching() ? "Nothing ringing." : "Off.", r.removeFromTop (34), juce::Justification::topLeft, 2);
            return;
        }
        g.setFont (monoFont (11.5f));
        for (const auto& h : hits)
        {
            if (r.getHeight() < 16) break;
            auto row = r.removeFromTop (17);
            g.setColour (p.bad);
            g.drawText (freqLabel (h.freq) + " Hz", row.removeFromLeft (76), juce::Justification::centredLeft);
            g.setColour (p.textDim);
            g.drawText (h.time + "  cut " + juce::String (juce::roundToInt (h.cutDb)) + " dB, Q 10", row, juce::Justification::centredLeft, true);
        }
    }

    void resized() override
    {
        auto top = getLocalBounds().reduced (12, 0).removeFromTop (30).withTrimmedTop (4);
        clearButton.setBounds (top.removeFromRight (56).withSizeKeepingCentre (56, 22));
        top.removeFromRight (6);
        watchToggle.setBounds (top.removeFromRight (96));
    }

private:
    std::vector<Hit> hits;
    juce::ToggleButton watchToggle;
    juce::TextButton clearButton { "Clear" };
};

} // namespace la::ui
