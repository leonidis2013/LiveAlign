// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Tests for LiveAlign's tuning logic (with JUCE's core): level balance, the processor sheet, the quick re-check's
// record, preferences and venue overrides, the Room EQ allowance, aim and coverage, gain and headroom, seat-to-seat
// variance, the change history, the air, the venue list and the MQTT climate feed (against a tiny broker here). Built with LA_BUILD_TESTS (target AppTests).
#include <cstdio>
#include <functional>
#include <cstdlib>
#include <string>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include "LevelBalance.h"
#include "ProcessorSheet.h"
#include "Prefs.h"
#include "RoomTuning.h"
#include "ShowChecks.h"
#include "VenueLibrary.h"
#include "Analysis/AirAbsorption.h"
#include "ClimateFeed.h"
#include "ListenOutput.h"
#include "InterfaceGuard.h"
#include "Monitoring.h"
#include "Analysis/SplMeter.h"
#include "AudioHealth.h"
#include "TraceStore.h"

using namespace la;

static int failures = 0, checks = 0;

static void check (bool ok, const std::string& what, double got = NAN, double want = NAN)
{
    ++checks;
    if (! ok)
    {
        ++failures;
        std::printf ("FAIL  %s  (got %.4f, want %.4f)\n", what.c_str(), got, want);
    }
}

static bool near (double a, double b, double tol) { return std::isfinite (a) && std::abs (a - b) <= tol; }

// A flat transfer function at `db` (1/12 octave, 20 Hz - 20 kHz).
static FreqTable flat (double db)
{
    FreqTable t;
    for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 12.0)) { t.freq.push_back (f); t.db.push_back (db); }
    return t;
}

static SpeakerSource speaker (const char* name, SpeakerRole role, SpeakerChannel ch, int out)
{
    SpeakerSource s;
    s.name = name;
    s.role = (int) role;
    s.channel = (int) ch;
    s.output = out;
    return s;
}

static LevelReading reading (int spk, const char* point, double db)
{
    LevelReading r;
    r.spk = spk;
    r.point = point;
    r.tf = flat (db);
    return r;
}

static void levelBalanceTests()
{
    SpeakerSystem sys;
    sys.sources = { speaker ("Main L", SpeakerRole::Main, SpeakerChannel::Left, 1), speaker ("Main R", SpeakerRole::Main, SpeakerChannel::Right, 2),
                    speaker ("Sub", SpeakerRole::Sub, SpeakerChannel::Mono, 3), speaker ("Front fill", SpeakerRole::Fill, SpeakerChannel::Mono, 4) };
    LevelBalance lv;
    lv.points = { "FOH", "Front" };
    lv.set (reading (0, "FOH", -20.0));
    lv.set (reading (1, "FOH", -17.5));      // right 2.5 dB louder at the centre
    lv.set (reading (2, "FOH", -14.0));
    lv.set (reading (3, "FOH", -40.0));
    lv.set (reading (0, "Front", -25.0));
    lv.set (reading (1, "Front", -25.0));
    lv.set (reading (3, "Front", -16.0));    // the fill well above the mains where it covers
    const auto t = lv.suggest (sys, 0, false, {});
    bool pair = false, fill = false, sub = false;
    for (const auto& x : t)
    {
        if (x.spk == 1 && x.attention) { pair = true; check (near (x.db, -2.5, 0.05), "level: the louder side turned down by the difference", x.db, -2.5); }
        if (x.spk == 3) { fill = true; check (x.attention && x.db < 0.0, "level: a fill louder than the mains is turned down", x.db, -1.0); }
        if (x.spk == 2) sub = true;
    }
    check (pair, "level: left / right mismatch found");
    check (fill, "level: the fill is judged at the seat it covers");
    check (sub, "level: the sub is judged against the tops");
    // within the tolerance: no trim
    lv.set (reading (1, "FOH", -19.5));
    for (const auto& x : lv.suggest (sys, 0, false, {})) if (x.spk == 1 || (x.spk == 0 && x.why.contains ("Main R"))) check (! x.attention && x.db == 0.0, "level: 0.5 dB apart is within 1 dB");
    // a tighter aim makes it a trim
    LevelAims tight;
    tight.pairTol = 0.25;
    bool trimmed = false;
    for (const auto& x : lv.suggest (sys, 0, false, tight)) trimmed = trimmed || (x.attention && x.why.contains ("Main R"));
    check (trimmed, "level: the venue's tighter tolerance is used");
    // the trim is capped
    lv.set (reading (3, "Front", 0.0));
    for (const auto& x : lv.suggest (sys, 0, false, {})) if (x.spk == 3) check (x.db >= -6.0 - 1e-9, "level: a trim never goes past the cap", x.db, -6.0);
    // a removed speaker takes its readings
    lv.speakerRemoved (1);
    bool any = false;
    for (const auto& r : lv.readings) any = any || r.spk == 3;
    check (! any, "level: readings renumber after a speaker is removed");
    // save / load
    juce::XmlElement x ("Body");
    lv.save (x);
    LevelBalance back;
    back.load (&x);
    check (back.readings.size() == lv.readings.size() && back.points == lv.points, "level: round trip");
}

static void sheetTests()
{
    SpeakerSystem sys;
    sys.sources = { speaker ("Main L", SpeakerRole::Main, SpeakerChannel::Left, 1), speaker ("Sub", SpeakerRole::Sub, SpeakerChannel::Mono, 3) };
    sys.sources[0].proc.hp = { xo::LinkwitzRiley, 24, 100.0 };
    sys.sources[0].proc.delayMs = 2.5;
    sys.sources[1].proc.invert = true;
    sys.sources[1].ampGain = "+32 dB";
    check (sheet::hasSettings (sys, 0) && sheet::hasSettings (sys, 1), "sheet: outputs with settings");
    check (sheet::pending (sys).size() == 2, "sheet: both pending before they are ticked", sheet::pending (sys).size(), 2);
    sheet::tickAll (sys);
    check (sheet::enteredCount (sys) == 2 && sheet::pending (sys).isEmpty(), "sheet: tick all");
    sys.sources[0].proc.delayMs = 3.0;
    check (! sheet::entered (sys, 0) && sheet::entered (sys, 1), "sheet: a change unticks only that output");
    sys.sources[1].ampGain = "+35 dB";
    check (sheet::entered (sys, 1), "sheet: the amp gain is for the record, it does not untick");
    const auto csv = sheet::csv (sys);
    juce::StringArray lines;
    lines.addLines (csv);
    lines.removeEmptyStrings();
    check (lines.size() == 3, "sheet: CSV has a header and a row per output", lines.size(), 3);
    check (lines[0].contains ("Amp gain") && lines[0].contains ("Limiter") && lines[2].contains ("+35 dB"), "sheet: CSV has the amp and limiter");
    check (sheet::row (sys, 1).polarity == "INVERTED", "sheet: polarity in words");
    check (sheet::text (sys, "Venue", "today").contains ("Amp gain: +35 dB"), "sheet: text has the amp gain");
}

// Groups: Main L, Main R and a centre as one group, two subs as another; the crossover reads each group's average; a
// processor with a group stage enters each group once and each output's delay and trim.
static void groupTests()
{
    SpeakerSystem sys;
    sys.sources = { speaker ("Main L", SpeakerRole::Main, SpeakerChannel::Left, 1), speaker ("Main R", SpeakerRole::Main, SpeakerChannel::Right, 2),
                    speaker ("Centre", SpeakerRole::Main, SpeakerChannel::Centre, 3), speaker ("Sub 1", SpeakerRole::Sub, SpeakerChannel::Mono, 4),
                    speaker ("Sub 2", SpeakerRole::Sub, SpeakerChannel::Mono, 5), speaker ("Fill", SpeakerRole::Fill, SpeakerChannel::Mono, 6) };
    sys.sources[1].linkTo = 0;
    sys.sources[2].linkTo = 0;
    sys.sources[4].linkTo = 3;
    check (sys.groupOf (2).size() == 3 && sys.groupOf (2).front() == 0, "groups: the centre is in the mains' group, led by Main L");
    check (sys.groupName (1) == "Mains" && sys.groupName (4) == "Subs" && sys.groupName (5) == "Fill", "groups: named by role (a lone speaker by its name)");
    check (sys.groupOutputs (0) == "Outputs 1, 2, 3", "groups: the outputs a group feeds");

    // the group's response: a 6 dB bump on Main R only shows up halved in power (+3.0 dB less ... about +3.97 dB on average)
    const auto grid = xo::analysisGrid();
    for (int i : { 0, 1 })
    {
        auto& t = sys.sources[(size_t) i].response;
        for (double f = 20.0; f <= 20000.0; f *= 1.05)
        {
            t.freq.push_back (f);
            t.db.push_back (i == 1 && f > 200.0 && f < 400.0 ? 6.0f : 0.0f);
            t.phase.push_back (0.0f);
        }
        sys.sources[(size_t) i].nearField = true;
    }
    int used = 0;
    const auto g = sys.groupResponse (0, grid, &used);
    size_t k300 = 0;
    for (size_t j = 0; j < grid.size(); ++j) if (std::abs (grid[j] - 300.0) < std::abs (grid[k300] - 300.0)) k300 = j;
    const double db300 = 20.0 * std::log10 (std::abs (g.h[k300]));
    check (used == 2 && std::abs (db300 - 10.0 * std::log10 ((1.0 + 3.981) / 2.0)) < 0.2, "groups: the crossover reads the members' power average", db300, 3.97);

    // the processor sheet with a group stage
    sys.sources[0].proc.hp = { xo::LinkwitzRiley, 24, 90.0 };
    sys.syncLinks();
    sys.sources[1].proc.delayMs = 1.2;
    sys.sources[2].trimDb = -2.0;
    sys.groupStage = true;
    const auto ls = sheet::lines (sys);
    check (ls.size() == 8 && ls[0].group && ls[0].spk == 0 && ! ls[1].group && ls[4].group && ls[4].spk == 3 && ! ls[7].group,
           "sheet: a group stage lists each group, then its outputs", (double) ls.size(), 8);
    check (sheet::groupRow (sys, 0).hpf.contains ("90 Hz") && sheet::row (sys, 1).hpf == "(group)" && sheet::row (sys, 2).gain.contains ("-2.0 dB trim"),
           "sheet: the group has the filters, each output its own trim");
    check (sheet::row (sys, 5).hpf == "off" && ! sheet::row (sys, 5).member, "sheet: a speaker in no group keeps its full line");
    sheet::tickAll (sys);
    check (sheet::enteredCount (sys) == sheet::lineCount (sys), "sheet: tick all ticks the group lines too");
    sys.sources[1].proc.delayMs = 1.5;
    check (! sheet::entered (sys, sheet::Line { false, 1 }) && sheet::entered (sys, sheet::Line { true, 0 }), "sheet: an output's delay unticks only that output");
    sys.sources[0].proc.hp.freq = 100.0;
    sys.syncLinks();
    check (! sheet::entered (sys, sheet::Line { true, 0 }) && sheet::entered (sys, sheet::Line { false, 2 }), "sheet: a group change unticks the group line only");
    sys.groupStage = false;
    check (sheet::lineCount (sys) == 6 && sheet::row (sys, 1).hpf.contains ("100 Hz"), "sheet: one output per speaker gives each its full settings");
}

