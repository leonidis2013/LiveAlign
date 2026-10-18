// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Local check only (not part of the release build): drives the processor with a simulated PA, room and three mics,
// saves screenshots of every page, and round-trips a venue file with two sessions. Build with -DLA_BUILD_PREVIEW=ON.
#include "../Source/UI/ListenSetup.h"
#include <array>
#include <random>
#include <typeinfo>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include "../Source/AcousticsSummary.h"
#include "../Source/UI/SettingsPanel.h"
#include "../Source/UI/CalibrationPanel.h"
#include "../Source/Report/TuningReport.h"

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

// A simple room tail (Schroeder: four combs into two all-passes) with a given reverberation time.
struct Reverb
{
    struct Line { std::vector<double> b; size_t p = 0; double g = 0.0; };
    std::array<Line, 4> combs;
    std::array<Line, 2> aps;
    void init (double fs, double rt, int seed)
    {
        const int cd[] = { 1557, 1617, 1491, 1422 }, ad[] = { 225, 556 };
        for (int i = 0; i < 4; ++i)
        {
            const int d = (int) (cd[i] * fs / 44100.0) + seed * 7;
            combs[(size_t) i].b.assign ((size_t) d, 0.0);
            combs[(size_t) i].g = std::pow (10.0, -3.0 * d / (rt * fs));
        }
        for (int i = 0; i < 2; ++i) { aps[(size_t) i].b.assign ((size_t) (ad[i] * fs / 44100.0), 0.0); aps[(size_t) i].g = 0.5; }
    }
    double process (double x)
    {
        double y = 0.0;
        for (auto& c : combs)
        {
            const double o = c.b[c.p];
            c.b[c.p] = x + o * c.g;
            c.p = (c.p + 1) % c.b.size();
            y += o;
        }
        y *= 0.25;
        for (auto& a : aps)
        {
            const double o = a.b[a.p];
            const double in = y + o * a.g;
            a.b[a.p] = in;
            a.p = (a.p + 1) % a.b.size();
            y = o - a.g * in;
        }
        return y;
    }
};

