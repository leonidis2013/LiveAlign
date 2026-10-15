// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <thread>
// Tuning step 5: room acoustics, before Room EQ (it decides whether EQ can help). A sweep at each ticked listening
// point, in the guided pop-up, gives a long impulse response: reverberation time and early decay per octave, clarity
// for speech (C50) and music (C80), a speech intelligibility estimate (STI). With the room modes, the coverage and the
// noise rating (from the noise check on Mics), a decision tree reads them: measured? usable? reverberation against the
// room's use, speech, and the verdict: room OK for EQ, EQ will help only partly, or treat before EQ.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "RoomPage.h"
#include "../PluginProcessor.h"
#include "../Analysis/Deconvolve.h"
#include "../AcousticsSummary.h"

namespace la::ui
{
// Octave band noise levels of the loudest mic in the noise floor check, when it was calibrated in dB SPL.
inline bool noiseOctaves (const LiveAlignProcessor& p, std::array<double, acoustics::kNoiseBands>& out, juce::String& who) { return noiseOctavesOf (p, out, who); }

class AcousticsChart : public juce::Component
{
public:
    enum Metric { RT = 0, EDT, C50, C80, Ts, Thirds, Etc, kNumMetrics };
    explicit AcousticsChart (LiveAlignProcessor& p) : proc (p) {}
    void setMetric (int m) { metric = juce::jlimit (0, kNumMetrics - 1, m); repaint(); }

    static juce::String metricName (int m)
    {
        static const char* n[] = { "Reverberation time (s) per octave", "Early decay time (s) per octave", "Clarity C50, speech (dB) per octave",
                                   "Clarity C80, music (dB) per octave", "Centre time Ts (ms) per octave", "Reverberation time (s) per 1/3 octave",
                                   "Energy-time curve (dB against the direct sound)" };
        return n[juce::jlimit (0, kNumMetrics - 1, m)];
    }
    static juce::String metricShort (int m)
    {
        static const char* n[] = { "RT", "EDT", "C50", "C80", "Ts", "RT 1/3", "ETC" };
        return n[juce::jlimit (0, kNumMetrics - 1, m)];
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        auto r = getLocalBounds().toFloat();
        drawPanel (g, r, metricName (metric));
        auto plot = r.reduced (14.0f).withTrimmedTop (30.0f).withTrimmedLeft (34.0f).withTrimmedBottom (18.0f);
        g.setColour (p.bg.withAlpha (0.5f));
        g.fillRect (plot);

        const auto& room = proc.room;
        std::vector<std::pair<const acoustics::Result*, juce::Colour>> each;
        if (room.acousticsFromSweeps())
        {
            for (const auto& w : room.sweeps)
                if (w.ac.valid) each.push_back ({ &w.ac, positionColour (juce::jmax (0, proc.positions.indexOf (w.position))) });
        }
        else
            for (const auto& m : room.meas)
                if (m.ac.valid && m.include) each.push_back ({ &m.ac, positionColour (juce::jmax (0, proc.positions.indexOf (m.position))) });
        if (each.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("Nothing measured yet. Press Measure with a sweep: the guided pop-up takes the mic to each ticked point.",
                              plot.reduced (40.0f).toNearestInt(), juce::Justification::centred, 3);
            return;
        }
        const auto avg = room.acousticsAverage();
        if (metric == Etc) { paintEtc (g, plot, each, avg); return; }

        // the columns: 63 Hz then the octaves, or the 1/3 octaves
        const bool thirds = metric == Thirds;
        const int cols = thirds ? acoustics::kThirds : acoustics::kBands + 1;
        auto colFreq = [&] (int k) { return thirds ? acoustics::thirds()[(size_t) k] : k == 0 ? 63.0 : acoustics::octaves()[(size_t) k - 1]; };
        const bool time = metric == RT || metric == EDT || thirds;
        double vMin = time ? 0.0 : metric == Ts ? 0.0 : -10.0, vMax = time ? 3.0 : metric == Ts ? 250.0 : 15.0;
        const double step = time ? 0.5 : metric == Ts ? 50.0 : 5.0;
        // stretch to the data when it runs off the top
        if (time)
            for (const auto& e : each)
                for (int k = 0; k < cols; ++k)
                {
                    const double v = value (*e.first, k, thirds);
                    if (std::isfinite (v) && v > vMax) vMax = std::ceil (v * 2.0) / 2.0;
                }
        vMax = std::min (vMax, 8.0 + (time ? 0.0 : 300.0));
        auto xAt = [&] (int k) { return plot.getX() + plot.getWidth() * ((float) k + 0.5f) / (float) cols; };
        auto yAt = [&] (double v) { return plot.getBottom() - (float) ((juce::jlimit (vMin, vMax, v) - vMin) / (vMax - vMin)) * plot.getHeight(); };

        // the typical range for the room's use (reverberation only)
        if (metric == RT || thirds)
        {
            double lo, hi, sti;
            RoomTuning::useTargets (room.use, lo, hi, sti);
            g.setColour (p.good.withAlpha (0.10f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (plot.getX(), yAt (hi), plot.getRight(), yAt (lo)));
            g.setColour (p.good.withAlpha (0.8f));
            g.setFont (font (10.5f));
            g.drawText ("typical in the mids for " + RoomTuning::useNames()[room.use].toLowerCase(), plot.reduced (6.0f).withHeight (14.0f).withY (yAt (hi) + 3.0f),
                        juce::Justification::centredRight);
        }
        g.setFont (font (10.5f));
        for (double v = vMin; v <= vMax + 1e-9; v += step)
        {
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) yAt (v), plot.getX(), plot.getRight());
            g.setColour (p.textDim);
            g.drawText (time ? juce::String (v, 1) : juce::String ((int) v), juce::Rectangle<float> (plot.getX() - 34.0f, yAt (v) - 7.0f, 30.0f, 14.0f),
                        juce::Justification::centredRight);
        }
        for (int k = 0; k < cols; ++k)
        {
            if (thirds && k % 2 == 1 && plot.getWidth() < 700.0f) continue;
            g.setColour (! thirds && k == 0 && ! avgReliable63 (avg) ? p.textFaint : p.textDim);
            g.drawText (freqLabel (colFreq (k)), juce::Rectangle<float> (xAt (k) - 30.0f, plot.getBottom() + 2.0f, 60.0f, 16.0f), juce::Justification::centred);
        }
        // the 63 Hz column is only as good as its decay range
        if (! thirds && ! avgReliable63 (avg))
        {
            g.setColour (p.bg.withAlpha (0.45f));
            g.fillRect (juce::Rectangle<float> (plot.getX(), plot.getY(), plot.getWidth() / (float) cols, plot.getHeight()));
            g.setColour (p.textFaint);
            g.setFont (font (9.5f));
            g.drawFittedText ("63 Hz: not enough decay to trust", juce::Rectangle<float> (plot.getX() + 2.0f, plot.getY() + 4.0f, plot.getWidth() / (float) cols - 4.0f, 40.0f).toNearestInt(),
                              juce::Justification::centredTop, 3);
        }
        // a ringing third octave
        if (thirds)
        {
            const auto sum = summariseAcoustics (proc);
            if (sum.ringingThird >= 0)
            {
                const float x = xAt (sum.ringingThird);
                g.setColour (p.warn.withAlpha (0.14f));
                g.fillRect (juce::Rectangle<float> (x - plot.getWidth() / (float) cols * 0.5f, plot.getY(), plot.getWidth() / (float) cols, plot.getHeight()));
                g.setColour (p.warn);
                g.setFont (font (10.5f, true));
                g.drawText ("rings " + juce::String (sum.ringingRatio, 1) + "x", juce::Rectangle<float> (x - 50.0f, plot.getY() + 4.0f, 100.0f, 14.0f), juce::Justification::centred);
            }
        }

