// Local check only (not part of the release build): drives the processor with a simulated PA + room,
// then saves screenshots of the editor in each view. Build with -DLA_BUILD_PREVIEW=ON.
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

class Driver : public juce::Timer
{
public:
    Driver (LiveAlignProcessor& p, juce::File out) : proc (p), dir (out)
    {
        filters = { Biquad::make (fs, 70.0, 0.7, 0, 0), Biquad::make (fs, 70.0, 0.7, 0, 0), Biquad::make (fs, 2500.0, 2.0, 6.0, 1),
                    Biquad::make (fs, 180.0, 1.2, -4.0, 1), Biquad::make (fs, 14000.0, 0.7, 0, 2) };
        delayLine.assign (48000, 0.0f);
        startTimer (20);
    }

    void feed (int samples)
    {
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        std::normal_distribution<double> nd (0.0, 0.1);
        for (int done = 0; done < samples; done += 512)
        {
            for (int i = 0; i < 512; ++i)
            {
                const float ref = (float) nd (rng);
                // system: 7 ms of travel, filters, one reflection 4 ms later at -8 dB, plus room noise
                double y = ref;
                for (auto& f : filters) y = f (y);
                delayLine[(size_t) (wp % delayLine.size())] = (float) y;
                const float direct = delayLine[(size_t) ((wp - 336 + delayLine.size()) % delayLine.size())];
                const float refl = delayLine[(size_t) ((wp - 528 + delayLine.size()) % delayLine.size())];
                ++wp;
                buf.setSample (0, i, 0.5f * direct + 0.2f * refl + (float) (nd (rng) * 0.01));
                buf.setSample (1, i, ref);
            }
            proc.processBlock (buf, midi);
        }
    }

    void timerCallback() override
    {
        ++ticks;
        if (ticks < 150) feed (960);   // ~20 ms of audio per 20 ms tick: real time
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

        if (ticks == 150) { editor.reset (dynamic_cast<LiveAlignEditor*> (proc.createEditor())); editor->setSize (1400, 900); proc.settings.view = 1; proc.settings.targetCurve = 4; }
        if (ticks == 170) shot ("transfer");
        if (ticks == 171) { proc.settings.paneCount = 3; editor->setPaneViews (1, 0, 2); }
        if (ticks == 190) shot ("three-stacked");
        if (ticks == 191) { proc.settings.paneCount = 2; proc.settings.paneLayout = 1; editor->setPaneViews (0, 2, 1); }
        if (ticks == 210) shot ("two-side-by-side");
        if (ticks == 211) { proc.settings.paneCount = 1; proc.settings.paneLayout = 0; editor->setPaneViews (1, 0, 2); proc.settings.theme = 1; proc.settings.temperatureUnit = 1; }
        if (ticks == 230) shot ("transfer-light");
        if (ticks == 231)
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
                la::ui::CalibrationPanel cp (proc.settings, [] { return -21.3f; }, [] { return juce::String ("7023270_90deg.txt"); });
                cp.setLookAndFeel (&laf);
                save (cp.createComponentSnapshot (cp.getLocalBounds()), "calibration");
                cp.setLookAndFeel (nullptr);
            }
        }
        if (ticks == 235)
        {
            const auto d = proc.analyzer.readDelayReport();
            std::printf ("delay report: valid=%d ms=%.3f inv=%d conf=%.1f  applied=%.3f ms\n", (int) d.valid, d.ms, (int) d.inverted,
                         d.confidenceDb, (double) proc.settings.delayMs.load());
            editor.reset();
            for (int i = proc.traces.size() - 1; i >= 0; --i) proc.traces.remove (i);
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
    }

    void shot (const char* name)
    {
        save (editor->createComponentSnapshot (editor->getLocalBounds()), name);
    }

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
    std::vector<Biquad> filters;
    std::vector<float> delayLine;
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
    proc->setPlayConfigDetails (2, 2, 48000.0, 512);
    proc->prepareToPlay (48000.0, 512);
    proc->settings.refSource = 3;   // input 2 is the loopback reference
    proc->settings.micChannel = 0;
    proc->settings.splCalibrated = true;
    proc->settings.splOffset = 115.0f;
    {
        Driver d (*proc, out);
        juce::MessageManager::getInstance()->runDispatchLoop();
    }
    proc.reset();
    return 0;
}
