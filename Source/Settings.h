#pragma once
// Everything the user sets, shared between the audio thread, the analysis thread and the UI.
// Plain atomics: the audio thread only reads them. Saved with the plug-in / app state.
#include <atomic>
#include <cmath>
#include <juce_core/juce_core.h>

namespace la
{
enum class View { Rta = 0, Transfer = 1, Impulse = 2, Waterfall = 3, Level = 4 };

constexpr int kMaxMics = 8;
constexpr int kAverageMic = kMaxMics;   // "average of all mics" in the mic picker

struct Settings
{
    // Measurement mics. Channels are 0-based input/output indices of the app / plug-in.
    std::atomic<int> micCount { 1 };
    std::atomic<int> micInput[kMaxMics] { { 0 }, { 2 }, { 3 }, { 4 }, { 5 }, { 6 }, { 7 }, { 8 } };
    std::atomic<float> micDelayMs[kMaxMics] {};           // reference delay compensation for each mic
    std::atomic<bool> micSplCalibrated[kMaxMics] {};
    std::atomic<float> micSplOffset[kMaxMics] { { 120.0f }, { 120.0f }, { 120.0f }, { 120.0f }, { 120.0f }, { 120.0f }, { 120.0f }, { 120.0f } };
    std::atomic<int> selectedMic { 0 };                    // shown on the Monitoring tab; kAverageMic = average
    std::atomic<int> refSource { 3 };        // 0 = off, 1 = generator (internal copy), 2.. = input (refSource - 2); default input 2
    std::atomic<int> genOutput { 0 };        // 0 = all outputs, 1.. = output (genOutput - 1)
    // Listening (headphones), this computer's: what to hear (0 off, 1 the reference, 2.. mic (n - 2)), where (an output of
    // the measurement interface, 0-based, or -1 another device), its level and mute.
    std::atomic<int> listenSource { 0 };
    std::atomic<int> listenChannel { -1 };
    std::atomic<float> listenGainDb { -12.0f };
    std::atomic<bool> listenMute { false };

    // Signal generator
    std::atomic<bool> genOn { false };
    std::atomic<bool> genLocked { false };   // a monitoring session: the generator never plays (nothing can reach the PA mid-show)
    std::atomic<int> genType { 0 };          // la::GenType
    std::atomic<float> genLevelDb { -20.0f };
    std::atomic<float> genFreq { 1000.0f };
    std::atomic<float> genSweepSec { 3.0f };
    std::atomic<float> genMaxDb { 0.0f };    // the generator never goes above this (Settings, Generator and safety)
    std::atomic<float> genFadeMs { 20.0f };  // switching on takes this long

    // Transfer function
    std::atomic<int> fftSize { 32768 };
    std::atomic<bool> multiWindow { false };     // multi-time-window transfer function (MTW)
    std::atomic<float> magThresholdDb { -90.0f }; // magnitude threshold (0 = off)
    std::atomic<int> averages { 8 };
    std::atomic<int> smoothing { 24 };       // 1/N octave
    std::atomic<float> coherenceBlank { 0.4f };   // hide transfer points below this coherence (0 = off)
    std::atomic<float> magRange { 20.0f };   // +/- dB shown

    // RTA
    std::atomic<int> rtaBands { 3 };         // 1/N octave
    std::atomic<int> rtaSpeed { 1 };         // 0 fast, 1 medium, 2 slow
    std::atomic<bool> rtaShowRef { false };
    std::atomic<bool> rtaPeakHold { false };

    std::atomic<float> calibratorDb { 94.0f };

    // Display
    std::atomic<bool> running { true };
    std::atomic<int> view { (int) View::Transfer };   // view of graph 1 (keys 1, 2, 3)
    std::atomic<int> paneCount { 3 };        // graphs on screen, 1 to 3
    std::atomic<int> paneLayout { 0 };       // 0 stacked (rows), 1 side by side (columns)
    std::atomic<int> paneView2 { (int) View::Rta }, paneView3 { (int) View::Waterfall };
    std::atomic<int> targetCurve { 0 };      // la::targets index, 0 = none
    // The target is lined up with the live trace once (when it is chosen) and then stays put; Re-level does it again.
    std::atomic<int> targetAnchorMask { 0 };          // bit 0: transfer function, bit 1: RTA
    std::atomic<float> targetTfLevelDb { 0.0f }, targetRtaLevelDb { 0.0f };
    std::atomic<int> currentTab { 0 };       // page shown (la::ui::Page)
    std::atomic<int> theme { 0 };            // 0 dark, 1 light
    std::atomic<float> temperatureC { 20.0f };
    std::atomic<float> humidity { 50.0f };   // relative humidity (%), for the air absorption
    std::atomic<int> distanceUnit { 0 };     // 0 feet, 1 metres
    std::atomic<int> temperatureUnit { 1 };  // 0 Celsius, 1 Fahrenheit
    std::atomic<int> irMode { 0 };           // 0 linear, 1 log (ETC)
    std::atomic<int> irRange { 1 };          // 0 = 20 ms, 1 = 100 ms, 2 = 500 ms
    std::atomic<int> wfSeconds { 1 };        // waterfall history: 0 = 10 s, 1 = 30 s, 2 = 60 s
    std::atomic<int> wfRange { 1 };          // waterfall colour range: 0 = 40 dB, 1 = 60 dB, 2 = 80 dB

