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

// The switch on the speaker itself, when it has a built-in filter: "its own low-pass at 100 Hz" / "bypassed".
inline juce::String onboardWords (const SpeakerSource& s)
{
    if (! s.onboard.present) return {};
    const juce::String what = s.onboard.lowPass ? "own low-pass" : "own high-pass";
    if (s.onboard.setHz <= 0.0) return what + ": bypassed";
    return what + " at " + juce::String (juce::roundToInt (s.onboard.setHz)) + " Hz" + (s.onboard.bypassable ? "" : " (always in)");
}

// The limiter threshold for this output from the speaker's voltage limit and the amp's gain, with the arithmetic:
// V limit -> dBu -> minus the amp's gain -> the processor's output level -> in the processor's units.
// Needs the amp gain as a number in dB (e.g. "+32 dB" or "32"); anything else leaves this empty.
inline juce::String limiterTarget (const SpeakerSystem& sys, const SpeakerSource& s)
{
    if (s.rmsVoltLimit <= 0.0) return {};
    const auto g = s.ampGain.trim();
    if (g.isEmpty() || ! g.containsAnyOf ("0123456789")) return {};
    const double ampDb = g.retainCharacters ("0123456789.-+").getDoubleValue();
    if (ampDb == 0.0 && ! g.containsChar ('0')) return {};
    const double vDbu = 20.0 * std::log10 (s.rmsVoltLimit / 0.7746);
    const double outDbu = vDbu - ampDb;
    juce::String t;
    t << juce::String (s.rmsVoltLimit, 1) << " V RMS = " << juce::String (vDbu, 1) << " dBu; minus the amp's " << (ampDb >= 0.0 ? "+" : "") << juce::String (ampDb, 1)
      << " dB = " << juce::String (outDbu, 1) << " dBu at the processor's output";
    if (sys.caps.limiterUnit == 1) t << " = " << juce::String (outDbu - sys.caps.fullScaleDbu, 1) << " dBFS (0 dBFS = " << juce::String (sys.caps.fullScaleDbu, 1) << " dBu)";
    else if (sys.caps.limiterUnit == 2) t << " = " << juce::String (0.7746 * std::pow (10.0, outDbu / 20.0), 2) << " V";
    t << ": set the limiter there (RMS), or a little under";
    return t;
}

struct Row
{
    int spk = 0;
    bool group = false;          // a group's line (processor with a group stage): its filters, level, polarity and EQ
    bool member = false;         // an output whose filters, level, polarity and EQ come from its group's line
    juce::String name, output, hpf, lpf, delay, polarity, gain, eq;
    juce::String onboard;        // the switch on the speaker itself (its built-in filter), when it has one
    juce::String amp, limiter;   // as set (for the record: not part of what is ticked as entered)
    juce::String limiterTarget;  // worked out from the voltage limit and the amp gain, with the arithmetic
};

// On a processor with a group stage, a speaker in a group takes most of its settings from the group's line.
inline bool memberLine (const SpeakerSystem& sys, int i) { return sys.groupStage && sys.inGroup (i); }

inline Row row (const SpeakerSystem& sys, int i)
{
    Row r;
    if (i < 0 || i >= (int) sys.sources.size()) return r;
    const auto& s = sys.sources[(size_t) i];
    r.spk = i;
    r.name = sys.nameOf (i) + (s.count > 1 ? " (" + juce::String (s.count) + " cabinets)" : juce::String());
    r.output = outputWords (s);
    r.delay = delayWords (s.proc.delayMs);
    r.amp = s.ampGain;
    r.limiter = s.limiter;
    r.onboard = onboardWords (s);
    r.limiterTarget = limiterTarget (sys, s);
    if (memberLine (sys, i))
    {
        const auto f1 = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 1); };
        r.member = true;
        r.hpf = r.lpf = r.polarity = "(group)";
        r.gain = std::abs (s.trimDb) > 0.04 ? f1 (s.trimDb) + " dB trim" : juce::String ("0 dB");
        r.eq = "none";
        return r;
    }
    r.hpf = filterWords (s.proc.hp);
    r.lpf = filterWords (s.proc.lp);
    r.polarity = s.proc.invert ? "INVERTED" : "normal";
    r.gain = gainWords (s);
    r.eq = eqWords (s.proc.eq);
    return r;
}

