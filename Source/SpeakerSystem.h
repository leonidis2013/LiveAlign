// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The speaker system of a venue: each source (a top, a sub, a fill ...), its response (measured with one mic, or
// imported from a file), the processing LiveAlign suggests for it, and the crossover between two of them.
// Saved in the venue file. Message thread only.
#include <vector>
#include <juce_core/juce_core.h>
#include "Analysis/Crossover.h"
#include "Analysis/CrossoverPair.h"
#include "Analysis/FreqTable.h"

namespace la
{
enum class SpeakerRole { Main = 0, Sub, Fill, Delay, Other };

inline juce::StringArray speakerRoleNames() { return { "Main / top", "Sub", "Fill", "Delay", "Other" }; }

// Where a source sits in the mix: its processor output channel.
enum class SpeakerChannel { Mono = 0, Left, Centre, Right, Stereo };

inline juce::StringArray speakerChannelNames() { return { "Mono", "Left", "Centre", "Right", "Stereo (L+R)" }; }
inline juce::String speakerChannelShort (int c)
{
    static const char* n[] = { "M", "L", "C", "R", "L+R" };
    return n[juce::jlimit (0, 4, c)];
}

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

    xo::Processing proc;           // suggested settings for the speaker processor

    bool hasResponse() const { return response.freq.size() > 2; }
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

    bool measured() const { return hasRef && hasAlign; }
    // Delay to add to `align` (relative to how they were measured, both with no processor delay).
    double neededMs (double refProcessingMs) const { return refArrivalMs + refProcessingMs - alignArrivalMs + precedenceMs; }
};

struct SpeakerSystem
{
    static constexpr int kMaxSources = 8;

    std::vector<SpeakerSource> sources;
    int measureMic = 0;
    bool timingLocked = false;
    double lockedDelayMs = 0.0;    // mic delay every measurement uses, so arrivals stay comparable

    int xoHigh = 0, xoLow = 1;     // crossover pair (indices into sources)
    bool xoChosen = false;         // the user picked the pair; otherwise it follows the speakers' roles
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
        const double used = s.imported ? lockedDelayMs + s.offsetMs : s.delayUsedMs;
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

    // Measured itself, or follows a speaker that is (the other side of a stereo pair need not be measured).
    bool covered (int i) const
    {
        if (i < 0 || i >= (int) sources.size()) return false;
        return sources[(size_t) i].hasResponse() || sources[(size_t) master (i)].hasResponse();
    }

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

    void swapPair()
    {
        std::swap (xoHigh, xoLow);
        xoChosen = true;
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
            c->setAttribute ("arrivalMs", s.arrivalMs);
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
            s.channel = juce::jlimit (0, 4, c->getIntAttribute ("channel", 0));
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
