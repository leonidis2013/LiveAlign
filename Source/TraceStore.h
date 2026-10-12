// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Saved traces: snapshots of a transfer function or RTA kept for comparison. They live in memory and are saved
// with the app / plug-in state and inside venue project files. FRD files are used to exchange them with other software.
// Message thread only.
#include <algorithm>
#include <juce_core/juce_core.h>
#include "Analysis/FreqTable.h"

namespace la
{
struct Trace
{
    enum Kind { Transfer = 0, Rta = 1 };

    juce::String name;
    Kind kind = Transfer;
    int colour = 0;
    bool visible = true;
    juce::String created;
    FreqTable data;   // freq, db, phase (transfer), extra = coherence (transfer)
    juce::String fromSession;   // set for traces shown from another session for comparison (not saved)
};

class TraceStore
{
public:
    static juce::File userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("LiveAlign");
    }

    // Folder used by version 0.1 / 0.2 for trace files; read once so nothing is lost on upgrade.
    static juce::File legacyTraceFolder() { return userFolder().getChildFile ("Traces"); }

    void importLegacyFolder()
    {
        auto files = legacyTraceFolder().findChildFiles (juce::File::findFiles, false, "*.latrace");
        files.sort();
        for (const auto& f : files)
        {
            auto t = parseFreqTable (f.loadFileAsString().toStdString());
            if (t.empty()) continue;
            Trace tr;
            tr.name = juce::String (t.metaValue ("name", f.getFileNameWithoutExtension().toStdString()));
            tr.kind = t.metaValue ("kind") == "rta" ? Trace::Rta : Trace::Transfer;
            tr.colour = juce::String (t.metaValue ("colour", "0")).getIntValue();
            tr.visible = t.metaValue ("visible", "1") != "0";
            tr.created = juce::String (t.metaValue ("created"));
            t.meta.clear();
            tr.data = std::move (t);
            traces.push_back (std::move (tr));
        }
        ++version;
    }

    int add (Trace t)
    {
        t.colour = nextColour();
        if (t.created.isEmpty()) t.created = juce::Time::getCurrentTime().toString (true, true, false, true);
        traces.push_back (std::move (t));
        ++version;
        return (int) traces.size() - 1;
    }

    void remove (int i)
    {
        if (! valid (i)) return;
        traces.erase (traces.begin() + i);
        ++version;
    }

    void clear() { traces.clear(); ++version; }

    // Traces of another session, shown for comparison until removed. They are never saved.
    void addComparison (const juce::XmlElement& sessionBody, const juce::String& sessionName)
    {
        TraceStore other;
        if (! other.load (sessionBody)) return;
        for (auto t : other.traces)
        {
            t.fromSession = sessionName;
            t.name = sessionName + ": " + t.name;
            t.colour = nextColour();
            traces.push_back (std::move (t));
        }
        ++version;
    }

    void clearComparisons()
    {
        traces.erase (std::remove_if (traces.begin(), traces.end(), [] (const Trace& t) { return t.fromSession.isNotEmpty(); }),
                      traces.end());
        ++version;
    }

    bool hasComparisons() const
    {
        for (const auto& t : traces) if (t.fromSession.isNotEmpty()) return true;
        return false;
    }

    void rename (int i, const juce::String& newName)
    {
        if (! valid (i) || newName.trim().isEmpty()) return;
        traces[(size_t) i].name = newName.trim();
        ++version;
    }

    void setVisible (int i, bool v)
    {
        if (! valid (i)) return;
        traces[(size_t) i].visible = v;
        ++version;
    }

    // Plain FRD (freq, dB, phase) for other software.
    bool exportFrd (int i, const juce::File& dest) const
    {
        if (! valid (i)) return false;
        const auto& t = traces[(size_t) i];
        FreqTable copy = t.data;
        copy.meta.clear();
        copy.extra.clear();
        const std::vector<std::string> header {
            "LiveAlign " + std::string (t.kind == Trace::Rta ? "RTA" : "transfer function") + ": " + t.name.toStdString(),
            "Freq(Hz) Mag(dB) Phase(deg)" };
        return dest.replaceWithText (writeFrd (copy, header, false));
    }

    // FRD / TXT / CSV from other software, or a LiveAlign trace file.
    juce::String importFile (const juce::File& src)
    {
        auto t = parseFreqTable (src.loadFileAsString().toStdString());
        if (t.empty()) return "No frequency / dB data found in " + src.getFileName();
        Trace tr;
        tr.name = juce::String (t.metaValue ("name", src.getFileNameWithoutExtension().toStdString()));
        tr.kind = t.metaValue ("kind") == "rta" ? Trace::Rta : Trace::Transfer;
        t.meta.clear();
        tr.data = std::move (t);
        add (std::move (tr));
        return {};
    }

    void save (juce::XmlElement& parent) const
    {
        auto* list = parent.createNewChildElement ("Traces");
        for (const auto& t : traces)
        {
            if (t.fromSession.isNotEmpty()) continue;
            auto* e = list->createNewChildElement ("Trace");
            e->setAttribute ("name", t.name);
            e->setAttribute ("kind", t.kind == Trace::Rta ? "rta" : "transfer");
            e->setAttribute ("colour", t.colour);
            e->setAttribute ("visible", t.visible);
            e->setAttribute ("created", t.created);
            FreqTable copy = t.data;
            copy.meta.clear();
            e->addTextElement (juce::String (writeFrd (copy, {}, true)));
        }
    }

    // Replaces the traces with the ones in `parent`. Returns false when it has no <Traces> element.
    bool load (const juce::XmlElement& parent)
    {
        const auto* list = parent.getChildByName ("Traces");
        if (list == nullptr) return false;
        traces.clear();
        for (const auto* e : list->getChildWithTagNameIterator ("Trace"))
        {
            Trace t;
            t.name = e->getStringAttribute ("name", "Trace");
            t.kind = e->getStringAttribute ("kind") == "rta" ? Trace::Rta : Trace::Transfer;
            t.colour = e->getIntAttribute ("colour", 0);
            t.visible = e->getBoolAttribute ("visible", true);
            t.created = e->getStringAttribute ("created");
            t.data = parseFreqTable (e->getAllSubText().toStdString());
            if (! t.data.empty()) traces.push_back (std::move (t));
        }
        ++version;
        return true;
    }

    const std::vector<Trace>& all() const { return traces; }
    int size() const { return (int) traces.size(); }
    bool valid (int i) const { return i >= 0 && i < (int) traces.size(); }
    int getVersion() const { return version; }

private:
    int nextColour() const
    {
        int used[8] {};
        for (const auto& t : traces) ++used[((t.colour % 8) + 8) % 8];
        int best = 0;
        for (int c = 1; c < 8; ++c) if (used[c] < used[best]) best = c;
        return best;
    }

    std::vector<Trace> traces;
    int version = 0;
};
} // namespace la