// A group's line: what is entered once on the group (its lead's settings, which every member shares).
inline Row groupRow (const SpeakerSystem& sys, int lead)
{
    Row r;
    if (lead < 0 || lead >= (int) sys.sources.size()) return r;
    const auto& s = sys.sources[(size_t) lead];
    const auto f1 = [] (double v) { return (v >= 0.0 ? "+" : "") + juce::String (v, 1); };
    r.spk = lead;
    r.group = true;
    r.name = sys.groupName (lead) + " group";
    r.output = sys.groupOutputs (lead);
    r.hpf = filterWords (s.proc.hp);
    r.lpf = filterWords (s.proc.lp);
    r.delay = "on each output";
    r.polarity = s.proc.invert ? "INVERTED" : "normal";
    r.gain = f1 (s.proc.gainDb) + " dB";
    r.eq = eqWords (s.proc.eq);
    return r;
}

// The sheet's lines in order. Outputs only: one per speaker. With a group stage: each group's line, then its outputs.
struct Line { bool group = false; int spk = 0; };
inline std::vector<Line> lines (const SpeakerSystem& sys)
{
    std::vector<Line> out;
    const int n = (int) sys.sources.size();
    if (! sys.groupStage) { for (int i = 0; i < n; ++i) out.push_back ({ false, i }); return out; }
    for (int i = 0; i < n; ++i)
    {
        if (sys.master (i) != i) continue;
        if (sys.inGroup (i))
        {
            out.push_back ({ true, i });
            for (int k : sys.groupOf (i)) out.push_back ({ false, k });
        }
        else out.push_back ({ false, i });
    }
    for (int i = 0; i < n; ++i)   // anything not reached (a broken link): its own line
    {
        bool seen = false;
        for (const auto& l : out) seen = seen || (! l.group && l.spk == i);
        if (! seen) out.push_back ({ false, i });
    }
    return out;
}
inline Row rowOf (const SpeakerSystem& sys, const Line& l) { return l.group ? groupRow (sys, l.spk) : row (sys, l.spk); }

inline juce::String signature (const Row& r) { return r.output + "|" + r.hpf + "|" + r.lpf + "|" + r.delay + "|" + r.polarity + "|" + r.gain + "|" + r.eq + "|" + r.onboard; }
inline juce::String signature (const SpeakerSystem& sys, int i) { return signature (row (sys, i)); }

inline bool entered (const SpeakerSystem& sys, int i)
{
    return i >= 0 && i < (int) sys.sources.size() && sys.sources[(size_t) i].sheetEntered == signature (sys, i);
}
inline bool entered (const SpeakerSystem& sys, const Line& l)
{
    if (l.spk < 0 || l.spk >= (int) sys.sources.size()) return false;
    return l.group ? sys.sources[(size_t) l.spk].groupSheetEntered == signature (groupRow (sys, l.spk)) : entered (sys, l.spk);
}
inline void setEntered (SpeakerSystem& sys, const Line& l, bool on)
{
    if (l.spk < 0 || l.spk >= (int) sys.sources.size()) return;
    auto& s = sys.sources[(size_t) l.spk];
    if (l.group) s.groupSheetEntered = on ? signature (groupRow (sys, l.spk)) : juce::String();
    else s.sheetEntered = on ? signature (sys, l.spk) : juce::String();
}

// Has settings to enter (anything but a flat, untouched output).
inline bool hasSettings (const SpeakerSystem& sys, int i)
{
    if (i < 0 || i >= (int) sys.sources.size()) return false;
    const auto& s = sys.sources[(size_t) i];
    if (memberLine (sys, i)) return s.proc.delayMs > 0.005 || std::abs (s.trimDb) > 0.04;
    return s.proc.hp.active() || s.proc.lp.active() || s.proc.delayMs > 0.005 || s.proc.invert || std::abs (s.totalGainDb()) > 0.04 || ! s.proc.eq.empty();
}
inline bool hasSettings (const SpeakerSystem& sys, const Line& l)
{
    if (! l.group) return hasSettings (sys, l.spk);
    if (l.spk < 0 || l.spk >= (int) sys.sources.size()) return false;
    const auto& p = sys.sources[(size_t) l.spk].proc;
    return p.hp.active() || p.lp.active() || p.invert || std::abs (p.gainDb) > 0.04 || ! p.eq.empty();
}

