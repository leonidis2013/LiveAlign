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
#include "Prefs.h"

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
    std::vector<float> fdw;  // on grid(): a frequency-dependent window's response minus the steady state (dB), from the impulse response

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

// STIPA measured at one position (IEC 60268-16, the direct method): the STI and each octave's transmission index.
struct StipaMeasurement
{
    juce::String position, micName, time;
    double sti = NAN;
    std::array<double, 7> mti {};
    bool corrected = false;     // masking and hearing threshold corrections (a calibrated mic)
    bool external = false;      // an external STIPA source (a talkbox), not LiveAlign's generator
};

// The difference a frequency-dependent window makes (dB on `grid`): the response from the first 15 cycles of each
// frequency (at least 5 ms) against the steady state from the whole impulse response. Where the room rings, the
// windowed response is the speaker and the early reflections; the steady state adds the reverberation. Normalised to
// 0 dB through 200 Hz - 8 kHz, smoothed 1/3 octave. `ir` is circular (the transfer function's impulse response).
inline std::vector<float> windowedDelta (const std::vector<float>& ir, double fs, const std::vector<double>& grid)
{
    std::vector<float> out (grid.size(), 0.0f);
    const int n = (int) ir.size();
    if (n < 1024 || fs <= 0.0) return out;
    int peak = 0;
    for (int i = 1; i < n; ++i) if (std::abs (ir[(size_t) i]) > std::abs (ir[(size_t) peak])) peak = i;
    const int start = (peak - (int) (0.001 * fs) + n) % n;
    std::vector<double> d (grid.size(), NAN);
    for (size_t k = 0; k < grid.size(); ++k)
    {
        const double f = grid[k];
        if (f < 60.0 || f > fs * 0.45) continue;
        const int W = std::min (n, (int) (std::clamp (15.0 / f, 0.005, 0.3) * fs));
        const int taper = std::max (1, W * 3 / 10);
        const double w = 2.0 * 3.14159265358979 * f / fs;
        std::complex<double> full (0.0, 0.0), win (0.0, 0.0);
        const std::complex<double> step = std::polar (1.0, -w);
        std::complex<double> ph (1.0, 0.0);
        for (int i = 0; i < n; ++i)
        {
            const double v = ir[(size_t) ((start + i) % n)];
            const auto x = v * ph;
            full += x;
            if (i < W)
            {
                const double g = i < W - taper ? 1.0 : 0.5 + 0.5 * std::cos (3.14159265358979 * (double) (i - (W - taper)) / (double) taper);
                win += x * g;
            }
            ph *= step;
            if ((i & 1023) == 1023) ph /= std::abs (ph);
        }
        if (std::abs (full) > 1e-20 && std::abs (win) > 1e-20) d[k] = 20.0 * std::log10 (std::abs (win) / std::abs (full));
    }
    const auto sm = eq::smooth (grid, d, 1.0 / 3.0);
    std::vector<double> mid;
    for (size_t k = 0; k < grid.size(); ++k) if (grid[k] >= 200.0 && grid[k] <= 8000.0 && std::isfinite (sm[k])) mid.push_back (sm[k]);
    const double m0 = mid.empty() ? 0.0 : eq::median (mid);
    for (size_t k = 0; k < grid.size(); ++k) out[k] = std::isfinite (sm[k]) ? (float) (sm[k] - m0) : 0.0f;
    return out;
}

