// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Session data for the later tuning steps and the report: the rings found by the feedback test, and the system
// "as found" (the processor settings before this session changed anything) for an as-found / as-left report.
#include <vector>
#include <juce_core/juce_core.h>
#include "Analysis/FeedbackDetector.h"
#include "SpeakerSystem.h"

namespace la
{
struct FeedbackTest
{
    struct Entry
    {
        fb::Ring ring;
        juce::String mic, time;     // which stage mic was being brought up, when
        bool applied = false;       // entered in the processor / console
    };
    std::vector<Entry> rings;
    juce::String micName { "Lectern mic" };
    int listenMic = 0;              // the measurement mic that listens
    bool done = false;

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("FeedbackTest");
        e->setAttribute ("micName", micName);
        e->setAttribute ("listenMic", listenMic);
        e->setAttribute ("done", done);
        for (const auto& r : rings)
        {
            auto* c = e->createNewChildElement ("Ring");
            c->setAttribute ("freq", r.ring.freq);
            c->setAttribute ("prominence", r.ring.prominenceDb);
            c->setAttribute ("level", r.ring.levelDb);
            c->setAttribute ("cut", r.ring.cutDb);
            c->setAttribute ("q", r.ring.q);
            c->setAttribute ("mic", r.mic);
            c->setAttribute ("time", r.time);
            c->setAttribute ("applied", r.applied);
        }
    }

    void load (const juce::XmlElement* parent)
    {
        *this = {};
        const auto* e = parent != nullptr ? parent->getChildByName ("FeedbackTest") : nullptr;
        if (e == nullptr) return;
        micName = e->getStringAttribute ("micName", "Lectern mic");
        listenMic = juce::jlimit (0, kMaxMics - 1, e->getIntAttribute ("listenMic"));
        done = e->getBoolAttribute ("done");
        for (const auto* c : e->getChildWithTagNameIterator ("Ring"))
        {
            Entry r;
            r.ring.freq = c->getDoubleAttribute ("freq");
            r.ring.prominenceDb = c->getDoubleAttribute ("prominence");
            r.ring.levelDb = c->getDoubleAttribute ("level");
            r.ring.cutDb = c->getDoubleAttribute ("cut", -6.0);
            r.ring.q = c->getDoubleAttribute ("q", 10.0);
            r.mic = c->getStringAttribute ("mic");
            r.time = c->getStringAttribute ("time");
            r.applied = c->getBoolAttribute ("applied");
            if (r.ring.freq > 0.0) rings.push_back (r);
        }
    }
};

// The speaker processing as LiveAlign found it at the start of this session's work.
struct AsFound
{
    struct Speaker { juce::String name; xo::Processing proc; };
    std::vector<Speaker> speakers;
    juce::String takenOn;

    bool taken() const { return takenOn.isNotEmpty(); }

    void take (const SpeakerSystem& sys, const juce::String& when)
    {
        speakers.clear();
        for (const auto& s : sys.sources) speakers.push_back ({ s.name, s.proc });
        takenOn = when;
    }

    void save (juce::XmlElement& parent) const
    {
        if (! taken()) return;
        auto* e = parent.createNewChildElement ("AsFound");
        e->setAttribute ("takenOn", takenOn);
        for (const auto& s : speakers)
        {
            auto* c = e->createNewChildElement ("Speaker");
            c->setAttribute ("name", s.name);
            SpeakerSystem::saveFilter (*c, "hp", s.proc.hp);
            SpeakerSystem::saveFilter (*c, "lp", s.proc.lp);
            c->setAttribute ("gainDb", s.proc.gainDb);
            c->setAttribute ("delayMs", s.proc.delayMs);
            c->setAttribute ("invert", s.proc.invert);
            c->setAttribute ("eqBands", (int) s.proc.eq.size());
        }
    }

    void load (const juce::XmlElement* parent)
    {
        *this = {};
        const auto* e = parent != nullptr ? parent->getChildByName ("AsFound") : nullptr;
        if (e == nullptr) return;
        takenOn = e->getStringAttribute ("takenOn");
        for (const auto* c : e->getChildWithTagNameIterator ("Speaker"))
        {
            Speaker s;
            s.name = c->getStringAttribute ("name");
            s.proc.hp = SpeakerSystem::loadFilter (*c, "hp");
            s.proc.lp = SpeakerSystem::loadFilter (*c, "lp");
            s.proc.gainDb = c->getDoubleAttribute ("gainDb");
            s.proc.delayMs = c->getDoubleAttribute ("delayMs");
            s.proc.invert = c->getBoolAttribute ("invert");
            s.proc.eq.resize ((size_t) juce::jlimit (0, 31, c->getIntAttribute ("eqBands")));
            speakers.push_back (s);
        }
    }
};
} // namespace la
