// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Level balance (tuning step 3, before time alignment): each speaker on its own, measured at the listening points,
// so the speakers play at the right level against each other where the audience hears them: left against right at
// the centre, a fill against the mains at the seats it covers, the sub against the tops against the target's low
// end. Also the quick re-check of a system already tuned. Session data. Message thread only.
#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>
#include "Analysis/FreqTable.h"
#include "Analysis/TargetCurves.h"
#include "SpeakerSystem.h"

namespace la
{
// One speaker alone at one point: its transfer function there (dB, 1/12 octave), its arrival, and the delay it had in
// the processor then (alignment comes after, so the arrival is compared allowing for the delay entered since).
struct LevelReading
{
    int spk = 0;
    juce::String point, when;
    FreqTable tf;              // dB only, 1/12 octave
    bool hasArrival = false;
    double arrivalMs = 0.0, delayAtMs = 0.0, trimAtDb = 0.0;
    double eqAtDb = 0.0;       // the speaker's EQ through its range then (the room EQ entered later changes its level)
    bool recheck = false;      // measured again after the trims went in
    int mic = 0;               // the mic it was measured with
    double genDb = NAN;        // the generator's level then (dBFS), and the A-weighted level it gave at the mic (dBA,
    double splDbA = NAN;       // when the mic was SPL-calibrated): Gain and headroom starts from what these predict

    double band (double lo, double hi) const { return SpeakerSystem::bandLevel (tf, lo, hi); }
};

struct LevelTrim
{
    int spk = -1;
    double db = 0.0;           // the change to make (negative: turn it down)
    juce::String why;
    bool attention = true;     // outside the tolerance (a change is suggested), or a note only
};

// The aims (Settings, Tuning defaults; a venue can have its own).
struct LevelAims { double pairTol = 1.0, fillHi = 0.0, fillLo = -3.0, subTol = 1.5, trimCap = 6.0; };

struct LevelBalance
{
    std::vector<LevelReading> readings;
    juce::StringArray points;         // the plan points measured at, in order (the first is the centre / FOH)
    bool applied = false;             // the trims were put on the processor sheet
    juce::String appliedOn;

    static constexpr double kPairTolDb = 1.0, kSubTolDb = 1.5, kMaxTrimDb = 6.0;
    using Aims = LevelAims;

    // A speaker's EQ through its own range (dB): readings taken before the room EQ went in are compared allowing for it.
    static double eqInRange (const SpeakerSystem& sys, int i)
    {
        if (i < 0 || i >= (int) sys.sources.size()) return 0.0;
        double lo, hi;
        sys.playRange (i, lo, hi);
        double p = 0.0;
        int n = 0;
        for (double f = lo; f <= hi; f *= 1.06) { p += std::pow (10.0, eq::totalDb (sys.sources[(size_t) i].proc.eq, f) / 10.0); ++n; }
        return n > 0 ? 10.0 * std::log10 (p / n) : 0.0;
    }

    const LevelReading* find (int spk, const juce::String& point, bool recheck) const
    {
        const LevelReading* best = nullptr;
        for (const auto& r : readings)
            if (r.spk == spk && r.point == point && r.recheck == recheck) best = &r;
        return best;
    }
    // The latest reading of a speaker at a point: the re-check when there is one.
    const LevelReading* latest (int spk, const juce::String& point) const
    {
        if (const auto* r = find (spk, point, true)) return r;
        return find (spk, point, false);
    }

    void set (LevelReading r)
    {
        readings.erase (std::remove_if (readings.begin(), readings.end(),
                                        [&r] (const LevelReading& x) { return x.spk == r.spk && x.point == r.point && x.recheck == r.recheck; }),
                        readings.end());
        readings.push_back (std::move (r));
    }

    // After speaker `i` was removed from the system.
    void speakerRemoved (int i)
    {
        readings.erase (std::remove_if (readings.begin(), readings.end(), [i] (const LevelReading& x) { return x.spk == i; }), readings.end());
        for (auto& r : readings) if (r.spk > i) --r.spk;
    }

