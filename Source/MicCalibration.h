// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// A measurement mic's calibration files: one for sound arriving on axis (0 degrees, the mic pointed at the speaker)
// and one for sound arriving from the side (90 degrees, the mic pointing up at the ceiling). Either may be missing.
#include <juce_core/juce_core.h>
#include "Analysis/FreqTable.h"

namespace la
{
enum CalAngle { Cal0 = 0, Cal90 = 1 };

inline juce::String calAngleName (int a) { return a == Cal90 ? juce::String (juce::CharPointer_UTF8 ("90\xc2\xb0")) : juce::String (juce::CharPointer_UTF8 ("0\xc2\xb0")); }
inline juce::String calAimText (int a) { return a == Cal90 ? "pointing up (" + calAngleName (Cal90) + ")" : "at the speaker (" + calAngleName (Cal0) + ")"; }

struct MicCalSet
{
    FreqTable table[2];
    juce::String name[2];

    bool has (int a) const { return ! table[a & 1].empty(); }
    bool any() const { return has (Cal0) || has (Cal90); }
    bool both() const { return has (Cal0) && has (Cal90); }
    void clear (int a) { table[a & 1] = {}; name[a & 1].clear(); }
    void clearAll() { clear (Cal0); clear (Cal90); }

    // The file to use for an aim: that angle's, else the other one (a mic with one file uses it either way).
    int pick (int aim) const
    {
        aim &= 1;
        if (has (aim)) return aim;
        if (has (1 - aim)) return 1 - aim;
        return -1;
    }

    // Short text for a table cell: the file name, or which angles are loaded.
    juce::String label() const
    {
        if (both()) return calAngleName (Cal0) + " + " + calAngleName (Cal90) + " files";
        if (has (Cal0)) return name[Cal0];
        if (has (Cal90)) return name[Cal90];
        return {};
    }

    juce::String names() const
    {
        juce::StringArray s;
        for (int a = 0; a < 2; ++a) if (has (a)) s.add (calAngleName (a) + ": " + name[a]);
        return s.joinIntoString (", ");
    }

    // Which angle a calibration file is for, from its name: miniDSP's "_90deg", "90 deg", "-90", "90°" ...
    static int angleFromName (const juce::String& fileName)
    {
        const auto n = fileName.toLowerCase().replace (juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")), "deg");
        for (const char* key : { "90deg", "90 deg", "90_deg", "90-deg", "_90", "-90", " 90", "(90", "90." , "vertical" })
            if (n.contains (key)) return Cal90;
        return Cal0;
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