        auto line = [&] (const acoustics::Result& res, juce::Colour c, float w, bool dots)
        {
            juce::Path path;
            bool pen = false;
            for (int k = 0; k < cols; ++k)
            {
                const double v = value (res, k, thirds);
                if (! std::isfinite (v)) { pen = false; continue; }
                const float x = xAt (k), y = yAt (v);
                if (! pen) { path.startNewSubPath (x, y); pen = true; } else path.lineTo (x, y);
                if (dots) { g.setColour (c); g.fillEllipse (x - 3.5f, y - 3.5f, 7.0f, 7.0f); }
            }
            strokeTrace (g, path, c, w);
        };
        for (const auto& e : each) line (*e.first, e.second.withAlpha (0.45f), 1.2f, false);
        line (avg, p.text, 2.6f, true);
    }

private:
    static bool avgReliable63 (const acoustics::Result& a) { return std::isfinite (a.b63.rangeDb) && a.b63.rangeDb >= 30.0; }

    double value (const acoustics::Result& res, int k, bool thirds) const
    {
        if (thirds) return res.rt3[(size_t) k];
        const auto& o = k == 0 ? res.b63 : res.band[(size_t) k - 1];
        switch (metric)
        {
            case RT: return o.rt();
            case EDT: return o.edt;
            case C50: return o.c50;
            case C80: return o.c80;
            case Ts: return o.ts;
            default: return NAN;
        }
    }

    // The energy-time curve of each point, with the first reflection and any echo marked.
    void paintEtc (juce::Graphics& g, juce::Rectangle<float> plot, const std::vector<std::pair<const acoustics::Result*, juce::Colour>>& each, const acoustics::Result& avg)
    {
        const auto& p = pal();
        const double t0 = -5.0, t1 = 300.0, dbLo = -60.0;
        auto xAt = [&] (double ms) { return plot.getX() + (float) ((ms - t0) / (t1 - t0)) * plot.getWidth(); };
        auto yAt = [&] (double db) { return plot.getY() + (float) (juce::jlimit (dbLo, 0.0, db) / dbLo) * plot.getHeight(); };
        g.setFont (font (10.5f));
        for (double db = 0.0; db >= dbLo; db -= 10.0)
        {
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) yAt (db), plot.getX(), plot.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 34.0f, yAt (db) - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
        }
        for (double ms = 0.0; ms <= t1; ms += 50.0)
        {
            g.setColour (p.grid);
            g.drawVerticalLine ((int) xAt (ms), plot.getY(), plot.getBottom());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) ms) + " ms", juce::Rectangle<float> (xAt (ms) - 30.0f, plot.getBottom() + 2.0f, 60.0f, 16.0f), juce::Justification::centred);
        }
        // the 50 ms (speech) and 80 ms (music) clarity limits
        for (double ms : { 50.0, 80.0 })
        {
            g.setColour (p.accent.withAlpha (0.35f));
            g.drawVerticalLine ((int) xAt (ms), plot.getY(), plot.getBottom());
            g.setColour (p.accent.withAlpha (0.8f));
            g.drawText (ms < 60.0 ? "C50" : "C80", juce::Rectangle<float> (xAt (ms) + 3.0f, plot.getY() + 3.0f, 40.0f, 13.0f), juce::Justification::centredLeft);
        }
        for (size_t k = 0; k < each.size(); ++k)
        {
            const auto& etc = each[k].first->etc;
            if (etc.empty()) continue;
            juce::Path path;
            bool pen = false;
            for (size_t i = 0; i < etc.size(); ++i)
            {
                const double ms = t0 + 0.5 * (double) i;
                if (ms > t1) break;
                const float x = xAt (ms), y = yAt (etc[i]);
                if (! pen) { path.startNewSubPath (x, y); pen = true; } else path.lineTo (x, y);
            }
            strokeTrace (g, path, each[k].second.withAlpha (each.size() > 1 ? 0.75f : 0.95f), 1.1f);
        }
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (plot.toNearestInt());
        if (std::isfinite (avg.itdgMs))
        {
            const float x = xAt (avg.itdgMs);
            g.setColour (p.good);
            g.drawVerticalLine ((int) x, plot.getY() + 18.0f, plot.getBottom());
            g.setFont (font (10.5f, true));
            g.drawText ("first reflection " + juce::String (avg.itdgMs, 1) + " ms", juce::Rectangle<float> (x + 4.0f, plot.getY() + 18.0f, 170.0f, 14.0f), juce::Justification::centredLeft);
        }
        if (avg.echoAudible() && std::isfinite (avg.echoMs))
        {
            const float x = xAt (avg.echoMs);
            g.setColour (p.bad);
            g.fillEllipse (x - 5.0f, yAt (std::isfinite (avg.echoDb) ? avg.echoDb : -10.0) - 5.0f, 10.0f, 10.0f);
            g.setFont (font (10.5f, true));
            g.drawText ("echo " + juce::String (juce::roundToInt (avg.echoMs)) + " ms", juce::Rectangle<float> (x + 8.0f, yAt (std::isfinite (avg.echoDb) ? avg.echoDb : -10.0) - 7.0f, 110.0f, 14.0f),
                        juce::Justification::centredLeft);
        }
        if (std::isfinite (avg.flutterMs))
        {
            g.setColour (p.warn);
            g.setFont (font (11.0f, true));
            g.drawText ("flutter echo: repeats every " + juce::String (juce::roundToInt (avg.flutterMs)) + " ms", plot.reduced (8.0f).removeFromBottom (16.0f), juce::Justification::centredRight);
        }
    }

    LiveAlignProcessor& proc;
    int metric = 0;
};

class AcousticsPage : public juce::Component, public GuidedSource, private juce::Timer
{
public:
    std::function<void()> onChanged, onOpenPlan;
    std::function<void (GuidedSource&)> onGuided;

    ~AcousticsPage() override { stopTimer(); proc.recorder.stop(); }

