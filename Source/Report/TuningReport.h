// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The tuning report: a PDF of the open session's system tuning. A cover with the contents, then chapters: a summary
// with the system as found and as left, the venue plan (landscape), the speakers and the settings for the processor
// (crossover, EQ, time alignment), room EQ, the feedback test, room acoustics, the measurement mics with their serial
// numbers and calibration, and the measured traces. Sections can be left out (Reports > Tuning report).
#include "ReportCommon.h"
#include "../UI/RoomPage.h"
#include "../UI/SpeakersPage.h"
#include "../UI/AcousticsPage.h"

namespace la::report
{
struct TuningSection { const char* id; const char* title; const char* what; };
inline const std::vector<TuningSection>& tuningSections()
{
    static const std::vector<TuningSection> s {
        { "summary", "Summary: as found and as left", "What was done, and the processor settings before and after this session" },
        { "plan", "Venue plan", "Where the speakers and tuning mics were (a landscape page)" },
        { "speakers", "Speakers and processor settings", "The system, the crossover, EQ, time alignment and the predicted sum" },
        { "room", "Room EQ", "The listening positions against the target, as found and as left" },
        { "feedback", "Feedback test", "The rings found and the cuts for them" },
        { "acoustics", "Room acoustics", "Reverberation, clarity, intelligibility, room modes, coverage and noise" },
        { "mics", "Measurement mics and checks", "Mics with serial numbers, calibration and this session's checks" },
        { "measurements", "Measurements", "The transfer functions and RTA traces captured in this session" } };
    return s;
}

struct ReportOptions
{
    juce::StringArray skip;      // section ids left out
    bool asFoundLeft = true;     // the summary shows the settings as found beside as left
    juce::String author;
};

// Writes the report. `plan` is the venue plan picture (tuning layer, white page). Returns an error message, or empty.
inline juce::String writeTuningReport (LiveAlignProcessor& p, const juce::File& file, const juce::Image& plan, const ReportOptions& opt)
{
    using namespace detail;
    pdf::Document doc;
    const auto& sessions = p.getSessions();
    const int cur = p.currentSession();
    const auto sessionName = cur < (int) sessions.size() ? sessions[(size_t) cur].name : juce::String ("Session");
    Layout L { doc, p.venueName + "  -  " + sessionName + "  -  Tuning report  -  LiveAlign " + JucePlugin_VersionString };
    const auto& sys = p.system;
    const auto& st = p.settings;
    const int nMics = p.tuningMicCount();
    const auto& room = p.room;
    auto has = [&opt] (const char* id) { return included (opt.skip, id); };

    // ---------- cover ----------
    L.y = cover (L, "SYSTEM TUNING REPORT", p.venueName, sessionName + "   -   " + LiveAlignProcessor::today());
    {
        std::vector<std::pair<juce::String, juce::String>> kv;
        if (opt.author.isNotEmpty()) kv.push_back ({ "Prepared by", opt.author });
        kv.push_back ({ "Venue file", p.projectFile != juce::File() ? p.projectFile.getFileName() : juce::String ("not saved") });
        kv.push_back ({ "Speakers", p.speakers.configured ? "Set up " + p.speakers.configuredOn + (p.speakers.changed ? " (marked as changed since)" : "")
                                                          : juce::String ("Not marked as set up") });
        kv.push_back ({ "Room", room.tuned ? "Tuned " + room.tunedOn : room.count (false) > 0 ? juce::String ("Measured, not marked as tuned") : juce::String ("Not measured") });
        kv.push_back ({ "Analysis", juce::String (p.getSampleRate() > 0.0 ? p.getSampleRate() / 1000.0 : 48.0, 1) + " kHz, FFT "
                                        + juce::String (st.fftSize.load() / 1024) + "k, " + juce::String (st.averages.load()) + " averages, minimum coherence "
                                        + juce::String (juce::roundToInt (st.coherenceBlank.load() * 100.0f)) + " %" });
        L.keyValues (kv);
        if (p.venueNotes.isNotEmpty()) { L.sub ("Venue notes"); L.para (p.venueNotes); }
    }
    const float contentsY = L.y + 14.0f;

    // ---------- summary ----------
    if (has ("summary"))
    {
        L.chapter ("Summary: as found and as left");
        std::vector<std::pair<juce::String, juce::String>> kv;
        int measured = 0;
        for (int i = 0; i < (int) sys.sources.size(); ++i) measured += sys.covered (i) ? 1 : 0;
        kv.push_back ({ "Speakers", juce::String (measured) + " of " + juce::String ((int) sys.sources.size()) + " measured"
                                        + (sys.placement == 0 ? " close up" : " from one spot") });
        if (sys.pairValid() && sys.sources[(size_t) sys.xoHigh].proc.hp.active())
        {
            const auto& f = sys.sources[(size_t) sys.xoHigh].proc.hp;
            kv.push_back ({ "Crossover", sys.sources[(size_t) sys.xoHigh].name + " / " + sys.sources[(size_t) sys.xoLow].name + ": " + xo::typeName (f.type) + " "
                                             + juce::String (f.slope) + " dB/oct at " + juce::String (juce::roundToInt (f.freq)) + " Hz" });
        }
        if (! sys.alignments.empty())
        {
            int checked = 0, measuredA = 0;
            for (const auto& a : sys.alignments) { measuredA += a.measured() ? 1 : 0; checked += a.checked() ? 1 : 0; }
            kv.push_back ({ "Time alignment", juce::String (measuredA) + " of " + juce::String ((int) sys.alignments.size()) + " measured, "
                                                  + juce::String (checked) + " checked together" });
        }
        if (room.count (false) > 0)
        {
            double lo, hi, rb, mb, ra = NAN, ma;
            room.eqRange (sys, lo, hi);
            room.deviation (room.average (false), lo, hi, rb, mb);
            if (room.count (true) > 0) room.deviation (room.average (true), lo, hi, ra, ma);
            kv.push_back ({ "Room EQ", "As found " + juce::String (rb, 1) + " dB RMS from the target"
                                           + (std::isfinite (ra) ? ", as left " + juce::String (ra, 1) + " dB" : juce::String (", no check measured yet"))
                                           + " (" + juce::String (room.count (false)) + " positions)" });
        }
        if (! p.feedback.rings.empty())
            kv.push_back ({ "Feedback test", juce::String ((int) p.feedback.rings.size()) + " rings on " + p.feedback.micName });
        if (room.acousticsCount() > 0)
        {
            const auto a = room.acousticsAverage();
            kv.push_back ({ "Room acoustics", "RT " + juce::String (a.rtMid(), 2) + " s, STI " + juce::String (a.sti, 2) + " ("
                                                  + acoustics::stiRating (a.sti) + ")" });
        }
        L.keyValues (kv);

        if (opt.asFoundLeft)
        {
            L.sub ("Processor settings, as found and as left");
            if (! p.asFound.taken())
                L.para ("Nothing was changed in this session, so the settings as found are the same as below.", 9.0f, dim);
            else
            {
                std::vector<juce::StringArray> rows;
                for (const auto& s : sys.sources)
                {
                    juce::String found = "-";
                    for (const auto& f : p.asFound.speakers) if (f.name == s.name) found = processingText (f.proc);
                    rows.push_back ({ s.name, found, processingText (s.proc) });
                }
                L.table ({ 1.4f, 3.0f, 3.0f }, { "Speaker", "As found (" + p.asFound.takenOn + ")", "As left" }, rows, 8.0f);
            }
            if (room.count (false) > 0 && room.count (true) > 0)
                L.para ("The room's response as found and as left (the latest check) is compared on the Room EQ pages.", 9.0f, dim);
        }
    }

    // ---------- plan (landscape) ----------
    if (has ("plan") && plan.isValid())
    {
        L.chapter ("Venue plan", true);
        L.para ("Where the speakers and the tuning mics were for this session (a sketch, not to scale).", 9.0f, dim);
        L.picture (plan, L.bottom() - L.y - 10.0f);
    }

    // the polarity of a speaker's cabinets, for the table
    auto polarityCell = [] (const SpeakerSource& s)
    {
        const int v = s.polaritySummary(), n = s.polarityChecked();
        if (v == polarity::Normal) return juce::String ("normal") + (s.count > 1 ? " (all " + juce::String (s.count) + ")" : juce::String());
        if (v == polarity::Inverted) return juce::String ("INVERTED");
        if (v == polarity::Unsure) return juce::String ("check");
        return n > 0 ? juce::String (n) + " of " + juce::String (s.count) + " checked" : juce::String ("not checked");
    };

    // ---------- speakers ----------
    if (has ("speakers"))
    {
        L.chapter ("Speakers and processor settings");
        {
            std::vector<juce::StringArray> rows;
            for (int i = 0; i < (int) sys.sources.size(); ++i)
            {
                const auto& s = sys.sources[(size_t) i];
                rows.push_back ({ s.name, s.model.isNotEmpty() ? s.model : juce::String ("-"), speakerRoleNames()[s.role], speakerChannelNames()[s.channel],
                                  s.output == 0 ? juce::String ("all") : juce::String (s.output), juce::String (s.count),
                                  s.hasResponse() ? (s.imported ? "imported" : s.nearField ? "measured close up" : "measured")
                                                  : sys.master (i) != i ? "as " + sys.sources[(size_t) sys.master (i)].name : juce::String ("not measured"),
                                  polarityCell (s) });
            }
            L.sub ("The system");
            L.table ({ 2.0f, 2.2f, 1.2f, 1.1f, 0.6f, 0.6f, 1.7f, 1.5f }, { "Speaker", "Model", "Role", "Channel", "Out", "Qty", "Response", "Polarity" }, rows);
            // the cabinets that need a look
            juce::StringArray issues;
            for (int i = 0; i < (int) sys.sources.size(); ++i)
            {
                const auto& s = sys.sources[(size_t) i];
                for (int c = 0; c < s.count; ++c)
                    if (const auto* pc = s.polarityOf (c); pc != nullptr && (pc->verdict == polarity::Inverted || pc->verdict == polarity::Unsure))
                    {
                        juce::String t = s.name + (s.count > 1 ? " cabinet " + juce::String (c + 1) : juce::String()) + ": "
                                         + (pc->verdict == polarity::Inverted ? "polarity looks inverted" : "polarity not clear");
                        if (pc->against >= 0 && pc->against < (int) sys.sources.size() && pc->against != i)
                            t << (pc->relative == polarity::Inverted ? ", opposite to " : pc->relative == polarity::Normal ? ", matches " : ", compared with ")
                              << sys.sources[(size_t) pc->against].name;
                        t << " (" << pc->when << ")";
                        issues.add (t);
                    }
            }
            if (! issues.isEmpty()) { L.sub ("Polarity to check"); for (const auto& t : issues) L.para (t, 9.5f); }
        }
        {
            std::vector<juce::StringArray> rows;
            for (const auto& s : sys.sources)
                rows.push_back ({ s.name, filterText (s.proc.hp, "HPF"), filterText (s.proc.lp, "LPF"), juce::String (s.proc.delayMs, 2) + " ms",
                                  s.proc.invert ? "inverted" : "normal", (s.proc.gainDb >= 0.0 ? "+" : "") + juce::String (s.proc.gainDb, 1) + " dB" });
            L.sub ("Settings for the speaker processor");
            L.table ({ 2.0f, 2.2f, 2.2f, 1.2f, 1.2f, 1.0f }, { "Speaker", "High-pass", "Low-pass", "Delay", "Polarity", "Gain" }, rows);
            L.para ("BW Butterworth, LR Linkwitz-Riley; slopes in dB/octave. Delay, polarity and gain are applied on each speaker's output.", 8.0f, dim);
        }
        {
            std::vector<juce::StringArray> rows;
            for (int i = 0; i < (int) sys.sources.size(); ++i)
            {
                const auto& s = sys.sources[(size_t) i];
                if (sys.master (i) != i) continue;
                int k = 0;
                for (const auto& b : s.proc.eq)
                {
                    ++k;
                    rows.push_back ({ k == 1 ? s.name : juce::String(), juce::String (k), juce::String (juce::roundToInt (b.freq)) + " Hz",
                                      (b.gainDb >= 0.0 ? "+" : "") + juce::String (b.gainDb, 1) + " dB", juce::String (b.q, 2) });
                }
            }
            if (! rows.empty())
            {
                L.sub ("Parametric EQ (from room EQ)");
                L.table ({ 2.4f, 0.6f, 1.4f, 1.4f, 1.0f }, { "Speaker", "Band", "Frequency", "Gain", "Q" }, rows);
                L.para ("Peaking (bell) filters. Speakers that share settings with another (e.g. Main R with Main L) take the same EQ.", 8.0f, dim);
            }
        }
        if (sys.pairValid() && sys.sources[(size_t) sys.xoHigh].proc.hp.active())
        {
            const auto& f = sys.sources[(size_t) sys.xoHigh].proc.hp;
            L.sub ("Crossover");
            juce::String t = sys.sources[(size_t) sys.xoHigh].name + " (high) / " + sys.sources[(size_t) sys.xoLow].name + " (low): " + xo::typeName (f.type) + " "
                             + juce::String (f.slope) + " dB/oct at " + juce::String (juce::roundToInt (f.freq)) + " Hz.";
            const auto& r = sys.lastResult;
            if (r.valid)
                t << " Found by LiveAlign: the two meet " << juce::roundToInt (r.efficiency * 100.0) << " % in phase through the crossover region, "
                  << juce::roundToInt (r.reliability * 100.0) << " % of it measured reliably.";
            if (sys.pairNearField()) t << " Measured close up; the delay between them comes from the time alignment below.";
            L.para (t);
        }
        {
            std::vector<juce::StringArray> rows;
            for (const auto& a : sys.alignments)
            {
                if (a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) continue;
                const double need = a.neededMs (sys.sources[(size_t) a.ref].proc.delayMs);
                juce::String check = "-";
                if (a.checked())
                    check = (a.sumGainDb() >= a.idealSumGainDb() - 1.5 ? "in phase: " : "not adding: ") + juce::String (a.sumGainDb(), 1) + " of "
                            + juce::String (a.idealSumGainDb(), 1) + " dB at " + juce::String (juce::roundToInt (a.checkFreq)) + " Hz";
                rows.push_back ({ sys.sources[(size_t) a.align].name + " to " + sys.sources[(size_t) a.ref].name, a.spot,
                                  a.measured() ? juce::String (a.alignArrivalMs - a.refArrivalMs, 2) + " ms" : juce::String ("-"),
                                  a.measured() ? juce::String (need, 2) + " ms" : juce::String ("not measured"), check });
            }
            if (! rows.empty())
            {
                L.sub ("Time alignment");
                L.table ({ 2.4f, 1.4f, 1.2f, 1.2f, 2.6f }, { "Speakers", "Where", "Arrival diff.", "Delay", "Check (both together)" }, rows);
            }
        }
        {
            bool any = false;
            for (const auto& s : sys.sources) any = any || s.hasResponse();
            if (any)
            {
                ui::SpeakerGraph graph (sys);
                const auto img = snapshotLight (graph, 820, 450, [&sys] (ui::SpeakerGraph& gr)
                {
                    gr.setWithProcessing (true);
                    if (sys.pairValid() && sys.sources[(size_t) sys.xoHigh].proc.hp.active()) gr.setXoMarker (sys.sources[(size_t) sys.xoHigh].proc.hp.freq);
                    gr.rebuild();
                });
                L.need (360.0f);
                L.sub ("Measured responses with the processing, and their predicted sum");
                L.picture (img, 330.0f);
            }
        }
    }

    // ---------- room EQ ----------
    if (has ("room"))
    {
        L.chapter ("Room EQ");
        if (room.count (false) == 0 && room.count (true) == 0)
            L.para ("The room was not measured in this session.", 9.5f, dim);
        else
        {
            double lo, hi;
            room.eqRange (sys, lo, hi);
            std::vector<std::pair<juce::String, juce::String>> rkv;
            rkv.push_back ({ "Target", targets::list()[(size_t) juce::jlimit (0, targets::count() - 1, room.target)].name });
            rkv.push_back ({ "EQ on", room.eqGroupName (sys) + (room.checkRound > 0 ? ", " + juce::String (room.checkRound) + (room.checkRound == 1 ? " check" : " checks") : juce::String()) });
            rkv.push_back ({ "EQ range", juce::String (juce::roundToInt (lo)) + " Hz - " + juce::String (juce::roundToInt (hi)) + " Hz" });
            auto dev = [&] (bool after)
            {
                double rms, mx;
                room.deviation (room.average (after), lo, hi, rms, mx);
                juce::String t = juce::String (rms, 1) + " dB RMS from the target (worst " + juce::String (mx, 1) + " dB), "
                                 + juce::String (room.count (after)) + (room.count (after) == 1 ? " position" : " positions");
                const double spread = room.levelSpread (after);
                if (std::isfinite (spread) && room.count (after) > 1) t << "; level across positions " << juce::String (spread, 1) << " dB";
                return t;
            };
            if (room.count (false) > 0) rkv.push_back ({ "As found", dev (false) });
            if (room.count (true) > 0) rkv.push_back ({ "As left (latest check)", dev (true) });
            rkv.push_back ({ "Status", room.tuned ? "Marked as tuned " + room.tunedOn : juce::String ("Not marked as tuned") });
            L.keyValues (rkv);

            ui::RoomGraph graph (p);
            const auto img = snapshotLight (graph, 820, 420, [lo, hi, &room] (ui::RoomGraph& gr)
            {
                gr.setRange (lo, hi);
                gr.setShow (true, true, true, room.count (true) == 0);
            });
            L.need (330.0f);
            L.sub ("Average of the positions against the target");
            L.picture (img, 300.0f);
            L.para ("Each position is levelled to its own 500 Hz - 4 kHz average, so the curves compare tonal balance. Dark: as found; "
                    "green: as left (the latest check); dashed green with a shaded band: the target, +/-3 dB; dashed teal: with the suggested EQ.", 8.0f, dim);

            std::vector<juce::StringArray> rows;
            for (const auto& m : room.meas)
            {
                const double l = m.levelDb();
                rows.push_back ({ m.position, m.after ? "check " + juce::String (room.checkRound) : juce::String ("as found"), m.micName, m.calUsed, m.time,
                                  std::isfinite (l) ? juce::String (l, 1) + " dB" : juce::String ("-"), m.include ? "yes" : "no" });
            }
            L.sub ("Measurements at the listening positions");
            L.table ({ 1.6f, 1.0f, 1.2f, 2.6f, 0.7f, 0.9f, 0.8f }, { "Position", "When", "Mic", "Calibration used", "Time", "Level", "Averaged" }, rows, 8.0f);

            if (! room.suggested.empty())
            {
                std::vector<juce::StringArray> eqRows;
                for (size_t i = 0; i < room.suggested.size(); ++i)
                {
                    const auto& b = room.suggested[i];
                    eqRows.push_back ({ juce::String ((int) i + 1), juce::String (juce::roundToInt (b.freq)) + " Hz",
                                        (b.gainDb >= 0.0 ? "+" : "") + juce::String (b.gainDb, 1) + " dB", juce::String (b.q, 2) });
                }
                L.sub ("Suggested EQ");
                L.table ({ 0.6f, 1.4f, 1.4f, 1.0f }, { "Band", "Frequency", "Gain", "Q" }, eqRows);
                L.para ("Limits: " + juce::String (room.maxBands) + " bands, cuts to " + juce::String (juce::roundToInt (room.maxCutDb)) + " dB, "
                        + (room.maxBoostDb > 0.0 ? "boosts to " + juce::String (juce::roundToInt (room.maxBoostDb)) + " dB (wide dips only)." : juce::String ("no boosts."))
                        + (room.baseEq.empty() ? juce::String() : " Added to the " + juce::String ((int) room.baseEq.size()) + " bands already on the speaker when measured."),
                        8.0f, dim);
            }
        }
    }

    // ---------- feedback ----------
    if (has ("feedback"))
    {
        L.chapter ("Feedback test");
        const auto& fbt = p.feedback;
        if (fbt.rings.empty()) L.para ("No feedback test was done in this session.", 9.5f, dim);
        else
        {
            L.para ("Stage mic rung out: " + fbt.micName + ", listening with " + p.mics.nameOf (fbt.listenMic) + ". The gain was brought up until it rang; "
                    "each ring was caught and given a narrow cut." + (fbt.done ? " Marked as done." : ""));
            std::vector<juce::StringArray> rows;
            for (const auto& r : fbt.rings)
                rows.push_back ({ juce::String (juce::roundToInt (r.ring.freq)) + " Hz", juce::String (juce::roundToInt (r.ring.prominenceDb)) + " dB",
                                  juce::String (juce::roundToInt (r.ring.cutDb)) + " dB", juce::String (juce::roundToInt (r.ring.q)), r.applied ? "yes" : "no", r.mic, r.time });
            L.table ({ 1.1f, 1.0f, 0.9f, 0.6f, 0.8f, 1.8f, 0.8f }, { "Frequency", "Above", "Cut", "Q", "In place", "Stage mic", "Time" }, rows);
            L.para ("Cuts are narrow bells (Q 10, about 1/7 octave) at the ring frequency. Put them on that mic's channel or its output.", 8.0f, dim);
        }
    }

    // ---------- acoustics ----------
    if (has ("acoustics"))
    {
        L.chapter ("Room acoustics");
        if (room.acousticsCount() == 0) L.para ("The room's acoustics come from the Room EQ measurements; none were made in this session.", 9.5f, dim);
        else
        {
            const auto a = room.acousticsAverage();
            double rtLo, rtHi, stiMin;
            RoomTuning::useTargets (room.use, rtLo, rtHi, stiMin);
            std::array<double, acoustics::kNoiseBands> L8 {};
            juce::String who;
            const bool noise = ui::noiseOctaves (p, L8, who);
            auto n2 = [] (double v, int d) { return ! std::isfinite (v) ? juce::String ("-") : d == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, d); };
            L.tiles ({ { "RT (500 Hz - 1 kHz)", n2 (a.rtMid(), 2), "s" }, { "EDT", n2 (a.mid (&acoustics::Band::edt), 2), "s" },
                       { "C50 / C80", n2 (a.mid (&acoustics::Band::c50), 1) + " / " + n2 (a.mid (&acoustics::Band::c80), 1), "dB" },
                       { "STI", n2 (a.sti, 2), acoustics::stiRating (a.sti) },
                       { "Noise", noise ? "NC " + juce::String (juce::roundToInt (acoustics::noiseCriterion (L8))) : juce::String ("-"), "" } });
            L.para ("Room use: " + RoomTuning::useNames()[room.use] + ". Typical reverberation for it: " + juce::String (rtLo, 1) + " - " + juce::String (rtHi, 1)
                    + " s; speech intelligibility (STI) of " + juce::String (stiMin, 2) + " or more.", 9.0f);

            ui::AcousticsChart chart (p);
            const auto img = snapshotLight (chart, 820, 380, [] (ui::AcousticsChart& c) { c.setMetric (0); });
            L.need (290.0f);
            L.picture (img, 270.0f);

            std::vector<juce::StringArray> rows;
            for (int b = 0; b < acoustics::kBands; ++b)
            {
                const auto& o = a.band[(size_t) b];
                rows.push_back ({ juce::String (juce::roundToInt (acoustics::octaves()[(size_t) b])) + " Hz", n2 (o.edt, 2), n2 (o.t20, 2), n2 (o.t30, 2),
                                  n2 (o.c50, 1), n2 (o.c80, 1), n2 (o.d50 * 100.0, 0) + " %" });
            }
            L.sub ("Per octave (average of the positions)");
            L.table ({ 1.0f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f }, { "Octave", "EDT (s)", "T20 (s)", "T30 (s)", "C50 (dB)", "C80 (dB)", "D50" }, rows, 8.0f);

            std::vector<juce::StringArray> prow;
            for (const auto& m : room.meas)
                if (m.ac.valid)
                    prow.push_back ({ m.position + (m.after ? juce::String (" (check)") : juce::String()), n2 (m.ac.rtMid(), 2), n2 (m.ac.mid (&acoustics::Band::edt), 2),
                                      n2 (m.ac.mid (&acoustics::Band::c50), 1), n2 (m.ac.mid (&acoustics::Band::c80), 1), n2 (m.ac.sti, 2) + " " + acoustics::stiRating (m.ac.sti) });
            L.sub ("Each position");
            L.table ({ 2.0f, 0.9f, 0.9f, 0.9f, 0.9f, 1.3f }, { "Position", "RT (s)", "EDT (s)", "C50", "C80", "STI" }, prow, 8.0f);

            const auto modes = room.modes();
            if (! modes.empty())
            {
                std::vector<juce::StringArray> mrow;
                for (const auto& m : modes)
                    mrow.push_back ({ juce::String (juce::roundToInt (m.freq)) + " Hz", "+" + juce::String (m.heightDb, 1) + " dB",
                                      std::isfinite (m.seatSpreadDb) ? juce::String (m.seatSpreadDb, 1) + " dB" : juce::String ("-") });
                L.sub ("Room modes (low-frequency peaks)");
                L.table ({ 1.0f, 1.0f, 1.6f }, { "Frequency", "Height", "Varies between seats" }, mrow, 8.0f);
            }
            const double spread = room.levelSpread (room.count (true) > 0);
            if (std::isfinite (spread)) L.para ("Coverage: the level (500 Hz - 4 kHz) varies " + juce::String (spread, 1) + " dB across the positions.", 9.0f);
            if (noise)
            {
                std::vector<juce::StringArray> nrow { {} };
                juce::StringArray head { "" };
                nrow[0].add ("dB SPL");
                for (int b = 0; b < acoustics::kNoiseBands; ++b) { head.add (juce::String (juce::roundToInt (acoustics::noiseOctaves()[(size_t) b])) + " Hz"); nrow[0].add (n2 (L8[(size_t) b], 0)); }
                L.sub ("Background noise (" + who + ", from the noise floor check)");
                L.table (std::vector<float> (9, 1.0f), head, nrow, 8.0f);
                L.para ("NC " + juce::String (juce::roundToInt (acoustics::noiseCriterion (L8))) + " (noise criterion), NR "
                        + juce::String (juce::roundToInt (acoustics::noiseRating (L8))) + " (noise rating).", 9.0f);
            }
            L.para ("Estimates from the system measurements at the listening positions (ISO 3382-1 decay, IEC 60268-16 indirect STI without "
                    "noise corrections). A full acoustic survey uses an omnidirectional source.", 8.0f, dim);
        }
    }

