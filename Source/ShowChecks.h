// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The checks between tuning and the show, and what they keep (session data unless noted):
//  - Aim and coverage: each speaker alone, its level across the points it covers (from Level balance's readings),
//    against the coverage spec, with what to do about a point that falls off ("tilt it down"), before and after.
//  - Gain and headroom: stepped pink noise up to the show level at FOH, per subsystem and together, watching for the
//    level to stop rising (compression, limiting) or the coherence to drop (distortion).
//  - Listening check: reference music at the points, notes per point, EQ in against bypassed.
//  - Seat-to-seat variance (Room EQ): how much the tonal balance differs from point to point.
//  - Change history (venue): every EQ, delay, trim and polarity change per output, with when, the step and why.
#include <cmath>
#include <map>
#include <vector>
#include "LevelBalance.h"
#include "VenuePlan.h"
#include "RoomTuning.h"

namespace la
{
// ---------------------------------------------------------------------------------------------------- coverage
struct CoveragePoint
{
    juce::String point;
    double db = NAN;          // against the speaker's own loudest point (0 there, negative elsewhere)
};

struct CoverageRow
{
    int spk = -1;
    juce::String onAxis;                  // its loudest point
    std::vector<CoveragePoint> points;    // the points it covers, in plan order
    double worstDb = 0.0;                 // the lowest of points[].db
    juce::String worstPoint;
    bool ok = true, enough = true;        // within the spec; measured at two points or more
    juce::String advice;                  // what to do when it is not
};

namespace coverage
{
constexpr double kLo = 1000.0, kHi = 4000.0;   // where the aim shows (below, the box is too wide to tell)

// Where a point and a speaker are on the plan, in metres when the room's size is known (else the drawing's units).
struct Geometry
{
    bool known = false;
    double dist = 0.0, offAxisDeg = 0.0, side = 0.0;   // side: + right of the speaker's axis, - left (seen from above)
};

inline Geometry geometryOf (const VenuePlan& plan, int spk, const juce::String& point)
{
    Geometry g;
    const int m = plan.markerOf (point);
    if (spk < 0 || spk >= (int) plan.speakers.size() || m < 0 || ! plan.speakers[(size_t) spk].placed) return g;
    const auto& s = plan.speakers[(size_t) spk];
    const auto& it = plan.items[(size_t) m];
    const double sx = plan.widthM > 0.0 ? plan.widthM : 1.0, sy = plan.lengthM > 0.0 ? plan.lengthM : 1.0;
    const double dx = (it.x - s.x) * sx, dy = (it.y - s.y) * sy;
    g.dist = std::hypot (dx, dy);
    if (g.dist < 1e-6) return g;
    const double a = juce::degreesToRadians ((double) s.angle);
    const double ax = std::cos (a), ay = std::sin (a);
    const double cosOff = juce::jlimit (-1.0, 1.0, (dx * ax + dy * ay) / g.dist);
    g.offAxisDeg = juce::radiansToDegrees (std::acos (cosOff));
    g.side = ax * dy - ay * dx;   // the cross product's sign: which side of the axis
    g.known = true;
    return g;
}

// What to do about a point that falls off.
inline juce::String adviceFor (const juce::String& name, const juce::String& point, double deficit, const Geometry& at, const Geometry& onAxis)
{
    const auto d = juce::String (deficit, 1) + " dB down at " + point;
    if (! at.known || ! onAxis.known) return name + " is " + d + ": check its aim toward " + point + " (place it on the plan for a better hint).";
    if (at.offAxisDeg > 55.0) return name + " is " + d + ", well off to its side: that seat needs a fill, or a wider box.";
    if (at.offAxisDeg > onAxis.offAxisDeg + 15.0) return name + " is " + d + ": turn it toward " + point + " (it points " + (at.side > 0.0 ? "left" : "right") + " of it).";
    if (at.dist > onAxis.dist * 1.25) return name + " is " + d + ": tilt it up, so it reaches further back.";
    if (at.dist < onAxis.dist * 0.8) return name + " is " + d + ": tilt it down, so it reaches the nearer seats.";
    return name + " is " + d + ": check its aim toward " + point + " (something in the way?).";
}

// The level of speaker `spk` across the points, from the readings `level (spk, point)` gives (NAN: not measured). A
// speaker covers a point when it is within 6 dB of the loudest top there. `tol`: the spec (plus or minus, dB).
inline std::vector<CoverageRow> analyse (const SpeakerSystem& sys, const juce::StringArray& points, const VenuePlan& plan, double tol,
                                         const std::function<double (int, const juce::String&)>& level)
{
    std::vector<CoverageRow> out;
    const int n = (int) sys.sources.size();
    auto isTop = [&sys] (int k) { return sys.sources[(size_t) k].role != (int) SpeakerRole::Sub; };
    for (int k = 0; k < n; ++k)
    {
        if (! isTop (k)) continue;
        CoverageRow row;
        row.spk = k;
        double best = -1e9;
        for (const auto& p : points)
        {
            const double v = level (k, p);
            if (! std::isfinite (v)) continue;
            double loudest = -1e9;
            for (int j = 0; j < n; ++j) if (isTop (j)) { const double o = level (j, p); if (std::isfinite (o)) loudest = std::max (loudest, o); }
            if (v < loudest - 6.0) continue;   // someone else's seat
            row.points.push_back ({ p, v });
            if (v > best) { best = v; row.onAxis = p; }
        }
        if (row.points.empty()) continue;
        for (auto& cp : row.points)
        {
            cp.db -= best;
            if (cp.db < row.worstDb) { row.worstDb = cp.db; row.worstPoint = cp.point; }
        }
        row.enough = row.points.size() >= 2;
        row.ok = -row.worstDb <= 2.0 * tol;
        if (! row.ok) row.advice = adviceFor (sys.nameOf (k), row.worstPoint, -row.worstDb, geometryOf (plan, k, row.worstPoint), geometryOf (plan, k, row.onAxis));
        else if (! row.enough) row.advice = sys.nameOf (k) + " is measured at one point only: tick a seat at the edge of its area on Level balance.";
        out.push_back (row);
    }
    return out;
}
} // namespace coverage

// What was re-aimed, and its levels before (kept from Level balance when the speaker was re-aimed).
struct CoverageData
{
    std::map<int, std::map<juce::String, double>> before;   // speaker -> point -> level (dB, 1 - 4 kHz), as found
    std::map<int, juce::String> reaimedOn;
    bool done = false;                                       // the user is happy with the coverage
    juce::String doneOn;

