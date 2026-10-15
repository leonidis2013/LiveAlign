// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Room tuning for one session: measurements of the whole system at the listening positions (several mics at once,
// or one mic moved between positions), their average against a target curve, the suggested EQ and the check after
// the EQ is in. The EQ itself is kept on the speaker (it belongs to the venue). Message thread only.
#include <array>
#include <vector>
#include <juce_core/juce_core.h>
#include "Analysis/EqFit.h"
#include "Analysis/RoomAcoustics.h"
#include "Analysis/FreqTable.h"
#include "Analysis/Banding.h"
#include "Analysis/TargetCurves.h"

namespace la
{
struct RoomMeasurement
{
    juce::String position, micName, calUsed, time;
    int mic = 0;
    bool after = false;      // taken after the EQ went in
    bool include = true;     // part of the average
    FreqTable tf;            // dB, phase, coherence (extra)
    acoustics::Result ac;    // room acoustics from this position's impulse response (when it was captured)

    double levelDb() const   // broad level, 500 Hz - 4 kHz
    {
        double s = 0.0;
        int n = 0;
        for (size_t i = 0; i < tf.freq.size(); ++i)
            if (tf.freq[i] >= 500.0 && tf.freq[i] <= 4000.0 && std::isfinite (tf.db[i])) { s += tf.db[i]; ++n; }
        return n > 0 ? s / n : NAN;
    }
};

// Room acoustics from a sweep at one position: a long impulse response, worked out after the sweep.
struct SweepMeasurement
{
    juce::String position, micName, time;
    acoustics::Result ac;
};

struct RoomTuning
{
    std::vector<RoomMeasurement> meas;
    std::vector<SweepMeasurement> sweeps;   // room acoustics measured with a sweep (preferred over the Room EQ estimate)
    int target = 4;                 // targets:: index (Worship / live music)
    // Which speakers the EQ goes on: the whole system (default), the mains, or one speaker (its index).
    static constexpr int kWholeSystem = -1, kMains = -2;
    int eqGroup = kWholeSystem;
    int checkRound = 0;             // how many times an EQ has been entered: the "after" measurements are the latest check
    int maxBands = 8;
    double maxBoostDb = 3.0, maxCutDb = 12.0;
    double fLo = 0.0, fHi = 0.0;    // 0 = from the speaker's crossover filters
    std::vector<eq::Band> suggested;
    std::vector<eq::Band> baseEq;   // the EQ in the processor: what was there as found, then each refinement you entered
    double offsetDb = 0.0, rmsBefore = 0.0, rmsAfter = 0.0;
    juce::StringArray skipped;      // positions left out of the measuring plan
    juce::StringArray acSkipped;    // positions left out of the room acoustics sweeps
    int use = 1;                    // what the room is for (room acoustics guide): see useNames()
    bool tuned = false;             // the user marked the room as tuned
    juce::String tunedOn;

    // The EQ for the speaker: what was there when measured, plus the suggestion.
    std::vector<eq::Band> combinedEq() const
    {
        auto v = baseEq;
        v.insert (v.end(), suggested.begin(), suggested.end());
        std::sort (v.begin(), v.end(), [] (const eq::Band& a, const eq::Band& b) { return a.freq < b.freq; });
        return v;
    }

    // The speakers the EQ goes on (one that follows another, as Main R set Same as Main L, gets it through that one).
    template <class System>
    std::vector<int> eqMembers (const System& sys) const
    {
        std::vector<int> out;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            if (sys.master (i) != i) continue;
            if (eqGroup == kWholeSystem || (eqGroup == kMains && sys.isMain (i)) || eqGroup == i) out.push_back (i);
        }
        if (out.empty())   // no mains (or the speaker went): the whole system
            for (int i = 0; i < (int) sys.sources.size(); ++i) if (sys.master (i) == i) out.push_back (i);
        return out;
    }

    template <class System>
    juce::String eqGroupName (const System& sys) const
    {
        if (eqGroup == kWholeSystem) return "the whole system";
        if (eqGroup == kMains) return "the mains";
        return eqGroup >= 0 && eqGroup < (int) sys.sources.size() ? sys.sources[(size_t) eqGroup].name : juce::String ("the whole system");
    }