    // Power sum of the given speakers' levels in a band at a point (each measured alone).
    double sumOf (const std::vector<int>& spks, const juce::String& point, double lo, double hi, bool recheck) const
    {
        double p = 0.0;
        int n = 0;
        for (int k : spks)
        {
            const auto* r = recheck ? latest (k, point) : find (k, point, false);
            if (r == nullptr) continue;
            const double v = r->band (lo, hi);
            if (! std::isfinite (v)) continue;
            p += std::pow (10.0, v / 10.0);
            ++n;
        }
        return n > 0 ? 10.0 * std::log10 (p) : NAN;
    }

    // What to change, from the readings: `recheck` uses the latest readings (to see whether the trims took).
    std::vector<LevelTrim> suggest (const SpeakerSystem& sys, int target, bool recheck = false, Aims aims = {}) const
    {
        const double kPairTolDb = aims.pairTol, kSubTolDb = aims.subTol, kMaxTrimDb = aims.trimCap;
        const double fillAim = 0.5 * (aims.fillHi + aims.fillLo);
        std::vector<LevelTrim> out;
        if (points.isEmpty()) return out;
        const auto centre = points[0];
        const int n = (int) sys.sources.size();
        auto level = [&] (int k, const juce::String& p, double lo, double hi)
        {
            const auto* r = recheck ? latest (k, p) : find (k, p, false);
            return r != nullptr ? r->band (lo, hi) : NAN;
        };
        auto f1 = [] (double v) { return juce::String (v, 1); };

        // left against right at the centre
        for (int l = 0; l < n; ++l)
            for (int r = 0; r < n; ++r)
            {
                const auto& L = sys.sources[(size_t) l];
                const auto& R = sys.sources[(size_t) r];
                if (L.role != R.role || L.channel != (int) SpeakerChannel::Left || R.channel != (int) SpeakerChannel::Right) continue;
                double lo, hi;
                sys.playRange (l, lo, hi);
                const double a = level (l, centre, lo, hi), b = level (r, centre, lo, hi);
                if (! std::isfinite (a) || ! std::isfinite (b)) continue;
                const double d = b - a;   // right louder: positive
                LevelTrim t;
                t.spk = d > 0.0 ? r : l;
                t.db = -std::abs (d);
                t.attention = std::abs (d) > kPairTolDb;
                t.why = sys.nameOf (r) + " is " + f1 (std::abs (d)) + " dB " + (d > 0.0 ? "louder" : "quieter") + " than " + sys.nameOf (l) + " at " + centre
                        + (t.attention ? ": turn " + sys.nameOf (t.spk) + " down " + f1 (std::abs (d)) + " dB." : ": within " + f1 (kPairTolDb) + " dB.");
                if (! t.attention) t.db = 0.0;
                out.push_back (t);
            }

        // the mains (tops), for the fills and the sub
        std::vector<int> mains;
        for (int k = 0; k < n; ++k) if (sys.sources[(size_t) k].role == (int) SpeakerRole::Main) mains.push_back (k);

        // a fill or delay against the mains at the seat it covers best (the point where it is loudest against them)
        for (int k = 0; k < n; ++k)
        {
            const int role = sys.sources[(size_t) k].role;
            if ((role != (int) SpeakerRole::Fill && role != (int) SpeakerRole::Delay) || mains.empty()) continue;
            double lo, hi;
            sys.playRange (k, lo, hi);
            lo = std::max (lo, 200.0);
            hi = std::max (hi, lo * 2.0);
            juce::String bestPoint;
            double best = -1e9;
            for (const auto& p : points)
            {
                const double f = level (k, p, lo, hi), m = sumOf (mains, p, lo, hi, recheck);
                if (std::isfinite (f) && std::isfinite (m) && f - m > best) { best = f - m; bestPoint = p; }
            }
            if (bestPoint.isEmpty()) continue;
            LevelTrim t;
            t.spk = k;
            if (best > aims.fillHi) t.db = fillAim - best;        // louder than the mains where it covers: down to the middle of the aim
            else if (best < aims.fillLo) t.db = fillAim - best;   // too quiet to help there: up to the middle
            const bool capped = std::abs (t.db) > kMaxTrimDb;
            t.db = juce::jlimit (-kMaxTrimDb, kMaxTrimDb, t.db);
            t.attention = std::abs (t.db) > 0.05;
            t.why = sys.nameOf (k) + " is " + f1 (best) + " dB against the mains at " + bestPoint + " (aim for " + f1 (aims.fillHi) + " to " + f1 (aims.fillLo) + " dB)"
                    + (t.attention ? ": change it " + (t.db > 0.0 ? juce::String ("+") : juce::String()) + f1 (t.db) + " dB" + (capped ? " (as far as a trim should go: check its aim and amp too)." : ".") : ".");
            if (points.size() < 2) t.why << " Measure it at the seats it covers too (add the point).";
            out.push_back (t);
        }

        // the sub against the tops at the centre, against the target's low end
        for (int k = 0; k < n; ++k)
        {
            if (sys.sources[(size_t) k].role != (int) SpeakerRole::Sub || mains.empty()) continue;
            double lo, hi;
            sys.playRange (k, lo, hi);
            hi = std::min (hi, 120.0);
            lo = std::min (lo, hi / 1.5);
            const double sub = level (k, centre, lo, hi), tops = sumOf (mains, centre, 500.0, 2000.0, recheck);
            if (! std::isfinite (sub) || ! std::isfinite (tops)) continue;
            double tLow = 0.0, tMid = 0.0;
            int nl = 0, nm = 0;
            for (double f = lo; f <= hi; f *= 1.06) { tLow += targets::db (target, f); ++nl; }
            for (double f = 500.0; f <= 2000.0; f *= 1.06) { tMid += targets::db (target, f); ++nm; }
            const double expected = tLow / juce::jmax (1, nl) - tMid / juce::jmax (1, nm);
            const double measured = sub - tops;
            LevelTrim t;
            t.spk = k;
            t.db = juce::jlimit (-kMaxTrimDb, kMaxTrimDb, expected - measured);
            t.attention = std::abs (t.db) > kSubTolDb;
            t.why = sys.nameOf (k) + " is " + f1 (measured) + " dB against the tops at " + centre + "; the target wants " + f1 (expected) + " dB"
                    + (t.attention ? ": change it " + (t.db > 0.0 ? juce::String ("+") : juce::String()) + f1 (t.db) + " dB." : ": close enough.");
            if (! t.attention) t.db = 0.0;
            out.push_back (t);
        }
        return out;
    }