    explicit AcousticsPage (LiveAlignProcessor& p) : proc (p), chart (p)
    {
        addAndMakeVisible (chart);
        for (int i = 0; i < AcousticsChart::kNumMetrics; ++i)
        {
            metricButtons[i].setButtonText (AcousticsChart::metricShort (i));
            metricButtons[i].setRadioGroupId (8301);
            metricButtons[i].setClickingTogglesState (true);
            metricButtons[i].onClick = [this, i] { if (metricButtons[i].getToggleState()) chart.setMetric (i); };
            metricButtons[i].setTooltip (AcousticsChart::metricName (i));
            addAndMakeVisible (metricButtons[i]);
        }
        metricButtons[0].setToggleState (true, juce::dontSendNotification);
        useBox.addItemList (RoomTuning::useNames(), 1);
        useBox.setTooltip ("What the room is mostly used for: sets the typical reverberation time and intelligibility to compare with");
        useBox.onChange = [this] { proc.room.use = useBox.getSelectedItemIndex(); changed(); chart.repaint(); };
        addAndMakeVisible (useBox);
        sweepButton.setTooltip ("A 4-second sweep (twice) through the whole system at each ticked point, in the guided pop-up: a long "
                                "impulse response for the reverberation time of any room. Quiet room, please.");
        sweepButton.onClick = [this]
        {
            if (phase != Phase::Idle) { stopSweep ("Stopped."); return; }
            redo.clear();
            if (onGuided) onGuided (*this);
            else startSweep();
        };
        addAndMakeVisible (sweepButton);
        planButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Points on the plan  \xe2\x86\x92")));
        planButton.onClick = [this] { if (onOpenPlan) onOpenPlan(); };
        addAndMakeVisible (planButton);
        sizeButton.setTooltip ("The room's length, width and height (or its volume): for the Schroeder frequency and the critical distance");
        sizeButton.onClick = [this] { askRoomSize(); };
        addAndMakeVisible (sizeButton);
        refresh();
    }

    // ---- the sweep: play, record, work out each mic's impulse response ----
    void startSweep()
    {
        auto& st = proc.settings;
        if (st.refSource.load() == 0) { status = "Set a reference on Mics first (the generator copy works)."; repaint(); return; }
        juce::StringArray where;
        for (int m = 0; m < st.micCount.load(); ++m)
            if (proc.mics.position[(size_t) m].isNotEmpty()) where.add (proc.mics.nameOf (m) + " at " + proc.mics.position[(size_t) m]);
        if (where.isEmpty()) { status = "Put a mic at a listening point first."; repaint(); return; }
        saved = { st.genType.load(), st.genSweepSec.load(), st.genOutput.load(), st.genOn.load() };
        st.genType = 3;            // log sweep
        sweepSec = juce::jlimit (2.0, 10.0, AppPrefs::get().value ("sweepSec"));
        st.genSweepSec = (float) sweepSec;
        st.genOutput = 0;          // the whole system
        st.genOn = true;
        st.running = true;
        phase = Phase::Lead;
        phaseStart = juce::Time::getMillisecondCounterHiRes();
        status = "Sweeping: " + where.joinIntoString (", ") + ". Keep the room quiet.";
        sweepButton.setButtonText ("Stop");
        startTimerHz (15);
        repaint();
    }

    void stopSweep (const juce::String& why)
    {
        proc.recorder.stop();
        restoreGenerator();
        phase = Phase::Idle;
        stopTimer();
        status = why;
        refreshButtons();
        repaint();
    }

    bool sweeping() const { return phase != Phase::Idle; }
    void showMetric (int m) { metricButtons[juce::jlimit (0, AcousticsChart::kNumMetrics - 1, m)].setToggleState (true, juce::dontSendNotification); chart.setMetric (m); }

    void refresh()
    {
        proc.syncPositionsWithPlan();
        useBox.setSelectedItemIndex (juce::jlimit (0, 3, proc.room.use), juce::dontSendNotification);
        refreshButtons();
        resized();
        repaint();
        chart.repaint();
    }

    // ---- what the navigation shows ----
    juce::StringArray points() const
    {
        juce::StringArray out;
        for (const auto& pt : proc.positions) if (! proc.room.acSkipped.contains (pt)) out.add (pt);
        return out;
    }
    bool sweptAt (const juce::String& pt) const
    {
        for (const auto& w : proc.room.sweeps) if (w.position == pt && w.ac.valid) return true;
        return false;
    }
    int sweptCount() const { int n = 0; for (const auto& pt : points()) n += sweptAt (pt) ? 1 : 0; return n; }
    juce::String nextHint() const
    {
        if (points().isEmpty()) return "Tick a listening point";
        if (sweptCount() < points().size()) return "Measure with a sweep at " + juce::String (points().size() - sweptCount()) + (points().size() - sweptCount() == 1 ? " point" : " points");
        return {};
    }
    void doNext() { if (phase == Phase::Idle && onGuided) onGuided (*this); }