// Pass / fail checks: the run exits non-zero when one fails (CI stops the release). --stop-after=<shot> ends the run
// after that screenshot (a quicker run of the early steps).
int gChecks = 0, gFailures = 0, gOverflows = 0;
juce::String gStopAfter;
bool gStrictOverflow = false;
juce::StringArray gOverflowSeen;
void check (bool ok, const juce::String& what)
{
    ++gChecks;
    if (! ok) ++gFailures;
    std::printf ("CHECK %s: %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
}

class Driver : public juce::Timer
{
public:
    Driver (LiveAlignProcessor& p, juce::File out) : proc (p), dir (out)
    {
        system = { Biquad::make (fs, 2500.0, 2.0, 4.0, 1) };   // the processor's EQ (room EQ adds to it); a peak to tune out
        paths = { { { Biquad::make (fs, 180.0, 1.2, -4.0, 1) }, 336, 192, 0.5f, 0.4f },
                  { { Biquad::make (fs, 120.0, 1.0, 5.0, 1) }, 432, 260, 0.35f, 0.3f },
                  { { Biquad::make (fs, 400.0, 1.5, -3.0, 1) }, 576, 120, 0.25f, 0.5f } };
        top = { Biquad::make (fs, 75.0, 0.7, 0, 0), Biquad::make (fs, 2500.0, 2.0, 4.0, 1), Biquad::make (fs, 15000.0, 0.7, 0, 2) };
        sub = { Biquad::make (fs, 110.0, 0.7, 0, 2), Biquad::make (fs, 110.0, 0.7, 0, 2), Biquad::make (fs, 28.0, 0.7, 0, 0), Biquad::make (fs, 55.0, 2.0, 3.0, 1) };
        fillChain = top;
        lineTop.assign (48000, 0.0f);
        lineSub.assign (48000, 0.0f);
        lineFill.assign (48000, 0.0f);
        for (size_t m = 0; m < 3; ++m) reverbs[m].init (fs, 1.1 + 0.15 * (double) m, (int) m);
        la::clock::setSimulated (0.0);
        startTimer (5);
    }

    void feed (int samples)
    {
        juce::AudioBuffer<float> buf (16, 512);
        juce::MidiBuffer midi;
        std::normal_distribution<double> nd (0.0, 0.1);
        const size_t L = lineTop.size();
        // the generator's level against its default (-20 dBFS), and the amps limiting above -15 dBFS (for Gain and headroom)
        const double genDb = proc.settings.genOn.load() ? (double) juce::jmin (proc.settings.genLevelDb.load(), proc.settings.genMaxDb.load()) : -20.0;
        const double genGain = std::pow (10.0, (genDb + 20.0) / 20.0);
        const double ampGain = genDb > -15.0 ? std::pow (10.0, -(genDb + 15.0) * 0.7 / 20.0) : 1.0;
        for (int done = 0; done < samples; done += 512)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i)
            {
                const float ref = (float) (nd (rng) * genGain);
                // while a speaker is measured, only the speaker on the generator's output plays; "all outputs" plays them all
                const int out = proc.settings.genOn.load() ? proc.settings.genOutput.load() : (consoleOut >= 0 ? consoleOut : 0);
                if (out != lastOut) { std::printf ("  sim: output %d at tick %d\n", out, ticks); lastOut = out; }
                double t = 0.0, sb = 0.0, fl = 0.0;
                // outputs: 1 / 2 main left / right, 3 sub, 4 front fill
                if (out == 0 || out == 1 || out == 2) { t = ref; for (auto& f : top) t = f (t); for (auto& f : system) t = f (t); }
                if (out == 2 && simRightInverted) t = -t;   // Main R wired the wrong way round
                if (out == 0 || out == 3) { sb = ref; for (auto& f : sub) sb = f (sb); for (auto& f : subSystem) sb = f (sb); sb *= 2.0 * subGain; }
                if (out == 4) { fl = ref; for (auto& f : fillChain) fl = f (fl); fl *= 0.5; }
                lineTop[wp % L] = (float) (t * ampGain);
                lineSub[wp % L] = (float) (sb * ampGain);
                lineFill[wp % L] = (float) (fl * ampGain);
                const int inputs[] = { 0, 2, 3 };
                const double ring = ringHz > 0.0 ? 0.08 * std::sin (2.0 * la::kPi * ringHz * (double) wp / fs) : 0.0;
                for (size_t m = 0; m < paths.size(); ++m)
                {
                    auto& pth = paths[m];
                    // the sub stands 2 ms further from the mics than the tops, the fill 3 ms closer; processor delays apply
                    auto at = [&] (const std::vector<float>& ln, long d) { const long n = (long) L; return (double) ln[(size_t) ((((long) wp - d) % n + n) % n)]; };
                    double v = pth.gain * (at (lineTop, pth.delay + simTopDelay) + pth.refl * at (lineTop, pth.delay + pth.reflDelay + simTopDelay))
                             + pth.gain * at (lineSub, pth.delay + 96 + simSubDelay)
                             + pth.gain * at (lineFill, pth.delay - 144);
                    for (auto& f : pth.filters) v = f (v);
                    v += 0.9 * reverbs[m].process (v);   // the room's reverberation
                    buf.setSample (inputs[m], i, (float) (v + ring + nd (rng) * 0.01));
                }
                buf.setSample (1, i, ref);
                ++wp;
            }
            proc.processBlock (buf, midi);
        }
    }

    void timerCallback() override
    {
        if (stopNow) { finish(); return; }
        ++ticks;
        feed (960);
        // the app's clock follows the audio fed, and the analysis catches up before the next step: the run does not
        // depend on how fast the computer is
        simMs += 960.0 / fs * 1000.0;
        la::clock::setSimulated (simMs);
        for (int waited = 0; waited < 5000 && proc.analyzer.samplesWaiting() > 1920; ++waited) juce::Thread::sleep (1);
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
        using P = la::ui::Page;
        if (ticks == 157)
        {
            // at start, before a venue is open: Home only
            shot ("home-boot");
            check (! editor->pageInSession ((int) P::Mics) && ! editor->pageInSession ((int) P::Monitor) && editor->pageInSession ((int) P::Home),
                   "at start: only Home until a venue is open");
            proc.venueOpen = true;   // the run's venue, as if opened
            editor->afterProjectLoadedForPreview();
        }
        if (ticks == 165) shot ("home-start");
        if (ticks == 166) editor->showPage ((int) P::Mics);
        if (ticks == 182) shot ("mic-setup");
        if (ticks == 183) editor->showPage ((int) P::Monitor);
        if (ticks == 200) shot ("monitor");
        if (ticks == 201) { proc.settings.paneCount = 3; editor->setPaneViews (1, 0, 2); }
        if (ticks == 218) shot ("monitor-three");
        if (ticks == 219) { proc.settings.paneCount = 1; editor->showPage ((int) P::Speakers); }
        if (ticks == 225) shot ("speakers-system");
        if (ticks == 226) { proc.plan.lengthM = 32.0; proc.plan.widthM = 20.0; proc.plan.heightM = 9.0; proc.setConditions (24.0f, 35.0f); proc.noteTunedConditions(); proc.setConditions (31.0f, 30.0f); editor->showPage ((int) P::Venue); }
        if (ticks == 232) shot ("venue-details");
        if (ticks == 233) { proc.settings.theme = 1; editor->showPage ((int) P::Home); }
        if (ticks == 240) shot ("home-light");
        if (ticks == 241) editor->showPage ((int) P::Monitor);
        if (ticks == 256) shot ("monitor-light");
        if (ticks == 257)
        {
            proc.settings.theme = 0;
            la::ui::currentPalette() = &la::ui::darkPalette();
            la::ui::LALookAndFeel laf;
            {
                la::ui::CalibrationPanel cp (proc.settings, 1, "Mic 2", [] { return -21.3f; }, [] { return juce::String ("7023270_90deg.txt"); });
                cp.setLookAndFeel (&laf);
                save (cp.createComponentSnapshot (cp.getLocalBounds()), "calibration");
                cp.setLookAndFeel (nullptr);
            }
        }
        if (ticks == 252)
        {
            // the guided pop-up on Speaker response, before measuring
            editor->showPage ((int) la::ui::Page::Response);
            // a stereo pair both on All outputs: the pop-up says which speaker to mute first
            savedOuts = { proc.system.sources[0].output, proc.system.sources[1].output };
            proc.system.sources[0].output = proc.system.sources[1].output = 0;
            editor->openGuided (editor->speakersPage());
        }
        if (ticks == 258 && editor->guided() != nullptr)
        {
            shot ("guided-speaker");
            editor->guided()->closeNow();
            proc.system.sources[0].output = savedOuts[0];
            proc.system.sources[1].output = savedOuts[1];
        }
        if (ticks == 262)
        {
            editor->showPage ((int) P::Response);
            editor->speakersPage().assumeMuted (true);   // the simulator mutes for itself
            editor->speakersPage().measure (0);
            next (1);
        }
        if (stage > 0 && stage < 99 && editor != nullptr) flow();
    }

    void next (int s) { stage = s; stageTick = ticks; }
    bool after (int t) const { return ticks == stageTick + t; }

    // The whole tuning flow on the simulated system, page by page, then the monitoring pages and the reports.
    void flow()
    {
        using P = la::ui::Page;
        auto& page = editor->speakersPage();
        auto& rp = editor->roomPageForPreview();
        const bool timeout = ticks - stageTick > 6000;
        auto behind = [this] { return (double) (proc.analyzer.samplesWritten() - proc.analyzer.samplesAnalysed()) / 48000.0; };
        switch (stage)
        {
            case 1:   // Main L, close up
                if (after (60)) shot ("response-measuring");
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& s = proc.system.sources[0];
                    std::printf ("status: %s (%d ticks, behind %.1f s)\n", page.statusText().toRawUTF8(), ticks - stageTick, behind());
                    check (s.hasResponse() && s.nearField, "speaker response: Main L measured close up");
                    std::printf ("measure Main: near=%d reliable=%.2f response=%d points=%d arrival=%.3f delayUsed=%.3f\n", (int) s.nearField,
                                 la::xo::usableReliability (s.response, 0.4), (int) s.hasResponse(), (int) s.response.freq.size(), s.arrivalMs, s.delayUsedMs);
                    page.measure (2);
                    next (2);
                }
                break;
            case 2:   // the sub, close up
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& s = proc.system.sources[2];
                    std::printf ("measure Sub: near=%d response=%d points=%d arrival=%.3f\n", (int) s.nearField, (int) s.hasResponse(), (int) s.response.freq.size(), s.arrivalMs);
                    auto pol = [this] (int i, int c)
                    {
                        const auto* pc = proc.system.sources[(size_t) i].polarityOf (c);
                        return pc == nullptr ? juce::String ("none")
                                             : juce::String (la::polarity::verdictName (pc->verdict)) + " (own " + la::polarity::verdictName (pc->own) + ", excess "
                                                   + juce::String (juce::roundToInt (pc->phaseDeg)) + " deg, peak " + (pc->peakInverted ? "-" : "+") + ", vs " + juce::String (pc->against)
                                                   + " " + la::polarity::verdictName (pc->relative) + ")";
                    };
                    std::printf ("polarity: Main L %s | Sub %s\n", pol (0, 0).toRawUTF8(), pol (2, 0).toRawUTF8());
                    shot ("response");
                    // Main R (same as Main L) wired the wrong way round: its own capture finds it against Main L
                    simRightInverted = true;
                    page.measure (1);
                    next (30);
                }
                break;
            case 30:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto* pc = proc.system.sources[1].polarityOf (0);
                    std::printf ("polarity check Main R (wired inverted): %s own=%s excess=%.0f rel=%s | %s\n", pc ? la::polarity::verdictName (pc->verdict) : "none",
                                 pc ? la::polarity::verdictName (pc->own) : "-", pc ? pc->phaseDeg : 0.0, pc ? la::polarity::verdictName (pc->relative) : "-",
                                 page.statusText().toRawUTF8());
                    shot ("polarity-inverted");
                    simRightInverted = false;   // fixed: measure it again
                    page.measure (1);
                    next (31);
                }
                break;
            case 31:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto* pc = proc.system.sources[1].polarityOf (0);
                    std::printf ("polarity check Main R (rewired): %s own=%s rel=%s\n", pc ? la::polarity::verdictName (pc->verdict) : "none",
                                 pc ? la::polarity::verdictName (pc->own) : "-", pc ? la::polarity::verdictName (pc->relative) : "-");
                    shot ("response-pair");
                    subGain = -subGain;   // the sub wired the wrong way round
                    page.measure (2);
                    next (32);
                }
                break;
            case 32:
            case 33:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto* pc = proc.system.sources[2].polarityOf (0);
                    std::printf ("polarity check Sub (%s): %s excess=%.0f\n", stage == 32 ? "wired inverted" : "rewired",
                                 pc ? la::polarity::verdictName (pc->verdict) : "none", pc ? pc->phaseDeg : 0.0);
                    subGain = -subGain;
                    if (stage == 32) { page.measure (2); next (33); }
                    else
                    {
                        subGain = -subGain;   // back as it was for the crossover
                        editor->showPage ((int) P::Crossover);
                        page.findCrossover();
                        next (3);
                    }
                }
                break;
            case 3:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& r = proc.system.lastResult;
                    check (r.valid && r.filter.freq > 60.0 && r.filter.freq < 250.0, "crossover: found between 60 and 250 Hz");
                    std::printf ("crossover: valid=%d %s %d dB/oct at %.1f Hz, low delay %.2f ms, invert %d, gain %.1f dB, eff %.2f, pairNear=%d alignNeeded=%d\n",
                                 (int) r.valid, la::xo::typeName (r.filter.type), r.filter.slope, r.filter.freq, r.delayLowMs, (int) r.invertLow, r.gainLowDb,
                                 r.efficiency, (int) proc.system.pairNearField(), (int) page.alignmentRequired());
                    // the crossover filters, polarity and level go in the (simulated) processor before the alignment
                    addFilters (top, proc.system.sources[0].proc.hp, true);
                    addFilters (sub, proc.system.sources[2].proc.lp, false);
                    subGain = std::pow (10.0, proc.system.sources[2].proc.gainDb / 20.0) * (proc.system.sources[2].proc.invert ? -1.0 : 1.0)
                              * (proc.system.sources[0].proc.invert ? -1.0 : 1.0);
                    simSubDelay = (int) std::lround (proc.system.sources[2].proc.delayMs * 48.0);
                    simTopDelay = (int) std::lround (proc.system.sources[0].proc.delayMs * 48.0);
                    next (4);
                }
                break;
            case 4:
                if (after (10))
                {
                    shot ("crossover");
                    // a front fill on output 4; then the level balance at FOH and the centre, before the time alignment
                    la::SpeakerSource fill;
                    fill.name = "Front fill";
                    fill.role = (int) la::SpeakerRole::Fill;
                    fill.output = 4;
                    proc.system.sources.push_back (fill);
                    page.refreshChoices();
                    proc.level.points = { "FOH", "Centre" };
                    editor->showPage ((int) P::Level);
                    levelTask = 0;
                    editor->levelPageForPreview().measureNow (0, 0, false);
                    next (60);
                }
                break;
            case 60:   // each speaker alone at each point
            case 61:   // and again at the centre after the trims
            {
                auto& lp = editor->levelPageForPreview();
                if (stageTick + 5 < ticks && (! lp.busy() || timeout))
                {
                    const int n = (int) proc.system.sources.size();
                    const int total = stage == 60 ? 2 * n : n;
                    if (after (6) || stageTick + 6 < ticks)
                    {
                        if (++levelTask < total)
                        {
                            lp.measureNow (stage == 60 ? levelTask / n : 0, levelTask % n, stage == 61);
                            stageTick = ticks;
                            break;
                        }
                        if (stage == 60)
                        {
                            for (const auto& t : lp.trims (false)) std::printf ("level: %s (trim %.1f dB on %d%s)\n", t.why.toRawUTF8(), t.db, t.spk, t.attention ? ", attention" : "");
                            shot ("level");
                            lp.applyNow();
                            // the sub's trim goes in the simulated processor too
                            subGain *= std::pow (10.0, proc.system.sources[2].trimDb / 20.0);
                            levelTask = 0;
                            lp.measureNow (0, 0, true);
                            next (61);
                        }
                        else
                        {
                            for (const auto& t : lp.trims (true)) std::printf ("level check: %s\n", t.why.toRawUTF8());
                            std::printf ("level done=%d\n", (int) lp.done());
                            check (lp.readingsCount (false) >= lp.tasksCount() && lp.tasksCount() > 0, "level balance: every speaker measured at every point");
                            shot ("level-checked");
                            editor->showPage ((int) P::Coverage);
                            next (62);
                        }
                    }
                }
                break;
            }
            case 62:   // aim and coverage, from the level readings; Main L re-aimed and measured again
                if (after (6))
                {
                    auto& cp = editor->coveragePageForPreview();
                    for (const auto& r : cp.rows())
                        std::printf ("coverage: %s on-axis %s worst %.1f at %s ok=%d %s\n", proc.system.nameOf (r.spk).toRawUTF8(), r.onAxis.toRawUTF8(), r.worstDb,
                                     r.worstPoint.toRawUTF8(), (int) r.ok, r.advice.toRawUTF8());
                    check (! cp.rows().empty(), "aim and coverage: rows from the level readings");
                    shot ("coverage");
                    cp.reaimNow (0);
                    next (63);
                }
                break;
            case 63:
            {
                auto& cp = editor->coveragePageForPreview();
                if (stageTick + 5 < ticks && (! cp.reaimBusy() || timeout))
                {
                    if (cp.reaimLeft() && ticks - stageTick < 20000) { cp.guidedStart(); stageTick = ticks; break; }
                    std::printf ("coverage re-aimed: before=%d points, %s\n", (int) proc.coverage.before[0].size(), cp.guidedStatus().toRawUTF8());
                    check (! proc.coverage.before[0].empty() && ! cp.reaimLeft(), "aim and coverage: re-aimed and measured again");
                    cp.refresh();
                    shot ("coverage-reaimed");
                    editor->showPage ((int) P::Align);
                    page.refreshChoices();
                    page.newAlignment();
                    page.measureAlignment (0);
                    next (5);
                }
                break;
            }
            case 5:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& a = proc.system.alignments[0];
                    std::printf ("alignment 1: ref=%s align=%s at %s arrivals %.3f / %.3f phase=%d %.2f need=%.3f\n", proc.system.sources[(size_t) a.ref].name.toRawUTF8(),
                                 proc.system.sources[(size_t) a.align].name.toRawUTF8(), a.spot.toRawUTF8(), a.refArrivalMs, a.alignArrivalMs, (int) a.phaseValid, a.phaseDelayMs,
                                 a.neededMs (proc.system.sources[(size_t) a.ref].proc.delayMs));
                    shot ("alignment-impulse");
                    check (! a.refIr.empty() && ! a.alignIr.empty(), "time alignment: both impulse responses captured");
                    page.applyAlignmentNow();
                    // the processor has the delay now: apply it in the simulation too
                    simSubDelay = (int) std::lround (proc.system.sources[2].proc.delayMs * 48.0);
                    simTopDelay = (int) std::lround (proc.system.sources[0].proc.delayMs * 48.0);

                    std::printf ("  applied: main %.2f ms, sub %.2f ms (%s)\n", proc.system.sources[0].proc.delayMs, proc.system.sources[2].proc.delayMs, page.statusText().toRawUTF8());
                    page.checkAlignment (0);
                    next (6);
                }
                break;
            case 6:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& a = proc.system.alignments[0];
                    std::printf ("alignment check at %.0f Hz: alone %.1f / %.1f dB, together %.1f dB, gain %.1f of %.1f dB: %s\n", a.checkFreq, a.refBandDb, a.alignBandDb,
                                 a.sumBandDb, a.sumGainDb(), a.idealSumGainDb(), page.statusText().toRawUTF8());
                    shot ("alignment");
                    page.newAlignment();
                    page.measureAlignment (1);
                    next (7);
                }
                break;
            case 7:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& a = proc.system.alignments[1];
                    std::printf ("alignment 2: %s to %s need=%.3f\n", proc.system.sources[(size_t) a.align].name.toRawUTF8(), proc.system.sources[(size_t) a.ref].name.toRawUTF8(),
                                 a.neededMs (proc.system.sources[(size_t) a.ref].proc.delayMs));
                    page.applyAlignmentNow();
                    // the main / sub alignment again with "music from the console", one speaker at a time
                    savedSub = proc.system.alignments[0].alignArrivalMs;
                    page.setAlignmentSignal (2);
                    consoleOut = proc.system.sources[(size_t) proc.system.alignments[0].ref].output;
                    page.measureAlignmentOne (0, 0);
                    next (50);
                }
                break;
            case 50:
            case 51:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& a = proc.system.alignments[0];
                    if (stage == 50)
                    {
                        std::printf ("music: %s measured: %s\n", proc.system.sources[(size_t) a.ref].name.toRawUTF8(), page.statusText().toRawUTF8());
                        consoleOut = proc.system.sources[(size_t) a.align].output;
                        page.measureAlignmentOne (0, 1);
                        next (51);
                        break;
                    }
                    std::printf ("music alignment: phase=%d %.2f need=%.3f (status %s)\n", (int) a.phaseValid, a.phaseDelayMs,
                                 a.neededMs (proc.system.sources[(size_t) a.ref].proc.delayMs), page.statusText().toRawUTF8());
                    consoleOut = -1;
                    page.setAlignmentSignal (0);
                    // room acoustics before Room EQ: a sweep at FOH (in a 22 x 14 x 7 m room)
                    proc.plan.lengthM = 22.0;
                    proc.plan.widthM = 14.0;
                    proc.plan.heightM = 7.0;
                    editor->showPage ((int) P::Acoustics);
                    proc.setMicPosition (0, "FOH");
                    editor->acousticsPageForPreview().startSweep();
                    next (40);
                }
                break;
            case 8:
                if (after (15)) { shot ("response-light"); proc.settings.theme = 0; editor->showPage ((int) P::Monitor); proc.settings.view = 3; ringHz = 3150.0; }
                if (after (200))
                {
                    std::printf ("feedback watch: %d marked (%s Hz)\n", (int) la::ui::feedbackMarks().size(),
                                 la::ui::feedbackMarks().empty() ? "-" : juce::String (juce::roundToInt (la::ui::feedbackMarks().back())).toRawUTF8());
                    shot ("monitor-waterfall");
                    ringHz = 0.0;
                    proc.settings.view = 1;
                    editor->showPage ((int) P::Room);
                    rp.setMeasuringAfter (false);
                    editor->openGuided (rp);   // the first round, guided: the mics go to the first planned positions
                    next (20);
                }
                break;
            case 20:   // guided: ready, count-in, measuring, done, next point
                if (after (10)) { shot ("guided-ready"); editor->guided()->pressPrimaryForTest(); }
                if (after (16)) { shot ("guided-countin"); editor->guided()->skipCountIn(); next (21); }
                break;
            case 21:
                if (after (40)) shot ("guided-measuring");
                if (stageTick + 10 < ticks && (! rp.busy() || timeout)) next (22);
                break;
            case 22:
                if (after (5))
                {
                    std::printf ("guided round 1: %s\n", rp.statusText().toRawUTF8());
                    shot ("guided-done");
                    editor->guided()->pressPrimaryForTest();   // Next point
                }
                if (after (10)) { shot ("guided-next"); editor->guided()->closeNow(); }
                if (after (14))
                {
                    std::printf ("room round 1: %s count=%d acoustics=%d\n", rp.statusText().toRawUTF8(), proc.room.count (false), proc.room.acousticsCount());
                    shot ("room-move");
                    rp.measure();   // the second round: the next positions
                    next (11);
                }
                break;
            case 10:
                if (after (40)) shot ("room-measuring");
                if (stageTick + 5 < ticks && (! rp.busy() || timeout))
                {
                    std::printf ("room round 1: %s count=%d acoustics=%d\n", rp.statusText().toRawUTF8(), proc.room.count (false), proc.room.acousticsCount());
                    shot ("room-move");
                    rp.measure();   // the second round: the next positions
                    next (11);
                }
                break;
            case 11:
                if (stageTick + 5 < ticks && (! rp.busy() || timeout))
                {
                    std::printf ("room round 2: %s count=%d spread=%.1f\n", rp.statusText().toRawUTF8(), proc.room.count (false), proc.room.levelSpread (false));
                    // without the room acoustics allowance, then with it (the default)
                    proc.venuePrefs.set ("roomAllow", 0.0);
                    rp.refresh();
                    rp.suggestNow();
                    std::printf ("room suggest (allowance off): %s\n", rp.statusText().toRawUTF8());
                    for (const auto& b : proc.room.suggested) std::printf ("  off: band %.0f Hz %.1f dB Q %.2f\n", b.freq, b.gainDb, b.q);
                    shot ("room-allow-off");
                    proc.venuePrefs.reset ("roomAllow");
                    rp.refresh();
                    rp.suggestNow();
                    std::printf ("room suggest: %s\n", rp.statusText().toRawUTF8());
                    for (const auto& n : proc.room.allow.notes) std::printf ("  allowance: %s\n", n.toRawUTF8());
                    for (const auto& b : proc.room.suggested) std::printf ("  band %.0f Hz %.1f dB Q %.2f\n", b.freq, b.gainDb, b.q);
                    shot ("room-allow-on");
                    rp.applyNow();
                    // the EQ goes on the whole system: the top and the sub
                    for (const auto& b : proc.system.sources[(size_t) proc.room.eqMembers (proc.system).front()].proc.eq)
                    {
                        system.push_back (Biquad::make (fs, b.freq, b.q, b.gainDb, 1));
                        subSystem.push_back (Biquad::make (fs, b.freq, b.q, b.gainDb, 1));
                    }
                    shot ("room-suggested");
                    rp.measure();
                    next (12);
                }
                break;
            case 12:
                if (stageTick + 5 < ticks && (! rp.busy() || timeout))
                {
                    double lo, hi, rb, mb, ra, ma;
                    proc.room.eqRange (proc.system, lo, hi);
                    proc.room.deviation (proc.room.average (false), lo, hi, rb, mb);
                    proc.room.deviation (proc.room.average (true), lo, hi, ra, ma);
                    std::printf ("room after: count=%d rms before %.2f after %.2f\n", proc.room.count (true), rb, ra);
                    check (proc.room.count (true) > 0 && ra < rb, "room EQ: closer to the target after the EQ");
                    check (std::isfinite (la::seatVariance (proc.room, false)), "room EQ: seat-to-seat variance");
                    rp.suggestNow();
                    std::printf ("room refine: %s (%d bands entered, hint '%s')\n", rp.statusText().toRawUTF8(), (int) proc.room.baseEq.size(), rp.nextHint().toRawUTF8());
                    rp.refresh();
                    shot ("room-listen");
                    // gain and headroom: the whole system in steps, then each speaker
                    editor->showPage ((int) P::Headroom);
                    editor->headroomPageForPreview().runNow (0);
                    next (70);
                }
                break;
            case 70:
            case 71:
            {
                auto& hp = editor->headroomPageForPreview();
                if (stageTick + 10 < ticks && (! hp.running() || ticks - stageTick > 6000))
                {
                    for (const auto& run : proc.headroom.runs)
                    {
                        std::printf ("headroom %s: steps=%d compressedAt=%d clean %.1f reached=%d ceiling=%d\n", run.name.toRawUTF8(), (int) run.steps.size(), run.compressedAt,
                                     run.cleanMax(), (int) run.reached, (int) run.ceiling);
                        for (const auto& st : run.steps) std::printf ("   %.0f dBFS -> %.1f (coh %.2f)\n", st.genDb, st.splDb, st.coherence);
                    }
                    if (stage == 70)
                    {
                        const auto* whole = proc.headroom.find (0);
                        check (whole != nullptr && whole->steps.size() >= 3 && (whole->compressedAt >= 0 || whole->reached || whole->ceiling),
                               "gain and headroom: the whole system run to an end");
                        check (whole != nullptr && whole->compressedAt >= 0, "gain and headroom: the simulated limiter is found");
                        shot ("headroom");
                        proc.system.sources[0].ampGain = "+32 dB";
                        proc.system.sources[0].limiter = "+14 dBu";
                        proc.system.sources[2].ampGain = "+35 dB";
                        hp.refresh();
                        hp.runNow (1);
                        next (71);
                        break;
                    }
                    check (proc.headroom.runs.size() >= 3, "gain and headroom: each speaker run");
                    shot ("headroom-each");
                    // the listening check: two points heard, one with a note
                    proc.listening.music = "Reference playlist (Steely Dan, Norah Jones, a spoken-word track)";
                    proc.listening.points["FOH"] = { 1, "Clear, even", true };
                    proc.listening.points["Centre"] = { 2, "Boomy, speech unclear", true };
                    editor->showPage ((int) P::Listening);
                    next (72);
                }
                break;
            }
            case 72:
                if (after (6))
                {
                    shot ("listening");
                    auto& l = proc.listening;
                    l.done = true;
                    l.doneOn = LiveAlignProcessor::today();
                    proc.room.tuned = true;
                    proc.room.tunedOn = LiveAlignProcessor::today();
                    editor->showPage ((int) P::Room);
                    editor->roomPageForPreview().refresh();
                    next (13);
                }
                break;
            case 13:
                if (after (5)) { shot ("room"); editor->showPage ((int) P::Feedback); editor->feedbackPageForPreview().doNext(); ringHz = 1250.0; }
                if (after (12)) shot ("feedback-listening");
                if (stageTick + 12 < ticks && (proc.feedback.rings.size() >= 1 || ticks - stageTick > 400) && ringHz > 1000.0 && ringHz < 2000.0) ringHz = 3150.0;
                if (stageTick + 12 < ticks && (proc.feedback.rings.size() >= 2 || ticks - stageTick > 800))
                {
                    ringHz = 0.0;
                    for (const auto& r : proc.feedback.rings) std::printf ("feedback ring: %.1f Hz above %.1f dB cut %.0f dB Q %.0f\n", r.ring.freq, r.ring.prominenceDb, r.ring.cutDb, r.ring.q);
                    bool r1 = false, r2 = false;
                    for (const auto& r : proc.feedback.rings) { r1 = r1 || std::abs (r.ring.freq - 1250.0) < 60.0; r2 = r2 || std::abs (r.ring.freq - 3150.0) < 150.0; }
                    check (r1 && r2, "feedback test: both rings found");
                    next (14);
                }
                break;
            case 14:
                if (after (5)) { shot ("feedback"); editor->feedbackPageForPreview().stopListening(); proc.feedback.rings.front().applied = true; }
                if (after (8))
                {
                    // venue plan: a named position, a note, curved seating and the monitoring layer
                    proc.plan.items.push_back ({ la::PlanItem::Marker, "Row 3 (alignment)", 0.36f, 0.42f, 0.0f, 0.0f });
                    proc.plan.items.push_back ({ la::PlanItem::Note, "Sub under stage lip", 0.82f, 0.12f, 0.0f, 0.0f });
                    proc.plan.items.push_back ({ la::PlanItem::Area, "Balcony", 0.86f, 0.62f, 0.2f, 0.3f, la::PlanItem::Curved });
                    proc.plan.edited = true;
                    editor->showPage ((int) P::Plan);
                    next (15);
                }
                break;
            case 40:
                if (after (60)) shot ("acoustics-sweeping");
                if (stageTick + 20 < ticks && (! editor->acousticsPageForPreview().sweeping() || ticks - stageTick > 1500))
                {
                    for (const auto& w : proc.room.sweeps) std::printf ("sweep at %s (%s): RT %.2f s EDT %.2f T30 %.2f range %.0f dB STI %.2f\n", w.position.toRawUTF8(),
                                                                          w.micName.toRawUTF8(), w.ac.rtMid(), w.ac.mid (&la::acoustics::Band::edt),
                                                                          w.ac.mid (&la::acoustics::Band::t30), w.ac.mid (&la::acoustics::Band::rangeDb), w.ac.sti);
                    next (41);
                }
                break;
            case 41:
                if (after (3))
                {
                    const auto a = proc.room.acousticsAverage();
                    check (a.rtMid() > 0.8 && a.rtMid() < 1.8, "room acoustics: the simulated reverberation time (about 1.2 s)");
                    check (a.sti > 0.4 && a.sti < 0.8, "room acoustics: STI");
                    std::printf ("acoustics: positions=%d RT %.2f s EDT %.2f C80 %.1f C50 %.1f STI %.2f (%s) modes=%d\n", proc.room.acousticsCount(), a.rtMid(),
                                 a.mid (&la::acoustics::Band::edt), a.mid (&la::acoustics::Band::c80), a.mid (&la::acoustics::Band::c50), a.sti, la::acoustics::stiRating (a.sti),
                                 (int) proc.room.modes().size());
                    {
                        const auto sm = la::summariseAcoustics (proc);
                        std::printf ("acoustics more: BR %.2f TR %.2f Ts %.0f ms 63Hz %.2f (%s) STI raw %.2f corrected %.2f ALcons %.1f%% ITDG %.1f ms echo %d (%.0f ms, EK %.2f / %.2f) "
                                     "flutter %.0f ms ringing %d Schroeder %.0f Hz Dc %s cov2k %.1f cov4k %.1f\n",
                                     sm.bassRatio, sm.trebleRatio, sm.tsMid, sm.rt63, sm.rt63Reliable ? "clear" : "unsure", sm.stiRaw, sm.stiCorrected, sm.alcons, sm.itdgMs,
                                     (int) sm.echo, sm.echoMs, sm.avg.echoSpeech, sm.avg.echoMusic, sm.flutterMs, sm.ringingThird, sm.schroederHz,
                                     sm.criticalDistance.empty() ? "-" : (sm.criticalDistance.front().name + " " + juce::String (sm.criticalDistance.front().metres, 1) + " m").toRawUTF8(),
                                     sm.coverage2k, sm.coverage4k);
                    }
                    shot ("acoustics");
                    editor->acousticsPageForPreview().showMetric (la::ui::AcousticsChart::Etc);
                    next (43);
                }
                break;
            case 43:
                if (after (4)) { shot ("acoustics-etc"); editor->acousticsPageForPreview().showMetric (la::ui::AcousticsChart::Thirds); }
                if (after (8))
                {
                    shot ("acoustics-thirds");
                    editor->acousticsPageForPreview().showMetric (la::ui::AcousticsChart::RT);
                    editor->openGuided (editor->acousticsPageForPreview());
                    next (42);
                }
                break;
            case 42:
                if (after (6)) { shot ("guided-acoustics"); editor->guided()->closeNow(); }
                if (after (10)) { proc.settings.theme = 1; editor->showPage ((int) P::Response); next (8); }
                break;
            case 15:
                if (after (8)) { shot ("plan"); editor->planPageForPreview().setLayer (1); }
                if (after (12)) { shot ("plan-monitoring"); editor->planPageForPreview().setLayer (0); save (editor->planPageForPreview().getCanvas().exportImage (1600, 0), "plan-export"); }
                if (after (14)) editor->showPage ((int) P::MonitorMic);
                if (after (30)) { shot ("monitor-mic"); next (16); }
                break;
            case 16:
                if (after (2))
                {
                    // an event's sound level, as if logged for two hours
                    proc.splLog.clear();
                    proc.splLog.limitLaeq = 96.0;
                    const double t0 = 1792000000.0;
                    std::mt19937 lr (5);
                    std::normal_distribution<double> nd (0.0, 1.5);
                    for (int i = 0; i < 7200; ++i)
                    {
                        const double t = t0 + i;
                        const double base = 88.0 + 6.0 * std::sin (i / 900.0) + (i > 4200 && i < 5400 ? 6.0 : 0.0);
                        proc.splLog.add (t, (float) (base + nd (lr)), (float) (base + 7.0 + nd (lr)), (float) (base + 6.0 + std::abs (nd (lr)) * 2.0));
                    }
                    std::printf ("spl log: points=%d LAeq %.1f LCeq %.1f max %.1f over %.0f min\n", (int) proc.splLog.points.size(), proc.splLog.leq (false),
                                 proc.splLog.leq (true), proc.splLog.maxLaf(), proc.splLog.minutesOver());
                    la::Trace t;
                    t.name = "Before tuning (FOH)";
                    t.created = "12 Oct 2026 19:40";
                    for (double f = 20.0; f <= 20000.0; f *= 1.05) { t.data.freq.push_back (f); t.data.db.push_back (3.0 * std::sin (std::log (f) * 2.0) - (f > 8000.0 ? 4.0 : 0.0)); }
                    proc.traces.add (t);
                    // the processor sheet, one output ticked as entered
                    proc.system.sources[0].sheetEntered = la::sheet::signature (proc.system, 0);
                    editor->showPage ((int) P::Sheet);
                    shot ("sheet");
                    std::printf ("history: %d changes\n", (int) proc.history.entries.size());
                    check (! proc.history.entries.empty(), "history: changes were logged");
                    {
                        // undo: a trim changed by hand comes back
                        const double was = proc.system.sources[0].trimDb;
                        for (int k = 0; k < 4; ++k) editor->undoTickForPreview();
                        proc.system.sources[0].trimDb = was + 3.0;
                        for (int k = 0; k < 4; ++k) editor->undoTickForPreview();
                        editor->undo();
                        check (std::abs (proc.system.sources[0].trimDb - was) < 1e-9, "undo: a trim comes back");
                        editor->redo();
                        check (std::abs (proc.system.sources[0].trimDb - (was + 3.0)) < 1e-9, "redo: and goes again");
                        editor->undo();
                    }
                    for (const auto& h : proc.history.entries) std::printf ("  %s | %s | %s | %s: %s\n", h.when.toRawUTF8(), h.output.toRawUTF8(), h.what.toRawUTF8(), h.step.toRawUTF8(), h.why.toRawUTF8());
                    editor->sheetPageForPreview().setHistoryShown (true);
                    shot ("sheet-history");
                    editor->sheetPageForPreview().setHistoryShown (false);
                    // a processor with a group stage: the mains' group line, then each output's delay and trim
                    proc.system.groupStage = true;
                    editor->sheetPageForPreview().refresh();
                    shot ("sheet-groups");
                    check (la::sheet::lineCount (proc.system) == (int) proc.system.sources.size() + 1 && la::sheet::lines (proc.system).front().group,
                           "processor sheet: a group stage puts the mains' group line first");
                    proc.system.groupStage = false;
                    editor->sheetPageForPreview().refresh();
                    editor->sheetPageForPreview().writePdf (dir.getChildFile ("sheet.pdf"));
                    std::printf ("sheet: %d bytes, csv:\n%s", (int) dir.getChildFile ("sheet.pdf").getSize(), la::sheet::csv (proc.system).toRawUTF8());
                    editor->showPage ((int) P::TuningReport);
                }
                if (after (10))
                {
                    shot ("report-page");
                    la::UserPrefs::set ("reportAuthor", "J. Ulicki Audio");
                    editor->exportReportTo (false, dir.getChildFile ("report.pdf"));
                    editor->exportReportTo (true, dir.getChildFile ("monitoring-report.pdf"));
                    std::printf ("reports: tuning %d bytes, monitoring %d bytes\n", (int) dir.getChildFile ("report.pdf").getSize(), (int) dir.getChildFile ("monitoring-report.pdf").getSize());
                    check (dir.getChildFile ("report.pdf").getSize() > 50000 && dir.getChildFile ("monitoring-report.pdf").getSize() > 20000, "reports: written");
                    {
                        juce::StringArray ids;
                        for (const auto& sec : la::report::tuningSections()) ids.add (sec.id);
                        check (ids.contains ("coverage") && ids.contains ("headroom") && ids.contains ("listening"), "reports: the new chapters are in the tuning report");
                    }
                    la::LockerMic m;
                    m.id = "preview";
                    m.model = "UMIK-1";
                    m.serial = "7012345";
                    m.cals.name[0] = "7012345.txt";
                    m.cals.table[0].freq = { 20, 1000, 20000 };
                    m.cals.table[0].db = { 1, 0, -1 };
                    m.cals.guessed[0] = true;   // the name gave no angle
                    m.cals.name[1] = "7012345_90deg.txt";
                    m.cals.table[1] = m.cals.table[0];
                    m.hasSpl = true;
                    m.splOffset = 111.25f;
                    m.splDate = "12 Oct 2026";
                    proc.locker.mics.push_back (m);
                    editor->showPage ((int) P::Locker);
                    shot ("locker");
                    proc.locker.load();
                    editor->showPage ((int) P::Home);
                }
                if (after (20))
                {
                    shot ("home");
                    // the quick re-check of the tuned system, point by point
                    auto& rc = editor->recheckForPreview();
                    rc.begin();
                    proc.system.sources[2].proc.invert = ! proc.system.sources[2].proc.invert;   // someone flipped the sub's polarity
                    rc.guidedStart();
                    next (80);
                }
                break;
            case 80:
            {
                auto& rc = editor->recheckForPreview();
                if (stageTick + 5 < ticks && (! rc.guidedBusy() || timeout))
                {
                    if (rc.current() >= 0 && ticks - stageTick < 20000) { rc.guidedStart(); stageTick = ticks; break; }
                    proc.system.sources[2].proc.invert = ! proc.system.sources[2].proc.invert;
                    for (const auto& l : proc.recheck.lines) std::printf ("recheck [%d]: %s\n", l.level, l.text.toRawUTF8());
                    editor->showPage ((int) P::Home);
                    next (81);
                }
                break;
            }
            case 81:
                if (after (10))
                {
                    shot ("home-recheck");
                    editor->showTour (4);
                    shot ("tour");
                    editor->closeTour();
                    // Settings: the tuning defaults with one value kept for this venue
                    proc.venuePrefs.set ("eqClose", 2.0);
                    editor->showPage ((int) P::Settings);
                    editor->settingsPageForPreview().showCategory ((int) la::PrefCat::Tuning);
                    next (82);
                }
                break;
            case 82:
                if (after (6)) { shot ("settings"); editor->settingsPageForPreview().showCategory ((int) la::PrefCat::Monitoring); next (83); }
                break;
            case 83:
                if (after (6)) { shot ("settings-monitoring"); editor->settingsPageForPreview().showCategory ((int) la::PrefCat::Measurement); next (84); }
                break;
            case 84:
                if (after (6))
                {
                    shot ("settings-measurement");
                    std::printf ("settings: venue overrides=%s\n", proc.venuePrefs.describe().joinIntoString ("; ").toRawUTF8());
                    proc.venuePrefs.reset ("eqClose");
                    // listening: the reference on the interface's last output, the level on the main screen
                    proc.setListening (1, juce::jmax (0, proc.numOutputs() - 1), {});
                    editor->settingsPageForPreview().showCategory ((int) la::PrefCat::Audio);
                    next (92);
                }
                break;
            case 92:
                if (after (20))
                {
                    shot ("settings-audio");
                    {
                        // the headphones section of the Audio interface window (the app's own window, drawn here on its own)
                        la::ui::ListenSetup ls (proc);
                        ls.setSize (540, 190);
                        juce::Image img (juce::Image::ARGB, 540, 190, true);
                        {
                            juce::Graphics g (img);
                            g.fillAll (juce::Colour (0xff161d22));
                            ls.paintEntireComponent (g, true);
                        }
                        save (img, "audio-window-listen");
                    }
                    check (proc.settings.listenSource.load() == 1 && editor->listenStripShownForPreview(),
                           "listening: the reference, with its level on the main screen");
                    proc.setListening (0, -1, {});   // off again for the rest of the run
                    editor->settingsPageForPreview().showCategory ((int) la::PrefCat::Units);
                    next (85);
                }
                break;
            case 85:
                if (after (6))
                {
                    shot ("settings-units");
                    // a room sensor (MQTT) with a reading: nothing changes until it is captured
                    climateWas = { proc.settings.temperatureC.load(), proc.settings.humidity.load() };
                    proc.climate.simulateForPreview (24.0f, 31.0f);
                    editor->settingsPageForPreview().showCategory ((int) la::PrefCat::Sensor);
                    next (86);
                }
                break;
            case 86:
                if (after (20))
                {
                    shot ("settings-sensor");
                    check (std::abs (proc.settings.temperatureC.load() - climateWas.first) < 1e-6f,
                           "room sensor: a reading changes nothing by itself");
                    editor->venuePageForPreview().captureFromSensor();
                    check (std::abs (proc.settings.temperatureC.load() - 24.0f) < 0.01f && std::abs (proc.settings.humidity.load() - 31.0f) < 0.01f,
                           "room sensor: Capture takes its temperature and humidity for the session");
                    editor->showPage ((int) P::Venue);
                    next (87);
                }
                break;
            case 87:
                if (after (8))
                {
                    shot ("venue-sensor");
                    // back to the tuned conditions; the sensor now reads warmer and drier than the tuning
                    proc.setConditions (climateWas.first, climateWas.second);
                    proc.climate.simulateForPreview (29.0f, 22.0f);
                    editor->showPage ((int) P::Monitor);
                    next (88);
                }
                break;
            case 88:
                if (after (40))
                {
                    shot ("monitor-conditions");
                    proc.climate.configure ({});   // no sensor for the rest of the run
                    // a show: a monitoring session of its own
                    editor->startSession ("Show " + LiveAlignProcessor::today(), 1);
                    next (89);
                }
                break;
            case 89:
                if (after (30))
                {
                    shot ("session-show");
                    check (proc.monitoringSession() && editor->pageInSession ((int) P::Monitor) && ! editor->pageInSession ((int) P::Room)
                               && ! editor->pageInSession ((int) P::Mics) && editor->pageInSession ((int) P::Locker),
                           "show session: the menu has monitoring, not the tuning steps");
                    proc.settings.genOn = true;
                    check (proc.settings.genLocked.load(), "show session: the generator is locked off");
                    check (! proc.tuningRoom().meas.empty() && proc.tuningRoomName().isNotEmpty(),
                           "show session: compared with the venue's tuning session");
                    proc.settings.genOn = false;
                    editor->showPage ((int) P::Plan);
                    next (91);
                }
                break;
            case 91:
                if (after (8))
                {
                    shot ("plan-show");
                    check (editor->planPageForPreview().layer() == 1, "show session: the plan opens on the monitoring points");
                    editor->showPage ((int) P::Home);
                    next (90);
                }
                break;
            case 90:
                if (after (10))
                {
                    shot ("home-show");
                    // back to the tuning session for the rest of the run
                    proc.switchSession (0);
                    editor->afterProjectLoadedForPreview();
                    check (! proc.monitoringSession() && ! proc.settings.genLocked.load() && editor->pageInSession ((int) P::Room)
                               && editor->planPageForPreview().layer() == 0,
                           "tuning session again: the tuning steps and the tuning points are back");
                    proc.deleteSession (1);
                    editor->afterProjectLoadedForPreview();
                    editor->showPage ((int) P::Home);
                    next (17);
                }
                break;
            case 17:
            {
                // the speaker system survives a venue save / open
                const auto f = dir.getChildFile ("speakers.lavenue");
                proc.saveProject (f);
                const auto before = proc.system.sources[2].response.freq.size();
                proc.newProject();
                proc.openProject (f);
                check (proc.system.sources.size() == 4 && proc.system.sources[2].response.freq.size() == before, "venue: the speakers survive a save and open");
                check (! proc.history.entries.empty() && proc.listening.done && ! proc.headroom.runs.empty(), "venue: history, listening and headroom survive a save and open");
                check (proc.monitorPoints.contains ("FOH"), "venue: the monitoring points survive");
                std::printf ("venue speakers: sources=%d subPoints=%d (was %d) lp=%d@%.1f alignments=%d checked=%d nearField=%d placement=%d\n", (int) proc.system.sources.size(),
                             (int) proc.system.sources[2].response.freq.size(), (int) before, proc.system.sources[2].proc.lp.slope, proc.system.sources[2].proc.lp.freq,
                             (int) proc.system.alignments.size(), proc.system.alignments.empty() ? -1 : (int) proc.system.alignments[0].checked(),
                             (int) proc.system.sources[0].nearField, proc.system.placement);
                std::printf ("venue polarity: Main L %s, Main R %s (against %d), Sub %s\n", la::polarity::verdictName (proc.system.sources[0].polaritySummary()),
                             la::polarity::verdictName (proc.system.sources[1].polaritySummary()),
                             proc.system.sources[1].polarityOf (0) != nullptr ? proc.system.sources[1].polarityOf (0)->against : -9,
                             la::polarity::verdictName (proc.system.sources[2].polaritySummary()));
                std::printf ("venue room: before=%d after=%d acoustics=%d tuned=%d feedback rings=%d asFound=%d log=%d monitorMic=%s plan monitor placed=%d\n", proc.room.count (false),
                             proc.room.count (true), proc.room.acousticsCount(), (int) proc.room.tuned, (int) proc.feedback.rings.size(), (int) proc.asFound.taken(),
                             (int) proc.splLog.points.size(), proc.monitorMic.name.toRawUTF8(), (int) proc.plan.monitorMic.placed);
                f.deleteFile();
                lockerAndUpdateChecks();
                sessionRoundTrip();
                crossoverPairCheck();
                templateAndRecovery();
                {
                    // deleting sessions down to none: the last one is replaced by an empty one of the same kind
                    while (proc.getSessions().size() > 1) proc.deleteSession ((int) proc.getSessions().size() - 1);
                    const int kind = proc.sessionKind();
                    proc.deleteSession (0);
                    check (proc.getSessions().size() == 1 && proc.sessionKind() == kind && proc.traces.size() == 0 && proc.room.meas.empty(),
                           "sessions: the only session can be deleted (an empty one takes its place)");
                }
                {
                    // New venue: named, and its first session of the kind picked (here a show)
                    editor->startVenue ("Grace Chapel", 1);
                    editor->showPage ((int) P::Home);
                    shot ("home-new-show-venue");
                    check (proc.venueName == "Grace Chapel" && proc.monitoringSession() && proc.getSessions().size() == 1 && ! proc.projectChangedNow()
                           && std::abs (proc.settings.temperatureC.load() - 20.0f) < 0.01f && ! editor->unsavedForPreview(),
                           "new venue: named, its first session a show, the usual conditions, nothing unsaved yet");
                    editor->startVenue ("Grace Chapel", 0);
                    shot ("home-new-tuning-venue");
                }
                finish();
                break;
            }
            default: break;
        }
    }

    // The crossover pair follows the roles: Sub first in the list still gives Main L above, Sub below.
    void crossoverPairCheck()
    {
        la::SpeakerSystem s;
        la::SpeakerSource sb, l, r;
        sb.name = "Sub"; sb.role = (int) la::SpeakerRole::Sub;
        l.name = "Main L"; l.role = (int) la::SpeakerRole::Main; l.channel = (int) la::SpeakerChannel::Left;
        r = l; r.name = "Main R"; r.channel = (int) la::SpeakerChannel::Right; r.linkTo = 1;
        s.sources = { sb, l, r };
        s.ensurePair();
        std::printf ("xo pair: high=%d low=%d inverted=%d", s.xoHigh, s.xoLow, (int) s.pairInverted());
        s.xoHigh = 0; s.xoLow = 1; s.xoChosen = true;
        std::printf ("  forced Sub/Main L: inverted=%d", (int) s.pairInverted());
        s.swapPair();
        std::printf ("  after swap: high=%d low=%d inverted=%d\n", s.xoHigh, s.xoLow, (int) s.pairInverted());
        // on screen: an upside-down pair shows the warning and Find best crossover refuses it
        std::swap (proc.system.xoHigh, proc.system.xoLow);
        proc.system.xoChosen = true;
        auto& page = editor->speakersPage();
        editor->showPage (2);
        page.refreshChoices();
        page.findCrossover();
        std::printf ("inverted find: status='%s'\n", page.statusText().toRawUTF8());
        shot ("speakers-inverted-pair");
        std::swap (proc.system.xoHigh, proc.system.xoLow);
    }

    void templateAndRecovery()
    {
        proc.venueName = "Tour A";
        proc.system.sources[0].proc.eq = { { 250.0, -3.0, 2.0 } };
        proc.system.sources[0].proc.hp = { la::xo::Butterworth, 24, 100.0 };
        proc.system.sources[0].response.freq = { 100, 1000, 10000 };
        proc.system.sources[0].response.db = { 0, 0, 0 };
        proc.positions.addIfNotAlreadyThere ("Mix riser");
        proc.newVenueFrom ("Tour B");
        check (proc.venueName == "Tour B" && proc.system.sources[0].response.freq.empty() && proc.system.sources[0].proc.eq.empty() && proc.positions.contains ("Mix riser")
               && proc.history.entries.empty(), "new venue from this one: keeps the set-up, starts the tuning fresh");
        std::printf ("template: name=%s sources=%d response=%d hp=%.0f eq=%d alignments=%d positions=%d sessions=%d changed=%d mic1=%s\n", proc.venueName.toRawUTF8(),
                     (int) proc.system.sources.size(), (int) proc.system.sources[0].response.freq.size(), proc.system.sources[0].proc.hp.freq,
                     (int) proc.system.sources[0].proc.eq.size(), (int) proc.system.alignments.size(), proc.positions.contains ("Mix riser") ? 1 : 0,
                     (int) proc.getSessions().size(), (int) proc.projectChanged(), proc.mics.name[0].toRawUTF8());
        proc.venueNotes = "Unsaved note";
        proc.autosave();
        const bool written = LiveAlignProcessor::recoveryFile().existsAsFile();
        proc.newProject();
        check (proc.system.sources.empty() && proc.settings.micCount.load() == 1 && ! proc.settings.micSplCalibrated[0].load() && proc.mics.position[0].isEmpty()
               && proc.venuePrefs.values.empty() && proc.monitorPoints.contains ("FOH"), "new venue: blank (no speakers, one mic, nothing attached)");
        const auto err = proc.restoreRecovery();
        check (written && err.isEmpty() && proc.venueNotes == "Unsaved note", "recovery: unsaved work comes back");
        std::printf ("recovery: written=%d err='%s' name=%s notes=%s changed=%d\n", (int) written, err.toRawUTF8(), proc.venueName.toRawUTF8(),
                     proc.venueNotes.toRawUTF8(), (int) proc.projectChanged());
        proc.clearRecovery();
        std::printf ("recovery cleared: %d\n", (int) ! LiveAlignProcessor::recoveryFile().existsAsFile());
    }

    void lockerAndUpdateChecks()
    {
        using U = la::UpdateChecker;
        std::printf ("versions: %d %d %d %d\n", U::compareVersions ("0.10.0", "0.9.9"), U::compareVersions ("v0.7.0", "0.7.0"),
                     U::compareVersions ("0.7.0", "0.7.1"), U::compareVersions ("1.0", "0.99.0"));
        const auto rel = U::parseRelease (R"({"tag_name":"v9.1.0","html_url":"https://github.com/x/y/releases/tag/v9.1.0","body":"New:\n- a\n- b"})", "0.7.0");
        const auto same = U::parseRelease (R"({"tag_name":"v0.7.0"})", "0.7.0");
        std::printf ("parse: state=%d latest=%s notes=%d | same state=%d | bad state=%d\n", (int) rel.state, rel.latest.toRawUTF8(),
                     rel.notes.length(), (int) same.state, (int) U::parseRelease ("not json", "0.7.0").state);

        // mic locker: save mic 1, change it, assign the saved mic to mic 3, check it came across
        const auto lockerBackup = la::MicLocker::file().loadFileAsString();
        // two calibration files picked up by name: miniDSP style "7012345.txt" (0 deg) and "7012345_90deg.txt"
        const auto f0 = dir.getChildFile ("7012345.txt"), f90 = dir.getChildFile ("7012345_90deg.txt");
        f0.replaceWithText ("\"Sens Factor =-1.5dB\"\n20 1.5\n1000 0\n10000 1.0\n20000 -2.0\n");
        f90.replaceWithText ("20 1.5\n1000 0\n10000 4.0\n20000 6.0\n");
        proc.loadCalibrationFile (0, f0);
        proc.loadCalibrationFile (0, f90);
        proc.setMicAim (0, 1);
        const auto up = proc.analyzer.getCalibrationName (0);
        proc.setAimOverride (0);
        const auto overridden = proc.analyzer.getCalibrationName (0);
        proc.setAimOverride (-1);
        proc.setMicAim (0, 0);
        {
            juce::StringArray names { "7012345_90deg.txt", "SoundID Reference 38Y362 30deg.txt", "UMIK-1 7012345.txt", "ECM8000 90.txt", "mic_3012345.txt",
                                      "M30 cal 45 deg.frd", "x_180deg.txt", "UMIK_60.txt" };
            juce::String got;
            for (const auto& n : names) got << la::calAngleName (la::MicCalSet::angleFromName (n)) << " ";
            std::printf ("cal names -> angles: %s(want 90 30 0 90 0 45 180 60)\n", got.toRawUTF8());
            la::MicCalSet set;
            set.table[la::Cal0].freq = { 20, 1000, 20000 }; set.table[la::Cal30].freq = { 20, 1000, 20000 }; set.table[la::Cal90].freq = { 20, 1000, 20000 };
            std::printf ("cal pick: aim 45 -> %s, aim 60 -> %s, aim 180 -> %s, label %s\n", la::calAngleName (set.pick (la::Cal45)).toRawUTF8(),
                         la::calAngleName (set.pick (la::Cal60)).toRawUTF8(), la::calAngleName (set.pick (la::Cal180)).toRawUTF8(), set.label().toRawUTF8());
        }
        std::printf ("cal angles: has0=%d has90=%d aimUp=%s override0=%s aim0=%s label=%s\n", (int) proc.calibrations (0).has (0),
                     (int) proc.calibrations (0).has (1), up.toRawUTF8(), overridden.toRawUTF8(), proc.analyzer.getCalibrationName (0).toRawUTF8(),
                     proc.calibrations (0).label().toRawUTF8());
        {
            auto x = proc.createProjectXml();
            proc.loadProjectXml (*x);
            std::printf ("cal venue round trip: has0=%d has90=%d aim=%d active=%s sens=%d\n", (int) proc.calibrations (0).has (0), (int) proc.calibrations (0).has (1),
                         proc.mics.aim[0], proc.analyzer.getCalibrationName (0).toRawUTF8(), (int) proc.calibrations (0).table[0].hasSensFactor);
        }
        f0.deleteFile();
        f90.deleteFile();
        proc.settings.micSplCalibrated[0] = true;
        proc.settings.micSplOffset[0] = 111.25f;
        const auto id = proc.saveMicToLocker (0, "UMIK-1 #7012345");
        proc.clearCalibration (2);
        proc.settings.micSplCalibrated[2] = false;
        proc.assignLockerMic (2, id);
        std::printf ("locker: entries=%d mic3 name=%s cal=%s offset=%.2f calibrated=%d linkMoved=%d\n", (int) proc.locker.mics.size(),
                     proc.mics.name[2].toRawUTF8(), proc.calibrations (2).label().toRawUTF8(), (double) proc.settings.micSplOffset[2].load(),
                     (int) proc.settings.micSplCalibrated[2].load(), (int) proc.mics.lockerId[0].isEmpty());
        proc.settings.micSplOffset[2] = 112.0f;
        const bool synced = proc.syncLocker();
        la::MicLocker reread;
        reread.load();
        const auto* e = reread.find (id);
        std::printf ("locker sync: saved=%d offset on disk=%.2f cal points=%d\n", (int) synced, e != nullptr ? (double) e->splOffset : -1.0,
                     e != nullptr ? (int) e->cals.table[1].freq.size() : -1);
        if (lockerBackup.isEmpty()) la::MicLocker::file().deleteFile(); else la::MicLocker::file().replaceWithText (lockerBackup);
        proc.locker.load();
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

    void shot (const char* name)
    {
        save (editor->createComponentSnapshot (editor->getLocalBounds()), name);
        overflowCheck (*editor, name);
        if (gStopAfter.isNotEmpty() && gStopAfter == name) stopNow = true;   // at the next tick: the step that took it carries on first
    }

    // Text that does not fit its button, label or box (it would be cut off or squeezed), on what is showing now.
    void overflowCheck (juce::Component& c, const char* where)
    {
        if (! c.isShowing() || c.getWidth() <= 0) return;
        juce::String text;
        juce::Font f { juce::FontOptions {} };
        int room = 0;
        if (auto* b = dynamic_cast<juce::TextButton*> (&c)) { text = b->getButtonText(); f = c.getLookAndFeel().getTextButtonFont (*b, b->getHeight()); room = b->getWidth() - 8; }
        else if (auto* l = dynamic_cast<juce::Label*> (&c)) { text = l->getText(); f = l->getFont(); room = l->getWidth() - l->getBorderSize().getLeftAndRight(); }
        else if (auto* tb = dynamic_cast<juce::ToggleButton*> (&c)) { text = tb->getButtonText(); f = juce::Font (juce::FontOptions (juce::jmin (15.0f, (float) tb->getHeight() * 0.75f))); room = tb->getWidth() - 50; }
        else if (auto* cb = dynamic_cast<juce::ComboBox*> (&c)) { text = cb->getText(); f = c.getLookAndFeel().getComboBoxFont (*cb); room = cb->getWidth() - 30; }
        if (text.isNotEmpty() && ! text.containsChar ('\n'))
        {
            const int need = juce::GlyphArrangement::getStringWidthInt (f, text);
            if (need > room + 2)
            {
                const auto key = juce::String (typeid (c).name()) + "|" + text;
                if (! gOverflowSeen.contains (key))
                {
                    gOverflowSeen.add (key);
                    ++gOverflows;
                    std::printf ("OVERFLOW in %s: \"%s\" needs %d px, has %d\n", where, text.toRawUTF8(), need, room);
                }
            }
        }
        for (auto* ch : c.getChildren()) overflowCheck (*ch, where);
    }

    void finish()
    {
        std::printf ("RESULT: %d checks, %d failed, %d text overflows\n", gChecks, gFailures, gOverflows);
        if (gStrictOverflow && gOverflows > 0) ++gFailures;
        stage = 99;
        stopTimer();
        editor.reset();
        juce::MessageManager::getInstance()->stopDispatchLoop();
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
    std::vector<Biquad> system;
    std::vector<MicPath> paths;
    std::vector<Biquad> top, sub;
    int stage = 0, stageTick = 0, lastOut = 0, levelTask = 0;
    std::vector<Biquad> fillChain;
    std::vector<float> lineTop, lineSub, lineFill;
    std::array<Reverb, 3> reverbs;
    double ringHz = 0.0, subGain = 1.0, simMs = 0.0;
    bool stopNow = false;
    std::pair<float, float> climateWas { 20.0f, 50.0f };

    // A processor filter as biquads: Butterworth of the slope's order, Linkwitz-Riley as two Butterworths of half.
    void addFilters (std::vector<Biquad>& chain, const la::xo::Filter& f, bool highPass)
    {
        if (! f.active()) return;
        const bool lr = f.type == la::xo::LinkwitzRiley;
        const int order = lr ? f.slope / 12 : f.slope / 6;
        for (int rep = 0; rep < (lr ? 2 : 1); ++rep)
            for (int k = 1; k <= order / 2; ++k)
            {
                const double q = 1.0 / (2.0 * std::cos ((2.0 * k - 1.0) * la::kPi / (2.0 * order)));
                chain.push_back (Biquad::make (fs, f.freq, q, 0.0, highPass ? 0 : 2));
            }
    }
    long simSubDelay = 0, simTopDelay = 0;
    bool simRightInverted = false;
    int consoleOut = -1;
    std::array<int, 2> savedOuts {};
    double savedSub = 0.0;   // "music from the console", routed to one output (-1: everything plays)
    std::vector<Biquad> subSystem;   // the processor's EQ on the sub
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
    for (int i = 2; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a.startsWith ("--stop-after=")) gStopAfter = a.fromFirstOccurrenceOf ("=", false, false);
        if (a == "--strict-overflow") gStrictOverflow = true;
    }
    {
        // the application icon (macOS and Windows): the tile with the usual margin around it
        juce::Image icon (juce::Image::ARGB, 1024, 1024, true);
        juce::Graphics ig (icon);
        la::ui::drawLogo (ig, juce::Rectangle<float> (1024.0f, 1024.0f).reduced (100.0f), true);
        juce::FileOutputStream os (out.getChildFile ("LiveAlignIcon.png"));
        if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (icon, os); }
    }
    auto proc = std::make_unique<LiveAlignProcessor>();
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (juce::AudioChannelSet::discreteChannels (16));
    layout.outputBuses.add (juce::AudioChannelSet::discreteChannels (8));
    proc->setBusesLayout (layout);
    proc->prepareToPlay (48000.0, 512);
    proc->setMonitorMode (false);   // made outside a host: not the VST3
    proc->system = la::SpeakerSystem::defaults();   // a new venue has no speakers: the simulated system's three
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
    return gFailures == 0 ? 0 : 1;
}