    void speakerRemoved (int i)
    {
        std::map<int, std::map<juce::String, double>> b;
        std::map<int, juce::String> r;
        for (auto& [k, v] : before) if (k != i) b[k > i ? k - 1 : k] = v;
        for (auto& [k, v] : reaimedOn) if (k != i) r[k > i ? k - 1 : k] = v;
        before = b;
        reaimedOn = r;
    }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("Coverage");
        e->setAttribute ("done", done);
        e->setAttribute ("doneOn", doneOn);
        for (const auto& [k, pts] : before)
        {
            auto* c = e->createNewChildElement ("Before");
            c->setAttribute ("spk", k);
            if (auto it = reaimedOn.find (k); it != reaimedOn.end()) c->setAttribute ("reaimedOn", it->second);
            for (const auto& [p, v] : pts)
            {
                auto* q = c->createNewChildElement ("Point");
                q->setAttribute ("name", p);
                q->setAttribute ("db", v);
            }
        }
    }
    void load (const juce::XmlElement* parent)
    {
        *this = {};
        const auto* e = parent != nullptr ? parent->getChildByName ("Coverage") : nullptr;
        if (e == nullptr) return;
        done = e->getBoolAttribute ("done");
        doneOn = e->getStringAttribute ("doneOn");
        for (const auto* c : e->getChildWithTagNameIterator ("Before"))
        {
            const int k = c->getIntAttribute ("spk", -1);
            if (k < 0) continue;
            if (c->hasAttribute ("reaimedOn")) reaimedOn[k] = c->getStringAttribute ("reaimedOn");
            for (const auto* q : c->getChildWithTagNameIterator ("Point")) before[k][q->getStringAttribute ("name")] = q->getDoubleAttribute ("db");
        }
    }
};