static void recheckTests()
{
    Recheck rc;
    rc.when = "16 Oct 2026 10:00";
    rc.lines = { { "Main L as tuned", 0 }, { "Sub 2 dB down", 1 }, { "Main R polarity inverted", 2 } };
    check (rc.worst() == 2, "recheck: the worst line", rc.worst(), 2);
    juce::XmlElement x ("Body");
    rc.save (x);
    Recheck back;
    back.load (&x);
    check (back.lines.size() == 3 && back.when == rc.when && back.worst() == 2, "recheck: round trip");
    Recheck none;
    juce::XmlElement y ("Body");
    none.save (y);
    check (y.getChildByName ("Recheck") == nullptr, "recheck: nothing saved when it was not run");
}

static void prefsTests()
{
    auto& ap = AppPrefs::get();
    check (near (ap.value ("eqClose"), 1.5, 1e-9), "prefs: default", ap.value ("eqClose"), 1.5);
    check (near (ap.value ("coverageTol"), 3.0, 1e-9) && near (ap.value ("targetDbA"), 95.0, 1e-9), "prefs: new defaults");
    const auto* d = prefs::find ("eqClose");
    check (d != nullptr && d->venue, "prefs: eqClose can be a venue's own");
    check (near (prefs::snap (*d, 1.7), 1.5, 1e-9), "prefs: a value snaps to the choices", prefs::snap (*d, 1.7), 1.5);
    VenuePrefs vp;
    vp.set ("eqClose", 2.0);
    check (vp.has ("eqClose") && near (vp.value ("eqClose"), 2.0, 1e-9), "prefs: venue override");
    check (near (vp.value ("lrTol"), ap.value ("lrTol"), 1e-9), "prefs: falls back to the default");
    vp.set ("genStartDb", -10.0);
    check (! vp.has ("genStartDb"), "prefs: a venue cannot override a computer preference");
    vp.set ("fillLo", -1.0);
    vp.set ("fillHi", -2.0);
    check (vp.value ("fillLo") <= vp.value ("fillHi"), "prefs: the fill range stays the right way round", vp.value ("fillLo"), vp.value ("fillHi"));
    juce::XmlElement x ("Venue");
    vp.save (x);
    VenuePrefs back;
    back.load (x);
    check (back.has ("eqClose") && near (back.value ("eqClose"), 2.0, 1e-9), "prefs: venue overrides round trip");
    vp.reset ("eqClose");
    check (! vp.has ("eqClose"), "prefs: reset");
    check (! back.describe().isEmpty(), "prefs: described for the report");
}

static void allowanceTests()
{
    RoomTuning room;
    room.updateAllowance (true, 120.0, 0.6, 0.5, 1, 12.0, 4.0);
    check (room.allow.wanted && ! room.allow.on, "allowance: wanted, but off with no acoustics measured");
    room.updateAllowance (false, 120.0, 0.6, 0.5, 1, 12.0, 4.0);
    check (! room.allow.wanted && ! room.allow.on, "allowance: switched off");
    // with a sweep's acoustics
    SweepMeasurement w;
    w.position = "FOH";
    for (int b = 0; b < acoustics::kBands; ++b) { w.ac.band[(size_t) b].t30 = 1.2; w.ac.band[(size_t) b].edt = 1.1; w.ac.band[(size_t) b].rangeDb = 50.0; }
    w.ac.sti = 0.6;
    w.ac.valid = true;
    room.sweeps.push_back (w);
    room.updateAllowance (true, NAN, 0.6, 0.5, 1, 12.0, 4.0);
    check (room.allow.on, "allowance: on once the acoustics are measured");
    check (room.allow.schroederAssumed && near (room.allow.schroederHz, 200.0, 1e-6), "allowance: Schroeder assumed without the room size", room.allow.schroederHz, 200.0);
    room.updateAllowance (true, 90.0, 0.6, 0.5, 1, 12.0, 4.0);
    check (! room.allow.schroederAssumed && near (room.allow.schroederHz, 90.0, 1e-6), "allowance: Schroeder from the room", room.allow.schroederHz, 90.0);
    check (room.allow.summary().contains ("90 Hz"), "allowance: summary");
    room.updateAllowance (true, 90.0, 0.3, 0.5, 3, 12.0, 4.0);
    check (room.allow.noHighBoost || room.allow.capCuts, "allowance: a poor room limits the EQ");
}

static void coverageTests()
{
    SpeakerSystem sys;
    sys.sources = { speaker ("Main L", SpeakerRole::Main, SpeakerChannel::Left, 1), speaker ("Sub", SpeakerRole::Sub, SpeakerChannel::Mono, 3),
                    speaker ("Fill", SpeakerRole::Fill, SpeakerChannel::Mono, 4) };
    VenuePlan plan;
    plan.lengthM = 30.0;
    plan.widthM = 20.0;
    plan.speakers[0] = { 0.5f, 0.1f, 90.0f, true };   // at the front, facing the audience
    plan.items.push_back ({ PlanItem::Marker, "Mid", 0.5f, 0.4f, 0.0f, 0.0f });
    plan.items.push_back ({ PlanItem::Marker, "Back", 0.5f, 0.95f, 0.0f, 0.0f });
    plan.items.push_back ({ PlanItem::Marker, "Front", 0.5f, 0.2f, 0.0f, 0.0f });
    std::map<std::pair<int, juce::String>, double> lv { { { 0, "Mid" }, -10.0 }, { { 0, "Back" }, -17.0 }, { { 0, "Front" }, -12.0 },
                                                        { { 1, "Mid" }, 0.0 }, { { 2, "Front" }, -30.0 } };
    auto level = [&lv] (int k, const juce::String& p) { auto it = lv.find ({ k, p }); return it != lv.end() ? it->second : NAN; };
    const auto rows = coverage::analyse (sys, { "Front", "Mid", "Back" }, plan, 3.0, level);
    check (rows.size() == 1, "coverage: the tops only (not the sub, not a fill drowned out at its only point)", (double) rows.size(), 1);
    const CoverageRow* m = nullptr;
    for (const auto& r : rows) if (r.spk == 0) m = &r;
    check (m != nullptr && m->onAxis == "Mid" && near (m->worstDb, -7.0, 1e-9) && m->worstPoint == "Back", "coverage: on-axis and worst point", m != nullptr ? m->worstDb : NAN, -7.0);
    check (m != nullptr && ! m->ok && m->advice.contains ("tilt it up"), "coverage: a far seat down: tilt it up");
    // a near seat down: tilt it down
    lv[{ 0, "Front" }] = -20.0;
    lv[{ 0, "Back" }] = -11.0;
    for (const auto& r : coverage::analyse (sys, { "Front", "Mid", "Back" }, plan, 3.0, level))
        if (r.spk == 0) check (r.advice.contains ("tilt it down"), "coverage: a near seat down: tilt it down");
    // off to its side: turn it
    plan.items.push_back ({ PlanItem::Marker, "Side", 0.9f, 0.4f, 0.0f, 0.0f });
    lv[{ 0, "Front" }] = -11.0;
    lv[{ 0, "Side" }] = -19.0;
    for (const auto& r : coverage::analyse (sys, { "Front", "Mid", "Back", "Side" }, plan, 3.0, level))
        if (r.spk == 0) check (r.worstPoint == "Side" && (r.advice.contains ("turn it toward") || r.advice.contains ("fill")), "coverage: a seat to the side: turn it");
    // within spec
    lv[{ 0, "Side" }] = -12.0;
    for (const auto& r : coverage::analyse (sys, { "Front", "Mid", "Back", "Side" }, plan, 3.0, level))
        if (r.spk == 0) check (r.ok && r.advice.isEmpty(), "coverage: within the spec");
    // the data round trip
    CoverageData cd;
    cd.before[0]["Mid"] = -10.0;
    cd.reaimedOn[0] = "today";
    juce::XmlElement x ("Body");
    cd.save (x);
    CoverageData back;
    back.load (&x);
    check (back.before[0]["Mid"] == -10.0 && back.reaimedOn[0] == "today", "coverage: round trip");
}