    // What the system can play, from the speaker responses measured earlier (with their processing), or from the
    // crossover filters when there are none: the group's speakers added together, its -6 dB points, and its roll-off
    // beyond them. The target follows that roll-off (a system with no sub is not asked for bass it cannot make), and
    // the EQ stays inside it. Call before using the target or the range (after the speakers or the group change).
    template <class System>
    void updateSystem (const System& sys)
    {
        const auto g = grid();
        sysShape.assign (g.size(), 0.0);
        std::vector<double> power (g.size(), 0.0);
        bool any = false, measured = false;
        // the group, with what follows it ("Same as") and, for the whole system, every speaker
        std::vector<int> who;
        for (int i = 0; i < (int) sys.sources.size(); ++i)
        {
            const int m = sys.master (i);
            for (int k : eqMembers (sys)) if (k == m) { who.push_back (i); break; }
        }
        for (int i : who)
        {
            const auto& s = sys.sources[(size_t) i];
            const int src = s.hasResponse() ? i : sys.master (i);
            const bool has = src >= 0 && src < (int) sys.sources.size() && sys.sources[(size_t) src].hasResponse();
            // the level each plays at: its close-up capture's own level is not the level in the room, so each one is
            // normalised to its own passband and only its shape (its roll-offs) is used
            std::vector<double> db (g.size(), NAN);
            for (size_t k = 0; k < g.size(); ++k)
            {
                const double filt = 20.0 * std::log10 (std::abs (s.proc.at (g[k])) + 1e-12) - s.proc.gainDb;
                double v = filt;
                if (has)
                {
                    const auto& t = sys.sources[(size_t) src].response;
                    if (g[k] < t.freq.front() || g[k] > t.freq.back()) { v = NAN; }
                    else v += t.dbAt (g[k]);
                }
                db[k] = v;
            }
            // to its own passband (smoothed 1/3 octave, the median of its top 10 dB)
            const auto sm = eq::smooth (g, db, 1.0 / 3.0);
            double peak = -1e9;
            for (double v : sm) if (std::isfinite (v)) peak = std::max (peak, v);
            if (peak < -1e8) continue;
            std::vector<double> top;
            for (double v : sm) if (std::isfinite (v) && v >= peak - 10.0) top.push_back (v);
            const double ref = eq::median (top) + (s.role == 1 ? 0.0 : 0.0);
            for (size_t k = 0; k < g.size(); ++k)
                if (std::isfinite (sm[k])) power[k] += std::pow (10.0, (sm[k] - ref) / 10.0);
            any = true;
            measured = measured || has;
        }
        sysMeasured = measured;
        sysHasShape = any;
        sysLo = 30.0;
        sysHi = 16000.0;
        sysNote.clear();
        if (! any) return;
        std::vector<double> S (g.size(), -120.0);
        for (size_t k = 0; k < g.size(); ++k) if (power[k] > 0.0) S[k] = 10.0 * std::log10 (power[k]);
        // 0 dB = its level through the mids
        std::vector<double> mids;
        for (size_t k = 0; k < g.size(); ++k) if (g[k] >= 300.0 && g[k] <= 3000.0) mids.push_back (S[k]);
        const double m0 = eq::median (mids);
        for (auto& v : S) v -= m0;
        // the -6 dB points, out from 1 kHz
        size_t k1 = 0;
        while (k1 + 1 < g.size() && g[k1] < 1000.0) ++k1;
        size_t lo = k1, hi = k1;
        while (lo > 0 && S[lo - 1] >= -6.0) --lo;
        while (hi + 1 < g.size() && S[hi + 1] >= -6.0) ++hi;
        sysLo = std::max (25.0, g[lo]);
        sysHi = std::min (18000.0, g[hi]);
        for (size_t k = 0; k < g.size(); ++k)
        {
            if (g[k] < sysLo) sysShape[k] = std::min (0.0, S[k] - S[lo]);
            else if (g[k] > sysHi) sysShape[k] = std::min (0.0, S[k] - S[hi]);
        }
        if (! sys.hasSub() && sysLo > 45.0)
        {
            juce::String hp;
            for (int i : who) if (sys.sources[(size_t) i].proc.hp.active()) { hp = " (" + juce::String (juce::roundToInt (sys.sources[(size_t) i].proc.hp.freq)) + " Hz high-pass)"; break; }
            sysNote = "No sub: below " + juce::String (juce::roundToInt (sysLo)) + " Hz the target follows the tops' own low end" + hp + ".";
        }
        else if (sysLo > 45.0)
            sysNote = "Below " + juce::String (juce::roundToInt (sysLo)) + " Hz the target follows " + (eqGroup == kMains ? juce::String ("the mains'") : juce::String ("the system's")) + " own roll-off.";
    }

