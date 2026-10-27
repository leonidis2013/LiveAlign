// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The speaker system of a venue: each source (a top, a sub, a fill ...), its response (measured with one mic, or
// imported from a file), the processing LiveAlign suggests for it, and the crossover between two of them.
// Saved in the venue file. Message thread only.
#include <algorithm>
#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>
#include "Analysis/Crossover.h"
#include "Analysis/CrossoverPair.h"
#include "Analysis/FreqTable.h"
#include "Analysis/Polarity.h"
#include "Settings.h"

namespace la
{
enum class SpeakerRole { Main = 0, Sub, Fill, Delay, Other };

inline juce::StringArray speakerRoleNames() { return { "Main / top", "Sub", "Fill", "Delay", "Other" }; }

// Where a source sits in the mix: its processor output channel. Left and right are separate speakers (a pair
// shares its settings); there is no combined L+R channel.
enum class SpeakerChannel { Mono = 0, Left, Centre, Right };
constexpr int kNumSpeakerChannels = 4;

inline juce::StringArray speakerChannelNames() { return { "Mono", "Left", "Centre", "Right" }; }
inline juce::String speakerChannelShort (int c)
{
    static const char* n[] = { "M", "L", "C", "R" };
    return n[juce::jlimit (0, kNumSpeakerChannels - 1, c)];
}

// The polarity of one cabinet: from its own measurement, and against the speaker it should match.
struct PolarityCheck
{
    int verdict = polarity::Unknown;   // the answer: normal, inverted, check, skipped
    int own = polarity::Unknown;       // from its own phase and impulse
    double phaseDeg = 0.0;             // its excess phase through its loudest octaves
    bool peakInverted = false;
    int against = -1;                  // the speaker it was compared with (-1: none)
    int relative = polarity::Unknown;  // Normal = same as `against`, Inverted = opposite
    juce::String when, mic;
    double levelDb = NAN;              // the capture's level through its own range (cabinets on one output should match)
    bool notClose = false;             // arrived much later than the other close-up captures: the mic was probably not close
    bool atCrossover = false;          // a sub whose close-up capture can't tell: its polarity is judged against the tops at the crossover
    bool processorInverted = false;    // the processor inverts this output on purpose: allowed for in the verdict

    bool done() const { return verdict != polarity::Unknown || atCrossover; }
};

struct SpeakerSource
{
    juce::String name;
    int role = (int) SpeakerRole::Main;
    int channel = (int) SpeakerChannel::Mono;
    juce::String model;            // the maker's model name, as the user enters it
    int count = 1;                 // cabinets on this output (two subs side by side, a column of two ...)
    int linkTo = -1;               // shares its settings with this speaker (the other side of a stereo pair), -1 = own
    int output = 0;                // generator output when measuring: 0 = all outputs, 1.. = output (n - 1)
    bool show = true;

    FreqTable response;            // dB + phase (+ coherence in extra) relative to the reference
    bool imported = false;
    juce::String source;           // "Mic 1, 12 Oct 2026" or the imported file name
    double delayUsedMs = 0.0;      // mic delay compensation the measurement was taken with
    double offsetMs = 0.0;         // imported only: extra arrival time the user enters
    bool hasArrival = false;
    double arrivalMs = 0.0;        // measured arrival at the mic (from the reference)
    bool nearField = false;        // measured close to the speaker: its own response, timed to its own arrival
    int measureMic = -1;           // close up: the mic used for this speaker (-1: the system's mic)
    std::vector<PolarityCheck> polarity;   // per cabinet (cabinet 1 from the response measurement)

    PolarityCheck* polarityOf (int cab)
    {
        if (cab < 0 || cab >= count) return nullptr;
        if ((int) polarity.size() < count) polarity.resize ((size_t) count);
        return &polarity[(size_t) cab];
    }
    const PolarityCheck* polarityOf (int cab) const { return cab >= 0 && cab < (int) polarity.size() ? &polarity[(size_t) cab] : nullptr; }

    // All its cabinets: inverted if any is, check if any is unclear, normal when every one is checked and normal.
    int polaritySummary() const
    {
        int normal = 0, worst = polarity::Unknown;
        for (int c = 0; c < count; ++c)
        {
            const auto* p = polarityOf (c);
            const int v = p != nullptr ? p->verdict : polarity::Unknown;
            if (v == polarity::Inverted) return polarity::Inverted;
            if (v == polarity::Unsure) worst = polarity::Unsure;
            normal += v == polarity::Normal ? 1 : 0;
        }
        if (worst == polarity::Unsure) return worst;
        return normal == count ? polarity::Normal : polarity::Unknown;   // not every cabinet checked yet
    }
    int polarityChecked() const
    {
        int n = 0;
        for (int c = 0; c < count; ++c) if (const auto* p = polarityOf (c)) n += p->done() ? 1 : 0;
        return n;
    }

    xo::Processing proc;           // suggested settings for the speaker processor
    double trimDb = 0.0;           // level trim from Level balance, on top of proc.gainDb (kept for a linked speaker too)
    double directivityQ = 0.0;     // directivity factor for the critical distance (0 = typical for its role)

    // A filter built into the speaker (a powered sub's own low-pass, a column's own high-pass): part of the chain.
    // Generic: any family and slope, fixed choices or a free range, bypassable or not.
    struct Onboard
    {
        bool present = false;
        bool lowPass = false;           // false: a high-pass, true: a low-pass
        int type = xo::Butterworth;     // xo::FilterType, or 0 for "unknown" (modelled as Butterworth)
        int slope = 24;                 // dB/oct
        std::vector<double> choices;    // the fixed frequencies it offers (empty: a continuous range)
        double rangeLo = 0.0, rangeHi = 0.0;   // the continuous range (when there are no fixed choices)
        bool bypassable = true;
        double measuredAtHz = 0.0;      // the imported / published curve was taken with the filter set here (0: not, or unknown)
        double latencyMs = -1.0;        // the speaker's own DSP latency (-1: unknown)
        double setHz = 0.0;             // where it is set now (0: bypassed / none)
        bool usable() const { return present && (! choices.empty() || rangeHi > rangeLo); }
        // the nearest setting it can actually take
        double snap (double hz) const
        {
            if (! choices.empty())
            {
                double best = choices.front();
                for (double c : choices) if (std::abs (std::log2 (c / hz)) < std::abs (std::log2 (best / hz))) best = c;
                return best;
            }
            return rangeHi > rangeLo ? juce::jlimit (rangeLo, rangeHi, hz) : hz;
        }
        xo::Filter filterAt (double hz) const { return { type == 0 ? xo::Butterworth : type, slope, hz }; }
        juce::String text() const
        {
            if (! present) return {};
            juce::String t = juce::String (lowPass ? "low-pass" : "high-pass") + " " + (type == 0 ? juce::String ("(family unknown)") : juce::String (xo::typeName (type))) + " " + juce::String (slope) + " dB/oct";
            if (! choices.empty()) { juce::StringArray c; for (double v : choices) c.add (juce::String (juce::roundToInt (v))); t << " at " << c.joinIntoString (" / ") << " Hz"; }
            else if (rangeHi > rangeLo) t << " " << juce::String (juce::roundToInt (rangeLo)) << " - " << juce::String (juce::roundToInt (rangeHi)) << " Hz";
            t << (bypassable ? ", can be bypassed" : ", always in");
            return t;
        }
    };
    Onboard onboard;

    // Protection, from the maker's data: the lowest high-pass it should be run with, and its voltage limit.
    double minSafeHpHz = 0.0;      // 0: not entered
    int minSafeHpSlope = 24;
    double rmsVoltLimit = 0.0;     // V RMS at the terminals (0: not entered)
    bool dspLatencyUnknown() const { return onboard.present && onboard.latencyMs < 0.0; }
    double coverH = 0.0, coverV = 0.0;   // the data sheet's coverage angles (degrees) the Q came from (0: not entered)
    bool hpProtective = false;     // its high-pass is the protective one for a system with no sub (not a crossover)
    juce::String ampGain, limiter; // as set on its amp channel and in the processor (free text: "+32 dB", "+12 dBu"), for the sheet

    // A typical directivity factor (Q) for the role: a sub radiates nearly all round, a long-throw top narrowly.
    static double typicalQ (int role)
    {
        switch ((SpeakerRole) role)
        {
            case SpeakerRole::Sub: return 1.5;
            case SpeakerRole::Fill: return 5.0;
            case SpeakerRole::Delay: return 8.0;
            case SpeakerRole::Main: return 10.0;
            case SpeakerRole::Other:
            default: return 5.0;
        }
    }
    double qFactor() const { return directivityQ > 0.0 ? directivityQ : typicalQ (role); }
    // Directivity factor from the coverage angles (horizontal x vertical, -6 dB, degrees), after Molloy:
    // Q = 180 / asin (sin (h/2) sin (v/2)). Wider than 180 degrees counts as 180 (a half-space, Q 2).
    static double qFromAngles (double h, double v)
    {
        const double hh = std::clamp (h, 5.0, 180.0), vv = std::clamp (v, 5.0, 180.0);
        const double a = std::asin (std::sin (hh * 3.14159265358979 / 360.0) * std::sin (vv * 3.14159265358979 / 360.0)) * 180.0 / 3.14159265358979;
        return std::round (180.0 / a * 10.0) / 10.0;
    }
    juce::String sheetEntered;     // the processor sheet line as it was when ticked "entered" (a change unticks it)
    juce::String groupSheetEntered;   // a group's lead: its group line on a processor with a group stage, as ticked

    // As found: the first measurement of this speaker, with the processing it had then (for the before / after view).
    FreqTable asFound;
    xo::Processing asFoundProc;
    juce::String asFoundWhen;