// ---------------------------------------------------------------------------------------------------- headroom
struct HeadroomStep
{
    double genDb = 0.0;       // the generator (dBFS)
    double splDb = NAN;       // at the FOH mic (dBA slow; dBFS(A) when the mic is not calibrated)
    double coherence = NAN;   // mean, 200 Hz - 5 kHz
};

struct HeadroomRun
{
    int output = 0;                       // 0: the whole system; else the speaker's output
    juce::String name;                    // "Whole system", "Main L" ...
    std::vector<HeadroomStep> steps;
    int compressedAt = -1;                // the first step that rose less than the generator did (or distorted)
    bool distorted = false, ceiling = false, reached = false, stopped = false;
    juce::String when;

    // The loudest clean level reached.
    double cleanMax() const
    {
        double m = NAN;
        for (int i = 0; i < (int) steps.size(); ++i)
            if ((compressedAt < 0 || i < compressedAt) && std::isfinite (steps[(size_t) i].splDb)) m = std::isfinite (m) ? std::max (m, steps[(size_t) i].splDb) : steps[(size_t) i].splDb;
        return m;
    }
};

namespace headroom
{
// Looks at the latest step: did the level rise with the generator (within `tol` dB), and is the coherence holding?
// Returns 0 clean, 1 compressing, 2 distorting.
inline int judge (const std::vector<HeadroomStep>& s, double tol = 1.0)
{
    if (s.size() < 2) return 0;
    const auto& a = s[s.size() - 2];
    const auto& b = s.back();
    if (! std::isfinite (a.splDb) || ! std::isfinite (b.splDb)) return 0;
    const double rose = b.splDb - a.splDb, asked = b.genDb - a.genDb;
    if (asked > 0.1 && rose < asked - tol) return 1;
    // distortion: only judged where the coherence was good to start with
    if (std::isfinite (s.front().coherence) && s.front().coherence >= 0.5 && std::isfinite (b.coherence) && b.coherence < s.front().coherence - 0.12) return 2;
    return 0;
}

// "8 dB of headroom", "3 dB short" ... for the whole-system run, against the target and the headroom wanted.
inline juce::String verdict (const HeadroomRun& r, double target, double wanted, bool spl, int& level)
{
    level = 0;
    const double clean = r.cleanMax();
    if (! std::isfinite (clean)) { level = 1; return "Not measured yet."; }
    if (! spl) { level = 1; return "The FOH mic is not calibrated: the levels are in dBFS(A), so the headroom against " + juce::String ((int) target) + " dBA cannot be told. Calibrate it on Mics."; }
    const double head = clean - target;
    juce::String t;
    if (r.compressedAt >= 0)
    {
        t = juce::String (r.distorted ? "Distortion" : "Compression") + " from " + juce::String (r.steps[(size_t) r.compressedAt].splDb, 1) + " dBA: clean to "
            + juce::String (clean, 1) + " dBA. ";
        if (head < 0.0) { level = 2; t << juce::String (-head, 1) << " dB short of the target: the system cannot reach " << (int) target << " dBA cleanly. Check the amp gains and limiters, or plan a lower show level."; }
        else if (head < wanted) { level = 1; t << "Only " << juce::String (head, 1) << " dB above the target (" << juce::String (juce::roundToInt (wanted)) << " wanted): little room for peaks."; }
        else t << juce::String (head, 1) << " dB of headroom above the target.";
        return t;
    }
    if (r.reached) return "Clean to " + juce::String (clean, 1) + " dBA: at least " + juce::String (head, 1) + " dB of headroom above the target, no compression seen.";
    if (r.ceiling)
    {
        level = 1;
        return "Clean to " + juce::String (clean, 1) + " dBA, but the generator reached its ceiling (Settings) "
               + (head < wanted ? "before the target plus headroom: raise the generator's ceiling, or the gain in the processor, and run it again." : "after the target plus headroom.");
    }
    level = 1;
    return "Stopped at " + juce::String (clean, 1) + " dBA, before the target plus headroom.";
}
} // namespace headroom

