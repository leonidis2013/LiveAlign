// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Local check only (not part of the release build): drives the processor with a simulated PA, room and three mics,
// saves screenshots of every page, and round-trips a venue file with two sessions. Build with -DLA_BUILD_PREVIEW=ON.
#include <array>
#include <random>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
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
        startTimer (20);
    }

    void feed (int samples)
    {
        juce::AudioBuffer<float> buf (16, 512);
        juce::MidiBuffer midi;
        std::normal_distribution<double> nd (0.0, 0.1);
        const size_t L = lineTop.size();
        for (int done = 0; done < samples; done += 512)
        {
            buf.clear();
            for (int i = 0; i < 512; ++i)
            {
                const float ref = (float) nd (rng);
                // while a speaker is measured, only the speaker on the generator's output plays; "all outputs" plays them all
                const int out = proc.settings.genOn.load() ? proc.settings.genOutput.load() : 0;
                if (out != lastOut) { std::printf ("  sim: output %d at tick %d\n", out, ticks); lastOut = out; }
                double t = 0.0, sb = 0.0, fl = 0.0;
                // outputs: 1 / 2 main left / right, 3 sub, 4 front fill
                if (out == 0 || out == 1 || out == 2) { t = ref; for (auto& f : top) t = f (t); for (auto& f : system) t = f (t); }
                if (out == 0 || out == 3) { sb = ref; for (auto& f : sub) sb = f (sb); sb *= 2.0 * subGain; }
                if (out == 4) { fl = ref; for (auto& f : fillChain) fl = f (fl); fl *= 0.5; }
                lineTop[wp % L] = (float) t;
                lineSub[wp % L] = (float) sb;
                lineFill[wp % L] = (float) fl;
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
        ++ticks;
        feed (960);
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
        if (ticks == 165) shot ("home-start");
        if (ticks == 166) editor->showPage ((int) P::Mics);
        if (ticks == 182) shot ("mic-setup");
        if (ticks == 183) editor->showPage ((int) P::Monitor);
        if (ticks == 200) shot ("monitor");
        if (ticks == 201) { proc.settings.paneCount = 3; editor->setPaneViews (1, 0, 2); }
        if (ticks == 218) shot ("monitor-three");
        if (ticks == 219) { proc.settings.paneCount = 1; editor->showPage ((int) P::Speakers); }
        if (ticks == 225) shot ("speakers-system");
        if (ticks == 226) { proc.settings.theme = 1; editor->showPage ((int) P::Home); }
        if (ticks == 240) shot ("home-light");
        if (ticks == 241) editor->showPage ((int) P::Monitor);
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
            editor->showPage ((int) P::Response);
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
                    shot ("response");
                    editor->showPage ((int) P::Crossover);
                    page.findCrossover();
                    next (3);
                }
                break;
            case 3:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& r = proc.system.lastResult;
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
                    // a front fill on output 4; then line the sub up with the main where they meet (FOH)
                    la::SpeakerSource fill;
                    fill.name = "Front fill";
                    fill.role = (int) la::SpeakerRole::Fill;
                    fill.output = 4;
                    proc.system.sources.push_back (fill);
                    editor->showPage ((int) P::Align);
                    page.refreshChoices();
                    page.newAlignment();
                    page.measureAlignment (0);
                    next (5);
                }
                break;
            case 5:
                if (stageTick + 5 < ticks && (! page.busy() || timeout))
                {
                    const auto& a = proc.system.alignments[0];
                    std::printf ("alignment 1: ref=%s align=%s at %s arrivals %.3f / %.3f phase=%d %.2f need=%.3f\n", proc.system.sources[(size_t) a.ref].name.toRawUTF8(),
                                 proc.system.sources[(size_t) a.align].name.toRawUTF8(), a.spot.toRawUTF8(), a.refArrivalMs, a.alignArrivalMs, (int) a.phaseValid, a.phaseDelayMs,
                                 a.neededMs (proc.system.sources[(size_t) a.ref].proc.delayMs));
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
                    proc.settings.theme = 1;
                    editor->showPage ((int) P::Response);
                    next (8);
                }
                break;
            case 8:
                if (after (15)) { shot ("response-light"); proc.settings.theme = 0; editor->showPage ((int) P::Monitor); proc.settings.view = 3; }
                if (after (50))
                {
                    shot ("monitor-waterfall");
                    proc.settings.view = 1;
                    editor->showPage ((int) P::Room);
                    rp.setMeasuringAfter (false);
                    rp.measure();   // the mics go to the first planned positions by themselves
                    next (10);
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
                    rp.suggestNow();
                    std::printf ("room suggest: %s\n", rp.statusText().toRawUTF8());
                    for (const auto& b : proc.room.suggested) std::printf ("  band %.0f Hz %.1f dB Q %.2f\n", b.freq, b.gainDb, b.q);
                    rp.applyNow();
                    for (const auto& b : proc.system.sources[(size_t) proc.room.eqSpeaker].proc.eq) system.push_back (Biquad::make (fs, b.freq, b.q, b.gainDb, 1));
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
                    proc.room.tuned = true;
                    proc.room.tunedOn = LiveAlignProcessor::today();
                    rp.refresh();
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
                    next (14);
                }
                break;
            case 14:
                if (after (5)) { shot ("feedback"); editor->feedbackPageForPreview().stopListening(); proc.feedback.rings.front().applied = true; editor->showPage ((int) P::Acoustics); }
                if (after (12))
                {
                    const auto a = proc.room.acousticsAverage();
                    std::printf ("acoustics: positions=%d RT %.2f s EDT %.2f C80 %.1f C50 %.1f STI %.2f (%s) modes=%d\n", proc.room.acousticsCount(), a.rtMid(),
                                 a.mid (&la::acoustics::Band::edt), a.mid (&la::acoustics::Band::c80), a.mid (&la::acoustics::Band::c50), a.sti, la::acoustics::stiRating (a.sti),
                                 (int) proc.room.modes().size());
                    shot ("acoustics");
                    // venue plan: a named position, a note, curved seating and the monitoring layer
                    proc.plan.items.push_back ({ la::PlanItem::Marker, "Row 3 (alignment)", 0.36f, 0.42f, 0.0f, 0.0f });
                    proc.plan.items.push_back ({ la::PlanItem::Note, "Sub under stage lip", 0.82f, 0.12f, 0.0f, 0.0f });
                    proc.plan.items.push_back ({ la::PlanItem::Area, "Balcony", 0.86f, 0.62f, 0.2f, 0.3f, la::PlanItem::Curved });
                    proc.plan.edited = true;
                    editor->showPage ((int) P::Plan);
                    next (15);
                }
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
                    editor->showPage ((int) P::TuningReport);
                }
                if (after (10))
                {
                    shot ("report-page");
                    la::UserPrefs::set ("reportAuthor", "J. Ulicki Audio");
                    editor->exportReportTo (false, dir.getChildFile ("report.pdf"));
                    editor->exportReportTo (true, dir.getChildFile ("monitoring-report.pdf"));
                    std::printf ("reports: tuning %d bytes, monitoring %d bytes\n", (int) dir.getChildFile ("report.pdf").getSize(), (int) dir.getChildFile ("monitoring-report.pdf").getSize());
                    la::LockerMic m;
                    m.id = "preview";
                    m.model = "UMIK-1";
                    m.serial = "7012345";
                    m.cals.name[0] = "7012345.txt";
                    m.cals.table[0].freq = { 20, 1000, 20000 };
                    m.cals.table[0].db = { 1, 0, -1 };
                    m.hasSpl = true;
                    m.splOffset = 111.25f;
                    m.splDate = "12 Oct 2026";
                    proc.locker.mics.push_back (m);
                    editor->showPage ((int) P::Locker);
                    shot ("locker");
                    proc.locker.load();
                    editor->showPage ((int) P::Home);
                }
                if (after (20)) { shot ("home"); next (17); }
                break;
            case 17:
            {
                // the speaker system survives a venue save / open
                const auto f = dir.getChildFile ("speakers.lavenue");
                proc.saveProject (f);
                const auto before = proc.system.sources[2].response.freq.size();
                proc.newProject();
                proc.openProject (f);
                std::printf ("venue speakers: sources=%d subPoints=%d (was %d) lp=%d@%.1f alignments=%d checked=%d nearField=%d placement=%d\n", (int) proc.system.sources.size(),
                             (int) proc.system.sources[2].response.freq.size(), (int) before, proc.system.sources[2].proc.lp.slope, proc.system.sources[2].proc.lp.freq,
                             (int) proc.system.alignments.size(), proc.system.alignments.empty() ? -1 : (int) proc.system.alignments[0].checked(),
                             (int) proc.system.sources[0].nearField, proc.system.placement);
                std::printf ("venue room: before=%d after=%d acoustics=%d tuned=%d feedback rings=%d asFound=%d log=%d monitorMic=%s plan monitor placed=%d\n", proc.room.count (false),
                             proc.room.count (true), proc.room.acousticsCount(), (int) proc.room.tuned, (int) proc.feedback.rings.size(), (int) proc.asFound.taken(),
                             (int) proc.splLog.points.size(), proc.monitorMic.name.toRawUTF8(), (int) proc.plan.monitorMic.placed);
                f.deleteFile();
                lockerAndUpdateChecks();
                sessionRoundTrip();
                crossoverPairCheck();
                templateAndRecovery();
                stage = 99;
                editor.reset();
                juce::MessageManager::getInstance()->stopDispatchLoop();
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
        std::printf ("template: name=%s sources=%d response=%d hp=%.0f eq=%d alignments=%d positions=%d sessions=%d changed=%d mic1=%s\n", proc.venueName.toRawUTF8(),
                     (int) proc.system.sources.size(), (int) proc.system.sources[0].response.freq.size(), proc.system.sources[0].proc.hp.freq,
                     (int) proc.system.sources[0].proc.eq.size(), (int) proc.system.alignments.size(), proc.positions.contains ("Mix riser") ? 1 : 0,
                     (int) proc.getSessions().size(), (int) proc.projectChanged(), proc.mics.name[0].toRawUTF8());
        proc.venueNotes = "Unsaved note";
        proc.autosave();
        const bool written = LiveAlignProcessor::recoveryFile().existsAsFile();
        proc.newProject();
        const auto err = proc.restoreRecovery();
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
    std::vector<Biquad> top, sub;
    int stage = 0, stageTick = 0, lastOut = 0;
    std::vector<Biquad> fillChain;
    std::vector<float> lineTop, lineSub, lineFill;
    std::array<Reverb, 3> reverbs;
    double ringHz = 0.0, subGain = 1.0;

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
    proc->setMonitorMode (false);   // made outside a host: not the VST3
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
