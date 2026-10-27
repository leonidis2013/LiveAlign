// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <array>
#include <cstdlib>
#include <utility>
// A measurement mic's calibration files, one per angle of incidence: 0 degrees (sound arriving on axis, the mic
// pointed at the speaker), 90 degrees (from the side, the mic pointing up at the ceiling), and the in-between and
// rear angles some makers also supply (30, 45, 60, 180). Any of them may be missing.
#include <juce_core/juce_core.h>
#include "Analysis/FreqTable.h"

namespace la
{
// Slots for the angles. 0 and 90 keep their old numbers, so venue files and lockers from earlier versions still fit.
enum CalAngle { Cal0 = 0, Cal90 = 1, Cal30 = 2, Cal45 = 3, Cal60 = 4, Cal180 = 5 };
constexpr int kCalSlots = 6;

inline int calSlotDegrees (int a)
{
    static const int deg[kCalSlots] = { 0, 90, 30, 45, 60, 180 };
    return deg[juce::jlimit (0, kCalSlots - 1, a)];
}
// The slot for an angle in degrees: the nearest of the six.
inline int calSlotFromDegrees (int degrees)
{
    int best = 0, bestD = 1000;
    for (int a = 0; a < kCalSlots; ++a)
        if (std::abs (calSlotDegrees (a) - degrees) < bestD) { bestD = std::abs (calSlotDegrees (a) - degrees); best = a; }
    return best;
}
// The slots in angle order, for menus: 0, 30, 45, 60, 90, 180.
inline std::array<int, kCalSlots> calSlotsInOrder() { return { Cal0, Cal30, Cal45, Cal60, Cal90, Cal180 }; }

inline juce::String calAngleName (int a) { return juce::String (calSlotDegrees (a)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); }
inline juce::String calAimText (int a)
{
    switch (a)
    {
        case Cal0: return "at the speaker (" + calAngleName (a) + ")";
        case Cal90: return "pointing up (" + calAngleName (a) + ")";
        case Cal180: return "facing away (" + calAngleName (a) + ")";
        default: return "angled " + calAngleName (a) + " off the speaker";
    }
}

struct MicCalSet
{
    FreqTable table[kCalSlots];
    juce::String name[kCalSlots];
    bool guessed[kCalSlots] {};   // the file's name gave no angle: the angle was a guess, to check (not saved)

    bool has (int a) const { return a >= 0 && a < kCalSlots && ! table[a].empty(); }
    int count() const { int n = 0; for (int a = 0; a < kCalSlots; ++a) n += has (a) ? 1 : 0; return n; }
    bool any() const { return count() > 0; }
    bool both() const { return count() > 1; }   // more than one angle: the aim decides which is used
    void clear (int a) { if (a >= 0 && a < kCalSlots) { table[a] = {}; name[a].clear(); guessed[a] = false; } }
    // A file put at another angle: the two swap when the other angle has a file too.
    void moveTo (int from, int to)
    {
        if (from < 0 || to < 0 || from >= kCalSlots || to >= kCalSlots || from == to) return;
        std::swap (table[from], table[to]);
        std::swap (name[from], name[to]);
        std::swap (guessed[from], guessed[to]);
        guessed[to] = false;   // set by hand now
    }
    // The first free angle for a file whose name says none: 0, then 90, then the others.
    int firstFree (const bool* taken = nullptr) const
    {
        for (int a : { Cal0, Cal90, Cal30, Cal45, Cal60, Cal180 })
            if (! has (a) && (taken == nullptr || ! taken[a])) return a;
        return -1;
    }
    void clearAll() { for (int a = 0; a < kCalSlots; ++a) clear (a); }

    // The file to use for an aim: the loaded angle closest to it (a mic with one file uses it whatever the aim).
    int pick (int aim) const
    {
        const int want = calSlotDegrees (aim);
        int best = -1, bestD = 1000;
        for (int a = 0; a < kCalSlots; ++a)
            if (has (a) && std::abs (calSlotDegrees (a) - want) < bestD) { bestD = std::abs (calSlotDegrees (a) - want); best = a; }
        return best;
    }

    // Short text for a table cell: the file name, or which angles are loaded.
    juce::String label() const
    {
        if (count() > 1)
        {
            juce::StringArray s;
            for (int a : calSlotsInOrder()) if (has (a)) s.add (calAngleName (a));
            return s.joinIntoString (" + ") + " files";
        }
        for (int a = 0; a < kCalSlots; ++a) if (has (a)) return name[a];
        return {};
    }

    juce::String names() const
    {
        juce::StringArray s;
        for (int a : calSlotsInOrder()) if (has (a)) s.add (calAngleName (a) + ": " + name[a]);
        return s.joinIntoString (", ");
    }

    // Which angle a calibration file is for, from its name, as the makers write it: "_90deg", "90 deg", "-90", "90°",
    // "30deg" ... A number counts only on its own (not part of a serial number). Nothing found: 0 degrees.
    static int angleFromName (const juce::String& fileName) { const int a = angleInName (fileName); return a >= 0 ? a : Cal0; }

    // The angle the name gives, or -1 when it gives none ("on-axis" and "vertical" count).
    static int angleInName (const juce::String& fileName)
    {
        const auto n = fileName.toLowerCase().replace (juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), "deg");
        if (n.contains ("vertical")) return Cal90;
        if (n.contains ("on-axis") || n.contains ("onaxis") || n.contains ("on_axis") || n.contains ("horizontal")) return Cal0;
        for (int a : { Cal180, Cal90, Cal60, Cal45, Cal30 })
        {
            const juce::String num (calSlotDegrees (a));
            for (int from = n.indexOf (num); from >= 0; from = n.indexOf (from + 1, num))
            {
                const int end = from + num.length();
                const juce::juce_wchar before = from > 0 ? n[from - 1] : ' ', after = end < n.length() ? n[end] : ' ';
                if (juce::CharacterFunctions::isDigit (before) || juce::CharacterFunctions::isDigit (after)) continue;
                const bool marked = n.substring (end).startsWith ("deg") || n.substring (end).startsWith (" deg") || n.substring (end).startsWith ("_deg")
                                    || n.substring (end).startsWith ("-deg");
                const bool separated = before == '_' || before == '-' || before == ' ' || before == '(' || after == '.';
                if (marked || separated) return a;
            }
        }
        // "0deg", "_0deg", "0 deg", "_0." : the on-axis file, said in so many words
        for (int from = n.indexOf ("0"); from >= 0; from = n.indexOf (from + 1, "0"))
        {
            const juce::juce_wchar before = from > 0 ? n[from - 1] : ' ';
            if (juce::CharacterFunctions::isDigit (before) || juce::CharacterFunctions::isLetter (before)) continue;
            const auto rest = n.substring (from + 1);
            if (rest.startsWith ("deg") || rest.startsWith (" deg") || rest.startsWith ("_deg") || rest.startsWith ("-deg")) return Cal0;
        }
        return -1;
    }

    static void saveTable (juce::XmlElement& e, const FreqTable& t)
    {
        if (t.hasSensFactor) e.setAttribute ("sensFactorDb", t.sensFactorDb);
        FreqTable copy = t;
        copy.meta.clear();
        e.addTextElement (juce::String (writeFrd (copy, {}, false)));
    }

    static FreqTable loadTable (const juce::XmlElement& e)
    {
        auto t = parseFreqTable (e.getAllSubText().toStdString());
        if (e.hasAttribute ("sensFactorDb")) { t.hasSensFactor = true; t.sensFactorDb = e.getDoubleAttribute ("sensFactorDb"); }
        return t;
    }
};
} // namespace la