// What Room EQ does with the room acoustics (Settings: "Allow for the room acoustics", on by default). Worked out
// by RoomTuning::updateAllowance from the acoustics and the measurements; the graph and the suggestion use it.
struct RoomAllowance
{
    bool wanted = true;          // the switch
    bool on = false;             // wanted, and the acoustics are measured
    double schroederHz = NAN;    // below it: the average of the seats, cuts only on modes most seats share, no boosts
    bool schroederAssumed = false;
    struct ModeCut { double freq, heightDb, q, gainDb; int seats, of; };
    std::vector<ModeCut> modeCuts;          // room modes to cut, with a width that matches how long they ring
    struct Null { double freq, depthDb; };
    std::vector<Null> nulls;                // dips not to boost: move the seat or the sub, not the EQ
    std::vector<double> targetEase;         // dB on grid(): the target's low end eased where the room rings long
    double easeDb = 0.0;
    std::vector<std::pair<double, double>> windowed;   // octaves corrected from the direct sound (long reverb)
    bool windowMissing = false;                        // wanted, but the measurements have no windowed response
    std::vector<std::pair<double, double>> noisy;      // left alone: the noise is too close
    bool noHighBoost = false;    // clarity is poor: no boosts above 2 kHz
    bool capCuts = false;        // the room needs treatment: cuts kept shallow
    double cutCap = 6.0;
    juce::StringArray notes;     // what changed, in words
    // The changes in a few words, for the Room EQ card (the notes are in the switch's tooltip and the report).
    juce::String summary() const
    {
        juce::StringArray t;
        t.add ((schroederAssumed ? "modes below ~" : "modes below ") + juce::String (juce::roundToInt (schroederHz)) + " Hz");
        if (! modeCuts.empty()) t.add (juce::String ((int) modeCuts.size()) + (modeCuts.size() == 1 ? " mode cut" : " mode cuts"));
        if (! nulls.empty()) t.add (juce::String ((int) nulls.size()) + (nulls.size() == 1 ? " null not boosted" : " nulls not boosted"));
        if (! windowed.empty()) t.add ("direct sound from " + juce::String (juce::roundToInt (windowed.front().first)) + " Hz");
        if (easeDb < -0.5) t.add ("low end eased " + juce::String (-easeDb, 1) + " dB");
        if (! noisy.empty()) t.add (juce::String ((int) noisy.size()) + (noisy.size() == 1 ? " noisy band skipped" : " noisy bands skipped"));
        if (noHighBoost) t.add ("no boosts above 2 kHz");
        if (capCuts) t.add ("cuts kept to " + juce::String (juce::roundToInt (cutCap)) + " dB");
        return t.joinIntoString (", ") + ".";
    }
};

struct RoomTuning
{
    std::vector<RoomMeasurement> meas;
    std::vector<SweepMeasurement> sweeps;   // room acoustics measured with a sweep (preferred over the Room EQ estimate)
    std::vector<StipaMeasurement> stipa;    // STIPA, measured directly (one per position, the latest)
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

