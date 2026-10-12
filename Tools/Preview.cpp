// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Local check only (not part of the release build): drives the processor with a simulated PA, room and three mics,
// saves screenshots of every page, and round-trips a venue file with two sessions. Build with -DLA_BUILD_PREVIEW=ON.
#include <random>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/UI/SettingsPanel.h"
#include "../Source/UI/CalibrationPanel.h"

namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    static Biquad make (double fs, double f, double q, double gainDb, int type)   // 0 HP, 1 peak, 2 LP
    {
        Biquad b;
        const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * la::kPi * f / fs, c = std::cos (w), al = std::sin (w) / (2.0 * q);
        double a0;
        if (type == 0) { b.b0 = (1 + c) / 2; b.b1 = -(1 + c); b.b2 = (1 + c) / 2; a0 = 1 + al; b.a1 = -2 * c; b.a2 = 1 - al; }
        else if (type == 2) { b.b0 = (1 - c) / 2; b.b1 = 1 - c; b.b2 = (1 - c) / 2; a0 = 1 + al; b.a1 = -2 * c; b.a2 = 1 - al; }
        else { b.b0 = 1 + al * A; b.b1 = -2 * c; b.b2 = 1 - al * A; a0 = 1 + al / A; b.a1 = -2 * c; b.a2 = 1 - al / A; }
        b.b0 /= a0; b.b1 /= a0; b.b2 /= a0; b.a1 /= a0; b.a2 /= a0;
        return b;
    }
    double operator() (double x)
    {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};

// One mic's view of the system: its own distance, reflection and room colouring.
struct MicPath
{
    std::vector<Biquad> filters;
    int delay, reflDelay;
    float gain, refl;
};

class Driver : public juce::Timer
{
public:
    Driver (LiveAlignProcessor& p, juce::File out) : proc (p), dir (out)
    {
        system = { Biquad::make (fs, 70.0, 0.7, 0, 0), Biquad::make (fs, 70.0, 0.7, 0, 0), Biquad::make (fs, 2500.0, 2.0, 6.0, 1),
                   Biquad::make (fs, 14000.0, 0.7, 0, 2) };
        paths = { { { Biquad::make (fs, 180.0, 1.2, -4.0, 1) }, 336, 192, 0.5f, 0.4f },
                  { { Biquad::make (fs, 120.0, 1.0, 5.0, 1) }, 432, 260, 0.35f, 0.3f },
                  { { Biquad::make (fs, 400.0, 1.5, -3.0, 1) }, 576, 120, 0.25f, 0.5f } };
        line.assign (48000, 0.0f);
        startTimer (20);
    }

