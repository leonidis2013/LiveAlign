// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Tests for LiveAlign's tuning logic (with JUCE's core): level balance, the processor sheet, the quick re-check's
// record, preferences and venue overrides, the Room EQ allowance, aim and coverage, gain and headroom, seat-to-seat
// variance, the change history, the air, the venue list and the MQTT climate feed (against a tiny broker here). Built with LA_BUILD_TESTS (target AppTests).
#include <cstdio>
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

int main()
{
    // a private home for the preferences file
    const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("la-tests-home");
    home.createDirectory();
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
    levelBalanceTests();
    sheetTests();
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
    home.deleteRecursively();
    std::printf ("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