struct HeadroomData
{
    std::vector<HeadroomRun> runs;   // the whole system first when there is one

    const HeadroomRun* find (int output) const { for (const auto& r : runs) if (r.output == output) return &r; return nullptr; }
    HeadroomRun& get (int output, const juce::String& name)
    {
        for (auto& r : runs) if (r.output == output) { r.name = name; return r; }
        runs.push_back ({});
        runs.back().output = output;
        runs.back().name = name;
        return runs.back();
    }

    void save (juce::XmlElement& parent) const
    {
        if (runs.empty()) return;
        auto* e = parent.createNewChildElement ("Headroom");
        for (const auto& r : runs)
        {
            auto* c = e->createNewChildElement ("Run");
            c->setAttribute ("output", r.output);
            c->setAttribute ("name", r.name);
            c->setAttribute ("compressedAt", r.compressedAt);
            c->setAttribute ("distorted", r.distorted);
            c->setAttribute ("ceiling", r.ceiling);
            c->setAttribute ("reached", r.reached);
            c->setAttribute ("when", r.when);
            juce::String t;
            for (const auto& s : r.steps) t << juce::String (s.genDb, 1) << ' ' << juce::String (s.splDb, 2) << ' ' << juce::String (s.coherence, 3) << '\n';
            c->addTextElement (t);
        }
    }
    void load (const juce::XmlElement* parent)
    {
        *this = {};
        const auto* e = parent != nullptr ? parent->getChildByName ("Headroom") : nullptr;
        if (e == nullptr) return;
        for (const auto* c : e->getChildWithTagNameIterator ("Run"))
        {
            HeadroomRun r;
            r.output = c->getIntAttribute ("output");
            r.name = c->getStringAttribute ("name");
            r.compressedAt = c->getIntAttribute ("compressedAt", -1);
            r.distorted = c->getBoolAttribute ("distorted");
            r.ceiling = c->getBoolAttribute ("ceiling");
            r.reached = c->getBoolAttribute ("reached");
            r.when = c->getStringAttribute ("when");
            juce::StringArray lines;
            lines.addLines (c->getAllSubText());
            for (const auto& l : lines)
            {
                juce::StringArray v;
                v.addTokens (l, " ", {});
                v.removeEmptyStrings();
                if (v.size() < 3) continue;
                r.steps.push_back ({ v[0].getDoubleValue(), v[1].getDoubleValue(), v[2].getDoubleValue() });
            }
            runs.push_back (r);
        }
    }
};

// ---------------------------------------------------------------------------------------------------- listening
struct ListeningData
{
    struct Point { int verdict = 0; juce::String notes; bool compared = false; };   // verdict: 0 not yet, 1 good, 2 a problem
    std::map<juce::String, Point> points;
    juce::String music;            // what was played
    bool done = false;
    juce::String doneOn;

    int count (int verdict) const { int n = 0; for (const auto& [k, v] : points) n += v.verdict == verdict ? 1 : 0; return n; }

