#pragma once
// Saved traces: snapshots of a transfer function or RTA kept for comparison, stored as small text files
// (FRD-compatible, with coherence as a 4th column) in the user's LiveAlign folder. Message thread only.
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
    juce::File file;
};

class TraceStore
{
public:
    static juce::File userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                   .getChildFile ("LiveAlign");
    }

    static juce::File traceFolder() { return userFolder().getChildFile ("Traces"); }

    TraceStore() { reload(); }

    void reload()
    {
        traces.clear();
        auto files = traceFolder().findChildFiles (juce::File::findFiles, false, "*.latrace");
        files.sort();
        for (const auto& f : files)
        {
            auto t = parseFreqTable (f.loadFileAsString().toStdString());
            if (t.empty()) continue;
            Trace tr;
            tr.file = f;
            tr.name = juce::String (t.metaValue ("name", f.getFileNameWithoutExtension().toStdString()));
            tr.kind = t.metaValue ("kind") == "rta" ? Trace::Rta : Trace::Transfer;
            tr.colour = juce::String (t.metaValue ("colour", "0")).getIntValue();
            tr.visible = t.metaValue ("visible", "1") != "0";
            tr.created = juce::String (t.metaValue ("created"));
            tr.data = std::move (t);
            traces.push_back (std::move (tr));
        }
        ++version;
    }

    int add (Trace t)
    {
        t.colour = nextColour();
        if (t.created.isEmpty()) t.created = juce::Time::getCurrentTime().toString (true, true, false, true);
        t.file = uniqueFile (t.name);
        traces.push_back (std::move (t));
        save ((int) traces.size() - 1);
        ++version;
        return (int) traces.size() - 1;
    }

    void remove (int i)
    {
        if (! valid (i)) return;
        traces[(size_t) i].file.deleteFile();
        traces.erase (traces.begin() + i);
        ++version;
    }

    void rename (int i, const juce::String& newName)
    {
        if (! valid (i) || newName.trim().isEmpty()) return;
        traces[(size_t) i].name = newName.trim();
        save (i);
        ++version;
    }

    void setVisible (int i, bool v)
    {
        if (! valid (i)) return;
        traces[(size_t) i].visible = v;
        save (i);
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

    const std::vector<Trace>& all() const { return traces; }
    int size() const { return (int) traces.size(); }
    bool valid (int i) const { return i >= 0 && i < (int) traces.size(); }
    int getVersion() const { return version; }

private:
    void save (int i)
    {
        auto& t = traces[(size_t) i];
        FreqTable copy = t.data;
        copy.meta = { { "name", t.name.toStdString() },
                      { "kind", t.kind == Trace::Rta ? "rta" : "transfer" },
                      { "colour", std::to_string (t.colour) },
                      { "visible", t.visible ? "1" : "0" },
                      { "created", t.created.toStdString() } };
        traceFolder().createDirectory();
        t.file.replaceWithText (writeFrd (copy, { "LiveAlign trace", "Freq(Hz) Mag(dB) Phase(deg) Coherence" }, true));
    }

    juce::File uniqueFile (const juce::String& name) const
    {
        traceFolder().createDirectory();
        const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S");
        const auto safe = juce::File::createLegalFileName (name).substring (0, 40);
        return traceFolder().getNonexistentChildFile (stamp + " " + safe, ".latrace", false);
    }

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
