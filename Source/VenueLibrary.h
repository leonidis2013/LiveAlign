// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The venues on this computer, for the list on Home: every venue file in the venues folder (Settings, Reports and
// files; else Documents/LiveAlign Venues) and the ones opened recently from anywhere else. Each shows its name, when it
// was saved and last used, how many sessions it has and whether it was marked tuned. Only the start of each file is
// read (the <Venue> element), so a big venue does not slow the list down.
#include <vector>
#include "UpdateChecker.h"

namespace la
{
struct VenueEntry
{
    juce::File file;
    juce::String name;
    juce::Time saved, lastUsed;
    int sessions = -1;   // -1: not known (a venue saved by an older version)
    int tuned = -1;      // -1: not known, 0: not tuned, 1: tuned
    bool inFolder = false;
};

namespace venues
{
inline juce::String extension() { return ".lavenue"; }

inline juce::File defaultFolder() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("LiveAlign Venues"); }

// Where venues are kept: the folder in Settings, else Documents/LiveAlign Venues (made when first needed).
inline juce::File folder (bool create = false)
{
    const juce::File set (UserPrefs::get ("venueFolder"));
    const auto f = set.isDirectory() ? set : defaultFolder();
    if (create && ! f.isDirectory()) f.createDirectory();
    return f;
}

// Recently opened venues, newest first: "time|path" lines.
inline std::vector<std::pair<juce::File, juce::Time>> recent()
{
    std::vector<std::pair<juce::File, juce::Time>> out;
    juce::StringArray lines;
    lines.addLines (UserPrefs::get ("recentVenues"));
    for (const auto& l : lines)
    {
        const auto t = l.upToFirstOccurrenceOf ("|", false, false), p = l.fromFirstOccurrenceOf ("|", false, false);
        if (p.isEmpty() || ! juce::File::isAbsolutePath (p)) continue;
        out.push_back ({ juce::File (p), juce::Time (t.getLargeIntValue()) });
    }
    return out;
}

inline void setRecent (const std::vector<std::pair<juce::File, juce::Time>>& list)
{
    juce::StringArray lines;
    for (size_t i = 0; i < list.size() && i < 30; ++i) lines.add (juce::String (list[i].second.toMilliseconds()) + "|" + list[i].first.getFullPathName());
    UserPrefs::set ("recentVenues", lines.joinIntoString ("\n"));
}

// A venue was opened or saved: it goes to the top of the recent list.
inline void noteUsed (const juce::File& f)
{
    if (f == juce::File()) return;
    auto list = recent();
    list.erase (std::remove_if (list.begin(), list.end(), [&f] (const auto& e) { return e.first == f; }), list.end());
    list.insert (list.begin(), { f, juce::Time::getCurrentTime() });
    setRecent (list);
}

inline void forget (const juce::File& f)
{
    auto list = recent();
    list.erase (std::remove_if (list.begin(), list.end(), [&f] (const auto& e) { return e.first == f; }), list.end());
    setRecent (list);
}

inline void renamed (const juce::File& from, const juce::File& to)
{
    auto list = recent();
    for (auto& e : list) if (e.first == from) e.first = to;
    setRecent (list);
}

// The <Venue> element at the start of a venue file, without reading the rest.
inline VenueEntry read (const juce::File& f)
{
    VenueEntry e;
    e.file = f;
    e.name = f.getFileNameWithoutExtension();
    e.saved = f.getLastModificationTime();
    juce::FileInputStream in (f);
    if (! in.openedOk()) return e;
    juce::MemoryBlock mb;
    in.readIntoMemoryBlock (mb, 256 * 1024);
    const auto text = mb.toString();
    const int start = text.indexOf ("<Venue ");
    if (start < 0) return e;
    const int end = text.indexOf (start, ">");
    if (end < 0) return e;
    auto tag = text.substring (start, end).trimEnd();
    if (! tag.endsWith ("/")) tag << "/";
    if (auto x = juce::XmlDocument::parse (tag + ">"))
    {
        if (x->getStringAttribute ("name").isNotEmpty()) e.name = x->getStringAttribute ("name");
        e.sessions = x->getIntAttribute ("sessions", -1);
        e.tuned = x->hasAttribute ("tuned") ? (x->getBoolAttribute ("tuned") ? 1 : 0) : -1;
        if (x->hasAttribute ("savedOn")) e.saved = juce::Time (x->getStringAttribute ("savedOn").getLargeIntValue());
    }
    return e;
}

// Every venue: the folder's (and one level of sub-folders) and the recent ones, sorted by last used, then last saved.
inline std::vector<VenueEntry> scan()
{
    std::vector<VenueEntry> out;
    const auto rec = recent();
    auto lastUsed = [&rec] (const juce::File& f)
    {
        for (const auto& r : rec) if (r.first == f) return r.second;
        return juce::Time();
    };
    juce::Array<juce::File> files;
    const auto dir = folder();
    if (dir.isDirectory())
        for (const auto& it : juce::RangedDirectoryIterator (dir, true, "*" + extension(), juce::File::findFiles))
            if (it.getFile().getParentDirectory() == dir || it.getFile().getParentDirectory().getParentDirectory() == dir) files.addIfNotAlreadyThere (it.getFile());
    const int inFolder = files.size();
    for (const auto& r : rec) if (r.first.existsAsFile()) files.addIfNotAlreadyThere (r.first);
    for (int i = 0; i < files.size(); ++i)
    {
        auto e = read (files[i]);
        e.inFolder = i < inFolder;
        e.lastUsed = lastUsed (files[i]);
        out.push_back (e);
    }
    std::sort (out.begin(), out.end(), [] (const VenueEntry& a, const VenueEntry& b)
    {
        const auto ka = a.lastUsed.toMilliseconds() > 0 ? a.lastUsed : a.saved, kb = b.lastUsed.toMilliseconds() > 0 ? b.lastUsed : b.saved;
        return ka > kb;
    });
    return out;
}

// A file name for a venue in a folder that does not clash with one already there.
inline juce::File freeFile (const juce::File& dir, const juce::String& name)
{
    return dir.getNonexistentChildFile (juce::File::createLegalFileName (name.isNotEmpty() ? name : juce::String ("Venue")), extension(), false);
}

// "3 h ago", "yesterday", "12 Oct"
inline juce::String when (juce::Time t)
{
    if (t.toMilliseconds() <= 0) return "-";
    const auto now = juce::Time::getCurrentTime();
    const double h = (now - t).inHours();
    if (h < 0.0) return t.formatted ("%d %b %Y");
    if (h < 1.0) return juce::String (juce::jmax (1, (int) ((now - t).inMinutes()))) + " min ago";
    if (h < 24.0) return juce::String ((int) h) + " h ago";
    if (h < 48.0) return "yesterday";
    return t.formatted (t.getYear() == now.getYear() ? "%d %b" : "%d %b %Y");
}
} // namespace venues
} // namespace la