    bool hasResponse() const { return response.freq.size() > 2; }
    double totalGainDb() const { return proc.gainDb + trimDb; }
};

// Time alignment of one speaker (`align`) to another (`ref`) at a listening spot where their coverage meets:
// arrivals and levels measured there, and the delay that makes `align` arrive with `ref`.
// Either end can be a set: the speaker's whole group (its lead and every speaker linked to it, such as the left and
// right of a stereo pair) plays together, as in the show. The delay then goes on every member of the align set, each
// keeping its own small trim (its offset from the others).
struct Alignment
{
    int ref = 0, align = 1;
    bool refSet = false, alignSet = false;   // the end is the speaker's whole group, playing together
    juce::String spot;              // named position (Row 3, Under balcony, Centre line ...)
    double precedenceMs = 0.0;      // extra delay so the sound still seems to come from `ref`
    bool hasRef = false, hasAlign = false;
    double refArrivalMs = 0.0, alignArrivalMs = 0.0;
    float refLevelDb = -200.0f, alignLevelDb = -200.0f;   // at the spot, same generator level
    juce::String measuredOn;
    // The crossover's own offset of `align` against `ref` (low minus high delay), when the two are the crossover pair
    // measured close to each speaker: the arrivals at the spot line them up, this adds what the crossover needs.
    double xoOffsetMs = 0.0;
    // For the crossover pair: both transfer functions at the spot (same timing), and the delay of `align` that makes
    // them add up best through the crossover region. Used instead of the arrivals when found.
    FreqTable refTf, alignTf;   // not saved
    std::vector<float> refIr, alignIr;   // each speaker's impulse response at the spot, on the same time base (not saved)
    int refIrZero = 0, alignIrZero = 0;  // the sample of time zero (the mic delay both were measured with)
    double irRate = 48000.0;
    double lockMs = 0.0;        // the mic delay both were measured with
    bool phaseValid = false;
    double phaseDelayMs = 0.0;
    // The crossover pair: the low side must also be inverted (found together with the delay: inverted plus half a
    // cycle often adds up better, and the two answers differ in how the phase slopes meet outside the crossover).
    // Relative to the polarity each end had while it was measured.
    bool phaseInvert = false;
    bool refInvertAtMeasure = false, alignInvertAtMeasure = false;
    // A set as the reference end (not the crossover pair): its members are measured one at a time, and the one
    // loudest in the align speaker's band at the spot is the reference (a fill or delay follows the nearer side).
    // -1: the end as set (a single speaker, or the set playing together).
    int refMemberUsed = -1;
    int refSpeaker() const { return refMemberUsed >= 0 ? refMemberUsed : ref; }
    // the delays each speaker had (in the processor, as set here) while it was measured
    double refDelayAtMeasure = 0.0, alignDelayAtMeasure = 0.0;
    // Check: the level in the band where they overlap with each alone and with both playing (dB, -200 = not measured)
    double checkFreq = 0.0;
    float refBandDb = -200.0f, alignBandDb = -200.0f, sumBandDb = -200.0f;
    juce::String checkedOn;

    bool measured() const { return hasRef && hasAlign; }
    bool checked() const { return sumBandDb > -150.0f && refBandDb > -150.0f && alignBandDb > -150.0f; }
    // Delay to add to `align` (relative to how they were measured, both with no processor delay).
    double neededMs (double refProcessingMs) const
    {
        if (phaseValid) return phaseDelayMs + alignDelayAtMeasure - refDelayAtMeasure + refProcessingMs + precedenceMs;
        return (refArrivalMs - refDelayAtMeasure) + refProcessingMs - (alignArrivalMs - alignDelayAtMeasure) + precedenceMs + xoOffsetMs;
    }

    // The delay of `b` (ms, may be negative) and its polarity that make the two transfer functions add up best through
    // the crossover at fc (fc/2 to 2fc). Both polarities are tried with every delay: at a sub, inverted plus half a
    // cycle often adds up as well as, or better than, normal. When the two polarities add up within 0.3 dB of each
    // other, the one whose phase difference stays within 45 degrees over the wider band fc/3 to 3fc wins: its phase
    // slopes meet rather than merely cross at the crossover. Only bins both measurements heard clearly count.
    static bool phaseMatch (const FreqTable& a, const FreqTable& b, double fc, double& bestMs, bool& invert)
    {
        if (! a.hasPhase() || ! b.hasPhase() || ! (fc > 0.0)) return false;
        constexpr double pi = 3.14159265358979;
        struct Bin { double w, ma, mb, pa, pb; bool inner; };
        std::vector<Bin> bins;
        // coherence of `b` at f: its nearest bin (both tables come off the same analysis grid)
        auto cohB = [&b] (double f)
        {
            if (b.extra.size() != b.freq.size() || b.freq.empty()) return 1.0;
            const auto it = std::lower_bound (b.freq.begin(), b.freq.end(), f);
            size_t i = (size_t) (it - b.freq.begin());
            if (i >= b.freq.size()) i = b.freq.size() - 1;
            if (i > 0 && std::abs (b.freq[i - 1] - f) < std::abs (b.freq[i] - f)) --i;
            return b.extra[i];
        };
        size_t inner = 0;
        for (size_t i = 0; i < a.freq.size(); ++i)
        {
            const double f = a.freq[i];
            if (f < fc / 3.0 || f > fc * 3.0) continue;
            if (a.extra.size() == a.freq.size() && a.extra[i] < 0.4) continue;
            if (cohB (f) < 0.4) continue;
            const double db = b.dbAt (f), ph = b.phaseAt (f);
            if (! std::isfinite (db) || ! std::isfinite (ph) || ! std::isfinite (a.db[i]) || ! std::isfinite (a.phase[i])) continue;
            const bool in = f >= fc / 2.0 && f <= fc * 2.0;
            inner += in ? 1 : 0;
            bins.push_back ({ f, std::pow (10.0, a.db[i] / 20.0), std::pow (10.0, db / 20.0), a.phase[i] * pi / 180.0, ph * pi / 180.0, in });
        }
        if (inner < 4) return false;

        // for each polarity: the delay that adds up best through fc/2 to 2fc, as the mean of |a + b|^2 / (|a| + |b|)^2
        double bestSum[2] = { -1.0, -1.0 }, bestD[2] = { 0.0, 0.0 };
        for (int p = 0; p < 2; ++p)
        {
            const double flip = p == 0 ? 0.0 : pi;
            for (double d = -20.0; d <= 20.0; d += 0.02)
            {
                double sum = 0.0;
                for (const auto& x : bins)
                {
                    if (! x.inner) continue;
                    const double ph = x.pb + flip - 2.0 * pi * x.w * d * 0.001;
                    const double re = x.ma * std::cos (x.pa) + x.mb * std::cos (ph), im = x.ma * std::sin (x.pa) + x.mb * std::sin (ph);
                    sum += (re * re + im * im) / ((x.ma + x.mb) * (x.ma + x.mb) + 1e-30);
                }
                if (sum > bestSum[p]) { bestSum[p] = sum; bestD[p] = d; }
            }
        }
        // the share of the wider band where the phase difference stays within 45 degrees, at each polarity's best delay
        auto slopesMeet = [&] (int p)
        {
            const double flip = p == 0 ? 0.0 : pi;
            int ok = 0, n = 0;
            for (const auto& x : bins)
            {
                double diff = x.pb + flip - 2.0 * pi * x.w * bestD[p] * 0.001 - x.pa;
                diff = std::fmod (diff + pi, 2.0 * pi);
                if (diff < 0.0) diff += 2.0 * pi;
                diff -= pi;
                ++n;
                ok += std::abs (diff) <= pi / 4.0 ? 1 : 0;
            }
            return n > 0 ? (double) ok / n : 0.0;
        };
        int pick = bestSum[1] > bestSum[0] ? 1 : 0;
        const double dbApart = 10.0 * std::log10 (std::max (bestSum[0], bestSum[1]) / (std::min (bestSum[0], bestSum[1]) + 1e-30));
        if (dbApart <= 0.3)
        {
            const double m0 = slopesMeet (0), m1 = slopesMeet (1);
            if (std::abs (m0 - m1) > 1e-9) pick = m1 > m0 ? 1 : 0;
        }
        bestMs = bestD[pick];
        invert = pick == 1;
        return true;
    }
    // In phase, the two add up to this much more than the louder one alone.
    double idealSumGainDb() const
    {
        const double a = std::pow (10.0, refBandDb / 20.0), b = std::pow (10.0, alignBandDb / 20.0);
        return 20.0 * std::log10 ((a + b) / std::max (a, b));
    }
    double sumGainDb() const { return sumBandDb - std::max (refBandDb, alignBandDb); }
};

struct SpeakerSystem
{
    static constexpr int kMaxSources = 8;

    std::vector<SpeakerSource> sources;
    int measureMic = 0;   // one spot (and the default close up): the mic for every speaker

    // The mic that measures speaker `i`: its own when measured close up, otherwise the system's.
    int micFor (int i, int micCount) const
    {
        int m = measureMic;
        if (placement == 0 && i >= 0 && i < (int) sources.size() && sources[(size_t) i].measureMic >= 0) m = sources[(size_t) i].measureMic;
        return juce::jlimit (0, juce::jmax (0, micCount - 1), m);
    }
    bool timingLocked = false;
    double lockedDelayMs = 0.0;    // mic delay every measurement uses, so arrivals stay comparable

