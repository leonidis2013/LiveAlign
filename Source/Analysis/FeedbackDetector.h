// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Feedback ring-out: finds frequencies that ring while the gain of a stage mic is brought up. Each spectrum frame
// (dB on a log grid, e.g. 1/24 octave) is searched for narrow peaks well above their neighbourhood; a peak that stays
// at the same frequency for several frames is a ring, not music or speech. Each ring gets a suggested notch.
// Plain C++, unit tested.
#include <algorithm>
#include <cmath>
#include <vector>

namespace la::fb
{
struct Ring
{
    double freq = 0.0;          // Hz, refined between grid points
    double prominenceDb = 0.0;  // above the median of its neighbourhood
    double levelDb = 0.0;
    // suggested notch (parametric cut)
    double cutDb = -6.0, q = 10.0;
};

struct Options
{
    double fMin = 80.0, fMax = 12000.0;
    double minProminenceDb = 12.0;   // a ring stands this far above its +/- 1/3 octave neighbourhood
    int framesToConfirm = 6;         // and stays this many frames (about half a second)
    int allowedMisses = 2;
    double mergeOct = 1.0 / 12.0;    // the same ring when this close
    // Live (a show: music and speech have sustained peaks too). A ring must last this long (s, with the frame times
    // given to push); 0 counts frames instead.
    double minSeconds = 0.0;
    // Feedback is a pure tone at one pitch: a peak whose pitch wanders more than this (a singer's vibrato, a slide)
    // is not a ring (0: not checked).
    double maxPitchSpreadOct = 0.0;
    // A note dies away; feedback holds or grows: a peak that falls this far below its loudest is not a ring (0: not checked).
    double maxDropDb = 0.0;
    // A note has overtones: a peak with another strong peak at 2, 3 or 4 times (or a half, a third, a quarter of) its
    // frequency in the same frame is an instrument or a voice, not a ring.
    bool rejectHarmonics = false;
};

// The cut to suggest for a ring of this prominence: enough to stop it with a little margin, never deeper than 12 dB.
inline double suggestedCut (double prominenceDb) { return -std::clamp (std::round (prominenceDb * 0.4), 4.0, 12.0); }

class Detector
{
public:
    explicit Detector (Options o = {}) : opt (o) {}
    void reset() { tracks.clear(); }
    void setOptions (const Options& o) { opt = o; }
    const Options& options() const { return opt; }

    // One spectrum frame. Returns the rings confirmed by this frame (each ring is reported once).
    std::vector<Ring> push (const std::vector<double>& f, const std::vector<float>& db, double timeSec = -1.0)
    {
        std::vector<Ring> out;
        const auto peaks = findPeaks (f, db);
        for (auto& t : tracks) t.seen = false;
        for (const auto& p : peaks)
        {
            if (p.prominenceDb < opt.minProminenceDb) continue;   // weaker peaks only count as overtones of others
            const bool harmonic = opt.rejectHarmonics && hasHarmonic (p, peaks);
            Track* best = nullptr;
            for (auto& t : tracks)
                if (std::abs (std::log2 (p.freq / t.ring.freq)) < opt.mergeOct && (best == nullptr || std::abs (p.freq - t.ring.freq) < std::abs (p.freq - best->ring.freq)))
                    best = &t;
            if (best == nullptr)
            {
                Track t;
                t.ring = p;
                t.firstT = timeSec;
                t.fLo = t.fHi = p.freq;
                t.maxLevel = p.levelDb;
                tracks.push_back (t);
                best = &tracks.back();
            }
            best->seen = true;
            best->misses = 0;
            ++best->hits;
            best->ring.freq = 0.7 * best->ring.freq + 0.3 * p.freq;
            best->ring.prominenceDb = std::max (best->ring.prominenceDb, p.prominenceDb);
            best->ring.levelDb = p.levelDb;
            best->fLo = std::min (best->fLo, p.freq);
            best->fHi = std::max (best->fHi, p.freq);
            best->maxLevel = std::max (best->maxLevel, p.levelDb);
            // not a ring (yet): music. Start over, so a held note that later turns into feedback is still caught.
            const bool wandered = opt.maxPitchSpreadOct > 0.0 && std::log2 (best->fHi / best->fLo) > opt.maxPitchSpreadOct;
            const bool dropped = opt.maxDropDb > 0.0 && p.levelDb < best->maxLevel - opt.maxDropDb;
            if (! best->reported && (harmonic || wandered || dropped))
            {
                best->hits = 0;
                best->firstT = timeSec;
                best->fLo = best->fHi = p.freq;
                best->maxLevel = p.levelDb;
                best->ring.prominenceDb = p.prominenceDb;
                continue;
            }
            const bool longEnough = opt.minSeconds > 0.0 && timeSec >= 0.0 ? (best->firstT >= 0.0 && timeSec - best->firstT >= opt.minSeconds)
                                                                            : best->hits >= opt.framesToConfirm;
            if (! best->reported && longEnough && best->hits >= opt.framesToConfirm)
            {
                best->reported = true;
                Ring r = best->ring;
                r.cutDb = suggestedCut (r.prominenceDb);
                r.q = 10.0;
                out.push_back (r);
            }
        }
        for (auto& t : tracks) if (! t.seen) ++t.misses;
        tracks.erase (std::remove_if (tracks.begin(), tracks.end(), [this] (const Track& t) { return ! t.reported && t.misses > opt.allowedMisses; }),
                      tracks.end());
        // reported rings stay (so they are not reported again) until reset
        return out;
    }

private:
    struct Track
    {
        Ring ring;
        int hits = 0, misses = 0;
        bool seen = false, reported = false;
        double firstT = -1.0, fLo = 0.0, fHi = 0.0, maxLevel = -200.0;
    };