    // ---- guided: one sweep at each ticked point ----
    juce::String guidedTitle() const override { return "Room acoustics"; }
    int currentPoint() const
    {
        const auto pts = points();
        for (int k = 0; k < pts.size(); ++k) if (pts[k] == redo) return k;
        for (int k = 0; k < pts.size(); ++k) if (! sweptAt (pts[k])) return k;
        return -1;
    }
    int micAt (const juce::String& pt) const
    {
        for (int m = 0; m < proc.tuningMicCount(); ++m) if (proc.mics.position[(size_t) m] == pt) return m;
        return 0;
    }
    std::vector<GuideSpot> guidedSpots() override
    {
        std::vector<GuideSpot> out;
        const auto pts = points();
        const int now = currentPoint();
        for (int k = 0; k < pts.size(); ++k)
        {
            GuideSpot sp;
            sp.id = k;
            sp.label = pts[k];
            sp.marker = proc.plan.ensureMarker (pts[k]);
            sp.x = proc.plan.items[(size_t) sp.marker].x;
            sp.y = proc.plan.items[(size_t) sp.marker].y;
            sp.state = k == now ? GuideSpot::Now : sweptAt (pts[k]) && pts[k] != redo ? GuideSpot::Done : GuideSpot::ToDo;
            if (sp.state == GuideSpot::Now) { sp.mic = micAt (pts[k]); sp.how = "seated ear height, " + aimHowText (proc, sp.mic, RoomAim); }
            out.push_back (sp);
        }
        return out;
    }
    juce::String guidedHowTo() const override
    {
        return "First: unmute every speaker and keep the room quiet. The sweep plays twice through the whole system (about 10 seconds).";
    }
    bool guidedStart() override
    {
        const auto pts = points();
        const int k = currentPoint();
        if (k < 0) { status = "Every ticked point is measured."; return false; }
        proc.setMicPosition (micAt (pts[k]), pts[k]);
        startSweep();
        return phase != Phase::Idle;
    }
    void guidedStop() override { stopSweep ("Stopped."); }
    bool guidedBusy() const override { return phase != Phase::Idle; }
    float guidedProgress() const override { return phase == Phase::Working ? 0.95f : phase == Phase::Lead ? 0.02f : proc.recorder.progress() * 0.9f; }
    juce::String guidedPhase() const override { return phase == Phase::Working ? juce::String ("Working out the impulse response...") : juce::String ("Sweeping..."); }
    int guidedSerial() const override { return serial; }
    juce::String guidedStatus() const override { return status; }
    void guidedRedo (const std::vector<GuideSpot>& was) override { if (! was.empty()) redo = was.front().label; }
    void guidedClosed() override { redo.clear(); refresh(); }
    int guidedAimKind() const override { return RoomAim; }
    int guidedAimMic() const override { const auto pts = points(); const int k = currentPoint(); return k >= 0 ? micAt (pts[k]) : 0; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto& room = proc.room;
        const auto avg = room.acousticsAverage();
        double rtLo, rtHi, stiMin;
        RoomTuning::useTargets (room.use, rtLo, rtHi, stiMin);
        const bool fromSweeps = room.acousticsFromSweeps();

        // tiles
        auto tile = [&] (int i, const juce::String& label, const juce::String& value, const juce::String& unit, juce::Colour c)
        {
            drawTile (g, tiles[(size_t) i].toFloat(), label, value, unit, c, 22.0f);
        };
        auto num = [] (double v, int d) { return ! std::isfinite (v) ? juce::String ("-") : d == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, d); };
        const auto sum = summariseAcoustics (proc);
        const double rt = sum.rtMid;
        tile (0, fromSweeps ? "REVERB (RT, 500-1k)" : "REVERB (ESTIMATE)", num (rt, 2), "s", ! std::isfinite (rt) ? p.textDim : rt < rtLo * 0.85 || rt > rtHi * 1.15 ? p.warn : p.good);
        tile (1, "WARMTH / BRILLIANCE", num (sum.bassRatio, 2) + " / " + num (sum.trebleRatio, 2), "BR / TR",
              ! std::isfinite (sum.bassRatio) ? p.textDim : sum.bassRatio > 1.3 || sum.trebleRatio > 1.15 ? p.warn : p.text);
        tile (2, "CLARITY C50 / C80", num (sum.c50, 1) + " / " + num (sum.c80, 1), "dB", p.text);
        {
            const double sti = std::isfinite (sum.stiCorrected) ? sum.stiCorrected : sum.stiRaw;
            tile (3, "SPEECH (STI)", num (sti, 2),
                  std::isfinite (sum.alcons) ? juce::String (acoustics::stiRating (sti)) + ", " + juce::String (sum.alcons, 1) + "% ALcons" : juce::String (acoustics::stiRating (sti)),
                  ! std::isfinite (sti) ? p.textDim : sti >= stiMin ? p.good : p.warn);
        }
        {
            const double cov = std::isfinite (sum.coverage2k) ? std::max (sum.coverage2k, std::isfinite (sum.coverage4k) ? sum.coverage4k : 0.0) : NAN;
            tile (4, "COVERAGE (2 / 4 kHz)", std::isfinite (cov) ? juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + num (sum.coverage2k / 2.0, 1) + " / " + num (sum.coverage4k / 2.0, 1) : juce::String ("-"),
                  "dB", ! std::isfinite (cov) ? p.textDim : cov <= 6.0 ? p.good : p.warn);
        }
        tile (5, "NOISE (NC / NR)", sum.noiseKnown ? "NC " + juce::String (juce::roundToInt (sum.nc)) + " / NR " + juce::String (juce::roundToInt (sum.nr)) : juce::String ("-"),
              "", ! sum.noiseKnown ? p.textDim : sum.nc <= 35.0 ? p.good : p.warn);

        // measuring
        {
            juce::String t = status;
            if (t.isEmpty())
                t = fromSweeps ? juce::String (sweptCount()) + " of " + juce::String (points().size()) + " ticked points swept."
                               : room.acousticsCount() > 0 ? juce::String ("Only an estimate so far (from the Room EQ pink noise). Measure with a sweep for the real numbers.")
                                                           : juce::String ("Not measured yet: press Measure with a sweep.");
            g.setColour (phase != Phase::Idle ? p.accent : fromSweeps ? p.textDim : p.warn);
            g.setFont (font (11.5f, phase != Phase::Idle));
            g.drawFittedText (t, sweepInfo, juce::Justification::topLeft, 2);
        }

        // the points: tick to sweep there, with what came back
        drawPanel (g, posCard.toFloat(), "Listening points");
        for (int i = 0; i < proc.positions.size(); ++i)
        {
            const auto row = pointRow (i);
            if (row.getBottom() > pointsList.getBottom()) break;
            const auto& name = proc.positions[i];
            const bool on = ! room.acSkipped.contains (name);
            auto r = row.reduced (6, 0);
            const auto box = r.removeFromLeft (16).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            g.setColour (on ? positionColour (i) : p.textDim);
            if (on)
            {
                g.fillRoundedRectangle (box, 3.0f);
                juce::Path tick;
                tick.startNewSubPath (box.getX() + 3.0f, box.getCentreY());
                tick.lineTo (box.getX() + 6.0f, box.getBottom() - 3.5f);
                tick.lineTo (box.getRight() - 3.0f, box.getY() + 3.5f);
                g.setColour (p.bg);
                g.strokePath (tick, juce::PathStrokeType (1.8f));
            }
            else g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.3f);
            r.removeFromLeft (8);
            const acoustics::Result* res = nullptr;
            for (const auto& w : room.sweeps) if (w.position == name && w.ac.valid) res = &w.ac;
            g.setColour (p.text);
            g.setFont (font (12.0f, true));
            g.drawText (name, r.removeFromLeft (110), juce::Justification::centredLeft, true);
            g.setFont (monoFont (11.0f));
            g.setColour (res != nullptr ? p.textDim : p.textFaint);
            g.drawText (res != nullptr ? "RT " + num (res->rtMid(), 2) + "  STI " + num (res->sti, 2) : juce::String (on ? "to sweep" : "left out"), r, juce::Justification::centredLeft, true);
        }

        // the details
        paintDetails (g, sum, rtLo, rtHi);

        // the decision tree
        drawPanel (g, treeCard.toFloat(), "What it means");
        paintTree (g, avg, rtLo, rtHi, stiMin);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto top = r.removeFromTop (70);
        const int tw = (top.getWidth() - 5 * 8) / 6;
        for (int i = 0; i < 6; ++i) { tiles[(size_t) i] = top.removeFromLeft (tw); top.removeFromLeft (8); }
        r.removeFromTop (10);
        auto right = r.removeFromRight (420);
        r.removeFromRight (10);
        detailsCard = r.removeFromBottom (juce::jmin (232, r.getHeight() / 2));
        r.removeFromBottom (8);
        auto bar = r.removeFromBottom (32);
        r.removeFromBottom (6);
        chart.setBounds (r);
        const int bw = 58;
        for (auto& b : metricButtons) { b.setBounds (bar.removeFromLeft (bw).reduced (0, 2)); bar.removeFromLeft (3); }
        bar.removeFromLeft (10);
        sizeButton.setBounds (bar.removeFromRight (120).reduced (0, 2));
        bar.removeFromRight (6);
        useBox.setBounds (bar.removeFromRight (juce::jmin (300, bar.getWidth())).reduced (0, 2));
        sweepButton.setBounds (right.removeFromTop (34));
        right.removeFromTop (6);
        sweepInfo = right.removeFromTop (34);
        right.removeFromTop (6);
        const int listH = 36 + juce::jmax (1, proc.positions.size()) * 26 + 44;
        posCard = right.removeFromTop (juce::jmin (listH, right.getHeight() / 2));
        {
            auto c = posCard.reduced (12, 0).withTrimmedTop (34);
            planButton.setBounds (c.removeFromBottom (40).withSizeKeepingCentre (c.getWidth(), 28));
            pointsList = c;
        }
        right.removeFromTop (10);
        treeCard = right;
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (qArea.contains (e.getPosition())) { pickDirectivity(); return; }
        for (int i = 0; i < proc.positions.size(); ++i)
            if (pointRow (i).contains (e.getPosition()) && e.x < pointRow (i).getX() + 30)
            {
                const auto& name = proc.positions[i];
                if (proc.room.acSkipped.contains (name)) proc.room.acSkipped.removeString (name);
                else proc.room.acSkipped.add (name);
                changed();
                return;
            }
    }