static void headroomTests()
{
    std::vector<HeadroomStep> s { { -30.0, 70.0, 0.95 }, { -27.0, 73.0, 0.95 } };
    check (headroom::judge (s) == 0, "headroom: rising with the generator is clean");
    s.push_back ({ -24.0, 74.0, 0.95 });
    check (headroom::judge (s) == 1, "headroom: rising 1 dB for 3 dB is compression");
    s.back() = { -24.0, 76.0, 0.70 };
    check (headroom::judge (s) == 2, "headroom: the coherence dropping is distortion");
    HeadroomRun r;
    r.steps = { { -12.0, 89.0, 0.9 }, { -9.0, 92.0, 0.9 }, { -6.0, 95.0, 0.9 }, { -3.0, 98.0, 0.9 }, { 0.0, 99.0, 0.9 } };
    r.compressedAt = 4;
    check (near (r.cleanMax(), 98.0, 1e-9), "headroom: the clean maximum stops before compression", r.cleanMax(), 98.0);
    int lv = 0;
    auto v = headroom::verdict (r, 95.0, 6.0, true, lv);
    check (lv == 1 && v.contains ("Only 3.0 dB"), "headroom: less than wanted");
    v = headroom::verdict (r, 100.0, 6.0, true, lv);
    check (lv == 2 && v.contains ("short"), "headroom: short of the show level");
    r.compressedAt = -1;
    r.reached = true;
    v = headroom::verdict (r, 90.0, 6.0, true, lv);
    check (lv == 0 && v.contains ("at least"), "headroom: reached clean");
    v = headroom::verdict (r, 90.0, 6.0, false, lv);
    check (lv == 1 && v.contains ("not calibrated"), "headroom: needs a calibrated mic");
    HeadroomData hd;
    hd.get (0, "Whole system") = r;
    juce::XmlElement x ("Body");
    hd.save (x);
    HeadroomData back;
    back.load (&x);
    check (back.runs.size() == 1 && back.runs[0].steps.size() == 5 && back.runs[0].reached, "headroom: round trip");
}

static void varianceTests()
{
    FreqTable a = flat (0.0), b = flat (-6.0), c = flat (0.0);
    check (near (seatVariance ({ &a, &b }), 0.0, 1e-6), "variance: a level difference alone is not tonal variance", seatVariance ({ &a, &b }), 0.0);
    for (size_t i = 0; i < c.freq.size(); ++i) if (c.freq[i] > 2000.0) c.db[i] = 6.0;
    const double v = seatVariance ({ &a, &c });
    check (v > 0.5 && v < 4.0, "variance: a bright seat shows", v, 2.0);
    check (! std::isfinite (seatVariance ({ &a })), "variance: needs two points");
}

static void historyTests()
{
    SpeakerSystem a;
    a.sources = { speaker ("Main L", SpeakerRole::Main, SpeakerChannel::Left, 1), speaker ("Sub", SpeakerRole::Sub, SpeakerChannel::Mono, 3) };
    auto b = a;
    b.sources[0].proc.delayMs = 1.25;
    b.sources[1].trimDb = -2.0;
    b.sources[1].proc.invert = true;
    b.sources[0].proc.eq = { { 250.0, -3.0, 2.0 } };
    const auto d = ChangeLog::diff (a, b);
    check (d.size() == 4, "history: one line per change", (double) d.size(), 4);
    juce::StringArray all;
    for (const auto& x : d) all.add (x.first + ": " + x.second);
    const auto text = all.joinIntoString ("; ");
    check (text.contains ("Delay 0.00 -> 1.25 ms") && text.contains ("Trim") && text.contains ("Polarity inverted") && text.contains ("250 Hz -3.0 dB"), "history: in words: " + text.toStdString());
    // snapshot and roll back
    const auto snap = ChangeLog::snapshot (a);
    auto c = b;
    ChangeLog::restore (c, *snap);
    check (ChangeLog::diff (a, c).empty(), "history: roll back restores every output");
    ChangeLog log;
    log.add ("now", "Room EQ", "Main L", "EQ", "Room EQ entered", snap.get());
    juce::XmlElement x ("Venue");
    log.save (x);
    ChangeLog back;
    back.load (x);
    check (back.entries.size() == 1 && back.entries[0].before != nullptr && back.entries[0].why == "Room EQ entered", "history: round trip");
    for (size_t i = 0; i < ChangeLog::kMax + 10; ++i) log.add ("t", "s", "o", "w", "y", nullptr);
    check (log.entries.size() == ChangeLog::kMax, "history: capped", (double) log.entries.size(), (double) ChangeLog::kMax);
}

static void airTests()
{
    // ISO 9613-1, 20 C, 50 % RH: about 5 dB/km at 1 kHz and 0.1 dB/m at 8 kHz
    const double a1 = air::absorptionDbPerM (1000.0, 20.0, 50.0) * 1000.0, a8 = air::absorptionDbPerM (8000.0, 20.0, 50.0) * 1000.0;
    check (near (a1, 4.9, 0.8), "air: 1 kHz at 20 C / 50 %", a1, 4.9);
    check (near (a8, 105.0, 15.0), "air: 8 kHz at 20 C / 50 %", a8, 105.0);
    check (air::absorptionDbPerM (8000.0, 20.0, 15.0) > air::absorptionDbPerM (8000.0, 20.0, 60.0), "air: dry air takes more of the top");
    check (near (air::speedOfSound (20.0), 343.2, 0.5), "air: speed of sound", air::speedOfSound (20.0), 343.2);
    const double drift = air::delayDriftMs (10.0, 15.0, 30.0);
    check (drift < 0.0 && near (drift, -0.258, 0.02), "air: a 10 ms delay set at 15 C needs less at 30 C", drift, -0.258);
}

static void venueListTests()
{
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("la-venues-" + juce::String (juce::Random::getSystemRandom().nextInt()));
    dir.createDirectory();
    UserPrefs::set ("venueFolder", dir.getFullPathName());
    check (venues::folder() == dir, "venues: the folder from Settings");
    const auto f = dir.getChildFile ("Grace Hall" + venues::extension());
    f.replaceWithText ("<?xml version=\"1.0\"?>\n<LiveAlignProject format=\"2\"><Venue name=\"Grace &amp; Hall &gt; A\" notes=\"x\" sessions=\"3\" tuned=\"1\" savedOn=\"1790000000000\"/>"
                       "<Sessions/></LiveAlignProject>");
    const auto e = venues::read (f);
    check (e.name == "Grace & Hall > A" && e.sessions == 3 && e.tuned == 1, "venues: read from the start of the file");
    const auto old = dir.getChildFile ("Old" + venues::extension());
    old.replaceWithText ("<LiveAlignProject><Venue name=\"Old\" notes=\"\"/></LiveAlignProject>");
    const auto o = venues::read (old);
    check (o.sessions == -1 && o.tuned == -1 && o.name == "Old", "venues: an older file");
    venues::noteUsed (old);
    const auto list = venues::scan();
    check (list.size() == 2 && list[0].file == old, "venues: the last used first", (double) list.size(), 2);
    check (venues::freeFile (dir, "Grace Hall") != f, "venues: a new file does not clash");
    venues::forget (old);
    check (venues::recent().empty() || venues::recent()[0].first != old, "venues: forget");
    UserPrefs::set ("venueFolder", {});
    dir.deleteRecursively();
}