    int xoHigh = 0, xoLow = 1;     // crossover pair (indices into sources)
    bool xoChosen = false;         // the user picked the pair; otherwise it follows the speakers' roles
    int placement = 0;             // speaker response: 0 = close to each speaker, 1 = one spot for all of them
    double xoRelDelayMs = 0.0;     // the crossover's delay of the low speaker against the high one (low minus high)
    // The processor has a group (matrix) stage: a group's filters, level, polarity and EQ are entered once on the group,
    // and each output keeps its own delay and trim. Off: every output gets its full settings.
    bool groupStage = false;
    xo::Capabilities caps;
    static constexpr int kMaxOutputs = 8;
    std::vector<bool> outputHasHp = std::vector<bool> (kMaxOutputs, true), outputHasLp = std::vector<bool> (kMaxOutputs, true);   // per output: a high-pass / low-pass available
    bool outputOffersHp (int output) const { return output <= 0 || output > kMaxOutputs || outputHasHp[(size_t) (output - 1)]; }
    bool outputOffersLp (int output) const { return output <= 0 || output > kMaxOutputs || outputHasLp[(size_t) (output - 1)]; }
    xo::Options opt;
    xo::Result lastResult;         // shown until something changes
    std::vector<Alignment> alignments;
    static constexpr int kMaxAlignments = 8;

    static SpeakerSystem defaults()
    {
        SpeakerSystem s;
        SpeakerSource left, right, sub;
        left.name = "Stereo (L)";
        left.role = (int) SpeakerRole::Main;
        left.channel = (int) SpeakerChannel::Left;
        left.output = 1;
        right = left;
        right.name = "Stereo (R)";
        right.channel = (int) SpeakerChannel::Right;
        right.output = 2;
        right.linkTo = 0;
        sub.name = "Sub";
        sub.role = (int) SpeakerRole::Sub;
        sub.output = 3;
        s.sources = { left, right, sub };
        s.ensurePair();
        return s;
    }

    // Response on the analysis grid, at the common timing reference, without processing.
    xo::Response rawResponse (int i, const std::vector<double>& grid) const
    {
        if (i < 0 || i >= (int) sources.size() || ! sources[(size_t) i].hasResponse()) return {};
        const auto& s = sources[(size_t) i];
        // close-up measurements keep their own timing: no shift to the common reference
        const double used = s.nearField ? lockedDelayMs : s.imported ? lockedDelayMs + s.offsetMs : s.delayUsedMs;
        auto r = xo::fromTable (s.response, used, lockedDelayMs, grid);
        // the speaker's own filter was in the chain when the curve was taken (a published curve at its stated setting,
        // a measurement at the setting of the day): take it out, so the chain can be modelled at any setting. Never
        // more than 24 dB of it: past that the curve is noise, not the filter.
        const double inHz = s.onboard.present ? (s.imported ? s.onboard.measuredAtHz : s.onboard.setHz) : 0.0;
        if (inHz > 0.0)
        {
            const auto f = s.onboard.filterAt (inHz);
            for (size_t j = 0; j < r.f.size(); ++j)
            {
                const cplx h = xo::response (f, ! s.onboard.lowPass, r.f[j]);
                const double mag = std::abs (h);
                r.h[j] /= mag > 0.063 ? h : h * (0.063 / mag);   // the phase always, the level by at most 24 dB
            }
        }
        return r;
    }

    // The speaker's own filter as set now (its response at f), on top of the bare speaker.
    cplx onboardAt (int i, double f) const
    {
        const auto& s = sources[(size_t) i];
        if (! s.onboard.present || s.onboard.setHz <= 0.0) return { 1.0, 0.0 };
        return xo::response (s.onboard.filterAt (s.onboard.setHz), ! s.onboard.lowPass, f);
    }

    xo::Response processedResponse (int i, const std::vector<double>& grid) const
    {
        auto r = rawResponse (i, grid);
        if (! r.empty())
        {
            r = xo::processed (r, sources[(size_t) i].proc);
            for (size_t j = 0; j < r.f.size(); ++j) r.h[j] *= onboardAt (i, r.f[j]);
        }
        return r;
    }

    // A speaker's own filter that cannot be bypassed must be modelled wherever its response is used with its processing.
    bool onboardFixed (int i) const { return i >= 0 && i < (int) sources.size() && sources[(size_t) i].onboard.usable() && ! sources[(size_t) i].onboard.bypassable; }

    // ---- groups: a lead and the speakers set to share its settings (Main L with Main R and the centre ...) ----
    static const char* rolePlural (int role)
    {
        static const char* n[] = { "Mains", "Subs", "Fills", "Delays", "Others" };
        return n[juce::jlimit (0, 4, role)];
    }
    // The members of `i`'s group, its lead first.
    std::vector<int> groupOf (int i) const
    {
        std::vector<int> out;
        const int lead = master (i);
        if (lead < 0 || lead >= (int) sources.size()) return out;
        out.push_back (lead);
        for (int k = 0; k < (int) sources.size(); ++k) if (k != lead && master (k) == lead) out.push_back (k);
        return out;
    }
    bool inGroup (int i) const { return groupOf (i).size() > 1; }
    // "Mains", or "Delays (Delay 2)" when another group of the same role exists; a speaker on its own goes by its name.
    juce::String groupName (int i) const
    {
        const int lead = master (i);
        if (lead < 0 || lead >= (int) sources.size()) return {};
        if (! inGroup (lead)) return nameOf (lead);
        int same = 0;
        for (int k = 0; k < (int) sources.size(); ++k) same += master (k) == k && sources[(size_t) k].role == sources[(size_t) lead].role && inGroup (k) ? 1 : 0;
        const juce::String base = rolePlural (sources[(size_t) lead].role);
        return same > 1 ? base + " (" + nameOf (lead) + ")" : base;
    }
    // "Outputs 1, 2" for a group's members (All outputs when one of them plays from all).
    juce::String groupOutputs (int i) const
    {
        juce::StringArray o;
        for (int k : groupOf (i))
        {
            const int out = sources[(size_t) k].output;
            if (out == 0) return "All outputs";
            o.addIfNotAlreadyThere (juce::String (out));
        }
        return (o.size() == 1 ? "Output " : "Outputs ") + o.joinIntoString (", ");
    }

    // ---- time alignment ends: one speaker, or a set (its whole group playing together) ----
    // Who plays for an alignment end: the speaker alone, or every member of its group (the lead first).
    std::vector<int> endMembers (int i, bool set) const
    {
        if (i < 0 || i >= (int) sources.size()) return {};
        if (! set) return { i };
        return groupOf (i);
    }
    bool endHas (int i, bool set, int k) const
    {
        const auto m = endMembers (i, set);
        return std::find (m.begin(), m.end(), k) != m.end();
    }
    // A name with a " (L)" or " (R)" suffix: the base ("Stereo") and the side ("L").
    static bool splitSide (const juce::String& n, juce::String& base, juce::String& side)
    {
        for (const char* s : { "L", "R" })
            if (n.endsWith (" (" + juce::String (s) + ")")) { base = n.dropLastCharacters (4).trimEnd(); side = s; return base.isNotEmpty(); }
        return false;
    }
    // The set's name: "Stereo" for "Stereo (L)" and "Stereo (R)", otherwise the names joined ("Fill A + Fill B").
    // `together` adds who plays: "Stereo (L + R together)", "Fill A + Fill B (together)".
    juce::String setName (const std::vector<int>& members, bool together = false) const
    {
        if (members.empty()) return {};
        if (members.size() == 1) return nameOf (members.front());
        juce::StringArray names, sides;
        juce::String base;
        bool shared = true;
        for (int k : members)
        {
            names.add (nameOf (k));
            juce::String b, sd;
            if (! splitSide (nameOf (k), b, sd) || (base.isNotEmpty() && b != base) || sides.contains (sd)) shared = false;
            else { base = b; sides.add (sd); }
        }
        if (shared) return together ? base + " (" + sides.joinIntoString (" + ") + " together)" : base;
        return names.joinIntoString (" + ") + (together ? " (together)" : "");
    }
    juce::String endName (int i, bool set, bool together = false) const
    {
        if (i < 0 || i >= (int) sources.size()) return "?";
        return setName (endMembers (i, set), together);
    }
    // The generator output that plays `members`: their shared output, or 0 (all outputs; the others are muted at the
    // console or processor).
    // The reference end's processor delay now: the member the alignment was lined up to (a set measured one side at
    // a time), otherwise its lead. What `Alignment::neededMs` takes.
    double refDelayNow (const Alignment& a) const
    {
        const int i = a.refSpeaker();
        return i >= 0 && i < (int) sources.size() ? sources[(size_t) i].proc.delayMs : 0.0;
    }
    int sharedOutput (const std::vector<int>& members) const
    {
        int o = -1;
        for (int k : members)
        {
            if (k < 0 || k >= (int) sources.size()) continue;
            const int ko = sources[(size_t) k].output;
            if (ko == 0 || (o >= 0 && ko != o)) return 0;
            o = ko;
        }
        return juce::jmax (0, o);
    }

    // The group's response for the crossover: the members' magnitudes power-averaged (one cabinet's quirk, such as a
    // wall bump on one side, does not pick the crossover), with the lead's phase and timing (the members are the
    // same kind of speaker, measured the same way; their timing at a seat is the time alignment's job).
    xo::Response groupResponse (int i, const std::vector<double>& grid, int* used = nullptr) const
    {
        const auto members = groupOf (i);
        if (used != nullptr) *used = 0;
        if (members.empty()) return {};
        int phaseFrom = -1;
        for (int k : members) if (sources[(size_t) k].hasResponse()) { phaseFrom = k; break; }
        if (phaseFrom < 0) return {};
        auto out = rawResponse (phaseFrom, grid);
        if (out.empty()) return out;
        std::vector<double> power (out.f.size(), 0.0), coh (out.f.size(), 0.0);
        int n = 0;
        for (int k : members)
        {
            if (! sources[(size_t) k].hasResponse() || sources[(size_t) k].nearField != sources[(size_t) phaseFrom].nearField) continue;
            const auto r = rawResponse (k, grid);
            if (r.f.size() != out.f.size()) continue;
            for (size_t j = 0; j < r.f.size(); ++j) { power[j] += std::norm (r.h[j]); coh[j] += r.cohAt (j); }
            ++n;
        }
        if (used != nullptr) *used = n;
        if (n <= 1) return out;
        out.coh.assign (out.f.size(), 1.0);
        for (size_t j = 0; j < out.f.size(); ++j)
        {
            const double mag = std::sqrt (power[j] / n), m0 = std::abs (out.h[j]);
            out.h[j] = m0 > 1e-12 ? out.h[j] * (mag / m0) : std::polar (mag, 0.0);
            out.coh[j] = coh[j] / n;
        }
        return out;
    }