    // ---- allowing for the room acoustics (Settings / Room EQ: "Allow for the room acoustics") ----
    // `wanted`: the switch. `schroederHz`: from the room's size (NaN: not given, 200 Hz is assumed). `sti`: the speech
    // intelligibility to judge clarity by (with the noise when known). `verdict`: acousticsVerdict(). Call before the
    // target, the graph or a suggestion (after updateSystem).
    void updateAllowance (bool wanted, double schroederHz, double sti, double stiMin, int verdict, double maxCutDb, double cutQ)
    {
        allow = {};
        allow.wanted = wanted;
        const auto g = grid();
        allow.targetEase.assign (g.size(), 0.0);
        if (! wanted || acousticsCount() == 0) return;
        allow.on = true;
        const auto a = acousticsAverage();
        const double rtMid = a.rtMid();
        double rtLo, rtHi, stiMinUse;
        useTargets (use, rtLo, rtHi, stiMinUse);
        allow.schroederAssumed = ! std::isfinite (schroederHz);
        allow.schroederHz = std::clamp (std::isfinite (schroederHz) ? schroederHz : 200.0, 60.0, 400.0);
        const double fS = allow.schroederHz;
        allow.notes.add ("Below " + juce::String (juce::roundToInt (fS)) + " Hz" + (allow.schroederAssumed ? " (assumed: set the Room size on Room acoustics)" : " (the Schroeder frequency)")
                         + ": the median of the seats, cuts only on modes most seats share, no boosts.");

        // the reverberation time at any frequency, from the octaves (63 Hz too when it is clear)
        std::vector<std::pair<double, double>> rtPts;
        if (std::isfinite (a.b63.rt()) && std::isfinite (a.b63.rangeDb) && a.b63.rangeDb >= 30.0) rtPts.push_back ({ 63.0, a.b63.rt() });
        for (int b = 0; b < acoustics::kBands; ++b) if (std::isfinite (a.band[(size_t) b].rt())) rtPts.push_back ({ acoustics::octaves()[(size_t) b], a.band[(size_t) b].rt() });
        auto rtAt = [&rtPts] (double f) -> double
        {
            if (rtPts.empty()) return NAN;
            if (f <= rtPts.front().first) return rtPts.front().second;
            for (size_t i = 1; i < rtPts.size(); ++i)
                if (f <= rtPts[i].first)
                {
                    const double t = std::log (f / rtPts[i - 1].first) / std::log (rtPts[i].first / rtPts[i - 1].first);
                    return rtPts[i - 1].second + t * (rtPts[i].second - rtPts[i - 1].second);
                }
            return rtPts.back().second;
        };

        // 1, 2: room modes most seats share, cut with a width that matches how long they ring
        const bool after = count (true) > 0 && checkRound > 0;
        for (const auto& m : modes (after))
        {
            if (m.freq > fS) continue;
            int seats = 0, of = 0;
            for (const auto& x : meas)
            {
                if (x.after != after || ! x.include || x.tf.freq.size() < 4) continue;
                ++of;
                std::vector<double> nb;
                for (double f = m.freq / std::sqrt (2.0); f <= m.freq * std::sqrt (2.0); f *= 1.03) nb.push_back (x.tf.dbAt (f));
                const double here = x.tf.dbAt (m.freq);
                if (std::isfinite (here) && here - eq::median (nb) >= 3.0) ++seats;
            }
            if (of > 1 && seats * 10 < of * 6) continue;   // fewer than 6 seats in 10: a seat's peak, not the room's
            const double T = std::isfinite (rtAt (m.freq)) ? rtAt (m.freq) : rtMid;
            const double q = std::clamp (m.freq * (std::isfinite (T) ? T : 0.5) / 2.2, 0.7, cutQ);
            RoomAllowance::ModeCut c { m.freq, m.heightDb, q, -std::min (m.heightDb, maxCutDb), seats, of };
            allow.modeCuts.push_back (c);
        }
        if (! allow.modeCuts.empty())
        {
            juce::StringArray f;
            for (const auto& c : allow.modeCuts) f.add (juce::String (juce::roundToInt (c.freq)) + " Hz (Q " + juce::String (c.q, 1) + ")");
            allow.notes.add ("Room modes cut to how long they ring: " + f.joinIntoString (", ") + ".");
        }
        // dips not to boost
        {
            const auto avg = eq::smooth (g, average (after), 1.0 / 12.0);
            for (size_t i = 2; i + 2 < g.size(); ++i)
            {
                if (g[i] < 30.0 || g[i] > std::max (fS, 300.0) || ! std::isfinite (avg[i])) continue;
                if (! (avg[i] < avg[i - 1] && avg[i] <= avg[i + 1])) continue;
                std::vector<double> nb;
                for (size_t k = 0; k < g.size(); ++k) if (std::abs (std::log2 (g[k] / g[i])) < 0.5 && std::isfinite (avg[k])) nb.push_back (avg[k]);
                const double depth = eq::median (nb) - avg[i];
                if (depth >= 6.0) allow.nulls.push_back ({ g[i], depth });
            }
            if (allow.nulls.size() > 4) allow.nulls.resize (4);
        }

        // 3: long reverberation: from the direct sound (a frequency-dependent window), above the Schroeder frequency
        {
            bool any = false;
            for (const auto& x : meas) any = any || (x.after == after && x.include && x.fdw.size() == g.size());
            for (int b = 0; b < acoustics::kBands; ++b)
            {
                const double fc = acoustics::octaves()[(size_t) b], rt = a.band[(size_t) b].rt();
                if (fc / std::sqrt (2.0) < fS || ! std::isfinite (rt)) continue;
                if (rt <= std::max (rtHi * 1.15, 1.2)) continue;
                if (! any) { allow.windowMissing = true; continue; }
                allow.windowed.push_back ({ fc / std::sqrt (2.0), fc * std::sqrt (2.0) });
            }
            if (! allow.windowed.empty())
                allow.notes.add ("Long reverberation from " + juce::String (juce::roundToInt (allow.windowed.front().first)) + " Hz up: corrected from the direct sound (a shorter window), not the reverb.");
            else if (allow.windowMissing)
                allow.notes.add ("The reverb is long, but these measurements have no impulse response kept: measure again to correct from the direct sound.");
        }

        // 4: the target's low end eased where the room rings long
        if (std::isfinite (rtMid) && rtMid > 0.05)
        {
            for (size_t i = 0; i < g.size(); ++i)
            {
                if (g[i] > 400.0) continue;
                const double r = rtAt (g[i]) / rtMid;
                if (! std::isfinite (r) || r <= 1.15) continue;
                const double fade = g[i] < 250.0 ? 1.0 : (400.0 - g[i]) / 150.0;   // eased in from 400 Hz down to 250 Hz
                allow.targetEase[i] = -std::min (4.0, 6.0 * std::log2 (r)) * fade;
            }
            allow.targetEase = eq::smooth (g, allow.targetEase, 1.0 / 3.0);
            for (auto& v : allow.targetEase) if (! std::isfinite (v)) v = 0.0;
            for (double v : allow.targetEase) allow.easeDb = std::min (allow.easeDb, v);
            if (allow.easeDb < -0.5)
                allow.notes.add ("The low end rings long: the target is eased down up to " + juce::String (-allow.easeDb, 1) + " dB there.");
        }

        // 5: the noise too close: the coherence of the measurements, and the sweeps' decay range
        {
            std::vector<double> coh (g.size(), 0.0);
            std::vector<int> n (g.size(), 0);
            for (const auto& x : meas)
            {
                if (x.after != after || ! x.include || x.tf.extra.size() != x.tf.freq.size()) continue;
                for (size_t i = 0; i < g.size(); ++i)
                {
                    if (g[i] < x.tf.freq.front() || g[i] > x.tf.freq.back()) continue;
                    // the nearest point's coherence
                    const auto it = std::lower_bound (x.tf.freq.begin(), x.tf.freq.end(), g[i]);
                    const size_t j = (size_t) std::min ((int) x.tf.freq.size() - 1, (int) (it - x.tf.freq.begin()));
                    coh[i] += x.tf.extra[j];
                    ++n[i];
                }
            }
            auto addRange = [this] (double lo, double hi)
            {
                if (! allow.noisy.empty() && lo <= allow.noisy.back().second * 1.01) allow.noisy.back().second = std::max (allow.noisy.back().second, hi);
                else allow.noisy.push_back ({ lo, hi });
            };
            // the coherence averaged over a third of an octave (the measurements keep only their reliable points, so a
            // low average means gaps and weak points across the seats), and the sweeps' decay range per octave
            std::vector<double> c (g.size(), NAN);
            for (size_t i = 0; i < g.size(); ++i) if (n[i] > 0) c[i] = coh[i] / n[i];
            const auto cs = eq::smooth (g, c, 1.0 / 3.0);
            for (size_t i = 0; i < g.size(); ++i)
            {
                bool bad = std::isfinite (cs[i]) && cs[i] < 0.45;
                for (int b = 0; b < acoustics::kBands && ! bad; ++b)
                {
                    const double fc = acoustics::octaves()[(size_t) b], rd = a.band[(size_t) b].rangeDb;
                    if (std::abs (std::log2 (g[i] / fc)) <= 0.5 && std::isfinite (rd) && rd < 25.0) bad = true;
                }
                if (bad) addRange (g[i] / std::pow (2.0, 1.0 / 48.0), g[i] * std::pow (2.0, 1.0 / 48.0));
            }
            // only stretches of a third of an octave or more count
            allow.noisy.erase (std::remove_if (allow.noisy.begin(), allow.noisy.end(), [] (const std::pair<double, double>& r) { return r.second / r.first < std::pow (2.0, 1.0 / 3.0); }),
                               allow.noisy.end());
            if (! allow.noisy.empty())
            {
                juce::StringArray t;
                for (const auto& r : allow.noisy) t.add (juce::String (juce::roundToInt (r.first)) + " - " + juce::String (juce::roundToInt (r.second)) + " Hz");
                allow.notes.add ("Left alone where the noise is too close to the measurement: " + t.joinIntoString (", ") + ".");
            }
        }

        // 6: poor clarity: no high-frequency boosts (they add to the reverb, not the words)
        const double c50 = a.mid (&acoustics::Band::c50);
        if ((std::isfinite (sti) && sti < stiMin) || (std::isfinite (c50) && c50 < -2.0))
        {
            allow.noHighBoost = true;
            allow.notes.add ("Speech clarity is poor: no boosts above 2 kHz. Aim the PA at the listeners and keep it off the walls, or raise the speech level.");
        }
        // 7: the room needs treatment: shallow cuts
        if (verdict >= 3)
        {
            allow.capCuts = true;
            allow.notes.add ("The room needs treatment: cuts kept to " + juce::String (juce::roundToInt (allow.cutCap)) + " dB; deep cuts only thin the sound there.");
        }
    }