// A broker just big enough for the feed: accepts one client, answers CONNECT with the given return code, then
// SUBSCRIBE, then publishes the given messages.
struct FakeBroker : juce::Thread
{
    int port = 0, connackCode = 0;
    std::vector<std::pair<juce::String, juce::String>> publish;
    juce::String gotUser;
    juce::StreamingSocket listener;
    FakeBroker() : juce::Thread ("fake broker") {}
    ~FakeBroker() override { listener.close(); stopThread (3000); }
    bool open()
    {
        for (int p = 18830; p < 18930; ++p) if (listener.createListener (p, "127.0.0.1")) { port = p; return true; }
        return false;
    }
    static bool readPacket (juce::StreamingSocket& s, juce::uint8& type, juce::MemoryBlock& body)
    {
        if (s.read (&type, 1, true) != 1) return false;
        size_t len = 0; int shift = 0;
        for (int i = 0; i < 4; ++i) { juce::uint8 b = 0; if (s.read (&b, 1, true) != 1) return false; len |= (size_t) (b & 0x7f) << shift; shift += 7; if (! (b & 0x80)) break; }
        body.setSize (len);
        return len == 0 || s.read (body.getData(), (int) len, true) == (int) len;
    }
    void run() override
    {
        std::unique_ptr<juce::StreamingSocket> c (listener.waitForNextConnection());
        if (c == nullptr) return;
        juce::uint8 t = 0; juce::MemoryBlock b;
        if (! readPacket (*c, t, b) || t != 0x10) return;
        // the user name follows the client id when the flag is set
        const auto* d = static_cast<const juce::uint8*> (b.getData());
        if (b.getSize() > 12 && (d[7] & 0x80))
        {
            size_t at = 10;
            const size_t idLen = ((size_t) d[at] << 8) | d[at + 1]; at += 2 + idLen;
            const size_t uLen = ((size_t) d[at] << 8) | d[at + 1];
            gotUser = juce::String::fromUTF8 ((const char*) d + at + 2, (int) uLen);
        }
        const juce::uint8 ack[] = { 0x20, 0x02, 0x00, (juce::uint8) connackCode };
        c->write (ack, 4);
        if (connackCode != 0) { juce::Thread::sleep (200); return; }
        if (! readPacket (*c, t, b) || t != 0x82) return;
        const juce::uint8 suback[] = { 0x90, 0x03, 0x00, 0x01, 0x00 };
        c->write (suback, 5);
        for (const auto& m : publish)
        {
            juce::MemoryBlock v;
            mqtt::putString (v, m.first);
            v.append (m.second.toRawUTF8(), strlen (m.second.toRawUTF8()));
            const auto pk = mqtt::packet (0x30, v);
            c->write (pk.getData(), (int) pk.getSize());
        }
        while (! threadShouldExit() && c->waitUntilReady (true, 100) >= 0) {}
    }
};

static void climateTests()
{
    check (mqtt::topicMatches ("zigbee2mqtt/foh", "zigbee2mqtt/foh") && ! mqtt::topicMatches ("zigbee2mqtt/foh", "zigbee2mqtt/stage"),
           "mqtt: exact topics");
    check (mqtt::topicMatches ("homeassistant/sensor/+/state", "homeassistant/sensor/foh_temp/state") && mqtt::topicMatches ("home/#", "home/a/b")
               && ! mqtt::topicMatches ("home/+", "home/a/b"),
           "mqtt: + and # wildcards");
    check (std::abs (mqtt::readNumber ("21.4", {}) - 21.4) < 1e-9 && std::abs (mqtt::readNumber ("\"71.6\"", {}) - 71.6) < 1e-9
               && std::abs (mqtt::readNumber (juce::String (juce::CharPointer_UTF8 ("22.5 \xc2\xb0" "C")), {}) - 22.5) < 1e-9,
           "mqtt: plain number payloads");
    check (std::abs (mqtt::readNumber ("{\"temperature\":21.7,\"humidity\":44}", "temperature") - 21.7) < 1e-9
               && std::abs (mqtt::readNumber ("{\"climate\":{\"rh\":\"51\"}}", "climate.rh") - 51.0) < 1e-9,
           "mqtt: JSON payloads by key and dotted path");
    check (std::isnan (mqtt::readNumber ("unavailable", {})) && std::isnan (mqtt::readNumber ("{\"battery\":90}", "temperature"))
               && std::isnan (mqtt::readNumber ("", {})),
           "mqtt: no number, no reading");

    // the whole thing against a broker: a Zigbee2MQTT-style JSON message in Fahrenheit
    {
        FakeBroker broker;
        if (! broker.open()) { check (false, "mqtt: a local port for the test broker"); return; }
        broker.publish = { { "zigbee2mqtt/FOH", "{\"temperature\":71.6,\"humidity\":40,\"battery\":97}" }, { "other/topic", "99" } };
        broker.startThread();
        ClimateFeed feed;
        ClimateFeed::Config c;
        c.enabled = true; c.host = "127.0.0.1"; c.port = broker.port; c.user = "tuner"; c.pass = "secret";
        c.tempTopic = "zigbee2mqtt/FOH"; c.humTopic = "zigbee2mqtt/FOH"; c.fahrenheit = true;
        feed.configure (c);
        for (int i = 0; i < 100 && feed.serial.load() == 0; ++i) juce::Thread::sleep (50);
        check (feed.serial.load() > 0, "mqtt: a reading arrives from the broker");
        check (std::abs (feed.tempC.load() - 22.0f) < 0.01f, "mqtt: temperature converted from Fahrenheit", feed.tempC.load(), 22.0);
        check (std::abs (feed.humidity.load() - 40.0f) < 0.01f, "mqtt: humidity from the same JSON message", feed.humidity.load(), 40.0);
        check (feed.live() && feed.status().contains ("Connected"), "mqtt: live and connected");
        check (broker.gotUser == "tuner", "mqtt: logs in with the user name");
        feed.stop();
        check (! feed.connected.load(), "mqtt: stops cleanly");
    }
    // a broker that turns the login down: said so plainly, nothing live
    {
        FakeBroker broker;
        if (! broker.open()) return;
        broker.connackCode = 4;
        broker.startThread();
        ClimateFeed feed;
        ClimateFeed::Config c;
        c.enabled = true; c.host = "127.0.0.1"; c.port = broker.port; c.user = "x"; c.pass = "wrong"; c.tempTopic = "t";
        feed.configure (c);
        for (int i = 0; i < 60 && ! feed.status().contains ("refused"); ++i) juce::Thread::sleep (50);
        check (feed.status().contains ("user name or password") && ! feed.live(), "mqtt: a refused login is reported", 0, 0);
    }
    // A real broker (CI runs Mosquitto on this port, with Home Assistant style retained states published first)
    if (const char* p = std::getenv ("LA_MQTT_TEST_PORT"))
    {
        ClimateFeed feed;
        ClimateFeed::Config c;
        c.enabled = true; c.host = "127.0.0.1"; c.port = juce::String (p).getIntValue();
        c.tempTopic = "homeassistant/sensor/+/temperature"; c.humTopic = "homeassistant/sensor/foh/humidity"; c.fahrenheit = true;
        feed.configure (c);
        for (int i = 0; i < 100 && (std::isnan (feed.tempC.load()) || std::isnan (feed.humidity.load())); ++i) juce::Thread::sleep (50);
        check (std::abs (feed.tempC.load() - 21.5f) < 0.01f, "mqtt (Mosquitto): retained temperature, wildcard topic", feed.tempC.load(), 21.5);
        check (std::abs (feed.humidity.load() - 38.0f) < 0.01f, "mqtt (Mosquitto): retained humidity", feed.humidity.load(), 38.0);
        feed.stop();
    }
}

// Listening on another device: the interface at 48 kHz, the headphones at 44.1 kHz on a clock 0.02 % off. Once
// filled, no gaps for a minute, the buffer stays near its target, and a sine comes out as a sine.
static void listenTests()
{
    ListenBridge b;
    b.setSourceRate (48000.0);
    b.reset();
    const double f = 1000.0, srcRate = 48000.0, dstRate = 44100.0 * 1.0002;
    std::vector<float> in (480), out (441);
    double phase = 0.0;
    int written = 0, gapsAfterStart = 0, maxFill = 0;
    double err = 0.0;
    int errN = 0;
    for (int block = 0; block < 6000; ++block)   // 60 s of 10 ms blocks
    {
        for (auto& v : in) { v = (float) std::sin (phase); phase += 2.0 * juce::MathConstants<double>::pi * f / srcRate; }
        b.push (in.data(), (int) in.size(), 1.0f);
        written += (int) in.size();
        const int before = b.underruns.load();
        b.pull (out.data(), (int) out.size(), dstRate);
        if (block > 20 && b.underruns.load() != before) ++gapsAfterStart;
        maxFill = juce::jmax (maxFill, b.bufferedSamples());
        if (block > 100)   // a sine in, a sine out: the sum of squares of the next-sample prediction error stays small
            for (size_t i = 2; i < out.size(); ++i)
            {
                const double w = 2.0 * juce::MathConstants<double>::pi * f / dstRate;
                const double pred = 2.0 * std::cos (w) * out[i - 1] - out[i - 2];
                err += (pred - out[i]) * (pred - out[i]);
                ++errN;
            }
    }
    check (gapsAfterStart == 0, "listen: no gaps between two clocks once filled", gapsAfterStart, 0);
    check (maxFill < 48000 * 0.2, "listen: the buffer stays near its target (no growing delay)", maxFill, 1920);
    check (std::sqrt (err / juce::jmax (1, errN)) < 0.01, "listen: a sine comes through clean", std::sqrt (err / juce::jmax (1, errN)), 0.0);
}