    // The speaker whose settings `i` follows (itself when it has its own).
    int master (int i) const
    {
        if (i < 0 || i >= (int) sources.size()) return i;
        const int l = sources[(size_t) i].linkTo;
        return l >= 0 && l < (int) sources.size() && l != i && sources[(size_t) l].linkTo < 0 ? l : i;
    }

    // Measured itself (or imported). A speaker set "Same as" another is measured too: its settings follow the other,
    // but its own capture checks its polarity, level and response against it. Only when the one it follows was
    // imported (nothing to compare with) does it not need its own.
    bool covered (int i) const
    {
        if (i < 0 || i >= (int) sources.size()) return false;
        if (sources[(size_t) i].hasResponse()) return true;
        const int m = master (i);
        return m != i && sources[(size_t) m].imported && sources[(size_t) m].hasResponse();
    }

    juce::String nameOf (int i) const
    {
        if (i < 0 || i >= (int) sources.size()) return {};
        const auto& n = sources[(size_t) i].name;
        return n.isNotEmpty() ? n : "Speaker " + juce::String (i + 1);
    }

    // Who else plays when `targets` are measured: speakers on the same output, or every speaker when one plays from
    // "All outputs" (or two targets on different outputs, which plays all of them). On most systems that is fine: the
    // others are muted at the console or processor for the measurement. Linked speakers share settings, not a feed.
    std::vector<int> alsoPlaying (const std::vector<int>& targets) const
    {
        std::vector<int> out;
        if (sources.size() < 2 || targets.empty()) return out;
        std::vector<int> outs;
        for (int t : targets) if (t >= 0 && t < (int) sources.size()) outs.push_back (sources[(size_t) t].output);
        bool all = false;
        for (int o : outs) all = all || o == 0 || o != outs.front();
        for (int k = 0; k < (int) sources.size(); ++k)
        {
            if (std::find (targets.begin(), targets.end(), k) != targets.end()) continue;
            const int o = sources[(size_t) k].output;
            if (all || o == 0 || std::find (outs.begin(), outs.end(), o) != outs.end()) out.push_back (k);
        }
        return out;
    }

    // "Mute Main R and Sub on the console or processor, so only Main L plays." Empty when nothing else plays.
    juce::String muteText (const std::vector<int>& targets) const
    {
        const auto others = alsoPlaying (targets);
        if (others.empty()) return {};
        juce::StringArray o, t;
        for (int k : others) o.add (nameOf (k));
        for (int k : targets) t.add (nameOf (k));
        auto join = [] (const juce::StringArray& a) { return a.size() <= 1 ? a.joinIntoString ("") : a.joinIntoString (", ", 0, a.size() - 1) + " and " + a[a.size() - 1]; };
        return "Mute " + join (o) + " on the console or processor, so only " + join (t) + (t.size() == 1 ? " plays." : " play.");
    }

    // The range a speaker plays (with its processing): within 10 dB of its smoothed peak, from its close-up
    // measurement; by its role when it has none (a sub 40 - 100 Hz, anything else 200 Hz - 5 kHz).
    // With no sub, a top still wants a protective high-pass near its own low end (where it is 10 dB down), so it is
    // not driven below what it can play. False for a sub, or when it has one already.
    bool topHighPass (int i, xo::Filter& f) const
    {
        if (i < 0 || i >= (int) sources.size() || hasSub()) return false;
        const auto& s = sources[(size_t) i];
        if (s.role == (int) SpeakerRole::Sub || master (i) != i || s.proc.hp.active()) return false;
        double lo, hi;
        playRange (i, lo, hi);
        const double fc = std::max (40.0, std::round (lo * 0.85 / 5.0) * 5.0);
        f.type = xo::Butterworth;
        f.slope = 24;
        f.freq = fc;
        return true;
    }

    void playRange (int i, double& lo, double& hi) const
    {
        const bool sub = i >= 0 && i < (int) sources.size() && sources[(size_t) i].role == (int) SpeakerRole::Sub;
        lo = sub ? 40.0 : 200.0;
        hi = sub ? 100.0 : 5000.0;
        const int m = i >= 0 && i < (int) sources.size() && sources[(size_t) i].hasResponse() ? i : master (i);
        if (m < 0 || m >= (int) sources.size() || ! sources[(size_t) m].hasResponse()) return;
        std::vector<double> g;
        for (double f = 25.0; f <= 18000.0; f *= std::pow (2.0, 1.0 / 12.0)) g.push_back (f);
        const auto& s = sources[(size_t) m];
        std::vector<double> db (g.size(), -300.0);
        for (size_t k = 0; k < g.size(); ++k)
        {
            if (g[k] < s.response.freq.front() || g[k] > s.response.freq.back()) continue;
            double sum = 0.0;
            int n = 0;
            for (double x = g[k] / 1.122; x <= g[k] * 1.122; x *= 1.03) { sum += s.response.dbAt (x); ++n; }
            db[k] = sum / juce::jmax (1, n) + 20.0 * std::log10 (std::abs (sources[(size_t) i].proc.at (g[k])) + 1e-12);
        }
        double peak = -300.0;
        for (double v : db) peak = std::max (peak, v);
        if (peak < -250.0) return;
        double l = 0.0, h = 0.0;
        for (size_t k = 0; k < g.size(); ++k) if (db[k] >= peak - 10.0) { if (l <= 0.0) l = g[k]; h = g[k]; }
        if (l > 0.0 && h > l * 1.4) { lo = l; hi = h; }
    }

    // A close-up capture's level through the speaker's own range (dB, NaN when there is none): left and right of a
    // pair, and the cabinets on one output, should match.
    static double bandLevel (const FreqTable& t, double lo, double hi)
    {
        double sum = 0.0;
        int n = 0;
        for (size_t k = 0; k < t.freq.size(); ++k)
            if (t.freq[k] >= lo && t.freq[k] <= hi && std::isfinite (t.db[k])) { sum += std::pow (10.0, t.db[k] / 10.0); ++n; }
        return n > 0 ? 10.0 * std::log10 (sum / n) : NAN;
    }

    // Close-up captures arrive within a few ms of each other (same distance, same chain). One that arrives much
    // later than the earliest of its kind (subs with subs, tops with tops) was probably not measured close up.
    bool lateArrival (int i, double& lateByMs) const
    {
        lateByMs = 0.0;
        if (i < 0 || i >= (int) sources.size()) return false;
        const auto& s = sources[(size_t) i];
        if (! s.hasArrival || ! s.nearField || s.imported) return false;
        double first = 1e9;
        int n = 0;
        for (int k = 0; k < (int) sources.size(); ++k)
        {
            const auto& o = sources[(size_t) k];
            // like with like: a sub's arrival comes later than a top's (its filters and its long impulse)
            if (k == i || ! o.hasArrival || ! o.nearField || o.imported || (o.role == (int) SpeakerRole::Sub) != (s.role == (int) SpeakerRole::Sub)) continue;
            first = std::min (first, o.arrivalMs);
            ++n;
        }
        if (n == 0) return false;
        lateByMs = s.arrivalMs - first;
        return lateByMs > kLateMs;
    }
    static constexpr double kLateMs = 3.0;

    // Linked speakers take their master's filters, level and polarity, and its delay unless a time alignment set their own.
    void syncLinks()
    {
        for (int i = 0; i < (int) sources.size(); ++i)
        {
            const int m = master (i);
            if (m == i) { sources[(size_t) i].linkTo = -1; continue; }
            auto& s = sources[(size_t) i];
            const auto& from = sources[(size_t) m].proc;
            bool aligned = false;
            // its own trim: a single-speaker alignment gave it its own delay (a set's delay goes on every member alike)
            for (const auto& a : alignments) aligned = aligned || (a.align == i && ! a.alignSet && a.measured());
            const double keepDelay = s.proc.delayMs;
            s.proc = from;
            if (aligned) s.proc.delayMs = keepDelay;
        }
    }

    bool pairValid() const
    {
        return xoHigh != xoLow && xoHigh >= 0 && xoLow >= 0 && xoHigh < (int) sources.size() && xoLow < (int) sources.size();
    }

    std::vector<int> roles() const
    {
        std::vector<int> r;
        for (const auto& src : sources) r.push_back (src.role);
        return r;
    }

    // The crossover pair from the roles (first main / top above, first sub below), unless the user picked one.
    // Call after speakers are added, removed or change role, and after a venue is opened.
    void ensurePair()
    {
        if (xoChosen && pairValid()) return;
        const auto p = xo::defaultPair (roles());
        xoHigh = p.high;
        xoLow = p.low;
        xoChosen = false;
    }

    // A sub above the crossover and a top below it.
    bool pairInverted() const { return xo::pairInverted (roles(), { xoHigh, xoLow }); }

    // A crossover is between a top and a sub: without a sub there is nothing to cross over (left and right mains are
    // two channels, not two bands).
    bool isMain (int i) const { return i >= 0 && i < (int) sources.size() && sources[(size_t) i].role == (int) SpeakerRole::Main; }