    // EQ range: where the system plays (its -6 dB points), unless set by hand. Without any speaker data: from the
    // group's crossover filters (or 30 Hz to 16 kHz).
    template <class System>
    void eqRange (const System& sys, double& lo, double& hi) const
    {
        if (sysHasShape)
        {
            lo = sysLo;
            hi = sysHi;
        }
        else
        {
            lo = 1e9;
            hi = 0.0;
            for (int i : eqMembers (sys))
            {
                const auto& pr = sys.sources[(size_t) i].proc;
                lo = std::min (lo, pr.hp.active() ? pr.hp.freq * 1.2 : 30.0);
                hi = std::max (hi, pr.lp.active() ? pr.lp.freq / 1.2 : 16000.0);
            }
            if (lo > 1e8) lo = 30.0;
            if (hi <= 0.0) hi = 16000.0;
        }
        if (fLo > 0.0) lo = fLo;
        if (fHi > 0.0) hi = fHi;
    }

    // From updateSystem(): the system's roll-off below and above its range (0 inside), on grid().
    std::vector<double> sysShape;
    double sysLo = 30.0, sysHi = 16000.0;
    bool sysHasShape = false, sysMeasured = false;
    juce::String sysNote;

    // What the room is used for, with typical mid-frequency reverberation times and the speech intelligibility to aim for.
    static juce::StringArray useNames() { return { "Speech, lectures", "Contemporary worship, amplified music", "Traditional worship, choir", "Concerts, classical music" }; }
    static void useTargets (int u, double& rtLo, double& rtHi, double& stiMin)
    {
        static const double lo[] = { 0.6, 0.8, 1.3, 1.6 }, hi[] = { 1.0, 1.3, 2.2, 2.2 }, sti[] = { 0.6, 0.5, 0.45, 0.4 };
        u = juce::jlimit (0, 3, u);
        rtLo = lo[u]; rtHi = hi[u]; stiMin = sti[u];
    }

    // The decision tree's answer, from the sweeps (or the estimate from the Room EQ measurements): 0 not measured,
    // 1 room OK for EQ, 2 EQ will help only partly, 3 treat the room before EQ. `why` says it in a sentence.
    int acousticsVerdict (juce::String& why) const
    {
        if (acousticsCount() == 0) { why = "Not measured yet."; return 0; }
        const auto a = acousticsAverage();
        double rtLo, rtHi, stiMin;
        useTargets (use, rtLo, rtHi, stiMin);
        const double rt = a.rtMid(), sti = a.sti;
        const double low = 0.5 * ((std::isfinite (a.band[0].rt()) ? a.band[0].rt() : rt) + (std::isfinite (a.band[1].rt()) ? a.band[1].rt() : rt));
        const double bassRatio = std::isfinite (rt) && rt > 0.05 ? low / rt : 1.0;
        if (std::isfinite (rt) && (rt > rtHi * 1.5 || (std::isfinite (sti) && sti < stiMin - 0.1)))
        {
            why = "Treat before EQ: the room rings longer than EQ can fix (" + juce::String (rt, 2) + " s against " + juce::String (rtLo, 1) + " - "
                  + juce::String (rtHi, 1) + " s for this use). EQ can't shorten reverberation.";
            return 3;
        }
        if ((std::isfinite (rt) && rt > rtHi * 1.15) || bassRatio > 1.3)
        {
            why = bassRatio > 1.3 ? "EQ will help only partly: the low end rings " + juce::String (bassRatio, 1) + " times longer than the mids (bass build-up). "
                                    "EQ can tame the level, not the decay."
                                  : "EQ will help only partly: the room is livelier than usual for this use (" + juce::String (rt, 2) + " s).";
            return 2;
        }
        why = "Room OK for EQ" + (std::isfinite (rt) ? " (" + juce::String (rt, 2) + " s, " + (std::isfinite (sti) ? "STI " + juce::String (sti, 2) : juce::String ("speech not rated")) + ")." : juce::String ("."));
        return 1;
    }