// ---- the audio interface kept: a simulated interface that can be unplugged and plugged back in
namespace
{
struct FakeState { juce::StringArray names { "Built-in", "Interface" }; };

class FakeDevice : public juce::AudioIODevice
{
public:
    explicit FakeDevice (const juce::String& n) : juce::AudioIODevice (n, "Fake") {}
    juce::StringArray getOutputChannelNames() override { return { "1", "2" }; }
    juce::StringArray getInputChannelNames() override { return { "1", "2" }; }
    juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
    juce::Array<int> getAvailableBufferSizes() override { return { 256 }; }
    int getDefaultBufferSize() override { return 256; }
    juce::String open (const juce::BigInteger&, const juce::BigInteger&, double, int) override { opened = true; return {}; }
    void close() override { opened = false; }
    bool isOpen() override { return opened; }
    void start (juce::AudioIODeviceCallback*) override { playing = true; }
    void stop() override { playing = false; }
    bool isPlaying() override { return playing; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override { return 256; }
    double getCurrentSampleRate() override { return 48000.0; }
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override { return 3; }
    juce::BigInteger getActiveInputChannels() const override { return 3; }
    int getOutputLatencyInSamples() override { return 0; }
    int getInputLatencyInSamples() override { return 0; }
private:
    bool opened = false, playing = false;
};

class FakeType : public juce::AudioIODeviceType
{
public:
    explicit FakeType (FakeState& s) : juce::AudioIODeviceType ("Fake"), state (s) {}
    void scanForDevices() override {}
    juce::StringArray getDeviceNames (bool) const override { return state.names; }
    int getDefaultDeviceIndex (bool) const override { return 0; }
    int getIndexOfDevice (juce::AudioIODevice* d, bool) const override { return d != nullptr ? state.names.indexOf (d->getName()) : -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice (const juce::String& out, const juce::String& in) override
    {
        const auto n = out.isNotEmpty() ? out : in;
        return state.names.contains (n) ? new FakeDevice (n) : nullptr;
    }
    void changed() { callDeviceChangeListeners(); }
private:
    FakeState& state;
};

class FakeManager : public juce::AudioDeviceManager
{
public:
    FakeState state;
    FakeType* type = nullptr;
    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override { types.add (type = new FakeType (state)); }
};
} // namespace

static void interfaceTests()
{
    FakeManager dm;
    dm.initialise (2, 2, nullptr, true);
    auto setup = dm.getAudioDeviceSetup();
    setup.outputDeviceName = setup.inputDeviceName = "Interface";
    dm.setAudioDeviceSetup (setup, true);
    auto name = [&dm] { return dm.getCurrentAudioDevice() != nullptr ? dm.getCurrentAudioDevice()->getName() : juce::String ("(none)"); };
    check (name() == "Interface", "interface: the interface is open to start with");
    bool choosing = false;
    InterfaceGuard guard (dm, dm.createStateXml(), 2, 2, [&choosing] { return choosing; });

    // unplugged: JUCE falls back to the built-in audio; the guard closes it and says what is gone
    dm.state.names = { "Built-in" };
    dm.type->changed();
    check (name() == "Built-in", "interface: JUCE on its own falls back to another device");
    guard.check();
    check (dm.getCurrentAudioDevice() == nullptr, "interface: unplugged, nothing else is opened in its place");
    check (guard.lostName() == "Interface", "interface: unplugged, its name is reported");
    check (guard.savedChoice() != nullptr && guard.savedChoice()->getStringAttribute ("audioOutputDeviceName") == "Interface",
           "interface: the interface stays the saved choice while it is gone");
    guard.check();
    check (dm.getCurrentAudioDevice() == nullptr, "interface: still closed when looked at again");

    // plugged back in: opened as it was
    dm.state.names = { "Built-in", "Interface" };
    dm.type->changed();
    guard.check();
    check (name() == "Interface" && guard.lostName().isEmpty(), "interface: plugged back in, it is opened again");

    // the person picks another device in the Audio interface window: that is the choice now
    choosing = true;
    setup = dm.getAudioDeviceSetup();
    setup.outputDeviceName = setup.inputDeviceName = "Built-in";
    dm.setAudioDeviceSetup (setup, true);
    guard.check();
    choosing = false;
    dm.state.names = { "Built-in" };
    dm.type->changed();
    guard.check();
    check (name() == "Built-in" && guard.lostName().isEmpty(), "interface: a device picked by hand is kept, the old one unplugged is no matter");
}

// ---- the show log: a row per minute, the parts, CSV, saved and loaded
// The show log's interval values come from the meter itself (IEC 61672-1): LAeq over the interval is the energy mean of
// the signal, not the mean of readings of the Slow meter, and LAF max is the Fast meter's running maximum.
static void splIntervalTests()
{
    const double fs = 48000.0;
    SplMeter m;
    m.prepare (fs);
    const double loudDb = -20.0, quietDb = -60.0;
    double phase = 0.0;
    auto sine = [&phase, fs] (std::vector<float>& out, double seconds, double db)
    {
        const double amp = std::pow (10.0, db / 20.0);   // a sine at `db` dBFS (AES17: a full-scale sine reads 0 dBFS)
        out.assign ((size_t) std::lround (seconds * fs), 0.0f);
        for (auto& v : out) { v = (float) (amp * std::sin (phase)); phase += 2.0 * kPi * 1000.0 / fs; }
    };
    std::vector<float> buf;
    // a quiet run-in, so the frames are flowing when the pattern starts; then loud 1 s, quiet 4 s (plus the frame
    // length, so the last of the quiet part has been seen)
    sine (buf, 1.0, quietDb);
    m.push (buf.data(), (int) buf.size());
    m.takeInterval();
    sine (buf, 1.0, loudDb);
    m.push (buf.data(), (int) buf.size());
    // the Slow meter's readings, once a second, as the old log sampled them
    double slowSum = std::pow (10.0, m.slowDb (SplMeter::A) / 10.0);
    for (int s = 0; s < 4; ++s)
    {
        sine (buf, 1.0, quietDb);
        m.push (buf.data(), (int) buf.size());
        slowSum += std::pow (10.0, m.slowDb (SplMeter::A) / 10.0);
    }
    sine (buf, 0.1, quietDb);
    m.push (buf.data(), (int) buf.size());
    const auto iv = m.takeInterval();
    const double energyMean = 10.0 * std::log10 ((1.0 * std::pow (10.0, loudDb / 10.0) + (iv.seconds - 1.0) * std::pow (10.0, quietDb / 10.0)) / iv.seconds)
                              + aWeightDb (1000.0);
    check (near (iv.seconds, 5.1, 0.05), "SPL interval: the audio in it", iv.seconds, 5.1);
    check (near (iv.laeq, energyMean, 0.3), "SPL interval: LAeq is the energy mean of the signal", iv.laeq, energyMean);
    check (near (iv.lafMax, loudDb + aWeightDb (1000.0), 0.3), "SPL interval: LAF max is the loud level", iv.lafMax, loudDb);
    // (the C filter's high-pass rings a little at the sudden onset: a few tenths over)
    check (near (iv.lcPeak, loudDb + cWeightDb (1000.0) + 3.01, 0.5), "SPL interval: LCpeak is the loud sine's peak", iv.lcPeak, loudDb + 3.01);
    check (near (iv.lceq, energyMean - aWeightDb (1000.0) + cWeightDb (1000.0), 0.3), "SPL interval: LCeq likewise", iv.lceq, energyMean);
    const double slowMean = 10.0 * std::log10 (slowSum / 5.0);
    std::printf ("SPL interval: LAeq %.2f (energy mean %.2f, the Slow readings' mean %.2f), LAF max %.2f\n", iv.laeq, energyMean, slowMean, iv.lafMax);
    // taken: the next interval starts afresh, and holds only what came after
    sine (buf, 2.0, quietDb);
    m.push (buf.data(), (int) buf.size());
    const auto next = m.takeInterval();
    check (near (next.laeq, quietDb + aWeightDb (1000.0), 0.3) && near (next.lafMax, quietDb + aWeightDb (1000.0), 0.5),
           "SPL interval: taking it starts the next one afresh", next.laeq, quietDb);
    check (near (next.seconds, 2.0, 0.05), "SPL interval: the next one's audio", next.seconds, 2.0);
    // the Leq since the reset is untouched by the interval
    check (m.leqSeconds() > 7.9, "SPL interval: the Leq since the reset keeps running", m.leqSeconds(), 8.0);

    // and the log takes a measured interval as one point, timed at its start
    SplLog log;
    log.addInterval (1000.0, 80.0f, 88.0f, 85.0f, NAN, NAN, 110.0f);
    log.addInterval (1005.0, 86.0f, 94.0f, 91.0f);
    log.addInterval (1010.0, -200.0f, -200.0f, -200.0f);   // no audio: not a point
    const double want = 10.0 * std::log10 ((std::pow (10.0, 8.0) + std::pow (10.0, 8.6)) / 2.0);
    check (log.points.size() == 2 && near (log.points[0].t, 1000.0, 1e-9) && near (log.leq (false), want, 0.01) && near (log.maxLaf(), 91.0, 1e-6)
           && near (log.maxPeak(), 110.0, 1e-6) && ! std::isfinite (log.points[1].lcPk),
           "show log: a measured interval is one point", log.leq (false), want);
}

static void showLogTests()
{
    SplLog log;
    log.interval = 5.0;
    log.limitLaeq = 80.0;
    log.limitMinutes = 1;
    const double t0 = 1760000000.0 - std::fmod (1760000000.0, 60.0);   // on a minute
    log.markPart (t0, "Worship");
    for (int s = 0; s < 180; ++s)   // three minutes, a reading a second: 70 dB, then 90 dB in the third minute
    {
        const float la = s < 120 ? 70.0f : 90.0f;
        log.add (t0 + s, la, la + 8.0f, la + 5.0f, 20.0f, 45.0f);
        if (s == 60) log.markPart (t0 + 60.0, "Sermon");
    }
    log.addHit (t0 + 130.0, 377.0f, -12.0f);
    const auto rows = log.minuteRows();
    check (rows.size() == 3, "show log: a row per minute", (double) rows.size(), 3);
    if (rows.size() == 3)
    {
        check (std::abs (rows[0].la - 70.0f) < 0.05f && std::abs (rows[0].lc - 78.0f) < 0.05f && std::abs (rows[0].laMax - 75.0f) < 0.05f,
               "show log: a minute's LAeq, LCeq and LAF max", rows[0].la, 70.0);
        check (rows[0].part == "Worship" && rows[1].part == "Sermon" && rows[2].part == "Sermon", "show log: each minute's part");
        check (rows[2].over && ! rows[0].over, "show log: over the limit only in the loud minute");
        check (rows[2].feedback == "377 Hz" && rows[0].feedback.isEmpty(), "show log: the feedback caught in its minute");
        check (std::abs (rows[1].tempC - 20.0f) < 0.05f && std::abs (rows[1].rh - 45.0f) < 0.5f, "show log: the room sensor's reading");
    }
    const auto parts = log.partSummaries();
    check (parts.size() == 2 && parts[0].name == "Worship" && parts[1].name == "Sermon", "show log: a summary per part");
    if (parts.size() == 2)
    {
        const double sermon = 10.0 * std::log10 ((std::pow (10.0, 7.0) + std::pow (10.0, 9.0)) / 2.0);
        check (std::abs (parts[0].la - 70.0) < 0.05 && std::abs (parts[1].la - sermon) < 0.1, "show log: a part's LAeq is its energy average", parts[1].la, sermon);
        check (parts[1].hits == 1 && parts[0].hits == 0, "show log: feedback counted in its part");
        check (parts[1].minutesOver > 0.5 && parts[0].minutesOver == 0.0, "show log: minutes over the limit per part", parts[1].minutesOver, 1.0);
    }
    const auto csv = log.minutesCsv (true);
    juce::StringArray lines;
    lines.addLines (csv.trim());
    check (lines.size() == 4 && lines[0].startsWith ("Date,Time,Part,LAeq") && lines[0].contains ("Temperature (F)"), "show log: CSV header and a line per minute",
           (double) lines.size(), 4);
    check (lines.size() == 4 && lines[3].contains (",Sermon,90.0,98.0,8.0,95.0,") && lines[3].contains (",over (LAeq),377 Hz,68.0,45"), "show log: CSV values (F)");
    check (log.partsCsv().contains ("Sermon,") && log.partsCsv().contains ("Worship,"), "show log: parts CSV");
    juce::XmlElement x ("Session");
    log.save (x);
    SplLog back;
    back.load (&x);
    check (back.points.size() == log.points.size() && back.parts.size() == 2 && back.hits.size() == 1 && back.parts[1].name == "Sermon"
               && std::abs (back.points[3].tempC - 20.0f) < 0.05f, "show log: saved and loaded with its parts, feedback and conditions");
    check (back.minuteRows().size() == 3 && back.minuteRows()[2].feedback == "377 Hz", "show log: the table is the same after loading");
    {
        // both limits (LAeq and LCeq), the peak limit, and the alarm
        SplLog L;
        L.interval = 5.0; L.limitMinutes = 1; L.limitLaeq = 85.0; L.limitC = false; L.limit2 = 95.0; L.peakLimit = 130.0;
        for (int s2 = 0; s2 < 240; ++s2)
        {
            const float la = s2 < 120 ? 80.0f : 84.0f;         // under the LAeq limit, but closing in
            const float lc = s2 < 180 ? 90.0f : 100.0f;        // over the LCeq limit in the last minute
            const float pk = s2 == 200 ? 131.0f : 115.0f;      // one peak over the peak limit
            L.add (t0 + s2, la, lc, la + 3.0f, NAN, NAN, pk);
        }
        const auto r2 = L.minuteRows();
        check (r2.size() == 4 && ! r2[0].over && r2[3].overWhat.contains ("LCeq") && r2[3].overWhat.contains ("peak") && ! r2[3].overWhat.contains ("LAeq"),
               "limits: a minute over the LCeq limit and the peak limit, not the LAeq one", r2.size(), 4);
        check (r2.size() == 4 && std::abs (r2[3].lcPeak - 131.0f) < 0.05f && std::abs (r2[0].lcPeak - 115.0f) < 0.05f, "limits: the minute's LCpeak");
        check (L.minutesOver2() > 0.5 && L.minutesOver() == 0.0 && L.peaksOver() == 1, "limits: time over each limit, peaks counted", L.minutesOver2(), 1.0);
        auto a = L.alarm (115.0f, true, 3.0);
        check (a.level == 2 && a.text.contains ("LCeq"), "alarm: red over the LCeq limit");
        SplLog q;
        q.interval = 5.0; q.limitMinutes = 1; q.limitLaeq = 85.0;
        for (int s2 = 0; s2 < 120; ++s2) q.add (t0 + s2, 83.0f, 90.0f, 86.0f);
        check (q.alarm (100.0f, true, 3.0).level == 1, "alarm: amber within 3 dB of the limit");
        check (q.alarm (100.0f, true, 1.0).level == 0, "alarm: green with a 1 dB margin");
        q.peakLimit = 135.0;
        check (q.alarm (136.0f, true, 3.0).level == 2 && q.alarm (136.0f, false, 3.0).level == 1, "alarm: a peak over the peak limit is red (only when calibrated)");
        SplLog none;
        check (none.alarm (100.0f, true, 3.0).level == -1, "alarm: no limit, no alarm");
        juce::XmlElement xx ("S");
        L.save (xx);
        SplLog back2;
        back2.load (&xx);
        check (back2.limit2 == 95.0 && back2.peakLimit == 130.0 && back2.points.size() == L.points.size() && std::isfinite (back2.points.back().lcPk),
               "limits: saved and loaded with the peaks");
        check (L.minutesCsv (false).contains ("LCpeak (dB)") && L.minutesCsv (false).contains ("1 min LCeq (dB)"), "limits: CSV has the peak and the second limit");
    }
    juce::XmlElement old ("Session");
    auto* e = old.createNewChildElement ("SplLog");
    e->setAttribute ("interval", 5.0);
    e->addTextElement ("1760000000 70.0 78.0 75.0\n1760000005 71.0 79.0 76.0\n");
    SplLog older;
    older.load (&old);
    check (older.points.size() == 2 && ! std::isfinite (older.points[0].tempC), "show log: a log saved before 0.26 loads (no temperature)");
}

// ---- audio health: simulated faults on two inputs (a mic, the console feed)
namespace
{
struct HealthSim
{
    AudioHealth h;
    double fs = 48000.0, clockFs = 48000.0;   // clockFs: the interface's real rate, against the computer's clock
    double nowMs = 1.0e6, phase = 0.0, phase2 = 0.0;
    juce::int64 n = 0;
    juce::Random rng { 7 };
    std::vector<float> a, b;
    double t0Sec = 1792000000.0;
    int block = 512;
    std::function<void (std::vector<float>&, juce::int64)> shape;   // changes the mic's block before it is processed
    HealthSim() { h.prepare (fs, 512); a.resize (512); b.resize (512); }
    double nowSec() const { return t0Sec + nowMs / 1000.0; }