    // Another strong peak in the same frame at a whole-number ratio (2, 3, 4) above or below: an overtone series.
    bool hasHarmonic (const Ring& p, const std::vector<Ring>& peaks) const
    {
        for (const auto& q : peaks)
        {
            if (&q == &p || q.prominenceDb < opt.minProminenceDb - 6.0) continue;
            const double r = q.freq > p.freq ? q.freq / p.freq : p.freq / q.freq;
            for (int k = 2; k <= 4; ++k) if (std::abs (std::log2 (r / k)) < 1.0 / 36.0) return true;
        }
        return false;
    }

    std::vector<Ring> findPeaks (const std::vector<double>& f, const std::vector<float>& db) const
    {
        std::vector<Ring> peaks;
        const int n = (int) std::min (f.size(), db.size());
        for (int i = 1; i + 1 < n; ++i)
        {
            if (f[(size_t) i] < opt.fMin || f[(size_t) i] > opt.fMax || ! std::isfinite (db[(size_t) i])) continue;
            if (! (db[(size_t) i] > db[(size_t) i - 1] && db[(size_t) i] >= db[(size_t) i + 1])) continue;
            // median of the +/- 1/3 octave neighbourhood, leaving out the peak's own +/- 1/24 octave
            std::vector<double> around;
            for (int k = 0; k < n; ++k)
            {
                const double oct = std::abs (std::log2 (f[(size_t) k] / f[(size_t) i]));
                if (oct <= 1.0 / 3.0 && oct > 1.0 / 20.0 && std::isfinite (db[(size_t) k])) around.push_back (db[(size_t) k]);
            }
            if (around.size() < 4) continue;
            std::nth_element (around.begin(), around.begin() + (long) around.size() / 2, around.end());
            const double prom = db[(size_t) i] - around[around.size() / 2];
            if (prom < (opt.rejectHarmonics ? opt.minProminenceDb - 6.0 : opt.minProminenceDb)) continue;
            // parabolic refinement on log frequency
            const double a = db[(size_t) i - 1], b = db[(size_t) i], c = db[(size_t) i + 1];
            const double den = a - 2.0 * b + c;
            const double off = den < 0.0 ? std::clamp (0.5 * (a - c) / den, -0.5, 0.5) : 0.0;
            const double l0 = std::log (f[(size_t) i]), step = off >= 0.0 ? std::log (f[(size_t) i + 1]) - l0 : l0 - std::log (f[(size_t) i - 1]);
            Ring r;
            r.freq = std::exp (l0 + off * step);
            r.prominenceDb = prom;
            r.levelDb = b;
            peaks.push_back (r);
        }
        return peaks;
    }

    Options opt;
    std::vector<Track> tracks;
};
} // namespace la::fb