    static std::vector<double> grid()
    {
        std::vector<double> g;
        for (double f = 20.0; f <= 20000.0; f *= std::pow (2.0, 1.0 / 24.0)) g.push_back (f);
        return g;
    }

    int count (bool after) const
    {
        int n = 0;
        for (const auto& m : meas) n += m.after == after ? 1 : 0;
        return n;
    }

    bool hasAt (const juce::String& position, bool after) const
    {
        for (const auto& m : meas) if (m.after == after && m.position == position) return true;
        return false;
    }

    // Power average of the included measurements (each normalised to its own 500 Hz - 4 kHz level, so a far
    // position counts for its tonal balance, not its loudness), on `grid()`. NaN where nothing was measured.
    std::vector<double> average (bool after, bool normalise = true) const
    {
        const auto g = grid();
        std::vector<double> sum (g.size(), 0.0);
        std::vector<int> n (g.size(), 0);
        for (const auto& m : meas)
        {
            if (m.after != after || ! m.include || m.tf.freq.size() < 4) continue;
            const double lvl = normalise ? m.levelDb() : 0.0;
            for (size_t i = 0; i < g.size(); ++i)
            {
                if (g[i] < m.tf.freq.front() || g[i] > m.tf.freq.back()) continue;
                const double v = m.tf.dbAt (g[i]) - (std::isfinite (lvl) ? lvl : 0.0);
                if (! std::isfinite (v)) continue;
                sum[i] += std::pow (10.0, v / 10.0);
                ++n[i];
            }
        }
        std::vector<double> out (g.size(), NAN);
        for (size_t i = 0; i < g.size(); ++i) if (n[i] > 0) out[i] = 10.0 * std::log10 (sum[i] / n[i]);
        return out;
    }

    // Room acoustics averaged over the positions that have them (the latest round of each position).
    // The sweeps when there are any (their impulse responses are long enough for any room), else the estimate from
    // the Room EQ measurements.
    acoustics::Result acousticsAverage() const
    {
        std::vector<const acoustics::Result*> from;
        for (const auto& w : sweeps) if (w.ac.valid) from.push_back (&w.ac);
        if (from.empty()) for (const auto& m : meas) if (m.ac.valid && m.include) from.push_back (&m.ac);
        acoustics::Result r;
        std::array<std::array<double, 9>, acoustics::kBands> sum {};
        std::array<std::array<int, 9>, acoustics::kBands> n {};
        double sti = 0.0;
        int nsti = 0;
        for (const auto* acp : from)
        {
            struct { const acoustics::Result& ac; } m { *acp };
            r.valid = true;
            for (int b = 0; b < acoustics::kBands; ++b)
            {
                const auto& o = m.ac.band[(size_t) b];
                const double v[9] = { o.edt, o.t20, o.t30, o.c50, o.c80, o.d50, o.mti, o.rangeDb, o.t15 };
                for (int k = 0; k < 9; ++k) if (std::isfinite (v[k])) { sum[(size_t) b][(size_t) k] += v[k]; ++n[(size_t) b][(size_t) k]; }
            }
            if (std::isfinite (m.ac.sti)) { sti += m.ac.sti; ++nsti; }
        }
        for (int b = 0; b < acoustics::kBands; ++b)
        {
            auto avg = [&] (int k) { return n[(size_t) b][(size_t) k] > 0 ? sum[(size_t) b][(size_t) k] / n[(size_t) b][(size_t) k] : NAN; };
            auto& o = r.band[(size_t) b];
            o.edt = avg (0); o.t20 = avg (1); o.t30 = avg (2); o.c50 = avg (3); o.c80 = avg (4); o.d50 = avg (5); o.mti = avg (6); o.rangeDb = avg (7); o.t15 = avg (8);
        }
        r.sti = nsti > 0 ? sti / nsti : NAN;
        return r;
    }