    bool hasSub() const
    {
        for (const auto& s : sources) if (s.role == (int) SpeakerRole::Sub) return true;
        return false;
    }
    bool xoNeeded() const
    {
        bool sub = false, top = false;
        for (const auto& s : sources) (s.role == (int) SpeakerRole::Sub ? sub : top) = true;
        return sub && top;
    }
    // The pair is a top and a sub (the only kind whose sum means anything).
    bool realPair() const
    {
        if (! pairValid()) return false;
        const bool hs = sources[(size_t) xoHigh].role == (int) SpeakerRole::Sub, ls = sources[(size_t) xoLow].role == (int) SpeakerRole::Sub;
        return hs != ls;
    }
    // Crossover filters left on speakers that are not a top / sub pair (from an earlier version, or a pair of mains).
    juce::StringArray strayCrossoverFilters() const
    {
        juce::StringArray out;
        if (realPair() && xoNeeded()) return out;
        for (const auto& s : sources)
            if (s.linkTo < 0 && ((s.proc.hp.active() && s.role != (int) SpeakerRole::Sub && ! (s.hpProtective && ! hasSub())) || s.proc.lp.active()))
                out.add (s.name);
        return out;
    }
    void removeCrossoverFilters()
    {
        for (auto& s : sources) { s.proc.hp = {}; s.proc.lp = {}; }
        if (pairValid()) { sources[(size_t) xoLow].proc.invert = false; sources[(size_t) xoLow].proc.gainDb = 0.0; }   // set by the crossover
        lastResult = {};
        syncLinks();
    }

    void swapPair()
    {
        std::swap (xoHigh, xoLow);
        xoChosen = true;
        xoRelDelayMs = -xoRelDelayMs;
    }

    // Both speakers of the crossover were measured close up, so their timing comes from a time alignment.
    bool pairNearField() const
    {
        return pairValid() && sources[(size_t) xoHigh].nearField && sources[(size_t) xoLow].nearField
               && sources[(size_t) xoHigh].hasResponse() && sources[(size_t) xoLow].hasResponse();
    }

    // The frequency where two speakers overlap, for the alignment check: the crossover for the pair, else 500 Hz.
    double overlapFreq (int a, int b) const
    {
        if (! pairValid()) return 500.0;
        // the crossover pair, or any sub with any top (Sub to Stereo (R), a second sub): they meet at the crossover
        const auto isSub = [this] (int i) { return i >= 0 && i < (int) sources.size() && sources[(size_t) i].role == (int) SpeakerRole::Sub; };
        const bool subAndTop = a >= 0 && b >= 0 && a < (int) sources.size() && b < (int) sources.size() && isSub (a) != isSub (b);
        if ((a == xoHigh && b == xoLow) || (a == xoLow && b == xoHigh) || subAndTop)
        {
            const auto& f = sources[(size_t) xoHigh].proc.hp.active() ? sources[(size_t) xoHigh].proc.hp : sources[(size_t) xoLow].proc.lp;
            if (f.active()) return f.freq;
            return std::sqrt (opt.fMin * opt.fMax);
        }
        return 500.0;
    }

    // ---- a speaker data file's extras ----
    // Keys a response file may carry as "* key=value" lines (LiveAlign's own, or added by hand to a maker's FRD):
    //   onboard=lpf|hpf   onboard-family=butterworth|linkwitz-riley|bessel|unknown   onboard-slope=24
    //   onboard-freqs=80/100/120  or  onboard-range=60-200   onboard-bypass=yes|no   onboard-measured-at=100
    //   onboard-latency-ms=1.3   min-safe-hpf=55   min-safe-hpf-slope=24   rms-volt-limit=48
    // Files without them load as before.
    static void applyFileExtras (SpeakerSource& s, const FreqTable& t)
    {
        auto has = [&t] (const char* k) { return ! t.metaValue (k).empty(); };
        auto str = [&t] (const char* k) { return juce::String (t.metaValue (k)).trim().toLowerCase(); };
        auto num = [&t] (const char* k) { return juce::String (t.metaValue (k)).getDoubleValue(); };
        if (has ("onboard"))
        {
            SpeakerSource::Onboard o;
            o.lowPass = str ("onboard").startsWith ("l");
            const auto fam = str ("onboard-family");
            o.type = fam.startsWith ("butter") ? xo::Butterworth : fam.startsWith ("link") ? xo::LinkwitzRiley : fam.startsWith ("bess") ? xo::Bessel : 0;
            if (has ("onboard-slope")) o.slope = juce::jlimit (6, 48, ((int) num ("onboard-slope") / 6) * 6);
            if (has ("onboard-freqs"))
            {
                juce::StringArray c;
                c.addTokens (str ("onboard-freqs"), "/,; ", {});
                for (const auto& v : c) if (v.getDoubleValue() > 0.0) o.choices.push_back (v.getDoubleValue());
                std::sort (o.choices.begin(), o.choices.end());
            }
            else if (has ("onboard-range"))
            {
                const auto r = str ("onboard-range");
                o.rangeLo = r.upToFirstOccurrenceOf ("-", false, false).getDoubleValue();
                o.rangeHi = r.fromFirstOccurrenceOf ("-", false, false).getDoubleValue();
            }
            if (has ("onboard-bypass")) o.bypassable = ! (str ("onboard-bypass").startsWith ("n") || str ("onboard-bypass") == "0" || str ("onboard-bypass").startsWith ("f"));
            if (has ("onboard-measured-at")) o.measuredAtHz = num ("onboard-measured-at");
            o.latencyMs = has ("onboard-latency-ms") ? std::max (0.0, num ("onboard-latency-ms")) : -1.0;
            o.present = ! o.choices.empty() || o.rangeHi > o.rangeLo;
            if (o.present) o.setHz = o.snap (o.measuredAtHz > 0.0 ? o.measuredAtHz : (! o.choices.empty() ? o.choices.front() : o.rangeLo));
            s.onboard = o;
        }
        if (has ("min-safe-hpf")) s.minSafeHpHz = std::max (0.0, num ("min-safe-hpf"));
        if (has ("min-safe-hpf-slope")) s.minSafeHpSlope = juce::jlimit (6, 48, ((int) num ("min-safe-hpf-slope") / 6) * 6);
        if (has ("rms-volt-limit")) s.rmsVoltLimit = std::max (0.0, num ("rms-volt-limit"));
    }

    // ---- the crossover search over the whole chain ----
    // Candidate tags: what the low source's processor output does about its own filter.
    enum XoTag { TagPlain = 0, TagOnboardOut = 1, TagStacked = 2 };

    // Every setting the chain can take through the search window: the processor's filters at the frequencies it
    // can be set to, and the speakers' own filters where they are in the chain. A speaker's filter that cannot be
    // bypassed is modelled at each of its settings; the processor's matching filter is then OUT (the speaker's own
    // filter does the job), and stacking the processor's filter on top is offered as a labelled alternative.
    // `goal`: 0 smoothest sum, 1 balanced, 2 max headroom (how much a crossover near a speaker's safe limit costs).
    // Nothing the chain cannot produce is ever offered; `why` says so when nothing is left.
    std::vector<xo::Candidate> chainCandidates (double fLo, double fHi, int goal, std::string* why = nullptr) const
    {
        std::vector<xo::Candidate> out;
        if (! pairValid()) return out;
        const auto& hiS = sources[(size_t) xoHigh];
        const auto& loS = sources[(size_t) xoLow];
        const bool hpOffered = outputOffersHp (hiS.output), lpOffered = outputOffersLp (loS.output);
        const bool loFixed = onboardFixed (xoLow) && loS.onboard.lowPass;
        const bool hiFixed = onboardFixed (xoHigh) && ! hiS.onboard.lowPass;
        // the headroom penalty: a crossover below a top's safe high-pass, or close to it (dB of cost per octave under)
        const double headroomWeight = goal == 0 ? 0.0 : goal == 1 ? 1.0 : 3.0;
        auto protectionCost = [&] (double fcHp, int hpSlope) -> double
        {
            if (hiS.minSafeHpHz <= 0.0) return 0.0;
            // an equal or steeper slope at or above the safe frequency protects fully; under it, the cost grows per octave
            const double under = std::log2 (hiS.minSafeHpHz / fcHp) + (hpSlope < hiS.minSafeHpSlope ? 0.25 : 0.0);
            return under > 0.0 ? headroomWeight * under * 2.0 : headroomWeight * 0.15 * std::max (0.0, 1.0 - std::log2 (fcHp / hiS.minSafeHpHz));
        };
        // the frequencies to try on the processor side
        std::vector<double> procFreqs;
        {
            double last = -1.0;
            for (double fc = fLo; fc <= fHi * 1.0001; fc *= std::pow (2.0, 1.0 / 24.0))
            {
                const double fs = caps.snapFreq (fc);
                if (std::abs (fs - last) > 1e-6) procFreqs.push_back (fs);
                last = fs;
            }
        }
        // the low side's own filter settings to try (a fixed one: each of its choices or the processor grid in its range)
        std::vector<double> loOwn;
        if (loFixed)
        {
            if (! loS.onboard.choices.empty()) loOwn = loS.onboard.choices;
            else for (double f : procFreqs) if (f >= loS.onboard.rangeLo && f <= loS.onboard.rangeHi) loOwn.push_back (f);
            loOwn.erase (std::remove_if (loOwn.begin(), loOwn.end(), [&] (double f) { return f < fLo / 1.5 || f > fHi * 1.5; }), loOwn.end());
            if (loOwn.empty() && ! loS.onboard.choices.empty()) loOwn = loS.onboard.choices;   // outside the window: still the only settings it has
        }
        std::vector<double> hiOwn;
        if (hiFixed)
        {
            if (! hiS.onboard.choices.empty()) hiOwn = hiS.onboard.choices;
            else for (double f : procFreqs) if (f >= hiS.onboard.rangeLo && f <= hiS.onboard.rangeHi) hiOwn.push_back (f);
        }
        for (int type = 1; type <= 3; ++type)
        {
            if (! caps.types[type]) continue;
            for (int si = 0; si < 8; ++si)
            {
                const int slope = (si + 1) * 6;
                if (! caps.slopes[si] || ! xo::slopeValid (type, slope)) continue;
                if (! loFixed && ! hiFixed)
                {
                    if (! hpOffered && ! lpOffered) continue;
                    for (double fc : procFreqs)
                    {
                        xo::Candidate c;
                        c.fc = fc;
                        if (hpOffered) c.hp = { type, slope, fc };
                        if (lpOffered) c.lp = { type, slope, fc };
                        c.extraCost = protectionCost (fc, hpOffered ? slope : 0);
                        out.push_back (c);
                    }
                    continue;
                }
                // the low speaker's own low-pass is always in: at each of its settings, the top's high-pass at the same
                // frequency (OUT on the sub's output), or stacked with the processor's low-pass too
                for (double own : loFixed ? loOwn : std::vector<double> { 0.0 })
                    for (double hown : hiFixed ? hiOwn : std::vector<double> { 0.0 })
                    {
                        const double fc = own > 0.0 ? own : hown;
                        if (fc <= 0.0) continue;
                        const double fp = caps.snapFreq (fc);
                        xo::Candidate c;
                        c.fc = fc;
                        if (own > 0.0) c.lowExtra = loS.onboard.filterAt (own);
                        if (hown > 0.0) c.highExtra = hiS.onboard.filterAt (hown);
                        if (hpOffered && hown <= 0.0) c.hp = { type, slope, fp };
                        if (lpOffered && own <= 0.0) c.lp = { type, slope, fp };
                        c.tag = TagOnboardOut;
                        c.extraCost = protectionCost (fc, hown > 0.0 ? hiS.onboard.slope : (hpOffered ? slope : 0));
                        // a processor filter of the same family and slope as the speaker's own keeps the two sides'
                        // phase together through the overlap (complementary filters): preferred when the sums are close
                        if (own > 0.0 && loS.onboard.type == type && loS.onboard.slope == slope) c.extraCost -= 0.1;
                        if (hown > 0.0 && hiS.onboard.type == type && hiS.onboard.slope == slope) c.extraCost -= 0.1;
                        out.push_back (c);
                        // stacked: the processor's filter on top of the speaker's own, as a labelled alternative
                        if ((own > 0.0 && lpOffered) || (hown > 0.0 && hpOffered))
                        {
                            xo::Candidate st = c;
                            if (own > 0.0 && lpOffered) st.lp = { type, slope, fp };
                            if (hown > 0.0 && hpOffered) st.hp = { type, slope, fp };
                            st.tag = TagStacked;
                            st.extraCost += 0.3;   // the plain chain first, when the two are close
                            out.push_back (st);
                        }
                    }
            }
        }
        if (out.empty() && why != nullptr)
        {
            if (! hpOffered && ! lpOffered) *why = "Neither output offers a filter: enter what the processor has (Processor details).";
            else if (loFixed && loOwn.empty()) *why = "The " + loS.name.toStdString() + "'s own low-pass has no setting inside the search range.";
            else *why = "No filter type and slope is enabled for your processor.";
        }
        return out;
    }

