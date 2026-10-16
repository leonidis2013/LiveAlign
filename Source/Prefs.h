// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Preferences that used to be fixed in the code: tuning tolerances, generator safety, monitoring defaults, reports and
// files. Each has a definition (its category, label, choices and default). Settings holds this computer's values (in
// the preferences file next to the mic locker); a venue can override the tuning ones (saved with the venue), so a loud
// room or a speech-only chapel keeps its own. Everything defaults to what the earlier versions did.
#include <functional>
#include <map>
#include <vector>
#include <juce_core/juce_core.h>
#include "UpdateChecker.h"
#include "Analysis/TargetCurves.h"

namespace la
{
enum class PrefCat { Audio = 0, Measurement, Display, Units, Tuning, Generator, Monitoring, Reports, Updates };
constexpr int kNumPrefCats = 9;
inline const char* prefCatName (int c)
{
    static const char* n[] = { "Audio", "Measurement", "Display", "Units and environment", "Tuning defaults", "Generator and safety",
                               "Monitoring", "Reports and files", "Updates" };
    return n[juce::jlimit (0, kNumPrefCats - 1, c)];
}

struct PrefDef
{
    const char* key;
    PrefCat cat;
    const char* group;         // a heading inside the category
    const char* label;
    const char* help;
    double def;
    std::vector<double> values;          // the choices
    std::function<juce::String (double)> text;
    bool venue = false;                  // a venue can override it
};

namespace prefs
{
inline juce::String num (double v, int d, const char* unit) { return juce::String (v, d) + unit; }
inline std::vector<double> range (double lo, double hi, double step)
{
    std::vector<double> v;
    for (double x = lo; x <= hi + step * 0.01; x += step) v.push_back (std::round (x / step) * step);
    return v;
}

inline const std::vector<PrefDef>& all()
{
    static const std::vector<PrefDef> d = [] {
        std::vector<PrefDef> v;
        auto dB = [] (int dec) { return [dec] (double x) { return (x > 0.0 ? "+" : "") + juce::String (x, dec) + " dB"; }; };
        auto plainDb = [] (int dec) { return [dec] (double x) { return juce::String (x, dec) + " dB"; }; };
        auto onOff = [] (double x) { return juce::String (x > 0.5 ? "On" : "Off"); };
        // ---- tuning ----
        v.push_back ({ "eqTarget", PrefCat::Tuning, "Room EQ", "Target (new venues)", "The tonal balance a new venue's Room EQ starts with", 4.0,
                       range (0.0, (double) targets::count() - 1.0, 1.0), [] (double x) { return juce::String (targets::list()[(size_t) juce::jlimit (0, targets::count() - 1, (int) x)].name); }, false });
        v.push_back ({ "eqClose", PrefCat::Tuning, "Room EQ", "Close enough", "A check this close to the target (RMS) is ready to mark as tuned", 1.5,
                       range (0.5, 4.0, 0.5), plainDb (1), true });
        v.push_back ({ "eqCutQ", PrefCat::Tuning, "Room EQ", "Narrowest cut (Q)", "Room EQ is broad: a narrower cut chases one seat", 4.0,
                       range (1.0, 10.0, 0.5), [] (double x) { return "Q " + juce::String (x, 1); }, true });
        v.push_back ({ "eqBoostQ", PrefCat::Tuning, "Room EQ", "Narrowest boost (Q)", "Boosts are kept wider still", 2.0,
                       range (0.5, 4.0, 0.5), [] (double x) { return "Q " + juce::String (x, 1); }, true });
        v.push_back ({ "eqBands", PrefCat::Tuning, "Room EQ", "Bands (new venues)", "Free parametric bands in your processor; each venue sets its own on Room EQ", 8.0,
                       { 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0, 16.0, 31.0 }, [] (double x) { return juce::String ((int) x) + " bands"; }, false });
        v.push_back ({ "eqMaxBoost", PrefCat::Tuning, "Room EQ", "Largest boost (new venues)", "Each venue sets its own on Room EQ", 3.0,
                       { 0.0, 2.0, 3.0, 6.0 }, [] (double x) { return x < 0.5 ? juce::String ("Cuts only") : "Up to " + juce::String ((int) x) + " dB"; }, false });
        v.push_back ({ "eqMaxCut", PrefCat::Tuning, "Room EQ", "Deepest cut", "The deepest cut Room EQ suggests", 12.0,
                       range (3.0, 18.0, 1.0), plainDb (0), true });
        v.push_back ({ "roomAllow", PrefCat::Tuning, "Room EQ", "Allow for the room acoustics", "Room EQ uses the room acoustics (modes, reverb, noise) when they are measured", 1.0,
                       { 0.0, 1.0 }, onOff, true });
        v.push_back ({ "lrTol", PrefCat::Tuning, "Level balance", "Left and right within", "How closely the two sides of a pair should match at the centre", 1.0,
                       range (0.5, 3.0, 0.5), plainDb (1), true });
        v.push_back ({ "fillHi", PrefCat::Tuning, "Level balance", "Fill, loudest", "A fill against the mains where it covers", 0.0,
                       range (-6.0, 3.0, 0.5), dB (1), true });
        v.push_back ({ "fillLo", PrefCat::Tuning, "Level balance", "Fill, quietest", "A fill against the mains where it covers", -3.0,
                       range (-9.0, 0.0, 0.5), dB (1), true });
        v.push_back ({ "subTol", PrefCat::Tuning, "Level balance", "Sub within", "The sub against the tops, as the target's low end asks", 1.5,
                       range (0.5, 4.0, 0.5), plainDb (1), true });
        v.push_back ({ "trimCap", PrefCat::Tuning, "Level balance", "Largest trim", "Beyond this, check the aim and the amp rather than trim", 6.0,
                       range (1.0, 12.0, 1.0), plainDb (0), true });
        v.push_back ({ "coverageTol", PrefCat::Tuning, "Aim and coverage", "Coverage within", "How evenly each speaker should cover its area (plus or minus)", 3.0,
                       range (1.0, 6.0, 0.5), [] (double x) { return juce::String (juce::CharPointer_UTF8 ("\xc2\xb1")) + juce::String (x, 1) + " dB"; }, true });
        v.push_back ({ "targetDbA", PrefCat::Tuning, "Gain and headroom", "Target level at FOH", "The loudest the show should run at FOH (A-weighted, slow)", 95.0,
                       range (80.0, 110.0, 1.0), [] (double x) { return juce::String ((int) x) + " dBA"; }, true });
        v.push_back ({ "headroomDb", PrefCat::Tuning, "Gain and headroom", "Headroom wanted", "Above the target level before the system compresses or limits", 6.0,
                       range (3.0, 15.0, 1.0), plainDb (0), true });
        v.push_back ({ "closeUpCm", PrefCat::Tuning, "Speaker response", "Close-up distance", "How far from the cone the mic goes for Speaker response", 30.0,
                       range (10.0, 100.0, 5.0), [] (double x) { return juce::String (juce::roundToInt (x / 2.54)) + " in (" + juce::String ((int) x) + " cm)"; }, true });
        v.push_back ({ "speechDbA", PrefCat::Tuning, "Room acoustics", "Speech level at the listener", "For the STI with the noise taken into account", 70.0,
                       range (50.0, 90.0, 1.0), [] (double x) { return juce::String ((int) x) + " dBA"; }, true });
        v.push_back ({ "recheckClose", PrefCat::Tuning, "Quick re-check", "Close-up level change", "Flagged when a speaker measures this much louder or quieter close up", 2.0,
                       range (0.5, 6.0, 0.5), plainDb (1), true });
        v.push_back ({ "recheckFoh", PrefCat::Tuning, "Quick re-check", "Level change at FOH", "Against the level balance", 1.5,
                       range (0.5, 6.0, 0.5), plainDb (1), true });
        v.push_back ({ "recheckDelay", PrefCat::Tuning, "Quick re-check", "Delay change", "A top's arrival against its delay (a sub gets three times as much)", 0.5,
                       range (0.1, 2.0, 0.1), [] (double x) { return juce::String (x, 1) + " ms"; }, true });
        v.push_back ({ "recheckTonal", PrefCat::Tuning, "Quick re-check", "Tonal change", "The whole system against Room EQ (RMS)", 2.0,
                       range (1.0, 5.0, 0.5), plainDb (1), true });
        // ---- generator ----
        v.push_back ({ "genStartDb", PrefCat::Generator, "Generator", "Level at start", "The generator's level when LiveAlign starts (it always starts off)", -20.0,
                       range (-60.0, 0.0, 1.0), [] (double x) { return juce::String ((int) x) + " dBFS"; } });
        v.push_back ({ "genMaxDb", PrefCat::Generator, "Generator", "Never louder than", "A ceiling for the generator, whatever the level control says", 0.0,
                       range (-40.0, 0.0, 1.0), [] (double x) { return x > -0.5 ? juce::String ("0 dBFS (no limit)") : juce::String ((int) x) + " dBFS"; } });
        v.push_back ({ "genFadeMs", PrefCat::Generator, "Generator", "Fade in", "How gently the generator starts (it always stops quickly)", 20.0,
                       { 20.0, 250.0, 500.0, 1000.0, 2000.0 }, [] (double x) { return x < 25.0 ? juce::String ("Instant (20 ms)") : x < 1000.0 ? juce::String ((int) x) + " ms" : juce::String (juce::roundToInt (x / 1000.0)) + " s"; } });
        v.push_back ({ "sweepSec", PrefCat::Generator, "Room acoustics", "Sweep length", "Each of the two sweeps at a point; longer is cleaner in a noisy or very live room", 4.0,
                       range (2.0, 10.0, 1.0), [] (double x) { return juce::String ((int) x) + " s"; } });
        // ---- monitoring ----
        v.push_back ({ "eventLimit", PrefCat::Monitoring, "Event level", "Limit (new sessions)", "The sound level limit a new session starts with", 0.0,
                       [] { std::vector<double> l { 0.0 }; for (int x = 75; x <= 110; ++x) l.push_back (x); return l; }(),
                       [] (double x) { return x < 1.0 ? juce::String ("None") : juce::String ((int) x) + " dB"; } });
        v.push_back ({ "eventLimitC", PrefCat::Monitoring, "Event level", "Weighting", "A for loudness, C to include the bass", 0.0,
                       { 0.0, 1.0 }, [] (double x) { return juce::String (x > 0.5 ? "C (LCeq)" : "A (LAeq)"); } });
        v.push_back ({ "eventMinutes", PrefCat::Monitoring, "Event level", "Averaged over", "The limit's averaging window", 15.0,
                       { 1.0, 5.0, 10.0, 15.0, 30.0, 60.0 }, [] (double x) { return juce::String ((int) x) + " min"; } });
        v.push_back ({ "logSec", PrefCat::Monitoring, "Event level", "Log a point every", "The sound level log's step", 5.0,
                       { 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 }, [] (double x) { return juce::String ((int) x) + " s"; } });
        v.push_back ({ "fbSense", PrefCat::Monitoring, "Feedback watch", "Sensitivity", "Higher catches rings sooner but music can set it off", 1.0,
                       { 0.0, 1.0, 2.0 }, [] (double x) { return juce::String (x < 0.5 ? "Low" : x < 1.5 ? "Normal" : "High"); } });
        v.push_back ({ "trackSec", PrefCat::Monitoring, "Delay tracking", "Follow the delay every", "How often the mic's delay is found again on the programme", 5.0,
                       { 2.0, 5.0, 10.0, 20.0, 30.0 }, [] (double x) { return juce::String ((int) x) + " s"; } });
        // ---- reports and files ----
        v.push_back ({ "paperA4", PrefCat::Reports, "Reports", "Paper size", "For the PDF reports and the processor sheet", 0.0,
                       { 0.0, 1.0 }, [] (double x) { return juce::String (x > 0.5 ? "A4" : "Letter"); } });
        v.push_back ({ "autosaveMin", PrefCat::Reports, "Files", "Autosave every", "A recovery copy of the venue, in case LiveAlign closes unexpectedly", 2.0,
                       { 1.0, 2.0, 5.0, 10.0, 15.0 }, [] (double x) { return juce::String ((int) x) + " min"; } });
        return v;
    }();
    return d;
}

inline const PrefDef* find (const juce::String& key)
{
    for (const auto& p : all()) if (key == p.key) return &p;
    return nullptr;
}

inline double snap (const PrefDef& d, double v)
{
    double best = d.def, bd = 1e300;
    for (double x : d.values) if (std::abs (x - v) < bd) { bd = std::abs (x - v); best = x; }
    return best;
}
} // namespace prefs

// This computer's values (cached; written to the preferences file when changed).
class AppPrefs
{
public:
    static AppPrefs& get() { static AppPrefs p; return p; }