    int acousticsCount() const
    {
        int k = 0;
        for (const auto& w : sweeps) k += w.ac.valid ? 1 : 0;
        if (k > 0) return k;
        for (const auto& m : meas) k += m.ac.valid && m.include ? 1 : 0;
        return k;
    }
    bool acousticsFromSweeps() const { for (const auto& w : sweeps) if (w.ac.valid) return true; return false; }

    static void saveAcoustics (juce::XmlElement& c, const acoustics::Result& ac)
    {
        auto* a = c.createNewChildElement ("Acoustics");
        a->setAttribute ("sti", ac.sti);
        for (int b = 0; b < acoustics::kBands; ++b)
        {
            const auto& o = ac.band[(size_t) b];
            juce::StringArray v;
            for (double x : { o.edt, o.t20, o.t30, o.c50, o.c80, o.d50, o.mti, o.rangeDb, o.t15 }) v.add (std::isfinite (x) ? juce::String (x, 4) : juce::String ("nan"));
            a->setAttribute ("b" + juce::String (b), v.joinIntoString (" "));
        }
    }
    static void loadAcoustics (const juce::XmlElement& c, acoustics::Result& ac)
    {
        const auto* a = c.getChildByName ("Acoustics");
        if (a == nullptr) return;
        ac.valid = true;
        ac.sti = a->hasAttribute ("sti") && a->getStringAttribute ("sti") != "nan" ? a->getDoubleAttribute ("sti") : NAN;
        for (int b = 0; b < acoustics::kBands; ++b)
        {
            juce::StringArray v;
            v.addTokens (a->getStringAttribute ("b" + juce::String (b)), " ", {});
            auto at = [&v] (int i) { return i < v.size() && v[i] != "nan" ? v[i].getDoubleValue() : NAN; };
            auto& o = ac.band[(size_t) b];
            o.edt = at (0); o.t20 = at (1); o.t30 = at (2); o.c50 = at (3); o.c80 = at (4); o.d50 = at (5); o.mti = at (6); o.rangeDb = at (7); o.t15 = at (8);
        }
    }

    // Low-frequency peaks of the average (room modes): frequency, height above the local median, and how much the
    // level at that frequency differs between the positions (standard deviation, dB).
    struct Mode { double freq, heightDb, seatSpreadDb; };
    std::vector<Mode> modes (bool after = false) const
    {
        std::vector<Mode> out;
        const auto g = grid();
        const auto avg = average (after);
        const auto sm = eq::smooth (g, avg, 1.0 / 12.0);
        for (size_t i = 2; i + 2 < g.size(); ++i)
        {
            if (g[i] < 30.0 || g[i] > 300.0 || ! std::isfinite (sm[i])) continue;
            if (! (sm[i] > sm[i - 1] && sm[i] >= sm[i + 1])) continue;
            std::vector<double> nb;
            for (size_t k = 0; k < g.size(); ++k)
                if (std::abs (std::log2 (g[k] / g[i])) < 0.5 && std::isfinite (sm[k])) nb.push_back (sm[k]);
            const double h = sm[i] - eq::median (nb);
            if (h < 4.0) continue;
            // the spread across seats at this frequency
            double s = 0.0, s2 = 0.0;
            int n = 0;
            for (const auto& m : meas)
            {
                if (m.after != after || ! m.include || m.tf.freq.size() < 4) continue;
                const double v = m.tf.dbAt (g[i]) - m.levelDb();
                if (! std::isfinite (v)) continue;
                s += v; s2 += v * v; ++n;
            }
            const double sd = n > 1 ? std::sqrt (std::max (0.0, s2 / n - (s / n) * (s / n))) : NAN;
            out.push_back ({ g[i], h, sd });
        }
        std::sort (out.begin(), out.end(), [] (const Mode& a, const Mode& b) { return a.heightDb > b.heightDb; });
        if (out.size() > 6) out.resize (6);
        std::sort (out.begin(), out.end(), [] (const Mode& a, const Mode& b) { return a.freq < b.freq; });
        return out;
    }