    // What each device is set to for the crossover: the speakers' own switches and the processor's outputs, by name.
    juce::String chainSettingsText (const xo::Result& r) const
    {
        if (! r.valid || ! pairValid()) return {};
        const auto& hiS = sources[(size_t) xoHigh];
        const auto& loS = sources[(size_t) xoLow];
        auto filt = [] (const xo::Filter& f, const char* what) -> juce::String
        {
            if (! f.active()) return juce::String (what) + " OUT";
            return juce::String (what) + " " + juce::String (juce::roundToInt (f.freq)) + " Hz " + xo::typeShort (f.type) + juce::String (f.slope);
        };
        juce::StringArray parts;
        if (r.lowExtra.active()) parts.add (loS.name + ": onboard " + filt (r.lowExtra, "LPF"));
        if (r.highExtra.active()) parts.add (hiS.name + ": onboard " + filt (r.highExtra, "HPF"));
        auto outName = [] (const SpeakerSource& s) { return s.output > 0 ? "Processor out " + juce::String (s.output) + " (" + s.name + ")" : "Processor (all outputs, " + s.name + ")"; };
        parts.add (outName (loS) + ": " + filt (r.lowFilter, "LPF") + (r.lowFilter.active() ? "" : r.lowExtra.active() ? " (the speaker's own filter does it)" : ""));
        parts.add (outName (hiS) + ": " + filt (r.filter, "HPF"));
        return parts.joinIntoString ("  ·  ");
    }

    // ---- XML ----
    static void saveFilter (juce::XmlElement& e, const juce::String& k, const xo::Filter& f)
    {
        e.setAttribute (k + "Type", f.type);
        e.setAttribute (k + "Slope", f.slope);
        e.setAttribute (k + "Freq", f.freq);
    }

    static xo::Filter loadFilter (const juce::XmlElement& e, const juce::String& k)
    {
        xo::Filter f;
        f.type = juce::jlimit (0, 3, e.getIntAttribute (k + "Type", 0));
        f.slope = juce::jlimit (6, 48, e.getIntAttribute (k + "Slope", 24));
        f.freq = juce::jlimit (10.0, 20000.0, e.getDoubleAttribute (k + "Freq", 100.0));
        return f;
    }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("SpeakerSystem");
        e->setAttribute ("measureMic", measureMic);
        e->setAttribute ("timingLocked", timingLocked);
        e->setAttribute ("lockedDelayMs", lockedDelayMs);
        e->setAttribute ("xoHigh", xoHigh);
        e->setAttribute ("xoLow", xoLow);
        e->setAttribute ("xoChosen", xoChosen);
        e->setAttribute ("placement", placement);
        e->setAttribute ("xoRelDelayMs", xoRelDelayMs);
        e->setAttribute ("groupStage", groupStage);
        e->setAttribute ("fMin", opt.fMin);
        e->setAttribute ("fMax", opt.fMax);
        juce::String types, slopes;
        for (bool b : caps.types) types << (b ? "1" : "0");
        for (bool b : caps.slopes) slopes << (b ? "1" : "0");
        e->setAttribute ("types", types);
        e->setAttribute ("slopes", slopes);
        e->setAttribute ("procFreqLo", caps.freqLo);
        e->setAttribute ("procFreqHi", caps.freqHi);
        e->setAttribute ("procFreqStepHz", caps.freqStepHz);
        e->setAttribute ("procFreqStepOct", caps.freqStepOct);
        e->setAttribute ("procDelayMaxMs", caps.delayMaxMs);
        e->setAttribute ("procLimiterUnit", caps.limiterUnit);
        e->setAttribute ("procFullScaleDbu", caps.fullScaleDbu);
        for (int o = 0; o < (int) outputHasHp.size(); ++o)
        {
            auto* oe = e->createNewChildElement ("Output");
            oe->setAttribute ("n", o + 1);
            oe->setAttribute ("hp", outputHasHp[(size_t) o]);
            oe->setAttribute ("lp", outputHasLp[(size_t) o]);
        }

        for (const auto& a : alignments)
        {
            auto* c = e->createNewChildElement ("Alignment");
            c->setAttribute ("ref", a.ref);
            c->setAttribute ("align", a.align);
            if (a.refSet) c->setAttribute ("refSet", true);
            if (a.alignSet) c->setAttribute ("alignSet", true);
            c->setAttribute ("spot", a.spot);
            c->setAttribute ("precedenceMs", a.precedenceMs);
            c->setAttribute ("hasRef", a.hasRef);
            c->setAttribute ("hasAlign", a.hasAlign);
            c->setAttribute ("refArrivalMs", a.refArrivalMs);
            c->setAttribute ("alignArrivalMs", a.alignArrivalMs);
            c->setAttribute ("refLevelDb", a.refLevelDb);
            c->setAttribute ("alignLevelDb", a.alignLevelDb);
            c->setAttribute ("measuredOn", a.measuredOn);
            c->setAttribute ("xoOffsetMs", a.xoOffsetMs);
            c->setAttribute ("phaseValid", a.phaseValid);
            c->setAttribute ("phaseDelayMs", a.phaseDelayMs);
            if (a.phaseInvert) c->setAttribute ("phaseInvert", true);
            if (a.refInvertAtMeasure) c->setAttribute ("refInvertAtMeasure", true);
            if (a.alignInvertAtMeasure) c->setAttribute ("alignInvertAtMeasure", true);
            if (a.refMemberUsed >= 0) c->setAttribute ("refMemberUsed", a.refMemberUsed);
            c->setAttribute ("refDelayAtMeasure", a.refDelayAtMeasure);
            c->setAttribute ("alignDelayAtMeasure", a.alignDelayAtMeasure);
            c->setAttribute ("checkFreq", a.checkFreq);
            c->setAttribute ("refBandDb", a.refBandDb);
            c->setAttribute ("alignBandDb", a.alignBandDb);
            c->setAttribute ("sumBandDb", a.sumBandDb);
            c->setAttribute ("checkedOn", a.checkedOn);
        }