    // From each speaker alone at `point` (its A-weighted level at a known generator level, allowing for the trim and
    // the EQ entered since): the generator level at which the whole system would give `wantDbA` there, the speakers
    // summed by power. NAN unless every speaker on an output has such a reading (a missing one would make the guess
    // too loud).
    double predictGenDb (const SpeakerSystem& sys, const juce::String& point, double wantDbA) const
    {
        double sum = 0.0;
        int n = 0;
        for (int k = 0; k < (int) sys.sources.size(); ++k)
        {
            const auto& s = sys.sources[(size_t) k];
            if (s.output <= 0) continue;
            const auto* r = latest (k, point);
            if (r == nullptr || ! std::isfinite (r->splDbA) || ! std::isfinite (r->genDb)) return NAN;
            const double atFullScale = r->splDbA - r->genDb + (s.trimDb - r->trimAtDb) + (eqInRange (sys, k) - r->eqAtDb);
            sum += std::pow (10.0, atFullScale / 10.0);
            ++n;
        }
        return n > 0 ? wantDbA - 10.0 * std::log10 (sum) : NAN;
    }

    // Cabinets on one output, from their close-up captures: within 1.5 dB of the first (a note: they share a level).
    static std::vector<LevelTrim> cabinetNotes (const SpeakerSystem& sys)
    {
        std::vector<LevelTrim> out;
        for (int k = 0; k < (int) sys.sources.size(); ++k)
        {
            const auto& s = sys.sources[(size_t) k];
            const auto* first = s.polarityOf (0);
            if (s.count < 2 || first == nullptr || ! std::isfinite (first->levelDb)) continue;
            for (int c = 1; c < s.count; ++c)
            {
                const auto* pc = s.polarityOf (c);
                if (pc == nullptr || ! std::isfinite (pc->levelDb)) continue;
                const double d = pc->levelDb - first->levelDb;
                if (std::abs (d) <= 1.5) continue;
                out.push_back ({ k, 0.0, sys.nameOf (k) + " cabinet " + juce::String (c + 1) + " is " + juce::String (std::abs (d), 1) + " dB "
                                             + (d > 0.0 ? "louder" : "quieter") + " than cabinet 1: check its amp channel, wiring and driver.", true });
            }
        }
        return out;
    }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("LevelBalance");
        e->setAttribute ("points", points.joinIntoString ("|"));
        e->setAttribute ("applied", applied);
        e->setAttribute ("appliedOn", appliedOn);
        for (const auto& r : readings)
        {
            auto* c = e->createNewChildElement ("Reading");
            c->setAttribute ("spk", r.spk);
            c->setAttribute ("point", r.point);
            c->setAttribute ("when", r.when);
            c->setAttribute ("hasArrival", r.hasArrival);
            c->setAttribute ("arrivalMs", r.arrivalMs);
            c->setAttribute ("delayAtMs", r.delayAtMs);
            c->setAttribute ("trimAtDb", r.trimAtDb);
            c->setAttribute ("eqAtDb", r.eqAtDb);
            c->setAttribute ("recheck", r.recheck);
            c->setAttribute ("mic", r.mic);
            if (std::isfinite (r.genDb)) c->setAttribute ("genDb", r.genDb);
            if (std::isfinite (r.splDbA)) c->setAttribute ("splDbA", r.splDbA);
            juce::String text;
            for (size_t i = 0; i < r.tf.freq.size(); ++i) text << juce::String (r.tf.freq[i], 2) << ' ' << juce::String (r.tf.db[i], 2) << '\n';
            c->addTextElement (text);
        }
    }

