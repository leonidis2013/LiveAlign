// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The processor sheet: every setting LiveAlign worked out, output by output, in plain words to enter on any
// processor or console: the crossover filters, delay (ms and feet), polarity, level (with the level-balance trim)
// and the room EQ bands. Each output is ticked off once entered; a change to its settings unticks it.
#include <juce_core/juce_core.h>
#include "SpeakerSystem.h"

namespace la::sheet
{
inline juce::String typeWords (int type)
{
    switch (type)
    {
        case xo::Butterworth: return "Butterworth";
        case xo::LinkwitzRiley: return "Linkwitz-Riley";
        case xo::Bessel: return "Bessel";
        default: return "Off";
    }
}

// "Butterworth 12 dB/oct at 91 Hz"
inline juce::String filterWords (const xo::Filter& f)
{
    if (! f.active()) return "off";
    return typeWords (f.type) + " " + juce::String (f.slope) + " dB/oct at " + juce::String (juce::roundToInt (f.freq)) + " Hz";
}

inline juce::String delayWords (double ms)
{
    if (ms < 0.005) return "0 ms";
    return juce::String (ms, 2) + " ms  (" + juce::String (ms * 1.1255, 2) + " ft, " + juce::String (ms * 0.343, 2) + " m)";
}

inline juce::String gainWords (const SpeakerSource& s)
{
    const auto f1 = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 1); };
    juce::String t = f1 (s.totalGainDb()) + " dB";
    if (std::abs (s.trimDb) > 0.04) t << "  (" << f1 (s.proc.gainDb) << " crossover, " << f1 (s.trimDb) << " level trim)";
    return t;
}

inline juce::String eqWords (const std::vector<eq::Band>& eq)
{
    if (eq.empty()) return "none";
    juce::StringArray b;
    for (const auto& e : eq)
        b.add ((e.freq >= 1000.0 ? juce::String (e.freq / 1000.0, e.freq >= 10000.0 ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (e.freq)) + " Hz")
               + " " + (e.gainDb >= 0.0 ? "+" : "") + juce::String (e.gainDb, 1) + " dB Q " + juce::String (e.q, 2));
    return b.joinIntoString (", ");
}

inline juce::String outputWords (const SpeakerSource& s) { return s.output == 0 ? juce::String ("All outputs") : "Output " + juce::String (s.output); }

struct Row
{
    int spk = 0;
    juce::String name, output, hpf, lpf, delay, polarity, gain, eq;
};

inline Row row (const SpeakerSystem& sys, int i)
{
    Row r;
    if (i < 0 || i >= (int) sys.sources.size()) return r;
    const auto& s = sys.sources[(size_t) i];
    r.spk = i;
    r.name = sys.nameOf (i) + (s.count > 1 ? " (" + juce::String (s.count) + " cabinets)" : juce::String());
    r.output = outputWords (s);
    r.hpf = filterWords (s.proc.hp);
    r.lpf = filterWords (s.proc.lp);
    r.delay = delayWords (s.proc.delayMs);
    r.polarity = s.proc.invert ? "INVERTED" : "normal";
    r.gain = gainWords (s);
    r.eq = eqWords (s.proc.eq);
    return r;
}

// Everything on the line: ticked "entered" stays ticked only while this stays the same.
inline juce::String signature (const SpeakerSystem& sys, int i)
{
    const auto r = row (sys, i);
    return r.output + "|" + r.hpf + "|" + r.lpf + "|" + r.delay + "|" + r.polarity + "|" + r.gain + "|" + r.eq;
}

inline bool entered (const SpeakerSystem& sys, int i)
{
    return i >= 0 && i < (int) sys.sources.size() && sys.sources[(size_t) i].sheetEntered == signature (sys, i);
}

inline int enteredCount (const SpeakerSystem& sys)
{
    int n = 0;
    for (int i = 0; i < (int) sys.sources.size(); ++i) n += entered (sys, i) ? 1 : 0;
    return n;
}

inline juce::String csvField (juce::String v)
{
    if (v.containsAnyOf (",\"\n")) v = "\"" + v.replace ("\"", "\"\"") + "\"";
    return v;
}

inline juce::String csv (const SpeakerSystem& sys)
{
    juce::String out = "Speaker,Output,High-pass,Low-pass,Delay,Polarity,Gain,EQ,Entered\n";
    for (int i = 0; i < (int) sys.sources.size(); ++i)
    {
        const auto r = row (sys, i);
        out << csvField (r.name) << ',' << csvField (r.output) << ',' << csvField (r.hpf) << ',' << csvField (r.lpf) << ',' << csvField (r.delay) << ','
            << csvField (r.polarity) << ',' << csvField (r.gain) << ',' << csvField (r.eq) << ',' << (entered (sys, i) ? "yes" : "no") << '\n';
    }
    return out;
}

inline juce::String text (const SpeakerSystem& sys, const juce::String& venue, const juce::String& date)
{
    juce::String out = "LiveAlign processor settings - " + venue + " - " + date + "\n\n";
    for (int i = 0; i < (int) sys.sources.size(); ++i)
    {
        const auto r = row (sys, i);
        out << r.name << "  (" << r.output << ")\n"
            << "  High-pass: " << r.hpf << "\n  Low-pass: " << r.lpf << "\n  Delay: " << r.delay << "\n  Polarity: " << r.polarity
            << "\n  Gain: " << r.gain << "\n  EQ: " << r.eq << "\n\n";
    }
    return out;
}
} // namespace la::sheet
