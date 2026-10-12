// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The speaker system of a venue: each source (a top, a sub, a fill ...), its response (measured with one mic, or
// imported from a file), the processing LiveAlign suggests for it, and the crossover between two of them.
// Saved in the venue file. Message thread only.
#include <vector>
#include <juce_core/juce_core.h>
#include "Analysis/Crossover.h"
#include "Analysis/FreqTable.h"

namespace la
{
enum class SpeakerRole { Main = 0, Sub, Fill, Delay, Other };

inline juce::StringArray speakerRoleNames() { return { "Main / top", "Sub", "Fill", "Delay", "Other" }; }

struct SpeakerSource
{
    juce::String name;
    int role = (int) SpeakerRole::Main;
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

struct SpeakerSystem
{
    static constexpr int kMaxSources = 8;

    std::vector<SpeakerSource> sources;
    int measureMic = 0;
    bool timingLocked = false;
    double lockedDelayMs = 0.0;    // mic delay every measurement uses, so arrivals stay comparable

    int xoHigh = 0, xoLow = 1;     // crossover pair (indices into sources)
    xo::Capabilities caps;
    xo::Options opt;
    xo::Result lastResult;         // shown until something changes

    static SpeakerSystem defaults()
    {
        SpeakerSystem s;
        SpeakerSource top, sub;
        top.name = "Main";
        top.role = (int) SpeakerRole::Main;
        top.output = 1;
        sub.name = "Sub";
        sub.role = (int) SpeakerRole::Sub;
        sub.output = 2;
        s.sources = { top, sub };
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

    bool pairValid() const
    {
        return xoHigh != xoLow && xoHigh >= 0 && xoLow >= 0 && xoHigh < (int) sources.size() && xoLow < (int) sources.size();
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
        e->setAttribute ("fMin", opt.fMin);
        e->setAttribute ("fMax", opt.fMax);
        juce::String types, slopes;
        for (bool b : caps.types) types << (b ? "1" : "0");
        for (bool b : caps.slopes) slopes << (b ? "1" : "0");
        e->setAttribute ("types", types);
        e->setAttribute ("slopes", slopes);

        for (const auto& s : sources)
        {
            auto* c = e->createNewChildElement ("Source");
            c->setAttribute ("name", s.name);
            c->setAttribute ("role", s.role);
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
    }
};
} // namespace la