    // ---------- mics ----------
    if (has ("mics"))
    {
        L.chapter ("Measurement mics and checks");
        {
            std::vector<juce::StringArray> rows;
            for (int m = 0; m < nMics; ++m)
            {
                const auto ms = p.micModelSerial (m);
                const auto& c = p.calibrations (m);
                juce::String cal = c.any() ? c.names() : p.mics.noCalFile[(size_t) m] ? juce::String ("none (flat)") : juce::String ("not loaded");
                const bool slot0 = p.isMonitorMode() && m == 0;
                juce::String spl = p.mics.noCalibrator ? juce::String ("dBFS (no calibrator)")
                                 : (! slot0 && st.micSplCalibrated[m].load()) ? juce::String (st.micSplOffset[m].load(), 2) + " dB" + (p.checks.splVerified[(size_t) m] ? ", checked this session" : ", not re-checked")
                                                                              : juce::String (slot0 ? "see the venue file" : "not calibrated");
                rows.push_back ({ juce::String (m + 1) + "  " + p.mics.nameOf (m), ms.isNotEmpty() ? ms : juce::String ("-"),
                                  p.mics.position[(size_t) m].isNotEmpty() ? p.mics.position[(size_t) m] : juce::String ("-"),
                                  "In " + juce::String (st.micInput[m].load() + 1), cal,
                                  p.ownAim (m) == 0 ? juce::String ("at speaker (0 deg)") : juce::String ("pointing up (90 deg)"), spl });
            }
            L.sub ("Mics");
            L.table ({ 1.6f, 1.7f, 1.1f, 0.6f, 2.4f, 1.3f, 1.6f }, { "Mic", "Model / serial", "Position", "Input", "Calibration files", "Aim", "Sound level" }, rows, 8.0f);
        }
        {
            const auto& c = p.checks;
            std::vector<std::pair<juce::String, juce::String>> checks;
            checks.push_back ({ "Reference", st.refSource.load() == 0 ? juce::String ("off") : st.refSource.load() == 1 ? juce::String ("generator (internal copy)")
                                                                                             : "Input " + juce::String (st.refSource.load() - 1) + " (loopback)" });
            checks.push_back ({ "Loopback check", c.loopbackSkipped ? juce::String ("skipped")
                                                  : c.loopbackDone ? juce::String (c.loopbackLatencyMs, 2) + " ms round trip, polarity " + (c.loopbackInverted ? "INVERTED" : "normal")
                                                                         + ", " + juce::String (c.loopbackLevelDbfs, 1) + " dBFS"
                                                                   : juce::String ("not done") });
            if (c.noiseDone)
            {
                juce::StringArray n;
                for (int m = 0; m < juce::jmin (nMics, c.noiseMics); ++m)
                    n.add (p.mics.nameOf (m) + " " + juce::String (c.noiseA[(size_t) m], 1) + (c.noiseIsSpl[(size_t) m] ? " dBA" : " dBFS(A)"));
                checks.push_back ({ "Room noise floor", n.joinIntoString (",  ") });
            }
            else checks.push_back ({ "Room noise floor", "not measured" });
            L.sub ("This session's checks");
            L.keyValues (checks);
        }
    }