    // one callback: mic = a sine and a little noise; feed = another sine (slipping a sample every `slipEvery` s)
    void step (double slipEvery = 0.0, bool deliver = true)
    {
        for (int i = 0; i < block; ++i)
        {
            a[(size_t) i] = (float) (0.2 * std::sin (phase) + 0.002 * (rng.nextFloat() - 0.5f));
            phase += 2.0 * juce::MathConstants<double>::pi * 180.0 / fs;
            if (slipEvery > 0.0 && std::fmod ((double) (n + i), slipEvery * fs) < 1.0) phase2 += 2.0 * juce::MathConstants<double>::pi * 160.0 / fs;   // a sample slips
            b[(size_t) i] = (float) (0.25 * std::sin (phase2) + 0.001 * (rng.nextFloat() - 0.5f));
            phase2 += 2.0 * juce::MathConstants<double>::pi * 160.0 / fs;
        }
        if (shape) shape (a, n);
        nowMs += block * 1000.0 / clockFs;
        if (deliver)
        {
            const float* ch[] = { a.data(), b.data() };
            h.process (ch, 2, block, nowMs + (rng.nextFloat() - 0.5f) * 2.0);   // callbacks a little early or late
            n += block;
        }
    }
    void run (double seconds, double slipEvery = 0.0)
    {
        const int blocks = (int) (seconds * clockFs / block);
        for (int k = 0; k < blocks; ++k) { step (slipEvery); if (k % 10 == 9) h.poll (nowSec(), -1, nowMs); }
    }
};
} // namespace

static void traceMathTests()
{
    auto make = [] (const juce::String& name, double offset, double tilt, double ph, int pointsPerOct)
    {
        Trace t;
        t.name = name;
        for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / pointsPerOct))
        {
            t.data.freq.push_back (f);
            t.data.db.push_back (offset + tilt * std::log2 (f / 1000.0));
            t.data.phase.push_back (ph);
        }
        return t;
    };
    const auto a = make ("EQ in", 2.0, -1.0, 170.0, 24), b = make ("Bypassed", 0.0, -1.0, -170.0, 12);   // b on a coarser grid
    const auto d = tracemath::difference (a, b);
    bool flat = ! d.data.empty();
    for (size_t i = 0; i < d.data.freq.size(); ++i) flat = flat && std::abs (d.data.db[i] - 2.0) < 0.01 && std::abs (std::abs (d.data.phase[i]) - 20.0) < 0.5;
    check (flat && d.kind == Trace::Transfer && d.name == "EQ in - Bypassed", "trace maths: a minus b, read between b's points, phase across +-180");
    const auto c = make ("Seat 3", 4.0, -1.0, 0.0, 24);
    const auto avg = tracemath::average ({ &a, &b, &c });
    bool mean = ! avg.data.empty();
    for (size_t i = 0; i < avg.data.freq.size(); ++i) mean = mean && std::abs (avg.data.db[i] - (2.0 + 0.0 + 4.0) / 3.0 - (-1.0 * std::log2 (avg.data.freq[i] / 1000.0))) < 0.01;
    check (mean, "trace maths: the average of three, in dB");
    Trace narrow = make ("Short", 0.0, 0.0, 0.0, 24);
    narrow.data.freq.resize (100); narrow.data.db.resize (100); narrow.data.phase.resize (100);   // stops partway
    const auto dn = tracemath::difference (a, narrow);
    check (! dn.data.empty() && dn.data.freq.back() <= narrow.data.freq.back() + 1e-9, "trace maths: only where both reach");
}