    // One-shot requests from the UI to the analysis thread
    std::atomic<bool> requestResetAverages { false };
    std::atomic<bool> requestFindDelay { false };
    std::atomic<bool> requestResetLeq { false };
    std::atomic<bool> requestClearPeak { false };
    std::atomic<bool> captureAllIr { false };     // put every mic's impulse response in the snapshot (room acoustics)

    // The mic whose delay / calibration the single-mic controls act on (the average uses mic 1).
    int focusMic() const { const int m = selectedMic.load(); return m >= kAverageMic || m >= micCount.load() ? 0 : juce::jmax (0, m); }
    std::atomic<float>& delayMs() { return micDelayMs[focusMic()]; }
    float delayMsValue() const { return micDelayMs[focusMic()].load(); }
    bool splCalibrated() const { return micSplCalibrated[focusMic()].load(); }

    std::atomic<int>& paneViewRef (int pane) { return pane == 0 ? view : (pane == 1 ? paneView2 : paneView3); }

    juce::String temperatureText() const
    {
        const float c = temperatureC.load();
        if (temperatureUnit.load() == 1) return juce::String (juce::roundToInt (c * 9.0f / 5.0f + 32.0f)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + "F";
        return juce::String (juce::roundToInt (c)) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")) + "C";
    }

    double speedOfSound() const { return 331.3 * std::sqrt (1.0 + temperatureC.load() / 273.15); }

    juce::String distanceText (double ms) const
    {
        const double metres = ms * 0.001 * speedOfSound();
        if (distanceUnit.load() == 1) return juce::String (metres, 2) + " m";
        return juce::String (metres * 3.28084, 1) + " ft";
    }

    void save (juce::XmlElement& x) const
    {
        x.setAttribute ("micCount", micCount.load());
        x.setAttribute ("selectedMic", selectedMic.load());
        for (int m = 0; m < kMaxMics; ++m)
        {
            const juce::String k (m);
            x.setAttribute ("micInput" + k, micInput[m].load());
            x.setAttribute ("micDelayMs" + k, micDelayMs[m].load());
            x.setAttribute ("micSplCalibrated" + k, micSplCalibrated[m].load());
            x.setAttribute ("micSplOffset" + k, micSplOffset[m].load());
        }
        x.setAttribute ("refSource", refSource.load());
        x.setAttribute ("genOutput", genOutput.load());
        x.setAttribute ("genType", genType.load());
        x.setAttribute ("genLevelDb", genLevelDb.load());
        x.setAttribute ("genFreq", genFreq.load());
        x.setAttribute ("genSweepSec", genSweepSec.load());
        x.setAttribute ("fftSize", fftSize.load());
        x.setAttribute ("multiWindow", multiWindow.load());
        x.setAttribute ("magThresholdDb", magThresholdDb.load());
        x.setAttribute ("averages", averages.load());
        x.setAttribute ("smoothing", smoothing.load());
        x.setAttribute ("coherenceBlank", coherenceBlank.load());
        x.setAttribute ("magRange", magRange.load());
        x.setAttribute ("rtaBands", rtaBands.load());
        x.setAttribute ("rtaSpeed", rtaSpeed.load());
        x.setAttribute ("rtaShowRef", rtaShowRef.load());
        x.setAttribute ("rtaPeakHold", rtaPeakHold.load());
        x.setAttribute ("view", view.load());
        x.setAttribute ("theme", theme.load());
        x.setAttribute ("temperatureC", temperatureC.load());
        x.setAttribute ("humidity", humidity.load());
        x.setAttribute ("distanceUnit", distanceUnit.load());
        x.setAttribute ("irMode", irMode.load());
        x.setAttribute ("irRange", irRange.load());
        x.setAttribute ("wfSeconds", wfSeconds.load());
        x.setAttribute ("wfRange", wfRange.load());
        x.setAttribute ("paneCount", paneCount.load());
        x.setAttribute ("currentTab", currentTab.load());
        x.setAttribute ("paneLayout", paneLayout.load());
        x.setAttribute ("paneView2", paneView2.load());
        x.setAttribute ("paneView3", paneView3.load());
        x.setAttribute ("targetCurve", targetCurve.load());
        x.setAttribute ("temperatureUnit", temperatureUnit.load());
        x.setAttribute ("calibratorDb", calibratorDb.load());
        // The generator is never saved as "on": the app always starts silent.
    }

    // This computer's preferences kept in the settings (Settings: Measurement, Display, Units; the generator's state):
    // a venue file does not change them.
    static const juce::StringArray& userKeys()
    {
        static const juce::StringArray k { "fftSize", "multiWindow", "magThresholdDb", "averages", "smoothing", "coherenceBlank", "magRange", "rtaBands", "rtaSpeed", "rtaShowRef",
                                           "rtaPeakHold", "view", "theme", "irMode", "irRange", "wfSeconds", "wfRange", "paneCount", "currentTab",
                                           "paneLayout", "paneView2", "paneView3", "temperatureUnit", "distanceUnit", "calibratorDb",
                                           "genType", "genLevelDb", "genFreq", "genSweepSec" };
        return k;
    }

    // A venue's settings over this computer's preferences.
    void loadVenue (const juce::XmlElement& venue)
    {
        juce::XmlElement merged ("Settings");
        save (merged);
        for (int i = 0; i < venue.getNumAttributes(); ++i)
            if (! userKeys().contains (venue.getAttributeName (i))) merged.setAttribute (venue.getAttributeName (i), venue.getAttributeValue (i));
        load (merged);
    }

    void load (const juce::XmlElement& x)
    {
        auto i = [&x] (const char* k, int def, int lo, int hi) { return juce::jlimit (lo, hi, x.getIntAttribute (k, def)); };
        auto f = [&x] (const char* k, float def, float lo, float hi) { return juce::jlimit (lo, hi, (float) x.getDoubleAttribute (k, def)); };
        micCount = i ("micCount", 1, 1, kMaxMics);
        selectedMic = i ("selectedMic", 0, 0, kAverageMic);
        for (int m = 0; m < kMaxMics; ++m)
        {
            const juce::String k (m);
            micInput[m] = juce::jlimit (0, 15, x.getIntAttribute ("micInput" + k, m == 0 ? 0 : m + 1));
            micDelayMs[m] = juce::jlimit (0.0f, 2000.0f, (float) x.getDoubleAttribute ("micDelayMs" + k, 0.0));
            micSplCalibrated[m] = x.getBoolAttribute ("micSplCalibrated" + k, false);
            micSplOffset[m] = juce::jlimit (0.0f, 200.0f, (float) x.getDoubleAttribute ("micSplOffset" + k, 120.0));
        }
        // version 0.1 - 0.3: a single mic
        if (x.hasAttribute ("micChannel")) micInput[0] = juce::jlimit (0, 15, x.getIntAttribute ("micChannel"));
        if (x.hasAttribute ("delayMs")) micDelayMs[0] = juce::jlimit (0.0f, 2000.0f, (float) x.getDoubleAttribute ("delayMs"));
        if (x.hasAttribute ("splCalibrated")) micSplCalibrated[0] = x.getBoolAttribute ("splCalibrated");
        if (x.hasAttribute ("splOffset")) micSplOffset[0] = juce::jlimit (0.0f, 200.0f, (float) x.getDoubleAttribute ("splOffset"));
        refSource = i ("refSource", 3, 0, 17);
        genOutput = i ("genOutput", 0, 0, 8);
        genType = i ("genType", 0, 0, 5);
        genLevelDb = f ("genLevelDb", -20.0f, -90.0f, 0.0f);
        genFreq = f ("genFreq", 1000.0f, 10.0f, 20000.0f);
        genSweepSec = f ("genSweepSec", 3.0f, 0.5f, 30.0f);
        const int fs = x.getIntAttribute ("fftSize", 32768);
        fftSize = (fs == 8192 || fs == 16384 || fs == 32768 || fs == 65536) ? fs : 32768;
        multiWindow = x.getBoolAttribute ("multiWindow", false);
        magThresholdDb = juce::jlimit (-120.0f, 0.0f, (float) x.getDoubleAttribute ("magThresholdDb", -90.0));
        averages = i ("averages", 8, 1, 128);
        smoothing = i ("smoothing", 24, 1, 48);
        coherenceBlank = f ("coherenceBlank", 0.4f, 0.0f, 0.95f);
        magRange = f ("magRange", 20.0f, 6.0f, 60.0f);
        rtaBands = i ("rtaBands", 3, 1, 48);
        rtaSpeed = i ("rtaSpeed", 1, 0, 2);
        rtaShowRef = x.getBoolAttribute ("rtaShowRef", false);
        rtaPeakHold = x.getBoolAttribute ("rtaPeakHold", false);
        view = i ("view", (int) View::Transfer, 0, 4);
        theme = i ("theme", 0, 0, 1);
        temperatureC = f ("temperatureC", 20.0f, -20.0f, 50.0f);
        humidity = f ("humidity", 50.0f, 0.0f, 100.0f);
        distanceUnit = i ("distanceUnit", 0, 0, 1);
        irMode = i ("irMode", 0, 0, 1);
        irRange = i ("irRange", 1, 0, 2);
        wfSeconds = i ("wfSeconds", 1, 0, 2);
        wfRange = i ("wfRange", 1, 0, 2);
        paneCount = i ("paneCount", 3, 1, 3);
        currentTab = i ("currentTab", 0, 0, 14);
        paneLayout = i ("paneLayout", 0, 0, 1);
        paneView2 = i ("paneView2", (int) View::Rta, 0, 4);
        paneView3 = i ("paneView3", (int) View::Waterfall, 0, 4);
        targetCurve = i ("targetCurve", 0, 0, 20);
        temperatureUnit = i ("temperatureUnit", 1, 0, 1);
        calibratorDb = f ("calibratorDb", 94.0f, 60.0f, 140.0f);
    }
};
} // namespace la