private:
    enum class Phase { Idle, Lead, Recording, Working };
    double sweepSec = 4.0;   // Settings, Generator and safety
    struct Saved { int type = 0; float sweepSec = 3.0f; int output = 0; bool on = false; } saved;

    juce::Rectangle<int> pointRow (int i) const { return { pointsList.getX(), pointsList.getY() + i * 26, pointsList.getWidth(), 24 }; }

    void refreshButtons()
    {
        sweepButton.setButtonText (phase != Phase::Idle ? juce::String ("Stop")
                                   : sweptCount() >= points().size() && ! points().isEmpty() ? juce::String ("Measure again with a sweep") : juce::String ("Measure with a sweep"));
        sweepButton.setColour (juce::TextButton::buttonColourId, phase != Phase::Idle ? pal().bad : pal().accent);
        sweepButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
    }

    void changed() { refreshButtons(); repaint(); if (onChanged) onChanged(); }

    // The decision tree, one node per question, each with the numbers and a plain sentence.
    void paintTree (juce::Graphics& g, const acoustics::Result& avg, double rtLo, double rtHi, double stiMin)
    {
        const auto& p = pal();
        const auto& room = proc.room;
        auto r = treeCard.reduced (12, 0).withTrimmedTop (34);
        struct Node { int state; juce::String title, text; };   // state 0 to do, 1 fine, 2 a look, 3 a problem
        std::vector<Node> nodes;
        const bool swept = room.acousticsFromSweeps(), any = room.acousticsCount() > 0;
        const double rt = avg.rtMid();
        nodes.push_back ({ swept ? 1 : any ? 2 : 0, "1  Measured?",
                           swept ? "Sweeps at " + juce::String (sweptCount()) + " point(s)." : any ? "Only an estimate from the Room EQ pink noise: sweep for the real numbers."
                                                                                                    : "Not yet: Measure with a sweep at each ticked point." });
        if (any)
        {
            // 2 usable: enough decay above the noise
            double worst = 200.0;
            for (const auto& w : room.sweeps) if (w.ac.valid && std::isfinite (w.ac.band[2].rangeDb)) worst = std::min (worst, w.ac.band[2].rangeDb);
            const bool noisy = worst < 30.0;
            nodes.push_back ({ noisy ? 2 : 1, "2  Usable?", noisy ? "Only " + juce::String (juce::roundToInt (worst)) + " dB of decay above the noise: sweep louder (or quieter room) and measure again."
                                                                  : "Clean enough: the decay stands well above the noise." });
            // 3 reverberation against the room's use, the lows against the mids
            if (std::isfinite (rt))
            {
                const double low = 0.5 * ((std::isfinite (avg.band[0].rt()) ? avg.band[0].rt() : rt) + (std::isfinite (avg.band[1].rt()) ? avg.band[1].rt() : rt));
                const double bass = rt > 0.05 ? low / rt : 1.0;
                const double hiRt = std::isfinite (avg.band[5].rt()) ? avg.band[5].rt() : rt;
                juce::String t = "RT " + juce::String (rt, 2) + " s (" + juce::String (rtLo, 1) + " - " + juce::String (rtHi, 1) + " s is usual for "
                                 + RoomTuning::useNames()[room.use].toLowerCase() + ").";
                int st = 1;
                if (bass > 1.3) { t << " The lows ring " << juce::String (bass, 1) << "x longer than the mids: bass build-up. Bass traps, sub placement or a cardioid sub array; EQ won't shorten the decay."; st = 2; }
                if (rt > rtHi * 1.15) { t << " Long in the mids and highs: absorption on the side and back walls; keep the PA off the walls."; st = std::max (st, rt > rtHi * 1.5 ? 3 : 2); }
                else if (rt < rtLo * 0.85) t << " Drier than usual: fine for the PA.";
                if (hiRt > rt * 1.2) t << " The highs ring longer than the mids: hard, bare surfaces.";
                const auto modes = room.modes (room.count (true) > 0 && room.count (false) == 0);
                if (! modes.empty()) t << " Room modes at " << freqLabel (modes.front().freq) << " Hz" << (modes.size() > 1 ? " and more" : "") << ": cut gently; moving the subs changes them.";
                const auto sum = summariseAcoustics (proc);
                if (sum.ringingThird >= 0) { t << " The " << freqLabel (acoustics::thirds()[(size_t) sum.ringingThird]) << " Hz third octave rings " << juce::String (sum.ringingRatio, 1) << "x its neighbours."; st = std::max (st, 2); }
                if (std::isfinite (sum.schroederHz)) t << " Below about " << juce::String (juce::roundToInt (sum.schroederHz)) << " Hz (the Schroeder frequency) single room modes rule: EQ there only helps where most seats agree.";
                else t << " Set the Room size for the Schroeder frequency and the critical distance.";
                nodes.push_back ({ st, "3  Reverberation", t });
            }
            // 4 speech, and late energy
            {
                const auto sum = summariseAcoustics (proc);
                const double edt = avg.mid (&acoustics::Band::edt), sti = std::isfinite (sum.stiCorrected) ? sum.stiCorrected : avg.sti;
                juce::String t;
                int st = 1;
                if (std::isfinite (sti))
                {
                    t << "STI " << juce::String (sti, 2) << (std::isfinite (sum.stiCorrected) ? (sum.noiseKnown ? " with the room noise" : " at speech level") : juce::String())
                      << " (" << acoustics::stiRating (sti) << ", " << juce::String (sum.alcons, 1) << "% ALcons; aim for " << juce::String (stiMin, 2) << " or more).";
                    if (sti < stiMin) st = sti < stiMin - 0.1 ? 3 : 2;
                    if (std::isfinite (sum.stiCorrected) && sum.noiseKnown && avg.sti - sum.stiCorrected > 0.05)
                        t << " The noise costs " << juce::String (avg.sti - sum.stiCorrected, 2) << ": quieten the HVAC or raise the speech level.";
                }
                if (sum.echo)
                {
                    const bool feet = proc.settings.distanceUnit.load() == 0;
                    t << " An echo " << juce::String (juce::roundToInt (sum.echoMs)) << " ms after the direct sound (its path about "
                      << (feet ? juce::String (juce::roundToInt (sum.echoExtraM * 3.28084)) + " ft" : juce::String (juce::roundToInt (sum.echoExtraM)) + " m") << " longer): EQ can't fix it; treat that surface or aim the PA off it.";
                    st = std::max (st, 2);
                }
                if (std::isfinite (sum.flutterMs))
                {
                    t << " A flutter echo every " << juce::String (juce::roundToInt (sum.flutterMs)) << " ms: two parallel surfaces; absorb or diffuse one.";
                    st = std::max (st, 2);
                }
                if (std::isfinite (edt) && std::isfinite (rt) && rt > edt * 1.4)
                {
                    t << " The decay starts fast and ends slow (EDT " << juce::String (edt, 2) << " s against RT " << juce::String (rt, 2) << " s): late reflections, "
                      << "an echo from a far wall or a coupled space (a balcony, a foyer). Listen for it at the back.";
                    st = std::max (st, 2);
                }
                if (t.isEmpty()) t = "Not rated.";
                nodes.push_back ({ st, "4  Speech and echoes", t });
            }
        }
        juce::String why;
        const int verdict = room.acousticsVerdict (why);
        nodes.push_back ({ verdict == 0 ? 0 : verdict == 1 ? 1 : verdict == 2 ? 2 : 3, "5  So:", why + (verdict > 0 && ! swept ? " (from the estimate)" : juce::String()) });

        for (size_t k = 0; k < nodes.size(); ++k)
        {
            const auto& n = nodes[k];
            const int lines = n.text.length() > 150 ? 4 : n.text.length() > 90 ? 3 : 2;
            auto row = r.removeFromTop (20 + lines * 15);
            if (row.getBottom() > treeCard.getBottom() - 6) break;
            const auto badge = row.removeFromLeft (22).toFloat().withHeight (22.0f).withSizeKeepingCentre (16.0f, 16.0f);
            drawStatusBadge (g, badge, n.state == 1 ? StepStatus::Ready : n.state == 0 ? StepStatus::NotStarted : StepStatus::Attention);
            if (n.state == 3) { g.setColour (p.bad); g.fillEllipse (badge); g.setColour (p.onAccent); g.setFont (font (12.0f, true)); g.drawText ("!", badge, juce::Justification::centred); }
            row.removeFromLeft (6);
            g.setColour (p.text);
            g.setFont (font (12.5f, true));
            g.drawText (n.title, row.removeFromTop (18), juce::Justification::centredLeft, true);
            g.setColour (n.state == 0 ? p.textDim : n.state == 1 ? p.textDim : n.state == 2 ? p.warn : p.bad);
            g.setFont (font (11.5f, k + 1 == nodes.size()));
            g.drawFittedText (n.text, row, juce::Justification::topLeft, lines);
            // the line down to the next node
            if (k + 1 < nodes.size())
            {
                g.setColour (p.border);
                g.drawVerticalLine (treeCard.getX() + 12 + 11, (float) row.getBottom() - 2.0f, (float) row.getBottom() + 6.0f);
            }
            r.removeFromTop (6);
        }
        g.setColour (p.textFaint);
        g.setFont (font (10.5f));
        g.drawFittedText ("From the system's own sweeps (ISO 3382 / IEC 60268-16 methods); a full acoustic survey uses an omnidirectional source.",
                          treeCard.reduced (12, 8).removeFromBottom (28), juce::Justification::bottomLeft, 2);
    }

    void restoreGenerator()
    {
        auto& st = proc.settings;
        st.genOn = false;
        st.genType = saved.type;
        st.genSweepSec = saved.sweepSec;
        st.genOutput = saved.output;
    }

    double rate() const { return proc.getSampleRate() > 1000.0 ? proc.getSampleRate() : 48000.0; }

    void timerCallback() override
    {
        const double el = juce::Time::getMillisecondCounterHiRes() - phaseStart;
        if (phase == Phase::Lead && el > 300.0)
        {
            // two sweeps and the silence after each: the second is clean of the start-up
            const double period = sweepSec + 0.5;
            proc.recorder.start ((int) (2.0 * period * rate() + 0.3 * rate()), proc.settings.micCount.load());
            phase = Phase::Recording;
        }
        else if (phase == Phase::Recording && proc.recorder.done())
        {
            restoreGenerator();
            phase = Phase::Working;
            status = "Working out each mic's impulse response...";
            // the deconvolution is a few large FFTs: off the message thread
            struct Job { std::vector<std::vector<float>> mics; std::vector<float> ref; double fs; int n; std::vector<juce::String> where, names; };
            auto job = std::make_shared<Job>();
            const auto& rec = proc.recorder;
            job->fs = rate();
            job->n = rec.length;
            job->ref = rec.ref;
            for (int m = 0; m < rec.numMics; ++m)
                if (proc.mics.position[(size_t) m].isNotEmpty())
                {
                    job->mics.push_back (rec.mic[(size_t) m]);
                    job->where.push_back (proc.mics.position[(size_t) m]);
                    job->names.push_back (proc.mics.nameOf (m));
                }
            juce::Component::SafePointer<AcousticsPage> safe (this);
            std::thread ([job, safe]
            {
                auto results = std::make_shared<std::vector<acoustics::Result>>();
                for (const auto& x : job->mics)
                    results->push_back (acoustics::analyse (deconv::impulse (job->ref.data(), x.data(), job->n, job->fs, 3.5), job->fs));
                juce::MessageManager::callAsync ([safe, job, results] { if (safe != nullptr) safe->sweepDone (job->where, job->names, *results); });
            }).detach();
        }
        else if (phase == Phase::Recording && el > (2.0 * (sweepSec + 0.5) + 6.0) * 1000.0)
        {
            stopSweep ("The recording did not finish: check the audio device.");
            return;
        }
        if (phase == Phase::Lead || phase == Phase::Recording) { if (! proc.settings.genOn.load()) { stopSweep ("Stopped: the generator was turned off."); return; } }
        repaint (sweepInfo.expanded (2));
    }

