// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The tuning report: a PDF of the open session's system tuning, in the order of the tuning steps. A cover with the
// contents, then chapters: a summary (what the report is about, the system as found and as left, the steps not done),
// the venue plan (landscape), the speakers (responses as found and as left, polarity, crossover, time alignment), the
// level balance, room acoustics with its verdict, room EQ, the feedback test, the processor sheet, the measurement
// mics with their serial numbers and calibration, and the measured traces. Steps not done in the session get no page.
// Sections can be left out (Reports > Tuning report).
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

namespace detail
{
// "18 Oct 2026 14:05" (how LiveAlign stamps changes and measurements) in minutes, for ordering; -1 when unreadable.
inline juce::int64 stampMinutes (const juce::String& t)
{
    juce::StringArray v;
    v.addTokens (t.trim(), " :", {});
    v.removeEmptyStrings();
    if (v.size() < 3) return -1;
    static const char* months[] = { "jan", "feb", "mar", "apr", "may", "jun", "jul", "aug", "sep", "oct", "nov", "dec" };
    int mon = -1;
    for (int i = 0; i < 12; ++i) if (v[1].substring (0, 3).equalsIgnoreCase (months[i])) mon = i;
    const int day = v[0].getIntValue(), year = v[2].getIntValue();
    if (mon < 0 || day < 1 || day > 31 || year < 2000) return -1;
    const int hh = v.size() >= 5 ? v[3].getIntValue() : 0, mm = v.size() >= 5 ? v[4].getIntValue() : 0;
    return (((juce::int64) year * 12 + mon) * 31 + (day - 1)) * 1440 + hh * 60 + mm;
}

// The crossover's frequency (the high speaker's high-pass, else the low speaker's low-pass), 0 when there is none.
inline double crossoverFreq (const SpeakerSystem& sys)
{
    if (! sys.pairValid()) return 0.0;
    const auto& hp = sys.sources[(size_t) sys.xoHigh].proc.hp;
    const auto& lp = sys.sources[(size_t) sys.xoLow].proc.lp;
    return hp.active() ? hp.freq : lp.active() ? lp.freq : 0.0;
}

// Whether a time alignment still matches the crossover: measured (or checked) before the crossover filters of
// either speaker last changed, or a sub and a top checked away from their crossover.
struct AlignmentState { bool delayStale = false, checkStale = false, checkOffXo = false; double xoFreq = 0.0; };
inline AlignmentState alignmentState (const SpeakerSystem& sys, const ChangeLog& history, const Alignment& a)
{
    AlignmentState st;
    const auto nameA = sys.nameOf (a.ref), nameB = sys.nameOf (a.align);
    juce::int64 lastXo = -1;
    for (const auto& e : history.entries)
        if (e.what.startsWith ("Crossover") && (e.output == nameA || e.output == nameB))
            lastXo = std::max (lastXo, stampMinutes (e.when));
    const auto measured = stampMinutes (a.measuredOn), checked = stampMinutes (a.checkedOn);
    st.delayStale = a.measured() && lastXo >= 0 && measured >= 0 && lastXo > measured;
    st.checkStale = a.checked() && lastXo >= 0 && checked >= 0 && lastXo > checked;
    const bool subA = sys.sources[(size_t) a.ref].role == (int) SpeakerRole::Sub, subB = sys.sources[(size_t) a.align].role == (int) SpeakerRole::Sub;
    st.xoFreq = subA != subB ? crossoverFreq (sys) : 0.0;
    st.checkOffXo = a.checked() && st.xoFreq > 0.0 && a.checkFreq > 0.0 && std::abs (std::log2 (a.checkFreq / st.xoFreq)) > 0.2;
    return st;
}

// The change history, newest first, with a run of changes to the same setting on the same output in one row
// ("Delay changed 6 times, now 3.05 ms").
inline std::vector<juce::StringArray> historyRows (const ChangeLog& h, size_t maxRows)
{
    std::vector<juce::StringArray> out;
    auto kind = [] (const juce::String& w) { return w.upToFirstOccurrenceOf (" ", false, false); };
    auto latest = [] (const juce::String& w)
    {
        return w.contains ("->") ? w.fromLastOccurrenceOf ("->", false, false).trim() : w.fromFirstOccurrenceOf (" ", false, false).trim();
    };
    for (int i = (int) h.entries.size(); --i >= 0 && out.size() < maxRows;)
    {
        const auto& e = h.entries[(size_t) i];
        juce::StringArray why;
        why.add (e.step + ": " + e.why);
        int j = i;
        while (j > 0 && h.entries[(size_t) j - 1].output == e.output && kind (h.entries[(size_t) j - 1].what) == kind (e.what))
        {
            --j;
            why.addIfNotAlreadyThere (h.entries[(size_t) j].step + ": " + h.entries[(size_t) j].why);
        }
        const int count = i - j + 1;
        juce::String what = e.what, when = e.when;
        if (count > 1)
        {
            const auto k = kind (e.what);
            const auto now = latest (e.what);
            what = k + " changed " + juce::String (count) + " times" + (k != "Crossover" && now.isNotEmpty() ? ", now " + now : juce::String());
            const auto& first = h.entries[(size_t) j].when;
            const auto day = e.when.upToLastOccurrenceOf (" ", false, false);
            if (first != e.when)
                when = first.upToLastOccurrenceOf (" ", false, false) == day ? day + " " + first.fromLastOccurrenceOf (" ", false, false) + " - " + e.when.fromLastOccurrenceOf (" ", false, false)
                                                                              : first + " - " + e.when;
        }
        if (why.size() > 3) { why.removeRange (3, why.size() - 3); why.add ("..."); }
        out.push_back ({ when, e.output, what, why.joinIntoString ("; ") });
        i = j;
    }
    return out;
}

// The noise floor check's octave bands in dBFS (the loudest mic), for when the levels are not calibrated.
inline bool noiseOctavesFs (const LiveAlignProcessor& p, std::array<double, acoustics::kNoiseBands>& out, juce::String& who)
{
    const auto& c = p.checks;
    if (! c.noiseDone) return false;
    int best = -1;
    for (int m = 0; m < c.noiseMics && m < kMaxMics; ++m)
        if (c.noiseOct[(size_t) m][3] > -150.0f && (best < 0 || c.noiseA[(size_t) m] > c.noiseA[(size_t) best])) best = m;
    if (best < 0) return false;
    const double off = c.noiseIsSpl[(size_t) best] ? (double) p.settings.micSplOffset[best].load() : 0.0;
    for (int b = 0; b < acoustics::kNoiseBands; ++b)
    {
        const float v = c.noiseOct[(size_t) best][(size_t) b];
        out[(size_t) b] = v > -150.0f ? (double) v - off : NAN;
    }
    who = p.mics.nameOf (best);
    return true;
}

// Acoustics that can't be a real room (a decay far too short, octaves or measures that disagree wildly): what looks
// wrong, or empty when it is plausible.
inline juce::String implausibleAcoustics (const acoustics::Result& a, double rt, double edt)
{
    if (std::isfinite (rt) && rt < 0.1) return "an RT of " + juce::String (rt, 2) + " s is shorter than any real room";
    if (std::isfinite (edt) && edt < 0.1) return "an EDT of " + juce::String (edt, 2) + " s is shorter than any real room";
    double lo = 1e9, hi = 0.0;
    for (int b = 0; b + 1 < acoustics::kBands; ++b)   // 125 Hz - 4 kHz
    {
        const auto& o = a.band[(size_t) b];
        const double v = o.rt();
        if (std::isfinite (v) && v > 0.0) { lo = std::min (lo, v); hi = std::max (hi, v); }
        if ((std::isfinite (v) && v < 0.05) || (std::isfinite (o.edt) && o.edt < 0.05))
            return "the decay at " + hzText (acoustics::octaves()[(size_t) b]) + " is under 0.05 s";
    }
    if (hi > 0.0 && lo < 1e8 && hi / lo > 5.0)
        return "the reverberation time varies more than five times between octaves (" + juce::String (lo, 2) + " - " + juce::String (hi, 2) + " s)";
    if (std::isfinite (rt) && std::isfinite (edt) && rt > 0.0 && (edt / rt > 3.0 || edt / rt < 0.25))
        return "the EDT (" + juce::String (edt, 2) + " s) and the RT (" + juce::String (rt, 2) + " s) are too far apart";
    return {};
}
} // namespace detail

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
    auto n2 = [] (double v, int d) { return ! std::isfinite (v) ? juce::String ("-") : d == 0 ? juce::String (juce::roundToInt (v)) : juce::String (v, d); };

    // ---------- what the report shows, worked out once (the same numbers everywhere) ----------
    const bool splCal = ! p.mics.noCalibrator;   // no calibrator: levels stay in dBFS, and nothing that needs dB SPL is shown
    const auto& acAvg = acs.avg;
    auto meanOf = [&room] (auto f)
    {
        double s = 0.0;
        int k = 0;
        for (const auto* x : room.acousticsResults()) { const double v = f (*x); if (std::isfinite (v)) { s += v; ++k; } }
        return k > 0 ? s / k : NAN;
    };
    // the mid-band values are the averages of the positions (as "Across the points" shows them)
    double rtShown = meanOf ([] (const acoustics::Result& x) { return x.rtMid(); });
    double edtShown = meanOf ([] (const acoustics::Result& x) { return x.mid (&acoustics::Band::edt); });
    double c50Shown = meanOf ([] (const acoustics::Result& x) { return x.mid (&acoustics::Band::c50); });
    double c80Shown = meanOf ([] (const acoustics::Result& x) { return x.mid (&acoustics::Band::c80); });
    if (! std::isfinite (rtShown)) rtShown = acs.rtMid;
    if (! std::isfinite (edtShown)) edtShown = acs.edtMid;
    if (! std::isfinite (c50Shown)) c50Shown = acs.c50;
    if (! std::isfinite (c80Shown)) c80Shown = acs.c80;
    const bool stiIsCorrected = splCal && std::isfinite (acs.stiCorrected);
    const double stiShown = stiIsCorrected ? acs.stiCorrected : acs.stiRaw;
    const juce::String stiNote = stiIsCorrected ? juce::String (acs.noiseKnown ? "with the noise" : "with masking")
                                                : juce::String (splCal ? "room only" : "uncorrected: the mic was not calibrated");
    const juce::String stiTile = stiIsCorrected ? juce::String (acs.noiseKnown ? "STI (with noise)" : "STI (corrected)")
                                                : juce::String (splCal ? "STI" : "STI (uncorrected)");
    const double alconsShown = acoustics::alcons (stiShown);
    juce::String acVerdict;
    const auto acOdd = room.acousticsCount() > 0 ? implausibleAcoustics (acAvg, rtShown, edtShown) : juce::String();
    {
        const int v = room.acousticsVerdict (acVerdict);
        if (acOdd.isNotEmpty())
            acVerdict = "Check the measurement: " + acOdd + ". This usually means the mic was too close to a speaker, the sweep clipped, or the "
                        "noise was too high. Measure again before acting on these numbers.";
        else if (v == 1)
            acVerdict = "Room OK for EQ (RT " + n2 (rtShown, 2) + " s, STI " + n2 (stiShown, 2) + ", " + stiNote + ").";
        else if (std::isfinite (acAvg.rtMid()) && std::isfinite (rtShown))
            acVerdict = acVerdict.replace (juce::String (acAvg.rtMid(), 2) + " s", juce::String (rtShown, 2) + " s");
    }

    const auto covRows = coverage::analyse (sys, p.level.points, p.plan, p.pref ("coverageTol"), [&p] (int k, const juce::String& pt)
    {
        const auto* r = p.level.find (k, pt, false);
        return r != nullptr ? r->band (coverage::kLo, coverage::kHi) : NAN;
    });
    std::vector<const Trace*> tfTraces, rtaTraces;
    for (const auto& t : p.traces.all())
    {
        if (t.fromSession.isNotEmpty() || t.data.freq.size() < 4) continue;
        (t.kind == Trace::Transfer ? tfTraces : rtaTraces).push_back (&t);
    }
    const bool levelDone = ! p.level.readings.empty(), acousticsDone = room.acousticsCount() > 0,
               roomDone = room.count (false) > 0 || room.count (true) > 0, headroomDone = ! p.headroom.runs.empty(),
               listeningDone = p.listening.done || ! p.listening.points.empty(), feedbackDone = ! p.feedback.rings.empty(),
               tracesDone = ! tfTraces.empty() || ! rtaTraces.empty();
    // the steps left out of this session: no page of their own, one line in the summary
    juce::StringArray notDone;
    for (const auto& [id, done] : std::vector<std::pair<const char*, bool>> { { "level", levelDone }, { "coverage", ! covRows.empty() }, { "acoustics", acousticsDone },
                                                                             { "room", roomDone }, { "headroom", headroomDone }, { "listening", listeningDone },
                                                                             { "feedback", feedbackDone }, { "measurements", tracesDone } })
        if (has (id) && ! done)
            for (const auto& s : tuningSections()) if (juce::String (s.id) == id) notDone.add (s.title);
    const auto notDoneText = notDone.isEmpty() ? juce::String() : "Not done this session: " + notDone.joinIntoString (", ") + ".";

    // ---------- cover: the title and the contents only ----------
    L.y = cover (L, "SYSTEM TUNING REPORT", p.venueName, sessionName + "   -   " + LiveAlignProcessor::today());
    const float contentsY = L.y + 14.0f;

    // what the report is about: under the first chapter's heading (after the contents)
    L.intro = [&]
    {
        L.sub ("About this report");
        std::vector<std::pair<juce::String, juce::String>> kv;
        if (opt.author.isNotEmpty()) kv.push_back ({ "Prepared by", opt.author });
        kv.push_back ({ "Venue file", p.projectFile != juce::File() ? p.projectFile.getFileName() : juce::String ("not saved") });
        kv.push_back ({ "Speakers", p.speakers.configured ? "Set up " + p.speakers.configuredOn + (p.speakers.changed ? " (marked as changed since)" : "")
                                                          : juce::String ("Not marked as set up") });
        kv.push_back ({ "Room EQ", room.tuned ? "Tuned " + room.tunedOn : room.count (false) > 0 ? juce::String ("Measured, not marked as tuned") : juce::String ("Not measured") });
        kv.push_back ({ "Room acoustics", acousticsDone ? "Measured at " + juce::String (room.acousticsCount()) + (room.acousticsCount() == 1 ? " position" : " positions")
                                                              + (room.acousticsFromSweeps() ? " (sweeps)" : " (estimated from the Room EQ measurements)")
                                                        : juce::String ("Not measured") });
        kv.push_back ({ "Sound levels", splCal ? juce::String ("dB SPL (calibrated mics)") : juce::String ("dBFS: no calibrator, so no dB SPL, NC or NR") });
        kv.push_back ({ "Analysis", juce::String (p.getSampleRate() > 0.0 ? p.getSampleRate() / 1000.0 : 48.0, 1) + " kHz, FFT "
                                        + juce::String (st.fftSize.load() / 1024) + "k, " + juce::String (st.averages.load()) + " averages, minimum coherence "
                                        + juce::String (juce::roundToInt (st.coherenceBlank.load() * 100.0f)) + " %" });
        L.keyValues (kv);
        if (p.venueNotes.isNotEmpty()) { L.sub ("Venue notes"); L.para (p.venueNotes); }
        if (! has ("summary") && notDoneText.isNotEmpty()) L.para (notDoneText, 9.0f, dim);
        L.para ("Methods: room acoustics per ISO 3382-1 (RT, EDT, C50, C80, Ts) from swept-sine impulse responses (ISO 18233); STI per IEC 60268-16; "
                "sound levels A- and C-weighted per IEC 61672-1; NC per ANSI S12.2 and NR per ISO 1996; air absorption per ISO 9613-1; coverage "
                "uniformity in the spirit of AVIXA A102.01.", 8.0f, dim);
    };

    // the time alignments against the crossover as it is now
    int alignStale = 0;
    for (const auto& a : sys.alignments)
    {
        if (a.ref < 0 || a.align < 0 || a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) continue;
        const auto as = alignmentState (sys, p.history, a);
        alignStale += as.delayStale || as.checkStale || as.checkOffXo ? 1 : 0;
    }

    // ---------- summary ----------
    if (has ("summary"))
    {
        L.chapter ("Summary: as found and as left");
        L.sub ("This session");
        std::vector<std::pair<juce::String, juce::String>> kv;
        {
            int measured = 0, imported = 0, following = 0;
            for (int i = 0; i < (int) sys.sources.size(); ++i)
            {
                const auto& s = sys.sources[(size_t) i];
                if (s.hasResponse()) (s.imported ? imported : measured) += 1;
                else if (sys.covered (i)) ++following;
            }
            juce::String t = juce::String (measured) + " of " + juce::String ((int) sys.sources.size()) + " measured"
                             + (measured > 0 ? (sys.placement == 0 ? " close up" : " from one spot") : juce::String());
            if (imported > 0) t << ", " << imported << " imported (modelled responses, not measured)";
            if (following > 0) t << ", " << following << " following an imported one";
            kv.push_back ({ "Speakers", t });
        }
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
                                                  + juce::String (checked) + " checked together"
                                                  + (alignStale > 0 ? ", " + juce::String (alignStale) + " to do again (they no longer match the crossover)" : juce::String()) });
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
        if (listeningDone)
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
        if (feedbackDone)
            kv.push_back ({ "Feedback test", juce::String ((int) p.feedback.rings.size()) + " rings on " + p.feedback.micName });
        if (acousticsDone)
            kv.push_back ({ "Room acoustics", acOdd.isNotEmpty() ? "RT " + n2 (rtShown, 2) + " s, EDT " + n2 (edtShown, 2) + " s: check the measurement (see Room acoustics)"
                                                                 : "RT " + n2 (rtShown, 2) + " s, STI " + n2 (stiShown, 2) + " (" + acoustics::stiRating (stiShown) + ", " + stiNote + ")" });
        L.keyValues (kv);
        if (notDoneText.isNotEmpty()) L.para (notDoneText, 9.0f, dim);

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
        L.picture (cropToContent (plan), L.bottom() - L.y - 10.0f, 26.0f);   // trimmed and a little wider than the text, so its labels print larger
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
                                  s.hasResponse() ? (s.imported ? "imported (modelled)" : s.nearField ? "measured close up" : "measured")
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
            const bool modelled = sys.sources[(size_t) sys.xoHigh].imported || sys.sources[(size_t) sys.xoLow].imported;
            if (r.valid)
            {
                if (modelled)
                    t << " Found by LiveAlign from modelled (imported) responses: the two meet " << juce::roundToInt (r.efficiency * 100.0)
                      << " % in phase through the crossover region.";
                else
                    t << " Found by LiveAlign: the two meet " << juce::roundToInt (r.efficiency * 100.0) << " % in phase through the crossover region, "
                      << juce::roundToInt (r.reliability * 100.0) << " % of it measured reliably.";
            }
            if (sys.pairNearField()) t << " Measured close up; the delay between them comes from the time alignment below.";
            L.para (t);
        }
        {
            std::vector<juce::StringArray> rows;
            bool anyStale = false;
            for (const auto& a : sys.alignments)
            {
                if (a.ref < 0 || a.align < 0 || a.ref >= (int) sys.sources.size() || a.align >= (int) sys.sources.size()) continue;
                const auto as = alignmentState (sys, p.history, a);
                const double need = a.neededMs (sys.sources[(size_t) a.ref].proc.delayMs);
                juce::String check = "-";
                if (a.checked())
                {
                    if (as.checkStale) check = "checked before the crossover changed: check again";
                    else if (as.checkOffXo) check = "checked at " + hzText (a.checkFreq) + ", not at the " + hzText (as.xoFreq) + " crossover: check again";
                    else check = (a.sumGainDb() >= a.idealSumGainDb() - 1.5 ? "in phase: " : "not adding: ") + juce::String (a.sumGainDb(), 1) + " of "
                                 + juce::String (a.idealSumGainDb(), 1) + " dB at " + hzText (a.checkFreq);
                }
                anyStale = anyStale || as.delayStale || as.checkStale || as.checkOffXo;
                rows.push_back ({ sys.endName (a.align, a.alignSet) + " to " + sys.endName (a.ref, a.refSet), a.spot,
                                  a.measured() ? juce::String (a.alignArrivalMs - a.refArrivalMs, 2) + " ms" : juce::String ("-"),
                                  a.measured() ? juce::String (need, 2) + " ms" + (as.delayStale ? " (measured before the crossover changed)" : "") : juce::String ("not measured"),
                                  check });
            }
            if (! rows.empty())
            {
                L.sub ("Time alignment");
                L.table ({ 2.4f, 1.4f, 1.2f, 1.6f, 2.6f }, { "Speakers", "Where", "Arrival diff.", "Delay", "Check (both together)" }, rows);
                if (anyStale)
                    L.para ("Marked rows no longer match the crossover as it is now: measure the time alignment (and its check) again. A sub and a top are "
                            "checked at their crossover frequency.", 8.0f, warn);
            }
        }
        {
            int measuredR = 0, importedR = 0;
            juce::StringArray importedNames;
            for (const auto& s : sys.sources)
                if (s.hasResponse() && s.show)
                {
                    if (s.imported) { ++importedR; importedNames.add (s.name); }
                    else ++measuredR;
                }
            if (measuredR + importedR > 0)
            {
                const double xoF = sys.realPair() ? crossoverFreq (sys) : 0.0;
                const juce::String what = importedR == 0 ? "Measured responses" : measuredR == 0 ? "Modelled (imported) responses" : "Measured and modelled responses";
                L.figure (what + " with the processing, and their predicted sum", speakerChart (sys, false, xoF), 330.0f);
                if (importedR > 0)
                    L.para ("Modelled: " + importedNames.joinIntoString (", ") + (importedR == 1 ? " is" : " are")
                            + " imported (a data sheet or a model), not measured here. Phase is shown only where each speaker plays and was measured reliably.", 8.0f, dim);
                else
                    L.para ("Phase is shown only where each speaker plays (within 24 dB of its peak) and was measured reliably.", 8.0f, dim);
                bool found = false;
                for (const auto& s : sys.sources) found = found || (s.show && s.asFound.freq.size() > 4);
                if (found) L.figure ("As found (dashed) and as left", speakerChart (sys, true, xoF), 330.0f);
            }
        }
    }

    // ---------- level balance ----------
    if (has ("level") && levelDone)
    {
        L.chapter ("Level balance");
        const auto& lv = p.level;
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

    // ---------- coverage ----------
    if (has ("coverage") && ! covRows.empty())
    {
        L.chapter ("Aim and coverage");
        const auto pm = juce::String (juce::CharPointer_UTF8 ("\xc2\xb1"));
        L.para ("Each speaker alone, at the points it covers (within 6 dB of the loudest top there), 1 - 4 kHz, against its own loudest point. "
                "The spec is " + pm + juce::String (p.pref ("coverageTol"), 1) + " dB." + (p.coverage.done ? " Accepted " + p.coverage.doneOn + "." : juce::String()), 9.5f);
        std::vector<juce::StringArray> t;
        for (const auto& r : covRows)
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
        for (const auto& r : covRows) if (r.advice.isNotEmpty()) L.para (r.advice, 9.0f);
    }

    // ---------- acoustics ----------
    if (has ("acoustics") && acousticsDone)
    {
        L.chapter ("Room acoustics");
        const auto& a = acAvg;
        double rtLo, rtHi, stiMin;
        RoomTuning::useTargets (room.use, rtLo, rtHi, stiMin);
        // background noise: octave bands in dB SPL from a calibrated mic, else dBFS (and no NC or NR)
        std::array<double, acoustics::kNoiseBands> L8 {};
        juce::String who;
        const bool noiseSpl = splCal && ui::noiseOctaves (p, L8, who);
        const bool noiseFs = ! noiseSpl && noiseOctavesFs (p, L8, who);
        L.tiles ({ { "RT (500 Hz - 1 kHz)", n2 (rtShown, 2), "s" }, { "EDT", n2 (edtShown, 2), "s" },
                   { "C50 / C80", n2 (c50Shown, 1) + " / " + n2 (c80Shown, 1), "dB" },
                   { stiTile, n2 (stiShown, 2), acoustics::stiRating (stiShown) },
                   { "Noise", noiseSpl ? "NC " + juce::String (juce::roundToInt (acoustics::noiseCriterion (L8))) : juce::String ("-"), noiseFs ? "uncalibrated" : "" } });
        L.para ("Room use: " + RoomTuning::useNames()[room.use] + ". Typical reverberation for it: " + juce::String (rtLo, 1) + " - " + juce::String (rtHi, 1)
                + " s; speech intelligibility (STI) of " + juce::String (stiMin, 2) + " or more.", 9.0f);
        if (! room.stipa.empty())
        {
            // STIPA, measured directly at the seats
            L.sub ("Speech intelligibility, measured (STIPA)");
            std::vector<juce::StringArray> sr;
            for (const auto& w : room.stipa)
            {
                juce::StringArray row { w.position, w.micName, n2 (w.sti, 2), acoustics::stiRating (w.sti), n2 (acoustics::alcons (w.sti), 1) + " %" };
                for (double v : w.mti) row.add (n2 (v, 2));
                row.add (w.time);
                sr.push_back (row);
            }
            L.table ({ 1.1f, 0.9f, 0.6f, 0.75f, 0.75f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.6f },
                     { "Point", "Mic", "STIPA", "Rating", "ALcons", "125", "250", "500", "1k", "2k", "4k", "8k", "At" }, sr, 7.5f);
            bool corr = true, ext = false;
            for (const auto& w : room.stipa) { corr = corr && w.corrected; ext = ext || w.external; }
            L.para ("STIPA per IEC 60268-16 (the direct method): the STIPA test signal" + juce::String (ext ? " (from an external source)" : " (LiveAlign's generator)")
                    + " through the system, 18 s at each point, with the room's noise and reverberation as they were. Per octave: the modulation transfer index."
                    + juce::String (corr && splCal ? " Masking and hearing threshold corrections applied (calibrated mics)." : " Without the level corrections (a mic was not calibrated)."),
                    8.0f, dim);
        }

        ui::AcousticsChart chart (p);
        L.picture (snapshotLight (chart, 820, 380, [] (ui::AcousticsChart& c) { c.setMetric (0); }), 270.0f);

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
            std::vector<juce::StringArray> t3 { {} };
            juce::StringArray h3;
            for (int b = 0; b < acoustics::kThirds; ++b)
            {
                h3.add (freqText (acoustics::thirds()[(size_t) b]));
                t3[0].add (n2 (a.rt3[(size_t) b], 2));
            }
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
            if (stiIsCorrected)
                kv.push_back ({ "STI", "room only " + n2 (acs.stiRaw, 2) + ";  with " + juce::String (acs.noiseKnown ? "the noise, " : "")
                                           + "masking and the hearing threshold at " + juce::String (juce::roundToInt (p.pref ("speechDbA"))) + " dBA speech: " + n2 (acs.stiCorrected, 2) });
            else if (! splCal)
                kv.push_back ({ "STI", "room only " + n2 (acs.stiRaw, 2) + " (uncorrected: the noise, masking and hearing threshold corrections need a calibrated mic)" });
            else
                kv.push_back ({ "STI", "room only " + n2 (acs.stiRaw, 2) });
            kv.push_back ({ "%ALcons", std::isfinite (alconsShown) ? n2 (alconsShown, 1) + " % (" + acoustics::alconsRating (alconsShown) + ")" : juce::String ("-") });
            kv.push_back ({ "First reflection (ITDG)", std::isfinite (acs.itdgMs) ? n2 (acs.itdgMs, 1) + " ms after the direct sound" : juce::String ("-") });
            kv.push_back ({ "Echo", acs.echo ? n2 (acs.echoMs, 0) + " ms after the direct sound, its path about " + dist (acs.echoExtraM) + " longer: audible (Dietsch / Kraak criterion)"
                                             : "none audible (speech EK " + n2 (a.echoSpeech, 2) + " of 1.0, music " + n2 (a.echoMusic, 2) + " of 1.8)" });
            kv.push_back ({ "Flutter echo", std::isfinite (acs.flutterMs) ? "repeats every " + n2 (acs.flutterMs, 0) + " ms (parallel surfaces about " + dist (acs.flutterSpacingM) + " apart)"
                                                                        : juce::String ("none found") });
            if (acs.ringingThird >= 0)
                kv.push_back ({ "Ringing third octave", hzText (acoustics::thirds()[(size_t) acs.ringingThird]) + ", " + n2 (acs.ringingRatio, 1) + " times its neighbours" });
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
                    sp.push_back ({ r.what == "STI" && stiIsCorrected ? juce::String ("STI (room only)") : r.what, n2 (r.s.mean, r.decimals) + " " + r.unit,
                                    r.s.n > 1 ? n2 (r.s.sd, r.decimals) : juce::String ("-"), n2 (r.s.lo, r.decimals) + " - " + n2 (r.s.hi, r.decimals), juce::String (r.s.n) });
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
            if (m.ac.valid && m.include)
                prow.push_back ({ m.position + (m.after ? juce::String (" (check)") : juce::String()), n2 (m.ac.rtMid(), 2), n2 (m.ac.mid (&acoustics::Band::edt), 2),
                                  n2 (m.ac.mid (&acoustics::Band::c50), 1), n2 (m.ac.mid (&acoustics::Band::c80), 1), n2 (m.ac.sti, 2) + " " + acoustics::stiRating (m.ac.sti) });
        L.sub ("Each position");
        L.table ({ 2.0f, 0.9f, 0.9f, 0.9f, 0.9f, 1.3f }, { "Position", "RT (s)", "EDT (s)", "C50", "C80", stiIsCorrected ? "STI (room only)" : "STI" }, prow, 8.0f);

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
        if (noiseSpl || noiseFs)
        {
            std::vector<juce::StringArray> nrow { {} };
            juce::StringArray head { "" };
            nrow[0].add (noiseSpl ? "dB SPL" : "dBFS");
            for (int b = 0; b < acoustics::kNoiseBands; ++b) { head.add (juce::String (juce::roundToInt (acoustics::noiseOctaves()[(size_t) b])) + " Hz"); nrow[0].add (n2 (L8[(size_t) b], 0)); }
            L.sub ("Background noise (" + who + ", from the noise floor check)");
            L.table (std::vector<float> (9, 1.0f), head, nrow, 8.0f);
            if (noiseSpl)
                L.para ("NC " + juce::String (juce::roundToInt (acoustics::noiseCriterion (L8))) + " (noise criterion), NR "
                        + juce::String (juce::roundToInt (acoustics::noiseRating (L8))) + " (noise rating).", 9.0f);
            else
                L.para ("In dBFS: the mic was not calibrated. NC and NR need a calibrated mic.", 9.0f, dim);
        }
        L.sub ("What it means");
        L.para (acVerdict, 10.0f, acOdd.isNotEmpty() ? warn : ink);
        L.para (juce::String (room.acousticsFromSweeps() ? "From sweeps through the system" : "Estimated from the Room EQ pink noise")
                    + " at the listening points (ISO 3382-1 and -2 decay and spread, IEC 60268-16 indirect STI with the noise, masking and threshold corrections, "
                    "Dietsch / Kraak echo criterion). A full acoustic survey uses an omnidirectional source.", 8.0f, dim);
    }

    // ---------- room EQ ----------
    if (has ("room") && roomDone)
    {
        L.chapter ("Room EQ");
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
        L.figure ("Average of the positions against the target", img, 300.0f);
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

    // ---------- gain and headroom ----------
    if (has ("headroom") && headroomDone)
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

    if (has ("listening") && listeningDone)
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

    // ---------- feedback ----------
    if (has ("feedback") && feedbackDone)
    {
        L.chapter ("Feedback test");
        const auto& fbt = p.feedback;
        L.para ("Stage mic rung out: " + fbt.micName + ", listening with " + p.mics.nameOf (fbt.listenMic) + ". The gain was brought up until it rang; "
                "each ring was caught and given a narrow cut." + (fbt.done ? " Marked as done." : ""));
        std::vector<juce::StringArray> rows;
        for (const auto& r : fbt.rings)
            rows.push_back ({ juce::String (juce::roundToInt (r.ring.freq)) + " Hz", juce::String (juce::roundToInt (r.ring.prominenceDb)) + " dB",
                              juce::String (juce::roundToInt (r.ring.cutDb)) + " dB", juce::String (juce::roundToInt (r.ring.q)), r.applied ? "yes" : "no", r.mic, r.time });
        L.table ({ 1.1f, 1.0f, 0.9f, 0.6f, 0.8f, 1.8f, 0.8f }, { "Frequency", "Above", "Cut", "Q", "In place", "Stage mic", "Time" }, rows);
        L.para ("Cuts are narrow bells (Q 10, about 1/7 octave) at the ring frequency. Put them on that mic's channel or its output.", 8.0f, dim);
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
            // the delay always with two decimals, as on the speaker pages ("0.00 ms", not "0 ms")
            const auto delay = ! ln.group && ln.spk >= 0 && ln.spk < (int) sys.sources.size() ? juce::String (sys.sources[(size_t) ln.spk].proc.delayMs, 2) + " ms"
                                                                                                : rw.delay.upToFirstOccurrenceOf ("  (", false, false);
            rows.push_back ({ sheet::entered (sys, ln) ? "yes" : "no", (rw.member ? "  " : "") + rw.name, rw.output, rw.hpf, rw.lpf, delay, rw.polarity, rw.gain });
        }
        L.table ({ 0.9f, 1.8f, 1.1f, 2.0f, 2.0f, 1.1f, 1.0f, 1.5f }, { "Entered", "Speaker", "Output", "High-pass", "Low-pass", "Delay", "Polarity", "Gain" }, rows, 7.5f);
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
            L.table ({ 1.3f, 1.0f, 3.2f, 2.0f }, { "When", "Output", "Change", "Step and why" }, historyRows (p.history, 60), 7.0f);
        }
    }

    // ---------- mics ----------
    if (has ("mics"))
    {
        L.chapter ("Measurement mics and checks");
        {
            const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            std::vector<juce::StringArray> rows;
            for (int m = 0; m < nMics; ++m)
            {
                const auto ms = p.micModelSerial (m);
                const auto name = p.mics.nameOf (m);
                // the name once: when it is the model and serial (or part of it), the model / serial column says it
                const bool same = ms.isNotEmpty() && (ms.containsIgnoreCase (name) || name.containsIgnoreCase (ms));
                const auto mic = same ? "Mic " + juce::String (m + 1) : juce::String (m + 1) + ". " + name;
                const auto model = ms.isEmpty() ? juce::String ("-") : same && name.length() > ms.length() ? name : ms;
                // one calibration file per line, by angle, without the file extension
                const auto& c = p.calibrations (m);
                juce::String cal;
                if (c.any())
                {
                    juce::StringArray files;
                    for (int a : calSlotsInOrder())
                        if (c.has (a))
                        {
                            const auto& fn = c.name[a];
                            files.add (calAngleName (a) + " " + (fn.containsChar ('.') ? fn.upToLastOccurrenceOf (".", false, false) : fn));
                        }
                    cal = files.joinIntoString ("\n");
                }
                else cal = p.mics.noCalFile[(size_t) m] ? juce::String ("none (flat)") : juce::String ("not loaded");
                const bool slot0 = p.isMonitorMode() && m == 0;
                juce::String spl = p.mics.noCalibrator ? juce::String ("dBFS (no calibrator)")
                                 : (! slot0 && st.micSplCalibrated[m].load()) ? juce::String (st.micSplOffset[m].load(), 2) + " dB" + (p.checks.splVerified[(size_t) m] ? ", checked" : ", not re-checked")
                                                                              : juce::String (slot0 ? "see the venue file" : "not calibrated");
                rows.push_back ({ mic, model, p.mics.position[(size_t) m].isNotEmpty() ? p.mics.position[(size_t) m] : juce::String ("-"),
                                  juce::String (st.micInput[m].load() + 1), cal,
                                  p.ownAim (m) == 0 ? "0" + deg + " (at speaker)" : "90" + deg + " (up)", spl });
            }
            L.sub ("Mics");
            L.table ({ 1.2f, 1.8f, 1.1f, 0.6f, 2.6f, 1.2f, 1.6f }, { "Mic", "Model / serial", "Position", "Input", "Calibration files", "Aim", "Sound level" }, rows, 8.0f);
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
                {
                    // without a calibrator, in dBFS (taking off the old offset when it was read in dB SPL)
                    const bool spl = splCal && c.noiseIsSpl[(size_t) m];
                    const double v = (double) c.noiseA[(size_t) m] - (! spl && c.noiseIsSpl[(size_t) m] ? (double) st.micSplOffset[m].load() : 0.0);
                    n.add (p.mics.nameOf (m) + " " + juce::String (v, 1) + (spl ? " dBA" : " dBFS(A)"));
                }
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
    if (has ("measurements") && tracesDone)
    {
        L.chapter ("Measurements");
        auto listTraces = [&] (const std::vector<const Trace*>& v)
        {
            for (size_t i = 0; i < v.size(); ++i)
            {
                L.need (13.0f);
                doc.rect (L.kMargin, L.y - 7.0f, 14.0f, 4.0f, seriesColour ((int) i));
                doc.text (L.kMargin + 22.0f, L.y, v[i]->name + (v[i]->created.isNotEmpty() ? "   (" + v[i]->created + ")" : juce::String()), 8.5f, pdf::Document::Regular, ink);
                L.y += 12.0f;
            }
            L.y += 6.0f;
        };
        if (! tfTraces.empty())
        {
            L.figure ("Transfer functions" + juce::String (st.targetCurve.load() > 0 ? juce::String (" with the target (dashed)") : juce::String()),
                      traceChart (tfTraces, true, st.targetCurve.load()), 300.0f);
            listTraces (tfTraces);
        }
        if (! rtaTraces.empty())
        {
            L.figure ("RTA", traceChart (rtaTraces, false, 0), 300.0f);
            listTraces (rtaTraces);
        }
    }

    // nothing portrait was written (every section left out): what the report is about still gets its page
    if (L.intro) L.chapter ("About this report");

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