    // ---------- measurements ----------
    if (has ("measurements"))
    {
        std::vector<const Trace*> tf, rta;
        for (const auto& t : p.traces.all())
        {
            if (t.fromSession.isNotEmpty() || t.data.freq.size() < 4) continue;
            (t.kind == Trace::Transfer ? tf : rta).push_back (&t);
        }
        L.chapter ("Measurements");
        if (tf.empty() && rta.empty()) L.para ("No traces were captured in this session. Capture them on Live monitoring (Capture, key C).", 9.5f, dim);
        auto listTraces = [&] (const std::vector<const Trace*>& v)
        {
            static const juce::uint32 cols[] = { 0xff0f9488, 0xff2563d6, 0xffc98a0a, 0xff7553e0, 0xffe0661a, 0xffd2478f, 0xff23a056, 0xff434d5a };
            for (size_t i = 0; i < v.size(); ++i)
            {
                L.need (13.0f);
                doc.rect (L.kMargin, L.y - 7.0f, 14.0f, 4.0f, juce::Colour (cols[i % 8]));
                doc.text (L.kMargin + 22.0f, L.y, v[i]->name + (v[i]->created.isNotEmpty() ? "   (" + v[i]->created + ")" : juce::String()), 8.5f, pdf::Document::Regular, ink);
                L.y += 12.0f;
            }
            L.y += 6.0f;
        };
        if (! tf.empty())
        {
            L.sub ("Transfer functions" + juce::String (st.targetCurve.load() > 0 ? juce::String (" with the target (dashed)") : juce::String()));
            L.picture (traceChart (tf, true, st.targetCurve.load()), 300.0f);
            listTraces (tf);
        }
        if (! rta.empty())
        {
            L.sub ("RTA");
            L.picture (traceChart (rta, false, 0), 300.0f);
            listTraces (rta);
        }
    }

    L.need (40.0f);
    L.y += 10.0f;
    L.para ("Prepared with LiveAlign " + juce::String (JucePlugin_VersionString) + " on " + LiveAlignProcessor::today()
            + (opt.author.isNotEmpty() ? " by " + opt.author : juce::String()) + ".", 8.5f, dim);

    L.contents (0, contentsY);
    L.footers();
    if (! doc.write (file, "System tuning report - " + p.venueName, opt.author)) return "Could not write " + file.getFullPathName();
    return {};
}
} // namespace la::report
