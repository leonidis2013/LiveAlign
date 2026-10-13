// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The mic locker: the measurement mics you own, kept on this computer (not in a venue file). Each one remembers
// its calibration file and its last sound level calibration, so a mic assigned to a venue's mic table brings them
// along. Mics in a venue that came from the locker stay linked: a new calibration updates the locker too.
#include <algorithm>
#include <vector>
#include <juce_core/juce_core.h>
#include "Analysis/FreqTable.h"
#include "MicCalibration.h"
#include "TraceStore.h"

namespace la
{
struct LockerMic
{
    juce::String id, name, notes;
    juce::String model, serial;    // e.g. "UMIK-1", "7012345"
    MicCalSet cals;                // 0 and 90 degree calibration files (either may be missing)
    bool noCalFile = false;        // this mic has no file (treated as flat)
    bool hasSpl = false;           // sound level calibrated at least once
    float splOffset = 120.0f;      // dB SPL at 0 dBFS, with the interface gain used then
    juce::String splDate, savedOn;

    // What to call it: the name given, else model and serial number.
    juce::String label() const
    {
        if (name.isNotEmpty()) return name;
        juce::String s = model;
        if (serial.isNotEmpty()) s << (s.isNotEmpty() ? " #" : "#") << serial;
        return s.isNotEmpty() ? s : juce::String ("Unnamed mic");
    }

    // Model and serial for tags and reports ("UMIK-1 #7012345"), empty when neither is set.
    juce::String modelSerial() const
    {
        juce::String s = model;
        if (serial.isNotEmpty()) s << (s.isNotEmpty() ? " #" : "#") << serial;
        return s;
    }

    juce::String summary() const
    {
        juce::StringArray s;
        s.add (cals.any() ? cals.names() : noCalFile ? juce::String ("no cal file (flat)") : juce::String ("no cal file yet"));
        s.add (hasSpl ? "sound level " + splDate : juce::String ("sound level not calibrated"));
        return s.joinIntoString ("  -  ");
    }
};

class MicLocker
{
public:
    std::vector<LockerMic> mics;

    static juce::File file() { return TraceStore::userFolder().getChildFile ("MicLocker.xml"); }

    void load() { loadFrom (file()); }
    bool save() const { return saveTo (file()); }

    const LockerMic* find (const juce::String& id) const
    {
        for (const auto& m : mics) if (m.id == id && id.isNotEmpty()) return &m;
        return nullptr;
    }
    LockerMic* find (const juce::String& id)
    {
        for (auto& m : mics) if (m.id == id && id.isNotEmpty()) return &m;
        return nullptr;
    }

    void remove (const juce::String& id)
    {
        mics.erase (std::remove_if (mics.begin(), mics.end(), [&id] (const LockerMic& m) { return m.id == id; }), mics.end());
    }

    void loadFrom (const juce::File& f)
    {
        mics.clear();
        auto x = juce::XmlDocument::parse (f);
        if (x == nullptr) return;
        for (const auto* e : x->getChildWithTagNameIterator ("Mic"))
        {
            LockerMic m;
            m.id = e->getStringAttribute ("id");
            if (m.id.isEmpty()) continue;
            m.name = e->getStringAttribute ("name");
            m.notes = e->getStringAttribute ("notes");
            m.model = e->getStringAttribute ("model");
            m.serial = e->getStringAttribute ("serial");
            m.noCalFile = e->getBoolAttribute ("noCalFile");
            m.hasSpl = e->getBoolAttribute ("hasSpl");
            m.splOffset = (float) e->getDoubleAttribute ("splOffset", 120.0);
            m.splDate = e->getStringAttribute ("splDate");
            m.savedOn = e->getStringAttribute ("savedOn");
            for (const auto* c : e->getChildWithTagNameIterator ("Calibration"))
            {
                // format 1 had one file and kept its name on the mic
                const auto nm = c->getStringAttribute ("name", e->getStringAttribute ("calName"));
                const int a = c->hasAttribute ("angle") ? (c->getIntAttribute ("angle") == 90 ? Cal90 : Cal0) : MicCalSet::angleFromName (nm);
                m.cals.table[a] = MicCalSet::loadTable (*c);
                m.cals.name[a] = nm;
            }
            mics.push_back (std::move (m));
        }
    }

    bool saveTo (const juce::File& f) const
    {
        juce::XmlElement x ("LiveAlignMicLocker");
        x.setAttribute ("format", 2);
        for (const auto& m : mics)
        {
            auto* e = x.createNewChildElement ("Mic");
            e->setAttribute ("id", m.id);
            e->setAttribute ("name", m.name);
            e->setAttribute ("notes", m.notes);
            e->setAttribute ("model", m.model);
            e->setAttribute ("serial", m.serial);
            e->setAttribute ("noCalFile", m.noCalFile);
            e->setAttribute ("hasSpl", m.hasSpl);
            e->setAttribute ("splOffset", m.splOffset);
            e->setAttribute ("splDate", m.splDate);
            e->setAttribute ("savedOn", m.savedOn);
            for (int a = 0; a < 2; ++a)
            {
                if (! m.cals.has (a)) continue;
                auto* c = e->createNewChildElement ("Calibration");
                c->setAttribute ("angle", a == Cal90 ? 90 : 0);
                c->setAttribute ("name", m.cals.name[a]);
                MicCalSet::saveTable (*c, m.cals.table[a]);
            }
        }
        f.getParentDirectory().createDirectory();
        return x.writeTo (f);
    }
};
} // namespace la