static void healthTests()
{
    {
        HealthSim s;
        s.run (30.0);
        check (s.h.events().empty(), "audio health: clean audio, nothing found", (double) s.h.events().size(), 0);
        check (std::abs (s.h.ppm()) < 20.0, "audio health: the rate measured (a clean clock)", s.h.ppm(), 0);
        // a dropout: 6 callbacks never arrive (the computer missed them)
        for (int k = 0; k < 6; ++k) s.step (0.0, false);
        s.run (2.0);
        check (s.h.count (AudioHealth::Dropout) == 1, "audio health: a dropout found from the sample count", s.h.count (AudioHealth::Dropout), 1);
        if (! s.h.events().empty()) check (std::abs (s.h.events().back().samples - 6 * 512) < 700, "audio health: about how much went missing", s.h.events().back().samples, 6 * 512);
        // a gap of digital silence in the mic, and a block repeated on the feed
        s.step();
        std::fill (s.a.begin() + 100, s.a.begin() + 300, 0.0f);
        { const float* ch[] = { s.a.data(), s.b.data() }; s.h.process (ch, 2, s.block, s.nowMs); }
        s.step();
        { const float* ch[] = { s.a.data(), s.b.data() }; s.h.process (ch, 2, s.block, s.nowMs); s.n += s.block; s.nowMs += s.block * 1000.0 / s.clockFs; }   // the same blocks again
        s.run (1.0);
        check (s.h.count (AudioHealth::ZeroGap) >= 1, "audio health: a gap of digital silence in a live input");
        bool gapOnMic = false;
        for (const auto& e : s.h.events()) gapOnMic |= e.kind == AudioHealth::ZeroGap && e.channel == 0 && std::abs (e.samples - 200) <= 2;
        check (gapOnMic, "audio health: the gap is on the mic, 200 samples");
        check (s.h.count (AudioHealth::Repeat) >= 1, "audio health: a block repeated exactly");
        check (s.h.syncInterval (0) == 0.0 && s.h.syncInterval (1) == 0.0, "audio health: no regular clicks without slips");
    }
    {
        // the console feed slips a sample every 3.1 s: its clock is not locked to the interface's
        HealthSim s;
        s.run (40.0, 3.1);
        check (std::abs (s.h.syncInterval (1) - 3.1) < 0.1, "audio health: a click every 3.1 s on the feed (clock not locked)", s.h.syncInterval (1), 3.1);
        check (s.h.syncInterval (0) == 0.0, "audio health: the mic, on the same interface, is clean");
        check (s.h.count (AudioHealth::Dropout) == 0, "audio health: a slip is not a dropout");
    }
    {
        // music with sharp hits at random moments (a kick, a dropped stick): clicks, but never at a steady interval
        HealthSim s;
        juce::Random r (11);
        double nextHit = 0.5;
        int hitLeft = 0;
        float hitAmp = 0.0f;
        s.shape = [&] (std::vector<float>& a, juce::int64 start)
        {
            for (int i = 0; i < (int) a.size(); ++i)
            {
                const double t = (double) (start + i) / s.fs;
                if (t >= nextHit) { hitLeft = 2400; hitAmp = 0.6f; nextHit = t + 0.25 + r.nextDouble() * 2.5; }
                if (hitLeft > 0) { a[(size_t) i] += hitAmp * (r.nextFloat() - 0.5f); hitAmp *= 0.998f; --hitLeft; }
            }
        };
        s.run (90.0);
        check (s.h.recentClicks (0) >= 10, "audio health: the hits do sound like clicks (the test means something)", s.h.recentClicks (0), 10);
        check (s.h.syncInterval (0) == 0.0 && s.h.count (AudioHealth::Click) == 0, "audio health: random hits are not a clock fault");
        check (s.h.count (AudioHealth::ZeroGap) == 0 && s.h.count (AudioHealth::Repeat) == 0, "audio health: music is not a gap or a repeat");
    }
    {
        // an interface running 750 ppm fast against the computer's clock
        HealthSim s;
        s.clockFs = 48036.0;
        s.run (60.0);
        check (std::abs (s.h.ppm() - 750.0) < 40.0, "audio health: the interface's real rate (750 ppm fast)", s.h.ppm(), 750.0);
        check (s.h.count (AudioHealth::Dropout) == 0, "audio health: a fast clock is not a dropout");
    }
    {
        // stopped for a while (the interface closed, the computer asleep): not a dropout
        HealthSim s;
        s.run (5.0);
        s.nowMs += 4000.0;
        s.h.poll (s.nowSec(), -1, s.nowMs);
        check (s.h.isStopped(), "audio health: no audio for a while is seen as stopped");
        s.run (3.0);
        check (s.h.count (AudioHealth::Dropout) == 0 && ! s.h.isStopped(), "audio health: a stop is not a dropout", s.h.count (AudioHealth::Dropout), 0);
        s.h.poll (s.nowSec(), 0, s.nowMs);
        s.h.poll (s.nowSec(), 2, s.nowMs);
        check (s.h.count (AudioHealth::DriverOverload) == 2, "audio health: the driver's own overload count");
    }
}

// A modelled speaker as a measured table (minimum phase): `hp` Butterworth high-pass of `hpOrder`, `lp` low-pass of `lpOrder`.
static FreqTable modelled (double hp, int hpOrder, double lp, int lpOrder, double levelDb = 0.0)
{
    FreqTable t;
    const xo::Filter h { xo::Butterworth, hpOrder * 6, hp }, l { xo::Butterworth, lpOrder * 6, lp };
    for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 24.0))
    {
        const auto v = (hp > 0.0 ? xo::response (h, true, f) : cplx (1.0, 0.0)) * (lp > 0.0 ? xo::response (l, false, f) : cplx (1.0, 0.0));
        t.freq.push_back (f);
        t.db.push_back (20.0 * std::log10 (std::max (std::abs (v), 1e-9)) + levelDb);
        t.phase.push_back (std::arg (v) * 180.0 / kPi);
    }
    return t;
}