public:
    void sweepDone (const std::vector<juce::String>& where, const std::vector<juce::String>& names, const std::vector<acoustics::Result>& results)
    {
        stopTimer();
        phase = Phase::Idle;
        juce::StringArray done, failed;
        auto& sw = proc.room.sweeps;
        for (size_t k = 0; k < results.size() && k < where.size(); ++k)
        {
            if (! results[k].valid || ! std::isfinite (results[k].rtMid())) { failed.add (where[k]); continue; }
            sw.erase (std::remove_if (sw.begin(), sw.end(), [&] (const SweepMeasurement& w) { return w.position == where[k]; }), sw.end());
            sw.push_back ({ where[k], names[k], juce::Time::getCurrentTime().formatted ("%H:%M"), results[k] });
            done.add (where[k] + " " + juce::String (results[k].rtMid(), 2) + " s");
        }
        status = done.isEmpty() ? juce::String ("Nothing clear came back: raise the level or reduce the noise, and check the reference.")
                                : "RT60 " + done.joinIntoString (", ") + "." + (failed.isEmpty() ? juce::String() : " Not clear at " + failed.joinIntoString (", ") + ".");
        if (! done.isEmpty()) { ++serial; redo.clear(); }
        changed();
        chart.repaint();
    }

private:
    LiveAlignProcessor& proc;
    AcousticsChart chart;
    juce::TextButton sweepButton { "Measure with a sweep" }, planButton;
    Phase phase = Phase::Idle;
    double phaseStart = 0.0;
    juce::String status, redo;
    int serial = 0;
    juce::Rectangle<int> sweepInfo, pointsList;
    juce::TextButton metricButtons[AcousticsChart::kNumMetrics];
    juce::TextButton sizeButton { "Room size..." };
    juce::Rectangle<int> detailsCard, qArea;
    juce::ComboBox useBox;
    std::array<juce::Rectangle<int>, 6> tiles;
    juce::Rectangle<int> posCard, treeCard;

    // ---- the details: reverberation, speech and reflections, the room ----
    void paintDetails (juce::Graphics& g, const AcousticsSummary& a, double rtLo, double rtHi)
    {
        const auto& p = pal();
        drawPanel (g, detailsCard.toFloat(), "Details");
        auto r = detailsCard.reduced (14, 0).withTrimmedTop (32).withTrimmedBottom (8);
        const int colW = r.getWidth() / 3;
        auto num = [] (double v, int d) { return ! std::isfinite (v) ? juce::String ("-") : d == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, d); };
        const bool feet = proc.settings.distanceUnit.load() == 0;
        auto dist = [feet] (double m) { return ! std::isfinite (m) ? juce::String ("-") : feet ? juce::String (juce::roundToInt (m * 3.28084)) + " ft" : juce::String (m, 1) + " m"; };
        qArea = {};
        auto column = [&] (juce::Rectangle<int> c, const juce::String& title, const std::vector<std::tuple<juce::String, juce::String, juce::Colour>>& rows)
        {
            g.setColour (p.textDim);
            g.setFont (font (10.5f, true));
            g.drawText (title.toUpperCase(), c.removeFromTop (18), juce::Justification::centredLeft);
            for (const auto& [k, v, col] : rows)
            {
                if (c.getHeight() < 16) break;
                auto line = c.removeFromTop (17);
                g.setColour (p.textDim);
                g.setFont (font (11.5f));
                g.drawText (k, line.removeFromLeft (line.getWidth() * 48 / 100), juce::Justification::centredLeft, true);
                g.setColour (col);
                g.setFont (monoFont (11.0f));
                g.drawText (v, line, juce::Justification::centredLeft, true);
            }
            return c;
        };
        const auto text = p.text, warn = p.warn, dim = p.textFaint;
        // reverberation
        {
            std::vector<std::tuple<juce::String, juce::String, juce::Colour>> rows;
            rows.push_back ({ "RT 500 Hz - 1 kHz", num (a.rtMid, 2) + " s  (" + juce::String (rtLo, 1) + " - " + juce::String (rtHi, 1) + ")", text });
            rows.push_back ({ "EDT", num (a.edtMid, 2) + " s", text });
            rows.push_back ({ "RT at 63 Hz", std::isfinite (a.rt63) ? num (a.rt63, 2) + " s" + (a.rt63Reliable ? juce::String() : juce::String ("  (unsure)")) : juce::String ("-"),
                              a.rt63Reliable ? text : dim });
            rows.push_back ({ "Bass ratio (warmth)", num (a.bassRatio, 2) + "  (1.0 - 1.3)", std::isfinite (a.bassRatio) && a.bassRatio > 1.3 ? warn : text });
            rows.push_back ({ "Treble ratio", num (a.trebleRatio, 2) + "  (0.8 - 1.0)", std::isfinite (a.trebleRatio) && (a.trebleRatio > 1.1 || a.trebleRatio < 0.7) ? warn : text });
            rows.push_back ({ "Ringing 1/3 octave", a.ringingThird >= 0 ? freqLabel (acoustics::thirds()[(size_t) a.ringingThird]) + " Hz, " + juce::String (a.ringingRatio, 1) + "x" : juce::String ("none"),
                              a.ringingThird >= 0 ? warn : text });
            for (const auto& sp : a.spread)
                if (sp.what.startsWith ("RT") && sp.s.n > 1)
                    rows.push_back ({ "RT across points", juce::String (sp.s.lo, 2) + " - " + juce::String (sp.s.hi, 2) + " s", text });
            column (r.removeFromLeft (colW).reduced (0, 0).withTrimmedRight (12), "Reverberation", rows);
        }
        // speech and reflections
        {
            std::vector<std::tuple<juce::String, juce::String, juce::Colour>> rows;
            rows.push_back ({ "STI (room only)", num (a.stiRaw, 2), text });
            rows.push_back ({ a.noiseKnown ? "STI with noise" : "STI at the level", std::isfinite (a.stiCorrected) ? num (a.stiCorrected, 2) + "  (" + juce::String (juce::roundToInt (proc.pref ("speechDbA"))) + " dBA)" : juce::String ("-"), text });
            rows.push_back ({ "%ALcons", std::isfinite (a.alcons) ? num (a.alcons, 1) + " %  " + acoustics::alconsRating (a.alcons) : juce::String ("-"), std::isfinite (a.alcons) && a.alcons > 11.0 ? warn : text });
            rows.push_back ({ "Centre time Ts", num (a.tsMid, 0) + " ms", text });
            rows.push_back ({ "First reflection", std::isfinite (a.itdgMs) ? num (a.itdgMs, 1) + " ms after the direct" : juce::String ("-"), text });
            rows.push_back ({ "Echo", a.echo ? num (a.echoMs, 0) + " ms, " + dist (a.echoExtraM) + " further" : juce::String ("none heard"), a.echo ? p.bad : text });
            rows.push_back ({ "Flutter echo", std::isfinite (a.flutterMs) ? "every " + num (a.flutterMs, 0) + " ms (" + dist (a.flutterSpacingM) + " apart)" : juce::String ("none"),
                              std::isfinite (a.flutterMs) ? warn : text });
            column (r.removeFromLeft (colW).withTrimmedRight (12), "Speech and reflections", rows);
        }
        // the room
        {
            std::vector<std::tuple<juce::String, juce::String, juce::Colour>> rows;
            const double v = a.volume;
            rows.push_back ({ "Volume", v > 0.0 ? (feet ? juce::String (juce::roundToInt (v * 35.3147)) + " cu ft" : juce::String (juce::roundToInt (v)) + " m3") : juce::String ("set Room size"), v > 0.0 ? text : dim });
            rows.push_back ({ "Schroeder frequency", std::isfinite (a.schroederHz) ? num (a.schroederHz, 0) + " Hz" : juce::String ("-"), text });
            auto c = r.withTrimmedRight (0);
            auto rest = column (c, "Room and coverage", rows);
            // critical distance, per speaker (click to set a speaker's directivity)
            if (! a.criticalDistance.empty())
            {
                qArea = rest.removeFromTop (juce::jmin (rest.getHeight(), 17 * (int) juce::jmin ((size_t) 3, a.criticalDistance.size()) + 2));
                auto q = qArea;
                for (size_t i = 0; i < a.criticalDistance.size() && i < 3 && q.getHeight() >= 16; ++i)
                {
                    auto line = q.removeFromTop (17);
                    g.setColour (p.textDim);
                    g.setFont (font (11.5f));
                    g.drawText (i == 0 ? juce::String ("Critical distance") : juce::String(), line.removeFromLeft (line.getWidth() * 48 / 100), juce::Justification::centredLeft, true);
                    g.setColour (p.accent);
                    g.setFont (monoFont (11.0f));
                    g.drawText (a.criticalDistance[i].name + " " + dist (a.criticalDistance[i].metres) + " (Q " + juce::String (juce::roundToInt (a.criticalDistance[i].q)) + ")", line,
                                juce::Justification::centredLeft, true);
                }
            }
            auto line = rest.removeFromTop (17);
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawText ("Coverage 2 / 4 kHz", line.removeFromLeft (line.getWidth() * 48 / 100), juce::Justification::centredLeft, true);
            const double cov = std::max (std::isfinite (a.coverage2k) ? a.coverage2k : 0.0, std::isfinite (a.coverage4k) ? a.coverage4k : 0.0);
            g.setColour (! std::isfinite (a.coverage2k) ? dim : cov <= 6.0 ? p.good : warn);
            g.setFont (monoFont (11.0f));
            const auto pm = juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"));
            g.drawText (std::isfinite (a.coverage2k) ? pm + num (a.coverage2k / 2.0, 1) + " / " + pm + num (a.coverage4k / 2.0, 1) + " dB (spec " + pm + "3)" : juce::String ("needs Room EQ points"),
                        line, juce::Justification::centredLeft, true);
            line = rest.removeFromTop (17);
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            g.drawText ("Points", line.removeFromLeft (line.getWidth() * 48 / 100), juce::Justification::centredLeft, true);
            g.setColour (text);
            g.setFont (monoFont (11.0f));
            juce::String sp;
            for (const auto& row : a.spread)
                if (row.what == "STI" && row.s.n > 1) sp = "STI " + juce::String (row.s.lo, 2) + " - " + juce::String (row.s.hi, 2);
            g.drawText (juce::String (a.points) + (a.points == 1 ? " point" : " points") + (sp.isNotEmpty() ? ", " + sp : juce::String()), line, juce::Justification::centredLeft, true);
        }
    }

    // Length, width and height (or the volume), in the distance unit of Settings.
    void askRoomSize()
    {
        const bool feet = proc.settings.distanceUnit.load() == 0;
        const double k = feet ? 3.28084 : 1.0;
        auto* w = new juce::AlertWindow ("Room size", juce::String ("The room's inside size, in ") + (feet ? "feet" : "metres")
                                         + ". Or type its volume if it is not a box (it wins over the three sizes). Leave a box empty if you do not know it.",
                                         juce::MessageBoxIconType::NoIcon, this);
        auto fmt = [] (double v) { return v > 0.0 ? juce::String (v, 1) : juce::String(); };
        const auto& pl = proc.plan;
        w->addTextEditor ("l", fmt (pl.lengthM * k), "Length (" + juce::String (feet ? "ft" : "m") + ")");
        w->addTextEditor ("w", fmt (pl.widthM * k), "Width");
        w->addTextEditor ("h", fmt (pl.heightM * k), "Ceiling height (average)");
        w->addTextEditor ("v", pl.volumeM3 > 0.0 ? juce::String (juce::roundToInt (pl.volumeM3 * k * k * k)) : juce::String(), juce::String ("Volume (") + (feet ? "cu ft" : "m3") + ", optional)");
        w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<AcousticsPage> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w, k] (int result)
        {
            if (safe != nullptr && result == 1)
            {
                auto val = [w] (const char* id) { return juce::jmax (0.0, w->getTextEditorContents (id).trim().getDoubleValue()); };
                auto& pl2 = safe->proc.plan;
                pl2.lengthM = val ("l") / k;
                pl2.widthM = val ("w") / k;
                pl2.heightM = val ("h") / k;
                pl2.volumeM3 = val ("v") / (k * k * k);
                safe->changed();
            }
        }), true);
    }

    // A speaker's directivity factor for its critical distance.
    void pickDirectivity()
    {
        juce::PopupMenu m;
        int id = 1;
        std::vector<std::pair<int, double>> ids;
        for (int i = 0; i < (int) proc.system.sources.size(); ++i)
        {
            const auto& s = proc.system.sources[(size_t) i];
            if (s.role == (int) SpeakerRole::Sub) continue;
            juce::PopupMenu sub;
            sub.addItem (id, "Typical for a " + speakerRoleNames()[s.role].toLowerCase() + " (Q " + juce::String (juce::roundToInt (SpeakerSource::typicalQ (s.role))) + ")", true, s.directivityQ <= 0.0);
            ids.push_back ({ i, 0.0 });
            ++id;
            for (double q : { 2.0, 5.0, 8.0, 10.0, 15.0, 20.0, 30.0 })
            {
                sub.addItem (id, "Q " + juce::String (juce::roundToInt (q)), true, std::abs (s.directivityQ - q) < 0.01);
                ids.push_back ({ i, q });
                ++id;
            }
            m.addSubMenu (proc.system.nameOf (i), sub);
        }
        juce::Component::SafePointer<AcousticsPage> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (localAreaToGlobal (qArea)), [safe, ids] (int r)
        {
            if (safe == nullptr || r <= 0 || r > (int) ids.size()) return;
            safe->proc.system.sources[(size_t) ids[(size_t) r - 1].first].directivityQ = ids[(size_t) r - 1].second;
            safe->changed();
        });
    }
};
} // namespace la::ui