    void save (juce::XmlElement& parent) const
    {
        if (points.empty() && ! done && music.isEmpty()) return;
        auto* e = parent.createNewChildElement ("Listening");
        e->setAttribute ("music", music);
        e->setAttribute ("done", done);
        e->setAttribute ("doneOn", doneOn);
        for (const auto& [k, v] : points)
        {
            auto* c = e->createNewChildElement ("Point");
            c->setAttribute ("name", k);
            c->setAttribute ("verdict", v.verdict);
            c->setAttribute ("compared", v.compared);
            c->setAttribute ("notes", v.notes);
        }
    }
    void load (const juce::XmlElement* parent)
    {
        *this = {};
        const auto* e = parent != nullptr ? parent->getChildByName ("Listening") : nullptr;
        if (e == nullptr) return;
        music = e->getStringAttribute ("music");
        done = e->getBoolAttribute ("done");
        doneOn = e->getStringAttribute ("doneOn");
        for (const auto* c : e->getChildWithTagNameIterator ("Point"))
            points[c->getStringAttribute ("name")] = { juce::jlimit (0, 2, c->getIntAttribute ("verdict")), c->getStringAttribute ("notes"), c->getBoolAttribute ("compared") };
    }
};

// ---------------------------------------------------------------------------------------------------- variance
// How much the tonal balance differs from seat to seat (dB): each point's response against its own level (500 Hz - 4
// kHz), the spread across the points (standard deviation) at each sixth of an octave from 125 Hz to 8 kHz, averaged.
// Lower is better; under 2 dB is very even. NAN with fewer than two points.
inline double seatVariance (const std::vector<const FreqTable*>& tfs)
{
    if (tfs.size() < 2) return NAN;
    std::vector<double> mids;
    for (const auto* t : tfs)
    {
        double s = 0.0;
        int n = 0;
        for (size_t i = 0; i < t->freq.size(); ++i)
            if (t->freq[i] >= 500.0 && t->freq[i] <= 4000.0 && std::isfinite (t->db[i])) { s += t->db[i]; ++n; }
        mids.push_back (n > 0 ? s / n : NAN);
    }
    double total = 0.0;
    int bands = 0;
    for (double f = 125.0; f <= 8000.0; f *= std::pow (2.0, 1.0 / 6.0))
    {
        std::vector<double> v;
        for (size_t k = 0; k < tfs.size(); ++k)
        {
            if (! std::isfinite (mids[k]) || tfs[k]->freq.empty() || f < tfs[k]->freq.front() || f > tfs[k]->freq.back()) continue;
            // a sixth of an octave around f
            double s = 0.0;
            int n = 0;
            for (size_t i = 0; i < tfs[k]->freq.size(); ++i)
                if (tfs[k]->freq[i] >= f / std::pow (2.0, 1.0 / 12.0) && tfs[k]->freq[i] <= f * std::pow (2.0, 1.0 / 12.0) && std::isfinite (tfs[k]->db[i])) { s += tfs[k]->db[i]; ++n; }
            if (n > 0) v.push_back (s / n - mids[k]);
        }
        if (v.size() < 2) continue;
        double m = 0.0;
        for (double x : v) m += x;
        m /= (double) v.size();
        double var = 0.0;
        for (double x : v) var += (x - m) * (x - m);
        total += std::sqrt (var / (double) (v.size() - 1));
        ++bands;
    }
    return bands > 0 ? total / bands : NAN;
}

inline double seatVariance (const RoomTuning& room, bool after)
{
    std::vector<const FreqTable*> tfs;
    for (const auto& m : room.meas) if (m.after == after && m.include) tfs.push_back (&m.tf);
    return seatVariance (tfs);
}

// ---------------------------------------------------------------------------------------------------- history
// Every change to an output's processing (EQ, delay, trim, polarity), with when, the step and why. Venue data: it
// follows the system from session to session. Each entry keeps every output's processing as it was before, so it can
// be rolled back to.
struct ChangeLog
{
    struct Entry
    {
        juce::String when, step, output, what, why;
        std::unique_ptr<juce::XmlElement> before;   // every output's processing before this change (see snapshot)
        Entry() = default;
        Entry (const Entry& o) : when (o.when), step (o.step), output (o.output), what (o.what), why (o.why),
                                 before (o.before != nullptr ? std::make_unique<juce::XmlElement> (*o.before) : nullptr) {}
        Entry& operator= (const Entry& o)
        {
            when = o.when; step = o.step; output = o.output; what = o.what; why = o.why;
            before = o.before != nullptr ? std::make_unique<juce::XmlElement> (*o.before) : nullptr;
            return *this;
        }
        Entry (Entry&&) = default;
        Entry& operator= (Entry&&) = default;
    };
    std::vector<Entry> entries;
    static constexpr size_t kMax = 400;

