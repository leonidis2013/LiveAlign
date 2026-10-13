// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Room tuning for one session: measurements of the whole system at the listening positions (several mics at once,
// or one mic moved between positions), their average against a target curve, the suggested EQ and the check after
// the EQ is in. The EQ itself is kept on the speaker (it belongs to the venue). Message thread only.
#include <vector>
#include <juce_core/juce_core.h>
#include "Analysis/EqFit.h"
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

    double levelDb() const   // broad level, 500 Hz - 4 kHz
    {
        double s = 0.0;
        int n = 0;
        for (size_t i = 0; i < tf.freq.size(); ++i)
            if (tf.freq[i] >= 500.0 && tf.freq[i] <= 4000.0 && std::isfinite (tf.db[i])) { s += tf.db[i]; ++n; }
        return n > 0 ? s / n : NAN;
    }
};

struct RoomTuning
{
    std::vector<RoomMeasurement> meas;
    int target = 4;                 // targets:: index (Worship / live music)
    int eqSpeaker = 0;              // which speaker the EQ is for (index into the speaker system)
    int maxBands = 8;
    double maxBoostDb = 3.0, maxCutDb = 12.0;
    double fLo = 0.0, fHi = 0.0;    // 0 = from the speaker's crossover filters
    std::vector<eq::Band> suggested;
    std::vector<eq::Band> baseEq;   // EQ that was already on the speaker when the "before" positions were measured
    double offsetDb = 0.0, rmsBefore = 0.0, rmsAfter = 0.0;
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

    // EQ range: above the speaker's high-pass (or 30 Hz), below its low-pass (or 16 kHz), unless set.
    template <class System>
    void eqRange (const System& sys, double& lo, double& hi) const
    {
        lo = 30.0;
        hi = 16000.0;
        if (eqSpeaker >= 0 && eqSpeaker < (int) sys.sources.size())
        {
            const auto& pr = sys.sources[(size_t) eqSpeaker].proc;
            if (pr.hp.active()) lo = pr.hp.freq * 1.2;
            if (pr.lp.active()) hi = pr.lp.freq / 1.2;
        }
        if (fLo > 0.0) lo = fLo;
        if (fHi > 0.0) hi = fHi;
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

    std::vector<double> targetCurve() const
    {
        const auto g = grid();
        std::vector<double> t (g.size());
        for (size_t i = 0; i < g.size(); ++i) t[i] = targets::db (target, g[i]);
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
        e->setAttribute ("eqSpeaker", eqSpeaker);
        e->setAttribute ("maxBands", maxBands);
        e->setAttribute ("maxBoostDb", maxBoostDb);
        e->setAttribute ("maxCutDb", maxCutDb);
        e->setAttribute ("fLo", fLo);
        e->setAttribute ("fHi", fHi);
        e->setAttribute ("tuned", tuned);
        e->setAttribute ("tunedOn", tunedOn);
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
        eqSpeaker = e->getIntAttribute ("eqSpeaker");
        maxBands = juce::jlimit (1, 31, e->getIntAttribute ("maxBands", 8));
        maxBoostDb = juce::jlimit (0.0, 12.0, e->getDoubleAttribute ("maxBoostDb", 3.0));
        maxCutDb = juce::jlimit (1.0, 24.0, e->getDoubleAttribute ("maxCutDb", 12.0));
        fLo = e->getDoubleAttribute ("fLo");
        fHi = e->getDoubleAttribute ("fHi");
        tuned = e->getBoolAttribute ("tuned");
        tunedOn = e->getStringAttribute ("tunedOn");
        for (const auto* c : e->getChildWithTagNameIterator ("Suggested"))
            suggested.push_back ({ c->getDoubleAttribute ("freq"), c->getDoubleAttribute ("gain"), c->getDoubleAttribute ("q", 1.0) });
        for (const auto* c : e->getChildWithTagNameIterator ("BaseEq"))
            baseEq.push_back ({ c->getDoubleAttribute ("freq"), c->getDoubleAttribute ("gain"), c->getDoubleAttribute ("q", 1.0) });
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
    }
};
} // namespace la