        for (const auto& s : sources)
        {
            auto* c = e->createNewChildElement ("Source");
            c->setAttribute ("name", s.name);
            c->setAttribute ("role", s.role);
            c->setAttribute ("channel", s.channel);
            c->setAttribute ("model", s.model);
            c->setAttribute ("count", s.count);
            c->setAttribute ("linkTo", s.linkTo);
            c->setAttribute ("output", s.output);
            c->setAttribute ("show", s.show);
            c->setAttribute ("imported", s.imported);
            c->setAttribute ("from", s.source);
            c->setAttribute ("delayUsedMs", s.delayUsedMs);
            c->setAttribute ("offsetMs", s.offsetMs);
            c->setAttribute ("hasArrival", s.hasArrival);
            for (int k = 0; k < (int) s.polarity.size(); ++k)
            {
                const auto& pc = s.polarity[(size_t) k];
                if (! pc.done()) continue;
                auto* pe = c->createNewChildElement ("Polarity");
                pe->setAttribute ("cabinet", k);
                pe->setAttribute ("verdict", pc.verdict);
                pe->setAttribute ("own", pc.own);
                pe->setAttribute ("phase", pc.phaseDeg);
                pe->setAttribute ("peakInverted", pc.peakInverted);
                pe->setAttribute ("against", pc.against);
                pe->setAttribute ("relative", pc.relative);
                pe->setAttribute ("when", pc.when);
                pe->setAttribute ("mic", pc.mic);
                if (std::isfinite (pc.levelDb)) pe->setAttribute ("levelDb", pc.levelDb);
                if (pc.notClose) pe->setAttribute ("notClose", true);
                if (pc.atCrossover) pe->setAttribute ("atCrossover", true);
                if (pc.processorInverted) pe->setAttribute ("processorInverted", true);
            }
            c->setAttribute ("arrivalMs", s.arrivalMs);
            c->setAttribute ("nearField", s.nearField);
            if (s.trimDb != 0.0) c->setAttribute ("trimDb", s.trimDb);
            if (s.directivityQ > 0.0) c->setAttribute ("directivityQ", s.directivityQ);
            if (s.coverH > 0.0 && s.coverV > 0.0) { c->setAttribute ("coverH", s.coverH); c->setAttribute ("coverV", s.coverV); }
            if (s.hpProtective) c->setAttribute ("hpProtective", true);
            if (s.onboard.present)
            {
                auto* ob = c->createNewChildElement ("Onboard");
                ob->setAttribute ("lowPass", s.onboard.lowPass);
                ob->setAttribute ("type", s.onboard.type);
                ob->setAttribute ("slope", s.onboard.slope);
                juce::StringArray ch;
                for (double v : s.onboard.choices) ch.add (juce::String (v));
                ob->setAttribute ("choices", ch.joinIntoString ("/"));
                ob->setAttribute ("rangeLo", s.onboard.rangeLo);
                ob->setAttribute ("rangeHi", s.onboard.rangeHi);
                ob->setAttribute ("bypassable", s.onboard.bypassable);
                ob->setAttribute ("measuredAtHz", s.onboard.measuredAtHz);
                ob->setAttribute ("latencyMs", s.onboard.latencyMs);
                ob->setAttribute ("setHz", s.onboard.setHz);
            }
            if (s.minSafeHpHz > 0.0) { c->setAttribute ("minSafeHpHz", s.minSafeHpHz); c->setAttribute ("minSafeHpSlope", s.minSafeHpSlope); }
            if (s.rmsVoltLimit > 0.0) c->setAttribute ("rmsVoltLimit", s.rmsVoltLimit);
            if (s.ampGain.isNotEmpty()) c->setAttribute ("ampGain", s.ampGain);
            if (s.limiter.isNotEmpty()) c->setAttribute ("limiter", s.limiter);
            if (s.sheetEntered.isNotEmpty()) c->setAttribute ("sheetEntered", s.sheetEntered);
            if (s.groupSheetEntered.isNotEmpty()) c->setAttribute ("groupSheetEntered", s.groupSheetEntered);
            if (s.asFound.freq.size() > 2)
            {
                auto* af = c->createNewChildElement ("AsFound");
                af->setAttribute ("when", s.asFoundWhen);
                saveFilter (*af, "hp", s.asFoundProc.hp);
                saveFilter (*af, "lp", s.asFoundProc.lp);
                af->setAttribute ("gainDb", s.asFoundProc.gainDb);
                af->setAttribute ("delayMs", s.asFoundProc.delayMs);
                af->setAttribute ("invert", s.asFoundProc.invert);
                juce::String text;
                for (size_t i = 0; i < s.asFound.freq.size(); ++i)
                    text << juce::String (s.asFound.freq[i], 3) << ' ' << juce::String (s.asFound.db[i], 2) << '\n';
                af->addTextElement (text);
            }
            if (s.measureMic >= 0) c->setAttribute ("measureMic", s.measureMic);
            saveFilter (*c, "hp", s.proc.hp);
            saveFilter (*c, "lp", s.proc.lp);
            c->setAttribute ("gainDb", s.proc.gainDb);
            c->setAttribute ("delayMs", s.proc.delayMs);
            c->setAttribute ("invert", s.proc.invert);
            for (const auto& b : s.proc.eq)
            {
                auto* q = c->createNewChildElement ("Eq");
                q->setAttribute ("freq", b.freq);
                q->setAttribute ("gain", b.gainDb);
                q->setAttribute ("q", b.q);
            }
            if (s.hasResponse())
            {
                juce::String text;
                const bool ph = s.response.hasPhase(), co = s.response.extra.size() == s.response.freq.size();
                for (size_t i = 0; i < s.response.freq.size(); ++i)
                {
                    text << juce::String (s.response.freq[i], 3) << ' ' << juce::String (s.response.db[i], 3);
                    if (ph) text << ' ' << juce::String (s.response.phase[i], 2);
                    if (co) text << ' ' << juce::String (s.response.extra[i], 3);
                    text << '\n';
                }
                c->createNewChildElement ("Response")->addTextElement (text);
            }
        }
    }