    double value (const juce::String& key) const
    {
        if (auto it = values.find (key); it != values.end()) return it->second;
        const auto* d = prefs::find (key);
        return d != nullptr ? d->def : 0.0;
    }
    void set (const juce::String& key, double v)
    {
        const auto* d = prefs::find (key);
        if (d == nullptr) return;
        values[key] = prefs::snap (*d, v);
        UserPrefs::set ("pref_" + key, juce::String (values[key]));
        // the fill's range stays the right way round
        if (key == "fillHi" && value ("fillLo") > values[key]) set ("fillLo", values[key]);
        if (key == "fillLo" && value ("fillHi") < values[key]) set ("fillHi", values[key]);
        ++serial;
    }
    void reset (const juce::String& key) { values.erase (key); UserPrefs::set ("pref_" + key, {}); ++serial; }
    bool isDefault (const juce::String& key) const { return values.find (key) == values.end(); }

    // Text preferences (report author, logo, folders).
    juce::String text (const juce::String& key) const { return UserPrefs::get (key); }
    void setText (const juce::String& key, const juce::String& v) { UserPrefs::set (key, v); ++serial; }

    int serial = 0;

private:
    AppPrefs()
    {
        if (auto x = juce::XmlDocument::parse (UserPrefs::file()))
            for (const auto& d : prefs::all())
            {
                const auto k = "pref_" + juce::String (d.key);
                if (x->hasAttribute (k) && x->getStringAttribute (k).isNotEmpty()) values[d.key] = prefs::snap (d, x->getDoubleAttribute (k));
            }
    }
    std::map<juce::String, double> values;
};

// A venue's overrides of the tuning preferences.
struct VenuePrefs
{
    std::map<juce::String, double> values;

