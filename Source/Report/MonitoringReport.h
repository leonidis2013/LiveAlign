// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The live monitoring report: a PDF of an event monitored with LiveAlign. A cover with the contents, then: the sound
// level over the event (LAeq, LCeq, the rolling LAeq against the limit, the loudest moments), the monitoring mic and
// its calibration with the system as it was set, the venue plan with the monitoring mic (landscape), and the
// transfer functions and RTA traces captured during the event.
#include "TuningReport.h"

namespace la::report
{
struct MonitoringSection { const char* id; const char* title; const char* what; };
inline const std::vector<MonitoringSection>& monitorSections()
{
    static const std::vector<MonitoringSection> s {
        { "level", "Sound level", "LAeq and LCeq over the event, the rolling LAeq against the limit, the loudest moments" },
        { "setup", "Monitoring mic and system", "The monitoring mic, its calibration and delay, the target and the speaker settings" },
        { "plan", "Venue plan", "Where the monitoring mic was (a landscape page)" },
        { "traces", "Captured traces", "Transfer functions and RTA captured during the event" } };
    return s;
}

namespace detail
{
// The sound level log as a chart: every 5 s (thin), the rolling LAeq (thick) and the limit (dashed).
inline juce::Image levelChart (const SplLog& log, int w = 1700, int h = 640)
{
    juce::Image img (juce::Image::RGB, w, h, true);
    juce::Graphics g (img);
    g.fillAll (juce::Colours::white);
    const auto plot = juce::Rectangle<float> (80.0f, 24.0f, (float) w - 110.0f, (float) h - 90.0f);
    if (log.points.size() < 2)
    {
        g.setColour (dim);
        g.setFont (juce::FontOptions (28.0f));
        g.drawText ("Not enough of the event was logged.", plot, juce::Justification::centred);
        return img;
    }
    const double t0 = log.start(), t1 = log.end();
    float lo = 1e9f, hi = -1e9f;
    for (const auto& p : log.points) { lo = std::min (lo, p.la); hi = std::max (hi, std::max (p.la, p.laMax)); }
    if (log.limitLaeq > 0.0) hi = std::max (hi, (float) log.limitLaeq);
    const float top = std::ceil ((hi + 3.0f) / 10.0f) * 10.0f, bot = std::floor ((lo - 3.0f) / 10.0f) * 10.0f;
    auto xAt = [&] (double t) { return plot.getX() + (float) ((t - t0) / std::max (1.0, t1 - t0)) * plot.getWidth(); };
    auto yAt = [&] (double db) { return plot.getY() + (float) ((top - db) / std::max (1.0f, top - bot)) * plot.getHeight(); };
    g.setFont (juce::FontOptions (22.0f));
    for (float db = bot; db <= top + 0.1f; db += 10.0f)
    {
        g.setColour (juce::Colour (0xffe3e8ee));
        g.drawHorizontalLine ((int) yAt (db), plot.getX(), plot.getRight());
        g.setColour (dim);
        g.drawText (juce::String ((int) db), juce::Rectangle<float> (10.0f, yAt (db) - 12.0f, 62.0f, 24.0f), juce::Justification::centredRight);
    }
    // time ticks every 15 or 30 minutes
    const double span = t1 - t0, step = span > 3 * 3600 ? 3600.0 : span > 5400 ? 1800.0 : span > 1800 ? 900.0 : 300.0;
    for (double t = std::ceil (t0 / step) * step; t <= t1; t += step)
    {
        g.setColour (juce::Colour (0xffe3e8ee));
        g.drawVerticalLine ((int) xAt (t), plot.getY(), plot.getBottom());
        g.setColour (dim);
        g.drawText (juce::Time ((juce::int64) (t * 1000.0)).formatted ("%H:%M"), juce::Rectangle<float> (xAt (t) - 50.0f, plot.getBottom() + 8.0f, 100.0f, 26.0f),
                    juce::Justification::centred);
    }
    g.setColour (juce::Colour (0xffc5ccd6));
    g.drawRect (plot, 1.5f);
    g.setColour (dim);
    g.drawText ("dB (A)", juce::Rectangle<float> (plot.getX(), (float) h - 36.0f, 300.0f, 26.0f), juce::Justification::centredLeft);

    juce::Path thin, mx, roll;
    const auto r = log.rollingLaeq();
    for (size_t i = 0; i < log.points.size(); ++i)
    {
        const float x = xAt (log.points[i].t);
        if (i == 0) { thin.startNewSubPath (x, yAt (log.points[i].la)); mx.startNewSubPath (x, yAt (log.points[i].laMax)); roll.startNewSubPath (x, yAt (r[i])); }
        else { thin.lineTo (x, yAt (log.points[i].la)); mx.lineTo (x, yAt (log.points[i].laMax)); roll.lineTo (x, yAt (r[i])); }
    }
    g.setColour (juce::Colour (0xffd2478f).withAlpha (0.35f));
    g.strokePath (mx, juce::PathStrokeType (1.5f));
    g.setColour (juce::Colour (0xff2563d6).withAlpha (0.55f));
    g.strokePath (thin, juce::PathStrokeType (1.8f));
    g.setColour (accent);
    g.strokePath (roll, juce::PathStrokeType (4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (log.limitLaeq > 0.0)
    {
        juce::Path lim, dashed;
        lim.startNewSubPath (plot.getX(), yAt (log.limitLaeq));
        lim.lineTo (plot.getRight(), yAt (log.limitLaeq));
        const float dashes[] = { 16.0f, 10.0f };
        juce::PathStrokeType (3.0f).createDashedStroke (dashed, lim, dashes, 2);
        g.setColour (bad);
        g.fillPath (dashed);
    }
    return img;
}
} // namespace detail

inline juce::String writeMonitoringReport (LiveAlignProcessor& p, const juce::File& file, const juce::Image& plan, const ReportOptions& opt)
{
    using namespace detail;
    pdf::Document doc;
    const auto& sessions = p.getSessions();
    const int cur = p.currentSession();
    const auto sessionName = cur < (int) sessions.size() ? sessions[(size_t) cur].name : juce::String ("Event");
    Layout L { doc, p.venueName + "  -  " + sessionName + "  -  Monitoring report  -  LiveAlign " + JucePlugin_VersionString };
    const auto& log = p.splLog;
    const auto& mm = p.monitorMic;
    auto has = [&opt] (const char* id) { return included (opt.skip, id); };
    auto n1 = [] (double v) { return std::isfinite (v) ? juce::String (v, 1) : juce::String ("-"); };

    L.y = cover (L, "LIVE MONITORING REPORT", p.venueName, sessionName + "   -   " + LiveAlignProcessor::today());
    {
        std::vector<std::pair<juce::String, juce::String>> kv;
        if (opt.author.isNotEmpty()) kv.push_back ({ "Prepared by", opt.author });
        kv.push_back ({ "Monitoring mic", mm.name + (mm.position.isNotEmpty() ? " at " + mm.position : juce::String()) });
        if (! log.empty())
            kv.push_back ({ "Logged", juce::Time ((juce::int64) (log.start() * 1000.0)).formatted ("%d %b %Y %H:%M") + " - "
                                          + juce::Time ((juce::int64) (log.end() * 1000.0)).formatted ("%H:%M") });
        kv.push_back ({ "Levels", mm.splCalibrated ? juce::String ("dB SPL (the mic was calibrated)") : juce::String ("dBFS: the mic was not calibrated") });
        L.keyValues (kv);
    }
    const float contentsY = L.y + 14.0f;

    if (has ("level"))
    {
        L.chapter ("Sound level");
        if (log.empty()) L.para ("The sound level was not logged in this session (it is logged while a Monitoring page is open).", 9.5f, dim);
        else
        {
            const double la = log.leq (false), lc = log.leq (true);
            const auto rolling = log.rollingLaeq();
            double peakRoll = -1e9;
            for (double v : rolling) peakRoll = std::max (peakRoll, v);
            L.tiles ({ { "LAeq (whole event)", n1 (la), "dB" }, { "LCeq", n1 (lc), "dB" }, { "LC - LA", n1 (lc - la), "dB" },
                       { "Highest " + juce::String (log.limitMinutes) + " min " + log.limitName(), n1 (peakRoll), "dB" }, { "Max LAF", n1 (log.maxLaf()), "dB" } });
            if (log.limitLaeq > 0.0)
                L.para ("Limit: " + juce::String (juce::roundToInt (log.limitLaeq)) + " " + log.limitUnit() + " " + log.limitName() + " over " + juce::String (log.limitMinutes) + " minutes. Over the limit for "
                        + juce::String (juce::roundToInt (log.minutesOver())) + " minutes of the event.", 9.5f, log.minutesOver() > 0.0 ? warn : ink);
            L.need (300.0f);
            L.picture (levelChart (log), 290.0f);
            L.para ("Blue: LAeq every " + juce::String (juce::roundToInt (log.interval)) + " seconds. Teal: the rolling " + juce::String (log.limitMinutes) + "-minute "
                    + log.limitName() + ". Pink: the loudest moment (LAF max) in each " + juce::String (juce::roundToInt (log.interval)) + " seconds." + juce::String (log.limitLaeq > 0.0 ? " Red dashes: the limit." : ""), 8.0f, dim);
            // the loudest 15-minute windows (quarter hours)
            std::vector<juce::StringArray> rows;
            const double q = 900.0;
            for (double t = std::floor (log.start() / q) * q; t < log.end(); t += q)
            {
                const double v = log.leq (false, t, t + q);
                if (! std::isfinite (v)) continue;
                rows.push_back ({ juce::Time ((juce::int64) (t * 1000.0)).formatted ("%H:%M") + " - " + juce::Time ((juce::int64) ((t + q) * 1000.0)).formatted ("%H:%M"),
                                  n1 (v), n1 (log.leq (true, t, t + q)), log.limitLaeq > 0.0 && v > log.limitLaeq ? "over" : "" });
            }
            L.sub ("Each quarter hour");
            L.table ({ 1.6f, 1.0f, 1.0f, 0.8f }, { "Time", "LAeq (dB)", "LCeq (dB)", "Limit" }, rows, 8.5f);
        }
    }

    if (has ("setup"))
    {
        L.chapter ("Monitoring mic and system");
        std::vector<std::pair<juce::String, juce::String>> kv;
        kv.push_back ({ "Mic", mm.name });
        if (const auto* e = p.locker.find (mm.lockerId)) if (e->modelSerial().isNotEmpty()) kv.push_back ({ "Model / serial", e->modelSerial() });
        kv.push_back ({ "Position", mm.position.isNotEmpty() ? mm.position : juce::String ("-") });
        kv.push_back ({ "Input", "Input " + juce::String (mm.input + 1) });
        kv.push_back ({ "Calibration files", mm.cal.any() ? mm.cal.names() : mm.noCalFile ? juce::String ("none (flat)") : juce::String ("not loaded") });
        kv.push_back ({ "Aim", calAimText (mm.aim) });
        kv.push_back ({ "Sound level", mm.splCalibrated ? juce::String (mm.splOffset, 2) + " dB SPL at 0 dBFS" + (mm.splDate.isNotEmpty() ? " (" + mm.splDate + ")" : juce::String())
                                                       : juce::String ("not calibrated") });
        kv.push_back ({ "Delay", juce::String (mm.delayMs, 2) + " ms" });
        if (p.settings.targetCurve.load() > 0) kv.push_back ({ "Target", targets::list()[(size_t) p.settings.targetCurve.load()].name });
        L.keyValues (kv);
        std::vector<juce::StringArray> rows;
        for (const auto& s : p.system.sources) rows.push_back ({ s.name, speakerChannelNames()[s.channel], processingText (s.proc) });
        L.sub ("Speaker processor settings (as set)");
        L.table ({ 1.4f, 0.8f, 4.0f }, { "Speaker", "Channel", "Settings" }, rows, 8.5f);
        if (p.tuningRoom().tuned) L.para ("The room was tuned " + p.tuningRoom().tunedOn
                                          + (p.tuningRoomName().isNotEmpty() ? " (" + p.tuningRoomName() + ")" : juce::String()) + ".", 9.0f, dim);
    }

    if (has ("plan") && plan.isValid())
    {
        L.chapter ("Venue plan", true);
        L.para ("Where the monitoring mic was (a sketch, not to scale).", 9.0f, dim);
        L.picture (plan, L.bottom() - L.y - 10.0f);
    }

    if (has ("traces"))
    {
        std::vector<const Trace*> tf, rta;
        for (const auto& t : p.traces.all())
        {
            if (t.fromSession.isNotEmpty() || t.data.freq.size() < 4 || t.name.startsWith ("Noise floor")) continue;
            (t.kind == Trace::Transfer ? tf : rta).push_back (&t);
        }
        L.chapter ("Captured traces");
        if (tf.empty() && rta.empty()) L.para ("No traces were captured. Capture them on Live monitoring (Capture, key C).", 9.5f, dim);
        if (! tf.empty()) { L.sub ("Transfer functions"); L.picture (traceChart (tf, true, p.settings.targetCurve.load()), 300.0f); }
        if (! rta.empty()) { L.sub ("RTA"); L.picture (traceChart (rta, false, 0), 300.0f); }
        std::vector<juce::StringArray> rows;
        for (const auto* t : tf) rows.push_back ({ t->name, "Transfer function", t->created });
        for (const auto* t : rta) rows.push_back ({ t->name, "RTA", t->created });
        if (! rows.empty()) L.table ({ 3.0f, 1.4f, 1.6f }, { "Trace", "Kind", "Captured" }, rows, 8.5f);
    }

    L.need (40.0f);
    L.y += 10.0f;
    L.para ("Prepared with LiveAlign " + juce::String (JucePlugin_VersionString) + " on " + LiveAlignProcessor::today()
            + (opt.author.isNotEmpty() ? " by " + opt.author : juce::String()) + ".", 8.5f, dim);
    L.contents (0, contentsY);
    L.footers();
    if (! doc.write (file, "Live monitoring report - " + p.venueName, opt.author)) return "Could not write " + file.getFullPathName();
    return {};
}
} // namespace la::report