// The crossover search over the whole chain: speakers' own filters, the processor's outputs, protection.
static void chainCrossoverTests()
{
    // Fixture A: a passive top (-10 dB at 60 Hz, flat above 150 Hz, safe high-pass 55 Hz 24 dB/oct) and a powered
    // sub (band-pass about 40 - 150 Hz) whose own LR24 low-pass is always in, at 80 / 100 / 120 Hz; the processor
    // offers Butterworth and Linkwitz-Riley 6 - 48 dB/oct.
    SpeakerSystem sys;
    sys.sources = { speaker ("Top", SpeakerRole::Main, SpeakerChannel::Mono, 1), speaker ("Sub", SpeakerRole::Sub, SpeakerChannel::Mono, 3) };
    auto& top = sys.sources[0];
    auto& sub = sys.sources[1];
    top.response = modelled (75.0, 2, 0.0, 0);        // -10 dB near 60 Hz, flat above 150
    top.imported = true;
    top.minSafeHpHz = 55.0;
    top.minSafeHpSlope = 24;
    // the sub's published curve: its band-pass with the onboard LR24 low-pass at 100 Hz in it (as a maker would publish)
    {
        auto bare = modelled (40.0, 4, 220.0, 4);   // with its own 100 Hz low-pass in, about 40 - 150 Hz at -10 dB
        const xo::Filter own { xo::LinkwitzRiley, 24, 100.0 };
        for (size_t i = 0; i < bare.freq.size(); ++i)
        {
            const auto v = xo::response (own, false, bare.freq[i]);
            bare.db[i] += 20.0 * std::log10 (std::max (std::abs (v), 1e-9));
            bare.phase[i] += std::arg (v) * 180.0 / kPi;
        }
        sub.response = bare;
        sub.imported = true;
        sub.onboard.present = true;
        sub.onboard.lowPass = true;
        sub.onboard.type = xo::LinkwitzRiley;
        sub.onboard.slope = 24;
        sub.onboard.choices = { 80.0, 100.0, 120.0 };
        sub.onboard.bypassable = false;
        sub.onboard.measuredAtHz = 100.0;
        sub.onboard.setHz = 100.0;
    }
    for (int i = 0; i < 8; ++i) sys.caps.slopes[i] = true;
    sys.caps.types[xo::Butterworth] = sys.caps.types[xo::LinkwitzRiley] = true;
    sys.caps.types[xo::Bessel] = false;
    sys.xoHigh = 0; sys.xoLow = 1; sys.xoChosen = true;
    const auto grid = xo::analysisGrid();
    const auto h = sys.groupResponse (0, grid), l = sys.groupResponse (1, grid);
    check (! h.empty() && ! l.empty(), "chain: both responses on the grid");
    // the onboard filter was taken out of the published curve: the bare sub is near full level at 100 Hz
    {
        size_t i100 = 0;
        for (size_t i = 0; i < grid.size(); ++i) if (std::abs (grid[i] - 100.0) < std::abs (grid[i100] - 100.0)) i100 = i;
        const double at100 = 20.0 * std::log10 (std::abs (l.h[i100])), at60 = 20.0 * std::log10 (std::abs (l.h[i100 - 36]));
        check (at100 > at60 - 4.0, "chain: the sub's own low-pass is removed from its published curve", at100 - at60, 0.0);
    }
    for (int goal = 0; goal <= 2; ++goal)
    {
        xo::Options opt;
        std::string why;
        const SpeakerSystem chain = sys;
        opt.candidates = [chain, goal] (double fLo, double fHi) { return chain.chainCandidates (fLo, fHi, goal); };
        check (! sys.chainCandidates (opt.fMin, opt.fMax, goal, &why).empty(), "chain: there are settings to try", 1.0, 1.0);
        const auto r = xo::optimise (h, l, sys.caps, opt);
        const juce::String g = goal == 0 ? "smoothest" : goal == 1 ? "balanced" : "max headroom";
        check (r.valid, ("chain (" + g + "): a crossover is found").toRawUTF8());
        check (r.lowExtra.active() && (near (r.lowExtra.freq, 80.0, 0.1) || near (r.lowExtra.freq, 100.0, 0.1) || near (r.lowExtra.freq, 120.0, 0.1)),
               ("chain (" + g + "): the sub's own low-pass at one of 80 / 100 / 120 Hz").toRawUTF8(), r.lowExtra.freq, 100.0);
        check (near (r.fc, r.lowExtra.freq, 0.1), ("chain (" + g + "): the crossover is at that setting").toRawUTF8(), r.fc, r.lowExtra.freq);
        if (r.tag == SpeakerSystem::TagOnboardOut) check (! r.lowFilter.active(), ("chain (" + g + "): the processor's sub low-pass is OUT").toRawUTF8());
        check (r.filter.active() && near (r.filter.freq, r.fc, 0.1), ("chain (" + g + "): the top's high-pass in the processor at the crossover").toRawUTF8(), r.filter.freq, r.fc);
        check (r.filter.freq >= 55.0, ("chain (" + g + "): never under the top's safe high-pass").toRawUTF8(), r.filter.freq, 55.0);
        if (goal == 1)
        {
            // this model's top is still 1 dB down at 100 Hz, so 120 Hz sums a touch flatter than 100: either is right
            check (r.fc >= 99.0 && r.fc <= 121.0, "chain (balanced): 100 or 120 Hz", r.fc, 100.0);
            check (r.filter.type == xo::LinkwitzRiley && r.filter.slope == 24, "chain (balanced): LR24 on the top's high-pass, matching the sub's own LR24", r.filter.type * 100 + r.filter.slope, 224);
            const auto text = sys.chainSettingsText (r);
            const juce::String fcText = juce::String (juce::roundToInt (r.fc)) + " Hz";
            check (text.contains ("Sub: onboard LPF " + fcText) && text.contains ("LPF OUT") && text.contains ("HPF " + fcText + " LR24"),
                   ("chain (balanced): the settings per device: " + text).toRawUTF8());
        }
        std::printf ("  chain %s: %s %d at %.1f Hz, sub own LPF %.0f, proc LPF %s, tag %d, delay %.2f, cost %.2f eff %.2f\n", g.toRawUTF8(), xo::typeName (r.filter.type), r.filter.slope, r.fc,
                     r.lowExtra.freq, r.lowFilter.active() ? "in" : "OUT", r.tag, r.delayLowMs, r.costDb, r.efficiency);
        if (std::getenv ("XO_PROBE") != nullptr)
            for (const auto& c : sys.chainCandidates (opt.fMin, opt.fMax, goal))
            {
                if (c.tag != SpeakerSystem::TagOnboardOut || ! c.hp.active() || c.hp.slope != 24) continue;
                xo::Options o1 = opt;
                o1.candidates = [c] (double, double) { return std::vector<xo::Candidate> { c }; };
                const auto rr = xo::optimise (h, l, sys.caps, o1);
                std::printf ("    %s %d @ %.0f: cost %.3f eff %.2f extra %.2f delay %.2f inv %d\n", xo::typeShort (c.hp.type), c.hp.slope, c.fc, rr.costDb, rr.efficiency, c.extraCost, rr.delayLowMs, (int) rr.invertLow);
            }
    }

    // No regression: a pair with no onboard filters gives exactly the same result through the chain as the plain search
    {
        SpeakerSystem plain;
        plain.sources = { speaker ("Top", SpeakerRole::Main, SpeakerChannel::Mono, 1), speaker ("Sub", SpeakerRole::Sub, SpeakerChannel::Mono, 3) };
        plain.sources[0].response = modelled (70.0, 2, 0.0, 0);
        plain.sources[1].response = modelled (35.0, 4, 160.0, 4, 6.0);
        plain.sources[0].imported = plain.sources[1].imported = true;
        plain.xoHigh = 0; plain.xoLow = 1; plain.xoChosen = true;
        const auto ph = plain.groupResponse (0, grid), pl = plain.groupResponse (1, grid);
        xo::Options a, b;
        const SpeakerSystem chain = plain;
        b.candidates = [chain] (double fLo, double fHi) { return chain.chainCandidates (fLo, fHi, 0); };
        const auto ra = xo::optimise (ph, pl, plain.caps, a), rb = xo::optimise (ph, pl, plain.caps, b);
        check (ra.valid && rb.valid && ra.filter.type == rb.filter.type && ra.filter.slope == rb.filter.slope && near (ra.fc, rb.fc, 1e-6)
               && near (ra.delayLowMs, rb.delayLowMs, 1e-6) && ra.invertLow == rb.invertLow && near (ra.gainLowDb, rb.gainLowDb, 1e-9),
               "chain: no onboard filters gives exactly the plain search's result", ra.fc, rb.fc);
    }

    // the processor's frequency steps: only settings it can take
    {
        xo::Capabilities c;
        c.freqStepOct = 1.0 / 3.0;
        c.freqLo = 20.0;
        check (near (c.snapFreq (95.0), 20.0 * std::pow (2.0, 7.0 / 3.0), 0.01), "processor: frequencies snap to its steps", c.snapFreq (95.0), 100.8);
        c.freqStepOct = 0.0;
        c.freqStepHz = 1.0;
        check (near (c.snapFreq (95.4), 95.0, 1e-9), "processor: 1 Hz steps", c.snapFreq (95.4), 95.0);
    }

    // the limiter target, with the arithmetic
    {
        SpeakerSystem ls;
        ls.sources = { speaker ("Top", SpeakerRole::Main, SpeakerChannel::Mono, 1) };
        ls.sources[0].rmsVoltLimit = 48.0;
        ls.sources[0].ampGain = "+32 dB";
        ls.caps.limiterUnit = 1;
        ls.caps.fullScaleDbu = 20.0;
        const auto t = sheet::limiterTarget (ls, ls.sources[0]);
        // 48 V = 35.8 dBu; minus 32 = 3.8 dBu; 0 dBFS = 20 dBu -> -16.2 dBFS
        check (t.contains ("35.8 dBu") && t.contains ("3.8 dBu") && t.contains ("-16.2 dBFS"), ("sheet: limiter target with the maths: " + t).toRawUTF8());
    }
}

int main()
{
    // a message thread (this one), for the audio device manager's change messages and device list listeners
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    // a private home for the preferences file
    const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("la-tests-home");
    home.createDirectory();
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
    levelBalanceTests();
    sheetTests();
    groupTests();
    recheckTests();
    prefsTests();
    allowanceTests();
    coverageTests();
    headroomTests();
    varianceTests();
    historyTests();
    airTests();
    venueListTests();
    climateTests();
    listenTests();
    interfaceTests();
    splIntervalTests();
    showLogTests();
    healthTests();
    traceMathTests();
    chainCrossoverTests();
    home.deleteRecursively();
    juce::DeletedAtShutdown::deleteAll();
    juce::MessageManager::deleteInstance();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