    bool has (const juce::String& key) const { return values.find (key) != values.end(); }
    double value (const juce::String& key) const
    {
        if (auto it = values.find (key); it != values.end()) return it->second;
        return AppPrefs::get().value (key);
    }
    void set (const juce::String& key, double v)
    {
        const auto* d = prefs::find (key);
        if (d == nullptr || ! d->venue) return;
        values[key] = prefs::snap (*d, v);
        if (key == "fillHi" && value ("fillLo") > values[key]) set ("fillLo", values[key]);
        if (key == "fillLo" && value ("fillHi") < values[key]) set ("fillHi", values[key]);
    }
    void reset (const juce::String& key) { values.erase (key); }

    void save (juce::XmlElement& parent) const
    {
        if (values.empty()) return;
        auto* e = parent.createNewChildElement ("VenuePrefs");
        for (const auto& [k, v] : values) e->setAttribute (k, v);
    }
    void load (const juce::XmlElement& parent)
    {
        values.clear();
        if (const auto* e = parent.getChildByName ("VenuePrefs"))
            for (const auto& d : prefs::all())
                if (d.venue && e->hasAttribute (d.key)) values[d.key] = prefs::snap (d, e->getDoubleAttribute (d.key));
    }
    // The overrides in words, for the report.
    juce::StringArray describe() const
    {
        juce::StringArray out;
        for (const auto& [k, v] : values)
            if (const auto* d = prefs::find (k)) out.add (juce::String (d->label) + ": " + d->text (v) + " (default " + d->text (AppPrefs::get().value (k)) + ")");
        return out;
    }
};
} // namespace la