    // Coverage: how much the level differs between positions (500 Hz - 4 kHz), max minus min, in dB.
    double levelSpread (bool after) const
    {
        double lo = 1e9, hi = -1e9;
        for (const auto& m : meas)
        {
            if (m.after != after || ! m.include) continue;
            const double l = m.levelDb();
            if (! std::isfinite (l)) continue;
            lo = std::min (lo, l);
            hi = std::max (hi, l);
        }
        return hi >= lo ? hi - lo : NAN;
    }

    // The target: its tonal shape where the system plays, and the system's own roll-off beyond (see updateSystem).
    std::vector<double> targetCurve() const
    {
        const auto g = grid();
        std::vector<double> t (g.size());
        const bool shaped = sysHasShape && sysShape.size() == g.size();
        for (size_t i = 0; i < g.size(); ++i)
        {
            const double f = shaped ? juce::jlimit (sysLo, sysHi, g[i]) : g[i];
            t[i] = targets::db (target, f) + (shaped ? sysShape[i] : 0.0);
        }
        return t;
    }

    // Deviation of an average from the target in the EQ range: RMS and max, after matching the level.
    void deviation (const std::vector<double>& avg, double lo, double hi, double& rms, double& mx) const
    {
        const auto g = grid();
        const auto t = targetCurve();
        const auto sm = eq::smooth (g, avg, 1.0 / 6.0);
        std::vector<double> d;
        for (size_t i = 0; i < g.size(); ++i) if (g[i] >= lo && g[i] <= hi && std::isfinite (sm[i])) d.push_back (sm[i] - t[i]);
        const double off = eq::median (d);
        double s = 0.0;
        mx = 0.0;
        for (double v : d) { s += (v - off) * (v - off); mx = std::max (mx, std::abs (v - off)); }
        rms = d.empty() ? NAN : std::sqrt (s / (double) d.size());
        if (d.empty()) mx = NAN;
    }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("RoomTuning");
        e->setAttribute ("target", target);
        e->setAttribute ("eqGroup", eqGroup);
        e->setAttribute ("checkRound", checkRound);
        e->setAttribute ("maxBands", maxBands);
        e->setAttribute ("maxBoostDb", maxBoostDb);
        e->setAttribute ("maxCutDb", maxCutDb);
        e->setAttribute ("fLo", fLo);
        e->setAttribute ("fHi", fHi);
        e->setAttribute ("tuned", tuned);
        e->setAttribute ("tunedOn", tunedOn);
        e->setAttribute ("skipped", skipped.joinIntoString ("|"));
        e->setAttribute ("acSkipped", acSkipped.joinIntoString ("|"));
        e->setAttribute ("use", use);
        for (const auto& b : suggested)
        {
            auto* c = e->createNewChildElement ("Suggested");
            c->setAttribute ("freq", b.freq);
            c->setAttribute ("gain", b.gainDb);
            c->setAttribute ("q", b.q);
        }
        for (const auto& b : baseEq)
        {
            auto* c = e->createNewChildElement ("BaseEq");
            c->setAttribute ("freq", b.freq);
            c->setAttribute ("gain", b.gainDb);
            c->setAttribute ("q", b.q);
        }
        for (const auto& w : sweeps)
        {
            auto* c = e->createNewChildElement ("Sweep");
            c->setAttribute ("position", w.position);
            c->setAttribute ("micName", w.micName);
            c->setAttribute ("time", w.time);
            saveAcoustics (*c, w.ac);
        }
        for (const auto& m : meas)
        {
            auto* c = e->createNewChildElement ("Measurement");
            c->setAttribute ("position", m.position);
            c->setAttribute ("micName", m.micName);
            c->setAttribute ("mic", m.mic);
            c->setAttribute ("calUsed", m.calUsed);
            c->setAttribute ("time", m.time);
            c->setAttribute ("after", m.after);
            c->setAttribute ("include", m.include);
            if (m.ac.valid) saveAcoustics (*c, m.ac);
            juce::String text;
            for (size_t i = 0; i < m.tf.freq.size(); ++i)
            {
                text << juce::String (m.tf.freq[i], 2) << ' ' << juce::String (m.tf.db[i], 2);
                if (m.tf.hasPhase()) text << ' ' << juce::String (m.tf.phase[i], 1);
                if (m.tf.extra.size() == m.tf.freq.size()) text << ' ' << juce::String (m.tf.extra[i], 3);
                text << '\n';
            }
            c->addTextElement (text);
        }
    }

    void load (const juce::XmlElement* parent)
    {
        *this = {};
        const auto* e = parent != nullptr ? parent->getChildByName ("RoomTuning") : nullptr;
        if (e == nullptr) return;
        target = juce::jlimit (0, targets::count() - 1, e->getIntAttribute ("target", 4));
        eqGroup = e->getIntAttribute ("eqGroup", kWholeSystem);
        checkRound = juce::jmax (0, e->getIntAttribute ("checkRound"));
        maxBands = juce::jlimit (1, 31, e->getIntAttribute ("maxBands", 8));
        maxBoostDb = juce::jlimit (0.0, 12.0, e->getDoubleAttribute ("maxBoostDb", 3.0));
        maxCutDb = juce::jlimit (1.0, 24.0, e->getDoubleAttribute ("maxCutDb", 12.0));
        fLo = e->getDoubleAttribute ("fLo");
        fHi = e->getDoubleAttribute ("fHi");
        tuned = e->getBoolAttribute ("tuned");
        tunedOn = e->getStringAttribute ("tunedOn");
        skipped.addTokens (e->getStringAttribute ("skipped"), "|", {});
        skipped.removeEmptyStrings();
        acSkipped.addTokens (e->getStringAttribute ("acSkipped"), "|", {});
        acSkipped.removeEmptyStrings();
        use = juce::jlimit (0, 3, e->getIntAttribute ("use", 1));
        for (const auto* c : e->getChildWithTagNameIterator ("Suggested"))
            suggested.push_back ({ c->getDoubleAttribute ("freq"), c->getDoubleAttribute ("gain"), c->getDoubleAttribute ("q", 1.0) });
        for (const auto* c : e->getChildWithTagNameIterator ("BaseEq"))
            baseEq.push_back ({ c->getDoubleAttribute ("freq"), c->getDoubleAttribute ("gain"), c->getDoubleAttribute ("q", 1.0) });
        for (const auto* c : e->getChildWithTagNameIterator ("Sweep"))
        {
            SweepMeasurement w;
            w.position = c->getStringAttribute ("position");
            w.micName = c->getStringAttribute ("micName");
            w.time = c->getStringAttribute ("time");
            loadAcoustics (*c, w.ac);
            if (w.ac.valid) sweeps.push_back (std::move (w));
        }
        for (const auto* c : e->getChildWithTagNameIterator ("Measurement"))
        {
            RoomMeasurement m;
            m.position = c->getStringAttribute ("position");
            m.micName = c->getStringAttribute ("micName");
            m.mic = c->getIntAttribute ("mic");
            m.calUsed = c->getStringAttribute ("calUsed");
            m.time = c->getStringAttribute ("time");
            m.after = c->getBoolAttribute ("after");
            m.include = c->getBoolAttribute ("include", true);
            loadAcoustics (*c, m.ac);
            juce::StringArray lines;
            lines.addLines (c->getAllSubText());
            for (const auto& line : lines)
            {
                juce::StringArray v;
                v.addTokens (line, " ", {});
                v.removeEmptyStrings();
                if (v.size() < 2) continue;
                m.tf.freq.push_back (v[0].getDoubleValue());
                m.tf.db.push_back (v[1].getDoubleValue());
                if (v.size() >= 3) m.tf.phase.push_back (v[2].getDoubleValue());
                if (v.size() >= 4) m.tf.extra.push_back (v[3].getDoubleValue());
            }
            if (m.tf.phase.size() != m.tf.freq.size()) m.tf.phase.clear();
            if (m.tf.extra.size() != m.tf.freq.size()) m.tf.extra.clear();
            meas.push_back (std::move (m));
        }
        if (! e->hasAttribute ("checkRound") && count (true) > 0) checkRound = 1;   // venues from before checks were counted
    }
};
} // namespace la