    void load (const juce::XmlElement* parent)
    {
        *this = {};
        const auto* e = parent != nullptr ? parent->getChildByName ("LevelBalance") : nullptr;
        if (e == nullptr) return;
        points.addTokens (e->getStringAttribute ("points"), "|", {});
        points.removeEmptyStrings();
        applied = e->getBoolAttribute ("applied");
        appliedOn = e->getStringAttribute ("appliedOn");
        for (const auto* c : e->getChildWithTagNameIterator ("Reading"))
        {
            LevelReading r;
            r.spk = c->getIntAttribute ("spk");
            r.point = c->getStringAttribute ("point");
            r.when = c->getStringAttribute ("when");
            r.hasArrival = c->getBoolAttribute ("hasArrival");
            r.arrivalMs = c->getDoubleAttribute ("arrivalMs");
            r.delayAtMs = c->getDoubleAttribute ("delayAtMs");
            r.trimAtDb = c->getDoubleAttribute ("trimAtDb");
            r.eqAtDb = c->getDoubleAttribute ("eqAtDb");
            r.recheck = c->getBoolAttribute ("recheck");
            r.mic = c->getIntAttribute ("mic", 0);
            r.genDb = c->hasAttribute ("genDb") ? c->getDoubleAttribute ("genDb") : NAN;
            r.splDbA = c->hasAttribute ("splDbA") ? c->getDoubleAttribute ("splDbA") : NAN;
            juce::StringArray lines;
            lines.addLines (c->getAllSubText());
            for (const auto& line : lines)
            {
                juce::StringArray v;
                v.addTokens (line, " ", {});
                v.removeEmptyStrings();
                if (v.size() < 2) continue;
                r.tf.freq.push_back (v[0].getDoubleValue());
                r.tf.db.push_back (v[1].getDoubleValue());
            }
            if (r.spk >= 0) readings.push_back (std::move (r));
        }
    }
};

// The quick re-check of a system already tuned: what has drifted since it was set up.
struct Recheck
{
    struct Line { juce::String text; int level = 0; };   // 0 fine, 1 a look, 2 changed
    std::vector<Line> lines;
    juce::String when;
    bool running = false;

    int worst() const { int w = 0; for (const auto& l : lines) w = std::max (w, l.level); return w; }

    void save (juce::XmlElement& parent) const
    {
        if (when.isEmpty()) return;
        auto* e = parent.createNewChildElement ("Recheck");
        e->setAttribute ("when", when);
        for (const auto& l : lines)
        {
            auto* c = e->createNewChildElement ("Line");
            c->setAttribute ("text", l.text);
            c->setAttribute ("level", l.level);
        }
    }
    void load (const juce::XmlElement* parent)
    {
        *this = {};
        const auto* e = parent != nullptr ? parent->getChildByName ("Recheck") : nullptr;
        if (e == nullptr) return;
        when = e->getStringAttribute ("when");
        for (const auto* c : e->getChildWithTagNameIterator ("Line")) lines.push_back ({ c->getStringAttribute ("text"), juce::jlimit (0, 2, c->getIntAttribute ("level")) });
    }
};
} // namespace la