    // The measurement to fit, with the allowance applied (the input of eq::fit), with `placed` (the mode cuts) in already.
    std::vector<double> fitInput (bool fromCheck, const std::vector<double>& target, const std::vector<eq::Band>& placed) const
    {
        const auto g = grid();
        auto m = average (fromCheck);
        if (! allow.on) return m;
        // the seats' median below the Schroeder frequency (a peak at one or two seats is not the room's)
        {
            std::vector<std::vector<double>> seats;
            for (const auto& x : meas)
            {
                if (x.after != fromCheck || ! x.include || x.tf.freq.size() < 4) continue;
                const double lvl = x.levelDb();
                std::vector<double> v (g.size(), NAN);
                for (size_t i = 0; i < g.size(); ++i)
                    if (g[i] >= x.tf.freq.front() && g[i] <= x.tf.freq.back()) v[i] = x.tf.dbAt (g[i]) - (std::isfinite (lvl) ? lvl : 0.0);
                seats.push_back (std::move (v));
            }
            if (seats.size() >= 3)
                for (size_t i = 0; i < g.size(); ++i)
                {
                    if (g[i] > allow.schroederHz) break;
                    std::vector<double> v;
                    for (const auto& sv : seats) if (std::isfinite (sv[i])) v.push_back (sv[i]);
                    if (v.size() >= 3) m[i] = eq::median (v);
                }
        }
        // long reverb: the direct sound's shape (the average windowed difference)
        if (! allow.windowed.empty())
        {
            std::vector<double> d (g.size(), 0.0);
            int n = 0;
            for (const auto& x : meas)
                if (x.after == fromCheck && x.include && x.fdw.size() == g.size()) { for (size_t i = 0; i < g.size(); ++i) d[i] += x.fdw[i]; ++n; }
            if (n > 0)
                for (size_t i = 0; i < g.size(); ++i)
                    for (const auto& r : allow.windowed)
                        if (g[i] >= r.first && g[i] <= r.second && std::isfinite (m[i])) { m[i] += d[i] / n; break; }
        }
        // the mode cuts go in first: what is left is fitted
        for (size_t i = 0; i < g.size(); ++i) if (std::isfinite (m[i])) m[i] += eq::totalDb (placed, g[i]);
        // the level the fit will match (as eq::fit does: the median in the mids)
        std::vector<double> d;
        for (size_t i = 0; i < g.size(); ++i) if (g[i] >= 200.0 && g[i] <= 4000.0 && std::isfinite (m[i])) d.push_back (m[i] - target[i]);
        const double off = d.empty() ? 0.0 : eq::median (d);
        const auto sm = eq::smooth (g, m, 1.0 / 6.0);
        for (size_t i = 0; i < g.size(); ++i)
        {
            if (! std::isfinite (m[i])) continue;
            const double t = target[i] + off;
            // no boosts below the Schroeder frequency (a dip there is a seat's null), nor above 2 kHz when clarity is poor
            if ((g[i] <= allow.schroederHz || (allow.noHighBoost && g[i] >= 2000.0)) && sm[i] < t) m[i] += t - sm[i];
            // left alone where the noise is too close
            for (const auto& r : allow.noisy) if (g[i] >= r.first && g[i] <= r.second) { m[i] += t - sm[i]; break; }
        }
        return m;
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
    RoomAllowance allow;            // from updateAllowance()
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
        if (a.echoAudible() || std::isfinite (a.flutterMs))
        {
            why = a.echoAudible() ? "EQ will help only partly: there is an echo (a strong late reflection " + juce::String (juce::roundToInt (a.echoMs)) + " ms after the "
                                    "direct sound). EQ can't remove it: treat that surface or aim the PA away from it."
                                  : "EQ will help only partly: there is a flutter echo (repeats every " + juce::String (juce::roundToInt (a.flutterMs)) + " ms between parallel "
                                    "surfaces). Absorption or diffusion on one of them is the fix.";
            return 2;
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
    acoustics::Result acousticsAverage() const { return averageOf (acousticsResults()); }

    // The results the average is made of: the sweeps when there are any, else the Room EQ estimate.
    std::vector<const acoustics::Result*> acousticsResults() const
    {
        std::vector<const acoustics::Result*> from;
        for (const auto& w : sweeps) if (w.ac.valid) from.push_back (&w.ac);
        if (from.empty()) for (const auto& m : meas) if (m.ac.valid && m.include) from.push_back (&m.ac);
        return from;
    }

    static acoustics::Result averageOf (const std::vector<const acoustics::Result*>& from)
    {
        acoustics::Result r;
        auto avgBand = [] (const std::vector<const acoustics::Band*>& bs, acoustics::Band& o)
        {
            auto mean = [&bs] (double acoustics::Band::* f)
            {
                double s = 0.0;
                int n = 0;
                for (const auto* b : bs) if (std::isfinite (b->*f)) { s += b->*f; ++n; }
                return n > 0 ? s / n : NAN;
            };
            o.edt = mean (&acoustics::Band::edt); o.t20 = mean (&acoustics::Band::t20); o.t30 = mean (&acoustics::Band::t30);
            o.c50 = mean (&acoustics::Band::c50); o.c80 = mean (&acoustics::Band::c80); o.d50 = mean (&acoustics::Band::d50);
            o.mti = mean (&acoustics::Band::mti); o.rangeDb = mean (&acoustics::Band::rangeDb); o.t15 = mean (&acoustics::Band::t15);
            o.ts = mean (&acoustics::Band::ts);
            for (int k = 0; k < acoustics::kModFreqs; ++k)
            {
                double s = 0.0;
                int n = 0;
                for (const auto* b : bs) if (b->m[(size_t) k] > 0.0) { s += b->m[(size_t) k]; ++n; }
                o.m[(size_t) k] = n > 0 ? s / n : 0.0;
            }
        };
        double sti = 0.0, itdg = 0.0;
        int nsti = 0, nitdg = 0;
        for (int b = 0; b < acoustics::kBands; ++b)
        {
            std::vector<const acoustics::Band*> bs;
            for (const auto* a : from) bs.push_back (&a->band[(size_t) b]);
            avgBand (bs, r.band[(size_t) b]);
        }
        {
            std::vector<const acoustics::Band*> bs;
            for (const auto* a : from) bs.push_back (&a->b63);
            avgBand (bs, r.b63);
        }
        for (int b = 0; b < acoustics::kThirds; ++b)
        {
            double s = 0.0;
            int n = 0;
            for (const auto* a : from) if (std::isfinite (a->rt3[(size_t) b])) { s += a->rt3[(size_t) b]; ++n; }
            r.rt3[(size_t) b] = n > 0 ? s / n : NAN;
        }
        for (const auto* a : from)
        {
            r.valid = true;
            if (std::isfinite (a->sti)) { sti += a->sti; ++nsti; }
            if (std::isfinite (a->itdgMs)) { itdg += a->itdgMs; ++nitdg; }
            // echoes and flutter: the worst point counts
            auto worse = [] (double x, double y) { return std::isfinite (y) && (! std::isfinite (x) || y > x); };
            const double was = std::isfinite (r.echoSpeech) ? std::max (r.echoSpeech, r.echoMusic / 1.8) : NAN;
            const double now = std::isfinite (a->echoSpeech) ? std::max (a->echoSpeech, std::isfinite (a->echoMusic) ? a->echoMusic / 1.8 : 0.0) : NAN;
            if (worse (was, now)) { r.echoSpeech = a->echoSpeech; r.echoMusic = a->echoMusic; r.echoMs = a->echoMs; r.echoDb = a->echoDb; }
            if (std::isfinite (a->flutterMs) && worse (r.flutterStrength, a->flutterStrength)) { r.flutterMs = a->flutterMs; r.flutterStrength = a->flutterStrength; }
            if (r.etc.empty()) r.etc = a->etc;
        }
        r.sti = nsti > 0 ? sti / nsti : NAN;
        r.itdgMs = nitdg > 0 ? itdg / nitdg : NAN;
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

    static juce::String bandText (const acoustics::Band& o)
    {
        juce::StringArray v;
        for (double x : { o.edt, o.t20, o.t30, o.c50, o.c80, o.d50, o.mti, o.rangeDb, o.t15, o.ts }) v.add (std::isfinite (x) ? juce::String (x, 4) : juce::String ("nan"));
        return v.joinIntoString (" ");
    }
    static void bandFrom (const juce::String& text, acoustics::Band& o)
    {
        juce::StringArray v;
        v.addTokens (text, " ", {});
        auto at = [&v] (int i) { return i < v.size() && v[i] != "nan" ? v[i].getDoubleValue() : NAN; };
        o.edt = at (0); o.t20 = at (1); o.t30 = at (2); o.c50 = at (3); o.c80 = at (4); o.d50 = at (5); o.mti = at (6); o.rangeDb = at (7); o.t15 = at (8); o.ts = at (9);
    }
    static juce::String numbers (const double* x, int n, int decimals)
    {
        juce::StringArray v;
        for (int i = 0; i < n; ++i) v.add (std::isfinite (x[i]) ? juce::String (x[i], decimals) : juce::String ("nan"));
        return v.joinIntoString (" ");
    }
    static void numbersFrom (const juce::String& text, double* x, int n, double missing)
    {
        juce::StringArray v;
        v.addTokens (text, " ", {});
        for (int i = 0; i < n; ++i) x[i] = i < v.size() && v[i] != "nan" ? v[i].getDoubleValue() : missing;
    }
    static void saveAcoustics (juce::XmlElement& c, const acoustics::Result& ac)
    {
        auto* a = c.createNewChildElement ("Acoustics");
        a->setAttribute ("sti", ac.sti);
        for (int b = 0; b < acoustics::kBands; ++b)
        {
            a->setAttribute ("b" + juce::String (b), bandText (ac.band[(size_t) b]));
            a->setAttribute ("m" + juce::String (b), numbers (ac.band[(size_t) b].m.data(), acoustics::kModFreqs, 4));
        }
        a->setAttribute ("b63", bandText (ac.b63));
        a->setAttribute ("rt3", numbers (ac.rt3.data(), acoustics::kThirds, 3));
        const double refl[] = { ac.itdgMs, ac.echoSpeech, ac.echoMusic, ac.echoMs, ac.echoDb, ac.flutterMs, ac.flutterStrength };
        a->setAttribute ("reflections", numbers (refl, 7, 3));
        juce::StringArray e;
        for (float v : ac.etc) e.add (juce::String (v, 1));
        a->setAttribute ("etc", e.joinIntoString (" "));
    }
    static void loadAcoustics (const juce::XmlElement& c, acoustics::Result& ac)
    {
        const auto* a = c.getChildByName ("Acoustics");
        if (a == nullptr) return;
        ac.valid = true;
        ac.sti = a->hasAttribute ("sti") && a->getStringAttribute ("sti") != "nan" ? a->getDoubleAttribute ("sti") : NAN;
        for (int b = 0; b < acoustics::kBands; ++b)
        {
            bandFrom (a->getStringAttribute ("b" + juce::String (b)), ac.band[(size_t) b]);
            numbersFrom (a->getStringAttribute ("m" + juce::String (b)), ac.band[(size_t) b].m.data(), acoustics::kModFreqs, 0.0);
        }
        if (a->hasAttribute ("b63")) bandFrom (a->getStringAttribute ("b63"), ac.b63);
        numbersFrom (a->getStringAttribute ("rt3"), ac.rt3.data(), acoustics::kThirds, NAN);
        double refl[7];
        numbersFrom (a->getStringAttribute ("reflections"), refl, 7, NAN);
        ac.itdgMs = refl[0]; ac.echoSpeech = refl[1]; ac.echoMusic = refl[2]; ac.echoMs = refl[3]; ac.echoDb = refl[4]; ac.flutterMs = refl[5]; ac.flutterStrength = refl[6];
        juce::StringArray e;
        e.addTokens (a->getStringAttribute ("etc"), " ", {});
        for (const auto& t : e) if (t.isNotEmpty()) ac.etc.push_back (t.getFloatValue());
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
            t[i] = targets::db (target, f) + (shaped ? sysShape[i] : 0.0) + (allow.on && allow.targetEase.size() == g.size() ? allow.targetEase[i] : 0.0);
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
        for (const auto& w : stipa)
        {
            auto* c = e->createNewChildElement ("Stipa");
            c->setAttribute ("position", w.position);
            c->setAttribute ("micName", w.micName);
            c->setAttribute ("time", w.time);
            c->setAttribute ("sti", w.sti);
            c->setAttribute ("corrected", w.corrected);
            c->setAttribute ("external", w.external);
            juce::StringArray m;
            for (double v : w.mti) m.add (juce::String (v, 4));
            c->setAttribute ("mti", m.joinIntoString (" "));
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
            if (! m.fdw.empty())
            {
                juce::StringArray v;
                for (float x : m.fdw) v.add (juce::String (x, 1));
                c->setAttribute ("fdw", v.joinIntoString (" "));
            }
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
        if (e == nullptr)
        {
            // a new session: this computer's defaults
            const auto& ap = AppPrefs::get();
            target = juce::jlimit (0, targets::count() - 1, (int) ap.value ("eqTarget"));
            maxBands = (int) ap.value ("eqBands");
            maxBoostDb = ap.value ("eqMaxBoost");
            return;
        }
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
        for (const auto* c : e->getChildWithTagNameIterator ("Stipa"))
        {
            StipaMeasurement w;
            w.position = c->getStringAttribute ("position");
            w.micName = c->getStringAttribute ("micName");
            w.time = c->getStringAttribute ("time");
            w.sti = c->getDoubleAttribute ("sti", NAN);
            w.corrected = c->getBoolAttribute ("corrected");
            w.external = c->getBoolAttribute ("external");
            juce::StringArray m;
            m.addTokens (c->getStringAttribute ("mti"), " ", {});
            for (int k = 0; k < 7 && k < m.size(); ++k) w.mti[(size_t) k] = m[k].getDoubleValue();
            if (std::isfinite (w.sti)) stipa.push_back (std::move (w));
        }
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
            {
                juce::StringArray v;
                v.addTokens (c->getStringAttribute ("fdw"), " ", {});
                v.removeEmptyStrings();
                if (v.size() == (int) grid().size()) for (const auto& t : v) m.fdw.push_back (t.getFloatValue());
            }
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
