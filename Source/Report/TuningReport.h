// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The tuning report: a PDF of the open session's system tuning, in the order of the tuning steps. A cover with the
// contents, then chapters: a summary with the system as found and as left, the venue plan (landscape), the speakers
// (responses as found and as left, polarity, crossover, time alignment), the level balance, room acoustics with its
// verdict, room EQ, the feedback test, the processor sheet, the measurement mics with their serial numbers and
// calibration, and the measured traces. Sections can be left out (Reports > Tuning report).
#include "ReportCommon.h"
#include "../UI/RoomPage.h"
#include "../UI/SpeakersPage.h"
#include "../UI/AcousticsPage.h"
#include "../AcousticsSummary.h"

namespace la::report
{
struct TuningSection { const char* id; const char* title; const char* what; };
inline const std::vector<TuningSection>& tuningSections()
{
    static const std::vector<TuningSection> s {
        { "summary", "Summary: as found and as left", "What was done, and the processor settings before and after this session" },
        { "plan", "Venue plan", "Where the speakers and tuning mics were (a landscape page)" },
        { "speakers", "Speakers", "The system, polarity, the crossover, time alignment, and the responses as found and as left" },
        { "level", "Level balance", "Each speaker's level at the listening points, the trims, and the check" },
        { "coverage", "Aim and coverage", "Each speaker across the seats it covers, against the spec, before and after re-aiming" },
        { "acoustics", "Room acoustics", "Reverberation, clarity, intelligibility, room modes, coverage, noise, and what it means" },
        { "room", "Room EQ", "The listening points against the target, as found and as left, and the seat-to-seat variance" },
        { "headroom", "Gain and headroom", "The level at FOH against the generator, the headroom above the show level, the amps and limiters" },
        { "listening", "Listening check", "The music played and the notes at each point" },
        { "feedback", "Feedback test", "The rings found and the cuts for them" },
        { "sheet", "Processor sheet", "Every setting to enter, output by output, which are entered, and the change history" },
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
    updateRoomAllowance (p);   // the target and the allowance as Room EQ shows them
    const auto acs = summariseAcoustics (p);

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
        L.para ("Methods: room acoustics per ISO 3382-1 (RT, EDT, C50, C80, Ts) from swept-sine impulse responses (ISO 18233); STI per IEC 60268-16; "
                "sound levels A- and C-weighted per IEC 61672-1; NC per ANSI S12.2 and NR per ISO 1996; air absorption per ISO 9613-1; coverage "
                "uniformity in the spirit of AVIXA A102.01.", 8.0f);
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
        {
            const double v0 = seatVariance (room, false), v1 = seatVariance (room, true);
            if (std::isfinite (v0)) kv.push_back ({ "Seat to seat", juce::String (v0, 1) + " dB as found" + (std::isfinite (v1) ? ", " + juce::String (v1, 1) + " dB as left" : juce::String()) });
        }
        if (const auto* hr = p.headroom.find (0))
        {
            int lv = 0;
            kv.push_back ({ "Headroom", headroom::verdict (*hr, p.pref ("targetDbA"), p.pref ("headroomDb"), st.micSplCalibrated[0].load(), lv) });
        }
        if (p.listening.done || ! p.listening.points.empty())
            kv.push_back ({ "Listening check", (p.listening.done ? "Done " + p.listening.doneOn + ": " : juce::String ("Started: "))
                                                   + juce::String (p.listening.count (1)) + " points good, " + juce::String (p.listening.count (2)) + " with notes" });
        {
            const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            juce::String c = juce::String (st.temperatureC.load(), 1) + deg + "C, " + juce::String (juce::roundToInt (st.humidity.load())) + " % humidity";
            if (std::isfinite (p.checks.tunedTempC))
                c << " (tuned at " << juce::String (p.checks.tunedTempC, 1) << deg << "C" << (std::isfinite (p.checks.tunedHumidity) ? ", " + juce::String (juce::roundToInt (p.checks.tunedHumidity)) + " %" : juce::String())
                  << (p.checks.tunedConditionsOn.isNotEmpty() ? ", " + p.checks.tunedConditionsOn : juce::String()) << ")";
            kv.push_back ({ "Conditions", c });
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
        L.chapter ("Speakers");
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
                bool found = false;
                for (const auto& s : sys.sources) found = found || s.asFound.freq.size() > 4;
                if (found)
                {
                    const auto img2 = snapshotLight (graph, 820, 450, [] (ui::SpeakerGraph& gr) { gr.setWithProcessing (true); gr.setShowAsFound (true); gr.rebuild(); });
                    L.need (360.0f);
                    L.sub ("As found (dashed) and as left");
                    L.picture (img2, 330.0f);
                }
            }
        }
    }