// The lines whose settings changed and are not ticked as entered yet: what to enter before the next measurement.
inline juce::StringArray pending (const SpeakerSystem& sys)
{
    juce::StringArray out;
    for (const auto& l : lines (sys))
    {
        if (! sys.groupStage && sys.master (l.spk) != l.spk) continue;   // outputs only: a group member follows its lead
        if (hasSettings (sys, l) && ! entered (sys, l)) out.add (l.group ? groupRow (sys, l.spk).name : sys.nameOf (l.spk));
    }
    return out;
}

// Ticks every line as entered (the "They're entered" checkpoint).
inline void tickAll (SpeakerSystem& sys)
{
    for (const auto& l : lines (sys)) setEntered (sys, l, true);
}

inline int lineCount (const SpeakerSystem& sys) { return (int) lines (sys).size(); }
inline int enteredCount (const SpeakerSystem& sys)
{
    int n = 0;
    for (const auto& l : lines (sys)) n += entered (sys, l) ? 1 : 0;
    return n;
}

inline juce::String csvField (juce::String v)
{
    if (v.containsAnyOf (",\"\n")) v = "\"" + v.replace ("\"", "\"\"") + "\"";
    return v;
}

inline juce::String csv (const SpeakerSystem& sys)
{
    juce::String out = "Speaker,Output,High-pass,Low-pass,Delay,Polarity,Gain,EQ,On the speaker,Amp gain,Limiter,Limiter target,Entered\n";
    for (const auto& l : lines (sys))
    {
        const auto r = rowOf (sys, l);
        out << csvField (r.name) << ',' << csvField (r.output) << ',' << csvField (r.hpf) << ',' << csvField (r.lpf) << ',' << csvField (r.delay) << ','
            << csvField (r.polarity) << ',' << csvField (r.gain) << ',' << csvField (r.eq) << ',' << csvField (r.onboard) << ',' << csvField (r.amp) << ',' << csvField (r.limiter) << ','
            << csvField (r.limiterTarget) << ',' << (entered (sys, l) ? "yes" : "no") << '\n';
    }
    return out;
}

inline juce::String text (const SpeakerSystem& sys, const juce::String& venue, const juce::String& date)
{
    juce::String out = "LiveAlign processor settings - " + venue + " - " + date + "\n";
    out << (sys.groupStage ? "Processor with a group stage: each group's line once, then each output's delay and trim.\n\n" : "\n");
    for (const auto& l : lines (sys))
    {
        const auto r = rowOf (sys, l);
        out << (r.member ? "  " : "") << r.name << "  (" << r.output << ")\n";
        const juce::String in = r.member ? "    " : "  ";
        if (! r.member) out << in << "High-pass: " << r.hpf << "\n" << in << "Low-pass: " << r.lpf << "\n";
        if (! r.group) out << in << "Delay: " << r.delay << "\n";
        if (! r.member) out << in << "Polarity: " << r.polarity << "\n";
        out << in << "Gain: " << r.gain;
        if (! r.member) out << "\n" << in << "EQ: " << r.eq;
        if (r.onboard.isNotEmpty()) out << "\n" << in << "On the speaker: " << r.onboard;
        if (r.amp.isNotEmpty()) out << "\n" << in << "Amp gain: " << r.amp;
        if (r.limiter.isNotEmpty()) out << "\n" << in << "Limiter: " << r.limiter;
        if (r.limiterTarget.isNotEmpty()) out << "\n" << in << "Limiter target: " << r.limiterTarget;
        out << "\n\n";
    }
    return out;
}
} // namespace la::sheet