    // The differences between two systems, one line per output and kind of change.
    static std::vector<std::pair<juce::String, juce::String>> diff (const SpeakerSystem& a, const SpeakerSystem& b)
    {
        std::vector<std::pair<juce::String, juce::String>> out;
        const int n = (int) std::min (a.sources.size(), b.sources.size());
        auto f2 = [] (double v) { return juce::String (v, 2); };
        auto f1 = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1); };
        for (int i = 0; i < n; ++i)
        {
            const auto& x = a.sources[(size_t) i];
            const auto& y = b.sources[(size_t) i];
            const auto name = b.nameOf (i);
            if (std::abs (x.proc.delayMs - y.proc.delayMs) > 0.005) out.push_back ({ name, "Delay " + f2 (x.proc.delayMs) + " -> " + f2 (y.proc.delayMs) + " ms" });
            if (std::abs (x.trimDb - y.trimDb) > 0.05) out.push_back ({ name, "Trim " + f1 (x.trimDb) + " -> " + f1 (y.trimDb) + " dB" });
            if (std::abs (x.proc.gainDb - y.proc.gainDb) > 0.05) out.push_back ({ name, "Gain " + f1 (x.proc.gainDb) + " -> " + f1 (y.proc.gainDb) + " dB" });
            if (x.proc.invert != y.proc.invert) out.push_back ({ name, juce::String ("Polarity ") + (y.proc.invert ? "inverted" : "normal") });
            if (eqText (x.proc.eq) != eqText (y.proc.eq))
                out.push_back ({ name, "EQ " + juce::String ((int) x.proc.eq.size()) + " -> " + juce::String ((int) y.proc.eq.size()) + " bands: " + (y.proc.eq.empty() ? juce::String ("flat") : eqText (y.proc.eq)) });
            if (std::abs (x.proc.hp.freq - y.proc.hp.freq) > 0.5 || x.proc.hp.type != y.proc.hp.type || x.proc.hp.slope != y.proc.hp.slope
                || std::abs (x.proc.lp.freq - y.proc.lp.freq) > 0.5 || x.proc.lp.type != y.proc.lp.type || x.proc.lp.slope != y.proc.lp.slope)
                out.push_back ({ name, "Crossover filters changed" });
        }
        if (a.sources.size() != b.sources.size()) out.push_back ({ "System", juce::String ((int) a.sources.size()) + " -> " + juce::String ((int) b.sources.size()) + " speakers" });
        return out;
    }

    static juce::String eqText (const std::vector<eq::Band>& bands)
    {
        juce::StringArray t;
        for (const auto& b : bands)
            t.add (juce::String (juce::roundToInt (b.freq)) + " Hz " + (b.gainDb > 0.0 ? "+" : "") + juce::String (b.gainDb, 1) + " dB Q" + juce::String (b.q, 1));
        return t.joinIntoString (", ");
    }

    // Each output's processing only (not the measurements): what a roll back puts back.
    static std::unique_ptr<juce::XmlElement> snapshot (const SpeakerSystem& sys)
    {
        auto x = std::make_unique<juce::XmlElement> ("Processing");
        for (const auto& src : sys.sources)
        {
            auto* c = x->createNewChildElement ("Out");
            const auto& p = src.proc;
            c->setAttribute ("delayMs", p.delayMs);
            c->setAttribute ("gainDb", p.gainDb);
            c->setAttribute ("trimDb", src.trimDb);
            c->setAttribute ("invert", p.invert);
            c->setAttribute ("hp", juce::String (p.hp.type) + " " + juce::String (p.hp.slope) + " " + juce::String (p.hp.freq, 2));
            c->setAttribute ("lp", juce::String (p.lp.type) + " " + juce::String (p.lp.slope) + " " + juce::String (p.lp.freq, 2));
            juce::StringArray eqs;
            for (const auto& b : p.eq) eqs.add (juce::String (b.freq, 2) + " " + juce::String (b.gainDb, 2) + " " + juce::String (b.q, 3));
            c->setAttribute ("eq", eqs.joinIntoString (";"));
        }
        return x;
    }
    static void restore (SpeakerSystem& sys, const juce::XmlElement& x)
    {
        int i = 0;
        for (const auto* c : x.getChildWithTagNameIterator ("Out"))
        {
            if (i >= (int) sys.sources.size()) break;
            auto& src = sys.sources[(size_t) i++];
            auto& p = src.proc;
            p.delayMs = c->getDoubleAttribute ("delayMs");
            p.gainDb = c->getDoubleAttribute ("gainDb");
            src.trimDb = c->getDoubleAttribute ("trimDb");
            p.invert = c->getBoolAttribute ("invert");
            auto filt = [c] (const char* k, xo::Filter& f)
            {
                juce::StringArray v;
                v.addTokens (c->getStringAttribute (k), " ", {});
                if (v.size() < 3) return;
                f.type = v[0].getIntValue(); f.slope = v[1].getIntValue(); f.freq = v[2].getDoubleValue();
            };
            filt ("hp", p.hp);
            filt ("lp", p.lp);
            p.eq.clear();
            juce::StringArray bands;
            bands.addTokens (c->getStringAttribute ("eq"), ";", {});
            for (const auto& b : bands)
            {
                juce::StringArray v;
                v.addTokens (b, " ", {});
                if (v.size() >= 3) p.eq.push_back ({ v[0].getDoubleValue(), v[1].getDoubleValue(), v[2].getDoubleValue() });
            }
        }
    }

    void add (const juce::String& when, const juce::String& step, const juce::String& output, const juce::String& what, const juce::String& why,
              const juce::XmlElement* systemBefore)
    {
        Entry e;
        e.when = when; e.step = step; e.output = output; e.what = what; e.why = why;
        if (systemBefore != nullptr) e.before = std::make_unique<juce::XmlElement> (*systemBefore);
        entries.push_back (std::move (e));
        if (entries.size() > kMax) entries.erase (entries.begin(), entries.begin() + (long) (entries.size() - kMax));
    }

    void save (juce::XmlElement& parent) const
    {
        if (entries.empty()) return;
        auto* e = parent.createNewChildElement ("History");
        for (const auto& en : entries)
        {
            auto* c = e->createNewChildElement ("Change");
            c->setAttribute ("when", en.when);
            c->setAttribute ("step", en.step);
            c->setAttribute ("output", en.output);
            c->setAttribute ("what", en.what);
            c->setAttribute ("why", en.why);
            if (en.before != nullptr) c->addChildElement (new juce::XmlElement (*en.before));
        }
    }
    void load (const juce::XmlElement& parent)
    {
        entries.clear();
        const auto* e = parent.getChildByName ("History");
        if (e == nullptr) return;
        for (const auto* c : e->getChildWithTagNameIterator ("Change"))
        {
            Entry en;
            en.when = c->getStringAttribute ("when");
            en.step = c->getStringAttribute ("step");
            en.output = c->getStringAttribute ("output");
            en.what = c->getStringAttribute ("what");
            en.why = c->getStringAttribute ("why");
            if (const auto* b = c->getFirstChildElement()) en.before = std::make_unique<juce::XmlElement> (*b);
            entries.push_back (std::move (en));
        }
    }
};
} // namespace la