    // ---------- level balance ----------
    if (has ("level"))
    {
        L.chapter ("Level balance");
        const auto& lv = p.level;
        if (lv.readings.empty()) L.para ("The levels were not balanced in this session.", 9.5f, dim);
        else
        {
            L.para ("Each speaker on its own, with its processing in place, at the listening points: its level through its own range (dB against "
                    "the reference). The first point is the centre, where left and right, and the sub and the tops, are compared.", 9.5f);
            std::vector<juce::StringArray> rows;
            for (int i = 0; i < (int) sys.sources.size(); ++i)
            {
                double lo, hi;
                sys.playRange (i, lo, hi);
                juce::StringArray row { sys.nameOf (i) };
                for (const auto& pt : lv.points)
                {
                    const auto* a = lv.find (i, pt, false);
                    const auto* b = lv.find (i, pt, true);
                    juce::String cell = a != nullptr ? juce::String (a->band (lo, hi), 1) : juce::String ("-");
                    if (b != nullptr) cell << " -> " << juce::String (b->band (lo, hi), 1);
                    row.add (cell);
                }
                row.add (std::abs (sys.sources[(size_t) i].trimDb) > 0.04 ? (sys.sources[(size_t) i].trimDb > 0.0 ? "+" : "") + juce::String (sys.sources[(size_t) i].trimDb, 1) + " dB" : juce::String ("-"));
                rows.push_back (row);
            }
            juce::StringArray head { "Speaker" };
            std::vector<float> widths { 2.0f };
            for (const auto& pt : lv.points) { head.add (pt + " (dB)"); widths.push_back (1.4f); }
            head.add ("Trim");
            widths.push_back (1.0f);
            L.table (widths, head, rows);
            L.para ("\"a -> b\": as found, then the check after the trims.", 8.0f, dim);
            juce::StringArray lines;
            for (const auto& t : lv.suggest (sys, room.target, true, p.levelAims())) lines.add (t.why);
            for (const auto& t : LevelBalance::cabinetNotes (sys)) lines.add (t.why);
            if (! lines.isEmpty()) { L.sub ("Result"); for (const auto& t : lines) L.para (t, 9.5f); }
        }
    }

    // ---------- acoustics ----------
    if (has ("coverage"))
    {
        const auto rows = coverage::analyse (sys, p.level.points, p.plan, p.pref ("coverageTol"), [&p] (int k, const juce::String& pt)
        {
            const auto* r = p.level.find (k, pt, false);
            return r != nullptr ? r->band (coverage::kLo, coverage::kHi) : NAN;
        });
        if (! rows.empty())
        {
            L.chapter ("Aim and coverage");
            const auto pm = juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"));
            L.para ("Each speaker alone, at the points it covers (within 6 dB of the loudest top there), 1 - 4 kHz, against its own loudest point. "
                    "The spec is " + pm + juce::String (p.pref ("coverageTol"), 1) + " dB." + (p.coverage.done ? " Accepted " + p.coverage.doneOn + "." : juce::String()), 9.5f);
            std::vector<juce::StringArray> t;
            for (const auto& r : rows)
            {
                juce::StringArray pts;
                for (const auto& cp : r.points) pts.add (cp.point + " " + (cp.point == r.onAxis ? juce::String ("0") : juce::String (cp.db, 1)));
                juce::String before = "-";
                if (auto b = p.coverage.before.find (r.spk); b != p.coverage.before.end())
                {
                    double best = -1e9, worst = 1e9;
                    for (const auto& [k, v] : b->second) { best = std::max (best, v); worst = std::min (worst, v); }
                    before = juce::String (best - worst, 1) + " dB";
                }
                t.push_back ({ sys.nameOf (r.spk), pts.joinIntoString (",  "), juce::String (-r.worstDb, 1) + " dB", before, r.ok ? juce::String ("within spec") : juce::String ("out") });
            }
            L.table ({ 1.2f, 4.0f, 0.9f, 1.0f, 1.0f }, { "Speaker", "Points (dB)", "Spread", "Before", "Verdict" }, t, 8.0f);
            for (const auto& r : rows) if (r.advice.isNotEmpty()) L.para (r.advice, 9.0f);
        }
    }