    void load (const juce::XmlElement& parent)
    {
        const auto* e = parent.getChildByName ("SpeakerSystem");
        if (e == nullptr) { *this = defaults(); return; }
        *this = {};
        measureMic = juce::jlimit (0, kMaxMics - 1, e->getIntAttribute ("measureMic"));
        timingLocked = e->getBoolAttribute ("timingLocked");
        lockedDelayMs = e->getDoubleAttribute ("lockedDelayMs");
        xoHigh = e->getIntAttribute ("xoHigh", 0);
        xoLow = e->getIntAttribute ("xoLow", 1);
        xoChosen = e->getBoolAttribute ("xoChosen", false);   // earlier versions picked the pair by list position
        placement = juce::jlimit (0, 1, e->getIntAttribute ("placement", 1));   // earlier venues measured from one spot
        xoRelDelayMs = e->getDoubleAttribute ("xoRelDelayMs");
        groupStage = e->getBoolAttribute ("groupStage", false);
        opt.fMin = juce::jlimit (20.0, 2000.0, e->getDoubleAttribute ("fMin", 40.0));
        opt.fMax = juce::jlimit (opt.fMin, 5000.0, e->getDoubleAttribute ("fMax", 250.0));
        const auto types = e->getStringAttribute ("types", "0110"), slopes = e->getStringAttribute ("slopes", "01110101");
        for (int i = 0; i < 4 && i < types.length(); ++i) caps.types[i] = types[i] == '1';
        for (int i = 0; i < 8 && i < slopes.length(); ++i) caps.slopes[i] = slopes[i] == '1';
        caps.freqLo = juce::jlimit (1.0, 20000.0, e->getDoubleAttribute ("procFreqLo", 10.0));
        caps.freqHi = juce::jlimit (caps.freqLo, 40000.0, e->getDoubleAttribute ("procFreqHi", 20000.0));
        caps.freqStepHz = juce::jlimit (0.0, 100.0, e->getDoubleAttribute ("procFreqStepHz"));
        caps.freqStepOct = juce::jlimit (0.0, 1.0, e->getDoubleAttribute ("procFreqStepOct"));
        caps.delayMaxMs = juce::jlimit (0.0, 10000.0, e->getDoubleAttribute ("procDelayMaxMs"));
        caps.limiterUnit = juce::jlimit (0, 2, e->getIntAttribute ("procLimiterUnit"));
        caps.fullScaleDbu = juce::jlimit (-10.0, 40.0, e->getDoubleAttribute ("procFullScaleDbu", 20.0));
        outputHasHp.assign (kMaxOutputs, true);
        outputHasLp.assign (kMaxOutputs, true);
        for (const auto* oe : e->getChildWithTagNameIterator ("Output"))
        {
            const int o = oe->getIntAttribute ("n") - 1;
            if (o < 0 || o >= kMaxOutputs) continue;
            outputHasHp[(size_t) o] = oe->getBoolAttribute ("hp", true);
            outputHasLp[(size_t) o] = oe->getBoolAttribute ("lp", true);
        }

        for (const auto* c : e->getChildWithTagNameIterator ("Source"))
        {
            if ((int) sources.size() >= kMaxSources) break;
            SpeakerSource s;
            s.name = c->getStringAttribute ("name");
            s.role = juce::jlimit (0, 4, c->getIntAttribute ("role"));
            s.channel = c->getIntAttribute ("channel", 0);
            if (s.channel < 0 || s.channel >= kNumSpeakerChannels) s.channel = 0;   // the old "L+R" channel is mono
            s.model = c->getStringAttribute ("model");
            s.count = juce::jlimit (1, 16, c->getIntAttribute ("count", 1));
            s.linkTo = c->getIntAttribute ("linkTo", -1);
            s.output = juce::jlimit (0, 8, c->getIntAttribute ("output"));
            s.show = c->getBoolAttribute ("show", true);
            s.imported = c->getBoolAttribute ("imported");
            s.source = c->getStringAttribute ("from");
            s.delayUsedMs = c->getDoubleAttribute ("delayUsedMs");
            s.offsetMs = c->getDoubleAttribute ("offsetMs");
            s.hasArrival = c->getBoolAttribute ("hasArrival");
            s.arrivalMs = c->getDoubleAttribute ("arrivalMs");
            s.nearField = c->getBoolAttribute ("nearField");
            s.trimDb = juce::jlimit (-24.0, 24.0, c->getDoubleAttribute ("trimDb"));
            s.directivityQ = juce::jlimit (0.0, 50.0, c->getDoubleAttribute ("directivityQ"));
            s.coverH = juce::jlimit (0.0, 360.0, c->getDoubleAttribute ("coverH"));
            s.coverV = juce::jlimit (0.0, 360.0, c->getDoubleAttribute ("coverV"));
            s.hpProtective = c->getBoolAttribute ("hpProtective");
            if (const auto* ob = c->getChildByName ("Onboard"))
            {
                s.onboard.present = true;
                s.onboard.lowPass = ob->getBoolAttribute ("lowPass");
                s.onboard.type = juce::jlimit (0, 3, ob->getIntAttribute ("type", xo::Butterworth));
                s.onboard.slope = juce::jlimit (6, 48, ob->getIntAttribute ("slope", 24));
                juce::StringArray ch;
                ch.addTokens (ob->getStringAttribute ("choices"), "/", {});
                for (const auto& v : ch) if (v.getDoubleValue() > 0.0) s.onboard.choices.push_back (v.getDoubleValue());
                s.onboard.rangeLo = ob->getDoubleAttribute ("rangeLo");
                s.onboard.rangeHi = ob->getDoubleAttribute ("rangeHi");
                s.onboard.bypassable = ob->getBoolAttribute ("bypassable", true);
                s.onboard.measuredAtHz = ob->getDoubleAttribute ("measuredAtHz");
                s.onboard.latencyMs = ob->getDoubleAttribute ("latencyMs", -1.0);
                s.onboard.setHz = ob->getDoubleAttribute ("setHz");
            }
            s.minSafeHpHz = juce::jlimit (0.0, 2000.0, c->getDoubleAttribute ("minSafeHpHz"));
            s.minSafeHpSlope = juce::jlimit (6, 48, c->getIntAttribute ("minSafeHpSlope", 24));
            s.rmsVoltLimit = juce::jlimit (0.0, 1000.0, c->getDoubleAttribute ("rmsVoltLimit"));
            s.ampGain = c->getStringAttribute ("ampGain");
            s.limiter = c->getStringAttribute ("limiter");
            s.sheetEntered = c->getStringAttribute ("sheetEntered");
            s.groupSheetEntered = c->getStringAttribute ("groupSheetEntered");
            if (const auto* af = c->getChildByName ("AsFound"))
            {
                s.asFoundWhen = af->getStringAttribute ("when");
                s.asFoundProc.hp = loadFilter (*af, "hp");
                s.asFoundProc.lp = loadFilter (*af, "lp");
                s.asFoundProc.gainDb = af->getDoubleAttribute ("gainDb");
                s.asFoundProc.delayMs = af->getDoubleAttribute ("delayMs");
                s.asFoundProc.invert = af->getBoolAttribute ("invert");
                juce::StringArray lines;
                lines.addLines (af->getAllSubText());
                for (const auto& line : lines)
                {
                    juce::StringArray v;
                    v.addTokens (line, " ", {});
                    v.removeEmptyStrings();
                    if (v.size() < 2) continue;
                    s.asFound.freq.push_back (v[0].getDoubleValue());
                    s.asFound.db.push_back (v[1].getDoubleValue());
                }
            }
            for (const auto* pe : c->getChildWithTagNameIterator ("Polarity"))
            {
                const int k = pe->getIntAttribute ("cabinet", -1);
                if (k < 0 || k >= 16) continue;
                if ((int) s.polarity.size() <= k) s.polarity.resize ((size_t) k + 1);
                auto& pc = s.polarity[(size_t) k];
                pc.verdict = juce::jlimit (0, 4, pe->getIntAttribute ("verdict"));
                pc.own = juce::jlimit (0, 4, pe->getIntAttribute ("own"));
                pc.phaseDeg = pe->getDoubleAttribute ("phase");
                pc.peakInverted = pe->getBoolAttribute ("peakInverted");
                pc.against = pe->getIntAttribute ("against", -1);
                pc.relative = juce::jlimit (0, 4, pe->getIntAttribute ("relative"));
                pc.when = pe->getStringAttribute ("when");
                pc.mic = pe->getStringAttribute ("mic");
                pc.levelDb = pe->hasAttribute ("levelDb") ? pe->getDoubleAttribute ("levelDb") : NAN;
                pc.notClose = pe->getBoolAttribute ("notClose");
                pc.atCrossover = pe->getBoolAttribute ("atCrossover");
                pc.processorInverted = pe->getBoolAttribute ("processorInverted");
            }
            s.measureMic = juce::jlimit (-1, kMaxMics - 1, c->getIntAttribute ("measureMic", -1));
            s.proc.hp = loadFilter (*c, "hp");
            s.proc.lp = loadFilter (*c, "lp");
            s.proc.gainDb = c->getDoubleAttribute ("gainDb");
            s.proc.delayMs = c->getDoubleAttribute ("delayMs");
            s.proc.invert = c->getBoolAttribute ("invert");
            for (const auto* q : c->getChildWithTagNameIterator ("Eq"))
                s.proc.eq.push_back ({ juce::jlimit (10.0, 20000.0, q->getDoubleAttribute ("freq", 1000.0)),
                                       juce::jlimit (-24.0, 24.0, q->getDoubleAttribute ("gain")), juce::jlimit (0.1, 20.0, q->getDoubleAttribute ("q", 1.0)) });
            if (const auto* r = c->getChildByName ("Response"))
            {
                juce::StringArray lines;
                lines.addLines (r->getAllSubText());
                for (const auto& line : lines)
                {
                    juce::StringArray v;
                    v.addTokens (line, " ", {});
                    v.removeEmptyStrings();
                    if (v.size() < 2) continue;
                    s.response.freq.push_back (v[0].getDoubleValue());
                    s.response.db.push_back (v[1].getDoubleValue());
                    if (v.size() >= 3) s.response.phase.push_back (v[2].getDoubleValue());
                    if (v.size() >= 4) s.response.extra.push_back (v[3].getDoubleValue());
                }
                if (s.response.phase.size() != s.response.freq.size()) s.response.phase.clear();
                if (s.response.extra.size() != s.response.freq.size()) s.response.extra.clear();
            }
            sources.push_back (std::move (s));
        }

        for (const auto* c : e->getChildWithTagNameIterator ("Alignment"))
        {
            if ((int) alignments.size() >= kMaxAlignments) break;
            Alignment a;
            a.ref = c->getIntAttribute ("ref");
            a.align = c->getIntAttribute ("align", 1);
            a.refSet = c->getBoolAttribute ("refSet", false);       // earlier versions: single speakers only
            a.alignSet = c->getBoolAttribute ("alignSet", false);
            if (a.ref < 0 || a.align < 0 || a.ref >= (int) sources.size() || a.align >= (int) sources.size()) continue;
            a.spot = c->getStringAttribute ("spot");
            a.precedenceMs = juce::jlimit (0.0, 30.0, c->getDoubleAttribute ("precedenceMs"));
            a.hasRef = c->getBoolAttribute ("hasRef");
            a.hasAlign = c->getBoolAttribute ("hasAlign");
            a.refArrivalMs = c->getDoubleAttribute ("refArrivalMs");
            a.alignArrivalMs = c->getDoubleAttribute ("alignArrivalMs");
            a.refLevelDb = (float) c->getDoubleAttribute ("refLevelDb", -200.0);
            a.alignLevelDb = (float) c->getDoubleAttribute ("alignLevelDb", -200.0);
            a.measuredOn = c->getStringAttribute ("measuredOn");
            a.xoOffsetMs = c->getDoubleAttribute ("xoOffsetMs");
            a.phaseValid = c->getBoolAttribute ("phaseValid");
            a.phaseDelayMs = c->getDoubleAttribute ("phaseDelayMs");
            a.phaseInvert = c->getBoolAttribute ("phaseInvert", false);
            a.refInvertAtMeasure = c->getBoolAttribute ("refInvertAtMeasure", false);
            a.alignInvertAtMeasure = c->getBoolAttribute ("alignInvertAtMeasure", false);
            a.refMemberUsed = c->getIntAttribute ("refMemberUsed", -1);
            if (a.refMemberUsed >= (int) sources.size() || ! a.refSet) a.refMemberUsed = -1;
            a.refDelayAtMeasure = c->getDoubleAttribute ("refDelayAtMeasure");
            a.alignDelayAtMeasure = c->getDoubleAttribute ("alignDelayAtMeasure");
            a.checkFreq = c->getDoubleAttribute ("checkFreq");
            a.refBandDb = (float) c->getDoubleAttribute ("refBandDb", -200.0);
            a.alignBandDb = (float) c->getDoubleAttribute ("alignBandDb", -200.0);
            a.sumBandDb = (float) c->getDoubleAttribute ("sumBandDb", -200.0);
            a.checkedOn = c->getStringAttribute ("checkedOn");
            alignments.push_back (a);
        }
        ensurePair();   // an unset or invalid pair comes from the roles
    }

    // After source `i` is removed: drop its alignments and renumber the rest.
    void sourceRemoved (int i)
    {
        alignments.erase (std::remove_if (alignments.begin(), alignments.end(), [i] (const Alignment& a) { return a.ref == i || a.align == i; }),
                          alignments.end());
        for (auto& a : alignments) { if (a.ref > i) --a.ref; if (a.align > i) --a.align; }
        for (auto& s : sources) { if (s.linkTo == i) s.linkTo = -1; else if (s.linkTo > i) --s.linkTo; }
        // the pair: renumber, and pick it again from the roles if one of its speakers went
        if (xoHigh == i || xoLow == i) xoChosen = false;
        if (xoHigh > i) --xoHigh;
        if (xoLow > i) --xoLow;
        ensurePair();
    }
};
} // namespace la