    void feed (int samples)
    {
        juce::AudioBuffer<float> buf (16, 512);
        juce::MidiBuffer midi;
        std::normal_distribution<double> nd (0.0, 0.1);
        const size_t L = line.size();
        for (int done = 0; done < samples; done += 512)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i)
            {
                const float ref = (float) nd (rng);
                double y = ref;
                for (auto& f : system) y = f (y);
                line[wp % L] = (float) y;
                const int inputs[] = { 0, 2, 3 };
                for (size_t m = 0; m < paths.size(); ++m)
                {
                    auto& pth = paths[m];
                    double v = pth.gain * line[(wp - (size_t) pth.delay + L) % L] + pth.gain * pth.refl * line[(wp - (size_t) (pth.delay + pth.reflDelay) + L) % L];
                    for (auto& f : pth.filters) v = f (v);
                    buf.setSample (inputs[m], i, (float) (v + nd (rng) * 0.01));
                }
                buf.setSample (1, i, ref);
                ++wp;
            }
            proc.processBlock (buf, midi);
        }
    }

    void timerCallback() override
    {
        ++ticks;
        if (ticks < 300) feed (960);
        if (ticks == 60) proc.settings.requestFindDelay = true;
        if (ticks == 140)
        {
            la::Trace t;
            t.name = "Before EQ";
            la::Snapshot s;
            proc.analyzer.readSnapshot (s, 0);
            for (size_t i = 0; i < s.tfFreq.size(); ++i)
                if (std::isfinite (s.tfMag[i])) { t.data.freq.push_back (s.tfFreq[i]); t.data.db.push_back (s.tfMag[i] - 3.0f); t.data.phase.push_back (s.tfPhase[i]); t.data.extra.push_back (s.tfCoh[i]); }
            proc.traces.add (t);
        }

        if (ticks == 150)
        {
            proc.uiAsStandalone = true;
            proc.settings.currentTab = 0;
            editor.reset (dynamic_cast<LiveAlignEditor*> (proc.createEditor()));
            editor->setSize (1480, 920);
            proc.settings.targetCurve = 4;
        }
        if (ticks == 165) shot ("home");
        if (ticks == 166) editor->showPage (1);
        if (ticks == 182) shot ("mic-setup");
        if (ticks == 183) { proc.settings.selectedMic = la::kAverageMic; editor->showPage (4); }
        if (ticks == 200) shot ("monitor-average");
        if (ticks == 201) { proc.settings.selectedMic = 1; proc.settings.paneCount = 3; editor->setPaneViews (1, 0, 2); }
        if (ticks == 218) shot ("monitor-three");
        if (ticks == 219) { proc.settings.paneCount = 1; editor->showPage (2); }
        if (ticks == 225) shot ("speakers");
        if (ticks == 226) { proc.settings.theme = 1; editor->showPage (0); }
        if (ticks == 240) shot ("home-light");
        if (ticks == 241) { editor->showPage (4); proc.settings.selectedMic = 0; }
        if (ticks == 256) shot ("monitor-light");
        if (ticks == 257)
        {
            proc.settings.theme = 0;
            la::ui::currentPalette() = &la::ui::darkPalette();
            la::ui::LALookAndFeel laf;
            {
                la::ui::SettingsPanel sp (proc);
                sp.setLookAndFeel (&laf);
                save (sp.createComponentSnapshot (sp.getLocalBounds()), "settings");
                sp.setLookAndFeel (nullptr);
            }
            {
                la::ui::CalibrationPanel cp (proc.settings, 1, "Mic 2", [] { return -21.3f; }, [] { return juce::String ("7023270_90deg.txt"); });
                cp.setLookAndFeel (&laf);
                save (cp.createComponentSnapshot (cp.getLocalBounds()), "calibration");
                cp.setLookAndFeel (nullptr);
            }
        }
        if (ticks == 262)
        {
            const auto d = proc.analyzer.readDelayReport();
            for (int m = 0; m < d.micCount; ++m)
                std::printf ("delay mic %d: valid=%d ms=%.3f inv=%d conf=%.1f applied=%.3f\n", m + 1, (int) d.valid[(size_t) m], d.ms[(size_t) m],
                             (int) d.inverted[(size_t) m], d.confidenceDb[(size_t) m], (double) proc.settings.micDelayMs[m].load());
            sessionRoundTrip();
            editor.reset();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }

    void sessionRoundTrip()
    {
        proc.venueName = "Test church";
        proc.mics.name[0] = "FOH";
        proc.mics.position[1] = "Balcony";
        proc.speakers.configured = true;
        proc.speakers.configuredOn = "11 Oct 2026";
        proc.checks.splVerified[0] = true;
        const int tracesS1 = proc.traces.size();
        proc.newSession ("Sunday");
        proc.traces.add ([] { la::Trace t; t.name = "Sunday TF"; t.data.freq = { 100, 1000 }; t.data.db = { 0, 1 }; return t; }());
        const bool verifiedReset = ! proc.checks.splVerified[0];
        const auto f = dir.getChildFile ("test.lavenue");
        const auto err = proc.saveProject (f);
        proc.newProject();
        const auto err2 = proc.openProject (f);
        std::printf ("venue: save '%s' open '%s' name=%s sessions=%d current=%d traces(open)=%d micName=%s pos=%s speakers=%d verifiedReset=%d changed=%d\n",
                     err.toRawUTF8(), err2.toRawUTF8(), proc.venueName.toRawUTF8(), (int) proc.getSessions().size(), proc.currentSession(),
                     proc.traces.size(), proc.mics.name[0].toRawUTF8(), proc.mics.position[1].toRawUTF8(), (int) proc.speakers.configured,
                     (int) verifiedReset, (int) proc.projectChanged());
        proc.switchSession (0);
        std::printf ("switch to session 1: traces=%d (was %d) splVerified=%d\n", proc.traces.size(), tracesS1, (int) proc.checks.splVerified[0]);
        proc.compareWithSession (1);
        std::printf ("compare with Sunday: traces=%d hasComparisons=%d\n", proc.traces.size(), (int) proc.traces.hasComparisons());
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        proc.newProject();
        proc.setStateInformation (state.getData(), (int) state.getSize());
        std::printf ("state: name=%s sessions=%d micCount=%d changed=%d\n", proc.venueName.toRawUTF8(), (int) proc.getSessions().size(),
                     proc.settings.micCount.load(), (int) proc.projectChanged());
        f.deleteFile();
    }

    void shot (const char* name) { save (editor->createComponentSnapshot (editor->getLocalBounds()), name); }

    void save (const juce::Image& img, const char* name)
    {
        juce::PNGImageFormat png;
        auto f = dir.getChildFile (juce::String (name) + ".png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        png.writeImageToStream (img, os);
        std::printf ("saved %s\n", f.getFullPathName().toRawUTF8());
    }

    LiveAlignProcessor& proc;
    juce::File dir;
    std::unique_ptr<LiveAlignEditor> editor;
    std::vector<Biquad> system;
    std::vector<MicPath> paths;
    std::vector<float> line;
    size_t wp = 0;
    int ticks = 0;
    double fs = 48000.0;
    std::mt19937 rng { 42 };
};
} // namespace

int laPreviewMain (int argc, char** argv);
int laPreviewMain (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File out (argc > 1 ? argv[1] : "/tmp");
    auto proc = std::make_unique<LiveAlignProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::discreteChannels (16));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (8));
    proc->setBusesLayout (layout);
    proc->prepareToPlay (48000.0, 512);
    proc->settings.refSource = 3;   // input 2 is the loopback reference
    proc->settings.micCount = 3;
    proc->settings.micInput[0] = 0;
    proc->settings.micInput[1] = 2;
    proc->settings.micInput[2] = 3;
    proc->mics.position[0] = "FOH";
    proc->mics.position[2] = "Rear";
    for (int m = 0; m < 3; ++m) { proc->settings.micSplCalibrated[m] = true; proc->settings.micSplOffset[m] = 115.0f; }
    proc->checks.splVerified[0] = true;
    {
        Driver d (*proc, out);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }
    proc.reset();
    return 0;
}