    if (has ("acoustics"))
    {
        L.chapter ("Room acoustics");
        if (room.acousticsCount() == 0) L.para ("The room's acoustics were not measured in this session.", 9.5f, dim);
        else
        {
            const auto a = room.acousticsAverage();
            double rtLo, rtHi, stiMin;
            RoomTuning::useTargets (room.use, rtLo, rtHi, stiMin);
            std::array<double, acoustics::kNoiseBands> L8 {};
            juce::String who;
            const bool noise = ui::noiseOctaves (p, L8, who);
            auto n2 = [] (double v, int d) { return ! std::isfinite (v) ? juce::String ("-") : d == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, d); };
            const double stiShown = std::isfinite (acs.stiCorrected) ? acs.stiCorrected : a.sti;
            L.tiles ({ { "RT (500 Hz - 1 kHz)", n2 (a.rtMid(), 2), "s" }, { "EDT", n2 (a.mid (&acoustics::Band::edt), 2), "s" },
                       { "C50 / C80", n2 (a.mid (&acoustics::Band::c50), 1) + " / " + n2 (a.mid (&acoustics::Band::c80), 1), "dB" },
                       { std::isfinite (acs.stiCorrected) ? "STI (with noise)" : "STI", n2 (stiShown, 2), acoustics::stiRating (stiShown) },
                       { "Noise", noise ? "NC " + juce::String (juce::roundToInt (acoustics::noiseCriterion (L8))) : juce::String ("-"), "" } });
            L.para ("Room use: " + RoomTuning::useNames()[room.use] + ". Typical reverberation for it: " + juce::String (rtLo, 1) + " - " + juce::String (rtHi, 1)
                    + " s; speech intelligibility (STI) of " + juce::String (stiMin, 2) + " or more.", 9.0f);

            ui::AcousticsChart chart (p);
            const auto img = snapshotLight (chart, 820, 380, [] (ui::AcousticsChart& c) { c.setMetric (0); });
            L.need (290.0f);
            L.picture (img, 270.0f);

            std::vector<juce::StringArray> rows;
            for (int b = -1; b < acoustics::kBands; ++b)
            {
                const auto& o = b < 0 ? a.b63 : a.band[(size_t) b];
                const bool unsure = b < 0 && ! acs.rt63Reliable;
                rows.push_back ({ (b < 0 ? juce::String ("63") : juce::String (juce::roundToInt (acoustics::octaves()[(size_t) b]))) + " Hz" + (unsure ? " *" : ""),
                                  n2 (o.edt, 2), n2 (o.t20, 2), n2 (o.t30, 2), n2 (o.c50, 1), n2 (o.c80, 1), n2 (o.d50 * 100.0, 0) + " %", n2 (o.ts, 0) });
            }
            L.sub ("Per octave (average of the positions)");
            L.table ({ 1.0f, 0.9f, 0.9f, 0.9f, 0.9f, 0.9f, 0.8f, 0.8f }, { "Octave", "EDT (s)", "T20 (s)", "T30 (s)", "C50 (dB)", "C80 (dB)", "D50", "Ts (ms)" }, rows, 8.0f);
            if (! acs.rt63Reliable) L.para ("* 63 Hz: not enough decay above the noise to trust.", 8.0f, dim);
            {
                std::vector<juce::StringArray> t3 { {}, {} };
                juce::StringArray h3;
                for (int b = 0; b < acoustics::kThirds; ++b)
                {
                    h3.add (ui::freqLabel (acoustics::thirds()[(size_t) b]));
                    t3[0].add (n2 (a.rt3[(size_t) b], 2));
                }
                t3.pop_back();
                L.sub ("Reverberation time per 1/3 octave (s)");
                L.table (std::vector<float> (acoustics::kThirds, 1.0f), h3, t3, 6.5f);
            }
            {
                const bool feet = st.distanceUnit.load() == 0;
                auto dist = [feet] (double m) { return ! std::isfinite (m) ? juce::String ("-") : feet ? juce::String (juce::roundToInt (m * 3.28084)) + " ft" : juce::String (m, 1) + " m"; };
                std::vector<std::pair<juce::String, juce::String>> kv;
                kv.push_back ({ "Bass ratio (warmth)", n2 (acs.bassRatio, 2) + "   (1.0 - 1.3 is usual; above means bass build-up)" });
                kv.push_back ({ "Treble ratio (brilliance)", n2 (acs.trebleRatio, 2) });
                kv.push_back ({ "Centre time Ts (500 Hz - 1 kHz)", n2 (acs.tsMid, 0) + " ms" });
                kv.push_back ({ "STI", "room only " + n2 (acs.stiRaw, 2) + (std::isfinite (acs.stiCorrected) ? ";  with " + juce::String (acs.noiseKnown ? "the noise, " : "")
                                                                                + "masking and the hearing threshold at " + juce::String (juce::roundToInt (p.pref ("speechDbA"))) + " dBA speech: " + n2 (acs.stiCorrected, 2)
                                                                           : juce::String()) });
                kv.push_back ({ "%ALcons", std::isfinite (acs.alcons) ? n2 (acs.alcons, 1) + " % (" + acoustics::alconsRating (acs.alcons) + ")" : juce::String ("-") });
                kv.push_back ({ "First reflection (ITDG)", std::isfinite (acs.itdgMs) ? n2 (acs.itdgMs, 1) + " ms after the direct sound" : juce::String ("-") });
                kv.push_back ({ "Echo", acs.echo ? n2 (acs.echoMs, 0) + " ms after the direct sound, its path about " + dist (acs.echoExtraM) + " longer: audible (Dietsch / Kraak criterion)"
                                                 : "none audible (speech EK " + n2 (a.echoSpeech, 2) + " of 1.0, music " + n2 (a.echoMusic, 2) + " of 1.8)" });
                kv.push_back ({ "Flutter echo", std::isfinite (acs.flutterMs) ? "repeats every " + n2 (acs.flutterMs, 0) + " ms (parallel surfaces about " + dist (acs.flutterSpacingM) + " apart)"
                                                                            : juce::String ("none found") });
                if (acs.ringingThird >= 0)
                    kv.push_back ({ "Ringing third octave", ui::freqLabel (acoustics::thirds()[(size_t) acs.ringingThird]) + " Hz, " + n2 (acs.ringingRatio, 1) + " times its neighbours" });
                if (acs.volume > 0.0)
                {
                    kv.push_back ({ "Room volume", feet ? n2 (acs.volume * 35.3147, 0) + " cu ft" : n2 (acs.volume, 0) + " m3" });
                    kv.push_back ({ "Schroeder frequency", n2 (acs.schroederHz, 0) + " Hz (below it, single room modes rule)" });
                    juce::StringArray dc;
                    for (const auto& d : acs.criticalDistance) dc.add (d.name + " " + dist (d.metres) + " (Q " + n2 (d.q, 0) + ")");
                    if (! dc.isEmpty()) kv.push_back ({ "Critical distance", dc.joinIntoString (",  ") });
                }
                else kv.push_back ({ "Room volume", "not given (the Schroeder frequency and the critical distance need it)" });
                if (std::isfinite (acs.coverage2k))
                {
                    const auto pm = juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"));
                    kv.push_back ({ "Coverage", pm + n2 (acs.coverage2k / 2.0, 1) + " dB at 2 kHz, " + pm + n2 (acs.coverage4k / 2.0, 1) + " dB at 4 kHz across "
                                                    + juce::String (acs.coveragePoints) + " points (a common spec is " + pm + "3 dB)" });
                }
                L.sub ("More measures");
                L.keyValues (kv);
                std::vector<juce::StringArray> sp;
                for (const auto& r : acs.spread)
                    if (r.s.n > 0)
                        sp.push_back ({ r.what, n2 (r.s.mean, r.decimals) + " " + r.unit, r.s.n > 1 ? n2 (r.s.sd, r.decimals) : juce::String ("-"),
                                        n2 (r.s.lo, r.decimals) + " - " + n2 (r.s.hi, r.decimals), juce::String (r.s.n) });
                if (! sp.empty())
                {
                    L.sub ("Across the points");
                    L.table ({ 1.6f, 1.0f, 1.0f, 1.4f, 0.6f }, { "Measure", "Average", "Std. dev.", "Range", "Points" }, sp, 8.0f);
                }
            }

            std::vector<juce::StringArray> prow;
            if (room.acousticsFromSweeps())
            {
                for (const auto& w : room.sweeps)
                    if (w.ac.valid)
                        prow.push_back ({ w.position + " (sweep)", n2 (w.ac.rtMid(), 2), n2 (w.ac.mid (&acoustics::Band::edt), 2), n2 (w.ac.mid (&acoustics::Band::c50), 1),
                                          n2 (w.ac.mid (&acoustics::Band::c80), 1), n2 (w.ac.sti, 2) + " " + acoustics::stiRating (w.ac.sti) });
            }
            else for (const auto& m : room.meas)
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
            {
                juce::String why;
                room.acousticsVerdict (why);
                L.sub ("What it means");
                L.para (why, 10.0f);
            }
            L.para (juce::String (room.acousticsFromSweeps() ? "From sweeps through the system" : "Estimated from the Room EQ pink noise")
                        + " at the listening points (ISO 3382-1 and -2 decay and spread, IEC 60268-16 indirect STI with the noise, masking and threshold corrections, "
                        "Dietsch / Kraak echo criterion). A full acoustic survey uses an omnidirectional source.", 8.0f, dim);
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
            rkv.push_back ({ "Room acoustics allowance", ! room.allow.wanted ? juce::String ("off (the average fitted as measured)")
                                                         : room.allow.on ? juce::String ("on") : juce::String ("on, but room acoustics were not measured (no effect)") });
            L.keyValues (rkv);
            if (room.allow.on && ! room.allow.notes.isEmpty())
            {
                L.sub ("Allowing for the room acoustics");
                for (const auto& n : room.allow.notes) L.para (n, 9.0f);
            }

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
                L.para ("Limits: " + juce::String (room.maxBands) + " bands, cuts to " + juce::String (juce::roundToInt (p.pref ("eqMaxCut"))) + " dB, "
                        + (room.maxBoostDb > 0.0 ? "boosts to " + juce::String (juce::roundToInt (room.maxBoostDb)) + " dB (wide dips only)." : juce::String ("no boosts."))
                        + (room.baseEq.empty() ? juce::String() : " Added to the " + juce::String ((int) room.baseEq.size()) + " bands already on the speaker when measured."),
                        8.0f, dim);
            }
        }
    }

    // ---------- feedback ----------
    if (has ("headroom") && ! p.headroom.runs.empty())
    {
        L.chapter ("Gain and headroom");
        const double target = p.pref ("targetDbA"), wanted = p.pref ("headroomDb");
        L.para ("Pink noise in steps through the system, read at FOH. Show level " + juce::String ((int) target) + " dBA, headroom wanted "
                + juce::String ((int) wanted) + " dB.", 9.5f);
        std::vector<juce::StringArray> t;
        for (const auto& run : p.headroom.runs)
        {
            int lv = 0;
            juce::String v = run.output == 0 ? headroom::verdict (run, target, wanted, st.micSplCalibrated[0].load(), lv)
                                             : run.compressedAt >= 0 ? juce::String (run.distorted ? "Distorts" : "Compresses") + " at " + juce::String (juce::roundToInt (run.steps[(size_t) run.compressedAt].genDb)) + " dBFS drive"
                                                                     : juce::String ("Clean at the show drive plus headroom");
            juce::StringArray steps;
            for (const auto& s2 : run.steps) if (std::isfinite (s2.splDb)) steps.add (juce::String (juce::roundToInt (s2.genDb)) + ">" + juce::String (s2.splDb, 1));
            t.push_back ({ run.name, run.when, std::isfinite (run.cleanMax()) ? juce::String (run.cleanMax(), 1) : juce::String ("-"), v, steps.joinIntoString ("  ") });
        }
        L.table ({ 1.1f, 1.0f, 0.8f, 3.0f, 3.0f }, { "Run", "When", "Clean max", "Result", "Steps (dBFS > level)" }, t, 7.5f);
        std::vector<juce::StringArray> amps;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const auto& s2 = sys.sources[(size_t) i];
            if (s2.ampGain.isNotEmpty() || s2.limiter.isNotEmpty()) amps.push_back ({ sys.nameOf (i), sheet::outputWords (s2), s2.ampGain, s2.limiter });
        }
        if (! amps.empty())
        {
            L.sub ("Amps and limiters (as set)");
            L.table ({ 1.5f, 1.0f, 1.5f, 1.5f }, { "Speaker", "Output", "Amp gain", "Limiter" }, amps, 8.0f);
        }
    }

    if (has ("listening") && (p.listening.done || ! p.listening.points.empty()))
    {
        L.chapter ("Listening check");
        const auto& l = p.listening;
        L.para ((l.music.isNotEmpty() ? "Music: " + l.music + ". " : juce::String()) + (l.done ? "Done " + l.doneOn + "." : juce::String ("Not finished.")), 9.5f);
        std::vector<juce::StringArray> t;
        for (const auto& pt : p.positions)
        {
            const auto it = l.points.find (pt);
            if (it == l.points.end()) { t.push_back ({ pt, "-", "", "" }); continue; }
            t.push_back ({ pt, it->second.verdict == 1 ? juce::String ("good") : it->second.verdict == 2 ? juce::String ("a problem") : juce::String ("-"),
                           it->second.compared ? juce::String ("yes") : juce::String ("no"), it->second.notes });
        }
        L.table ({ 1.2f, 0.8f, 0.9f, 5.0f }, { "Point", "Verdict", "EQ compared", "Notes" }, t, 8.0f);
    }

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

    // ---------- processor sheet ----------
    if (has ("sheet"))
    {
        L.chapter ("Processor sheet");
        L.para ("Every setting to enter, output by output. Filters in dB/octave; gain includes the crossover's level and the level-balance trim.", 9.5f);
        std::vector<juce::StringArray> rows;
        for (const auto& ln : sheet::lines (sys))
        {
            const auto rw = sheet::rowOf (sys, ln);
            rows.push_back ({ sheet::entered (sys, ln) ? "yes" : "no", (rw.member ? "  " : "") + rw.name, rw.output, rw.hpf, rw.lpf,
                              rw.delay.upToFirstOccurrenceOf ("  (", false, false), rw.polarity, rw.gain });
        }
        L.table ({ 0.7f, 1.8f, 1.1f, 2.0f, 2.0f, 1.0f, 1.0f, 1.6f }, { "Entered", "Speaker", "Output", "High-pass", "Low-pass", "Delay", "Polarity", "Gain" }, rows, 7.5f);
        for (const auto& ln : sheet::lines (sys))
        {
            const auto rw = sheet::rowOf (sys, ln);
            if (rw.eq == "none") continue;
            L.sub (rw.name + " EQ");
            L.para (rw.eq, 9.0f);
        }
        if (! p.history.entries.empty())
        {
            L.sub ("Change history (newest first)");
            std::vector<juce::StringArray> hist;
            for (int i = (int) p.history.entries.size(); --i >= 0 && hist.size() < 60;)
            {
                const auto& h = p.history.entries[(size_t) i];
                hist.push_back ({ h.when, h.output, h.what, h.step + ": " + h.why });
            }
            L.table ({ 1.0f, 1.0f, 3.4f, 2.0f }, { "When", "Output", "Change", "Step and why" }, hist, 7.0f);
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
        {
            const auto own = p.venuePrefs.describe();
            if (! own.isEmpty())
            {
                L.sub ("This venue's own tuning values (instead of the defaults)");
                for (const auto& t : own) L.para (t, 9.0f);
            }
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
