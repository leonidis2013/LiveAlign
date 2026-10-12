// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Venue and session data that is not an analysis setting. Message thread only.
//
// A venue file holds what stays with the room and the system (mics and their calibration, positions, the speaker
// setup) and a list of sessions. Each session is one visit: its own checks, delays and traces.
#include <array>
#include <memory>
#include <vector>
#include <juce_core/juce_core.h>
#include "Settings.h"

namespace la
{
// What belongs to the venue for each mic.
struct VenueMics
{
    std::array<juce::String, kMaxMics> name, position;
    std::array<bool, kMaxMics> noCalFile {};    // "this mic has no calibration file"
    bool noCalibrator = false;                  // levels stay in dBFS; no SPL calibration

    juce::String nameOf (int m) const { return name[(size_t) m].isNotEmpty() ? name[(size_t) m] : "Mic " + juce::String (m + 1); }

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("Mics");
        e->setAttribute ("noCalibrator", noCalibrator);
        for (int m = 0; m < kMaxMics; ++m)
        {
            auto* c = e->createNewChildElement ("Mic");
            c->setAttribute ("index", m);
            c->setAttribute ("name", name[(size_t) m]);
            c->setAttribute ("position", position[(size_t) m]);
            c->setAttribute ("noCalFile", noCalFile[(size_t) m]);
        }
    }

    void load (const juce::XmlElement& parent)
    {
        *this = {};
        if (const auto* e = parent.getChildByName ("Mics"))
        {
            noCalibrator = e->getBoolAttribute ("noCalibrator");
            for (const auto* c : e->getChildWithTagNameIterator ("Mic"))
            {
                const int m = c->getIntAttribute ("index", -1);
                if (m < 0 || m >= kMaxMics) continue;
                name[(size_t) m] = c->getStringAttribute ("name");
                position[(size_t) m] = c->getStringAttribute ("position");
                noCalFile[(size_t) m] = c->getBoolAttribute ("noCalFile");
            }
        }
    }
};

// The speaker setup is done once per venue and stays valid until something changes.
struct SpeakerStatus
{
    bool configured = false;
    juce::String configuredOn;
    bool changed = false;          // the user marked the system as changed since it was set up

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("Speakers");
        e->setAttribute ("configured", configured);
        e->setAttribute ("configuredOn", configuredOn);
        e->setAttribute ("changed", changed);
    }

    void load (const juce::XmlElement& parent)
    {
        *this = {};
        if (const auto* e = parent.getChildByName ("Speakers"))
        {
            configured = e->getBoolAttribute ("configured");
            configuredOn = e->getStringAttribute ("configuredOn");
            changed = e->getBoolAttribute ("changed");
        }
    }
};

// Checks that are redone (quickly) in every session.
struct SessionChecks
{
    std::array<bool, kMaxMics> splVerified {};   // calibrated or confirmed in this session

    bool loopbackSkipped = false;
    bool loopbackDone = false;
    double loopbackLatencyMs = 0.0;
    bool loopbackInverted = false;
    float loopbackLevelDbfs = -200.0f;

    bool noiseDone = false;
    int noiseMics = 0;
    std::array<float, kMaxMics> noiseA {}, noiseC {};
    std::array<bool, kMaxMics> noiseIsSpl {};

    juce::String setup;   // interface and wiring when the checks were done (see setupFingerprint)

    void save (juce::XmlElement& parent) const
    {
        auto* e = parent.createNewChildElement ("Checks");
        e->setAttribute ("loopbackSkipped", loopbackSkipped);
        e->setAttribute ("loopbackDone", loopbackDone);
        e->setAttribute ("loopbackLatencyMs", loopbackLatencyMs);
        e->setAttribute ("loopbackInverted", loopbackInverted);
        e->setAttribute ("loopbackLevelDbfs", loopbackLevelDbfs);
        e->setAttribute ("noiseDone", noiseDone);
        e->setAttribute ("noiseMics", noiseMics);
        e->setAttribute ("setup", setup);
        for (int m = 0; m < kMaxMics; ++m)
        {
            const juce::String k (m);
            e->setAttribute ("splVerified" + k, splVerified[(size_t) m]);
            e->setAttribute ("noiseA" + k, noiseA[(size_t) m]);
            e->setAttribute ("noiseC" + k, noiseC[(size_t) m]);
            e->setAttribute ("noiseIsSpl" + k, noiseIsSpl[(size_t) m]);
        }
    }

    void load (const juce::XmlElement& parent)
    {
        *this = {};
        if (const auto* e = parent.getChildByName ("Checks"))
        {
            loopbackSkipped = e->getBoolAttribute ("loopbackSkipped");
            loopbackDone = e->getBoolAttribute ("loopbackDone");
            loopbackLatencyMs = e->getDoubleAttribute ("loopbackLatencyMs");
            loopbackInverted = e->getBoolAttribute ("loopbackInverted");
            loopbackLevelDbfs = (float) e->getDoubleAttribute ("loopbackLevelDbfs", -200.0);
            noiseDone = e->getBoolAttribute ("noiseDone");
            noiseMics = e->getIntAttribute ("noiseMics");
            setup = e->getStringAttribute ("setup");
            for (int m = 0; m < kMaxMics; ++m)
            {
                const juce::String k (m);
                splVerified[(size_t) m] = e->getBoolAttribute ("splVerified" + k);
                noiseA[(size_t) m] = (float) e->getDoubleAttribute ("noiseA" + k, -200.0);
                noiseC[(size_t) m] = (float) e->getDoubleAttribute ("noiseC" + k, -200.0);
                noiseIsSpl[(size_t) m] = e->getBoolAttribute ("noiseIsSpl" + k);
            }
        }
        else if (const auto* old = parent.getChildByName ("MicSetup"))   // version 0.3
        {
            loopbackSkipped = old->getBoolAttribute ("loopbackSkipped");
            loopbackDone = old->getBoolAttribute ("loopbackDone");
            loopbackLatencyMs = old->getDoubleAttribute ("loopbackLatencyMs");
            loopbackInverted = old->getBoolAttribute ("loopbackInverted");
            loopbackLevelDbfs = (float) old->getDoubleAttribute ("loopbackLevelDbfs", -200.0);
            noiseDone = old->getBoolAttribute ("noiseDone");
            noiseMics = noiseDone ? 1 : 0;
            noiseA[0] = (float) old->getDoubleAttribute ("noiseA", -200.0);
            noiseC[0] = (float) old->getDoubleAttribute ("noiseC", -200.0);
            noiseIsSpl[0] = old->getBoolAttribute ("noiseIsSpl");
        }
    }
};

// Sessions other than the open one keep their contents as XML until they are opened.
struct SessionInfo
{
    juce::String id, name, created, notes;
    std::unique_ptr<juce::XmlElement> body;   // traces, checks, delays (null for the open session)

    SessionInfo() = default;
    SessionInfo (const SessionInfo& o) : id (o.id), name (o.name), created (o.created), notes (o.notes),
                                         body (o.body != nullptr ? std::make_unique<juce::XmlElement> (*o.body) : nullptr) {}
    SessionInfo& operator= (const SessionInfo& o)
    {
        id = o.id; name = o.name; created = o.created; notes = o.notes;
        body = o.body != nullptr ? std::make_unique<juce::XmlElement> (*o.body) : nullptr;
        return *this;
    }
    SessionInfo (SessionInfo&&) = default;
    SessionInfo& operator= (SessionInfo&&) = default;
};
} // namespace la
