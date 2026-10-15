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

    bool done() const { return verdict != polarity::Unknown; }
};

struct SpeakerSource
{
    juce::String name;
    int role = (int) SpeakerRole::Main;
    int channel = (int) SpeakerChannel::Mono;
    juce::String model;            // e.g. "JBL CBT 70J"
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
    juce::String sheetEntered;     // the processor sheet line as it was when ticked "entered" (a change unticks it)

    // As found: the first measurement of this speaker, with the processing it had then (for the before / after view).
    FreqTable asFound;
    xo::Processing asFoundProc;
    juce::String asFoundWhen;

    bool hasResponse() const { return response.freq.size() > 2; }
    double totalGainDb() const { return proc.gainDb + trimDb; }
};

// Time alignment of one speaker (`align`) to another (`ref`) at a listening spot where their coverage meets:
// arrivals and levels measured there, and the delay that makes `align` arrive with `ref`.
struct Alignment
{
    int ref = 0, align = 1;
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
    double lockMs = 0.0;        // the mic delay both were measured with
    bool phaseValid = false;
    double phaseDelayMs = 0.0;
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

    // The delay of `align` (ms, may be negative) that makes the two transfer functions add up best between fLo and fHi.
    static bool phaseMatch (const FreqTable& a, const FreqTable& b, double fLo, double fHi, double& bestMs)
    {
        if (! a.hasPhase() || ! b.hasPhase()) return false;
        struct Bin { double w, ma, mb, pa, pb; };
        std::vector<Bin> bins;
        for (size_t i = 0; i < a.freq.size(); ++i)
        {
            const double f = a.freq[i];
            if (f < fLo || f > fHi) continue;
            if (a.extra.size() == a.freq.size() && a.extra[i] < 0.4) continue;
            const double db = b.dbAt (f), ph = b.phaseAt (f);
            if (! std::isfinite (db) || ! std::isfinite (ph)) continue;
            bins.push_back ({ f, std::pow (10.0, a.db[i] / 20.0), std::pow (10.0, db / 20.0), a.phase[i] * 3.14159265358979 / 180.0, ph * 3.14159265358979 / 180.0 });
        }
        if (bins.size() < 4) return false;
        double best = -1.0;
        for (double d = -20.0; d <= 20.0; d += 0.02)
        {
            double sum = 0.0;
            for (const auto& x : bins)
            {
                const double ph = x.pb - 2.0 * 3.14159265358979 * x.w * d * 0.001;
                const double re = x.ma * std::cos (x.pa) + x.mb * std::cos (ph), im = x.ma * std::sin (x.pa) + x.mb * std::sin (ph);
                sum += (re * re + im * im) / ((x.ma + x.mb) * (x.ma + x.mb) + 1e-30);
            }
            if (sum > best) { best = sum; bestMs = d; }
        }
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
    xo::Capabilities caps;
    xo::Options opt;
    xo::Result lastResult;         // shown until something changes
    std::vector<Alignment> alignments;
    static constexpr int kMaxAlignments = 8;

    static SpeakerSystem defaults()
    {
        SpeakerSystem s;
        SpeakerSource left, right, sub;
        left.name = "Main L";
        left.role = (int) SpeakerRole::Main;
        left.channel = (int) SpeakerChannel::Left;
        left.output = 1;
        right = left;
        right.name = "Main R";
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
        return xo::fromTable (s.response, used, lockedDelayMs, grid);
    }

    xo::Response processedResponse (int i, const std::vector<double>& grid) const
    {
        auto r = rawResponse (i, grid);
        if (! r.empty()) r = xo::processed (r, sources[(size_t) i].proc);
        return r;
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
            for (const auto& a : alignments) aligned = aligned || (a.align == i && a.measured());
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
            if (s.linkTo < 0 && ((s.proc.hp.active() && s.role != (int) SpeakerRole::Sub) || s.proc.lp.active()))
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
        if ((a == xoHigh && b == xoLow) || (a == xoLow && b == xoHigh))
        {
            const auto& f = sources[(size_t) xoHigh].proc.hp.active() ? sources[(size_t) xoHigh].proc.hp : sources[(size_t) xoLow].proc.lp;
            if (f.active()) return f.freq;
            return std::sqrt (opt.fMin * opt.fMax);
        }
        return 500.0;
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
        e->setAttribute ("fMin", opt.fMin);
        e->setAttribute ("fMax", opt.fMax);
        juce::String types, slopes;
        for (bool b : caps.types) types << (b ? "1" : "0");
        for (bool b : caps.slopes) slopes << (b ? "1" : "0");
        e->setAttribute ("types", types);
        e->setAttribute ("slopes", slopes);

        for (const auto& a : alignments)
        {
            auto* c = e->createNewChildElement ("Alignment");
            c->setAttribute ("ref", a.ref);
            c->setAttribute ("align", a.align);
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
            }
            c->setAttribute ("arrivalMs", s.arrivalMs);
            c->setAttribute ("nearField", s.nearField);
            if (s.trimDb != 0.0) c->setAttribute ("trimDb", s.trimDb);
            if (s.sheetEntered.isNotEmpty()) c->setAttribute ("sheetEntered", s.sheetEntered);
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
        opt.fMin = juce::jlimit (20.0, 2000.0, e->getDoubleAttribute ("fMin", 40.0));
        opt.fMax = juce::jlimit (opt.fMin, 5000.0, e->getDoubleAttribute ("fMax", 250.0));
        const auto types = e->getStringAttribute ("types", "0110"), slopes = e->getStringAttribute ("slopes", "01110101");
        for (int i = 0; i < 4 && i < types.length(); ++i) caps.types[i] = types[i] == '1';
        for (int i = 0; i < 8 && i < slopes.length(); ++i) caps.slopes[i] = slopes[i] == '1';

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
            s.sheetEntered = c->getStringAttribute ("sheetEntered");
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
