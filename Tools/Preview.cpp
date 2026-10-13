// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

// Local check only (not part of the release build): drives the processor with a simulated PA, room and three mics,
// saves screenshots of every page, and round-trips a venue file with two sessions. Build with -DLA_BUILD_PREVIEW=ON.
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
        top = { Biquad::make (fs, 75.0, 0.7, 0, 0), Biquad::make (fs, 2500.0, 2.0, 4.0, 1), Biquad::make (fs, 15000.0, 0.7, 0, 2) };
        sub = { Biquad::make (fs, 110.0, 0.7, 0, 2), Biquad::make (fs, 110.0, 0.7, 0, 2), Biquad::make (fs, 28.0, 0.7, 0, 0), Biquad::make (fs, 55.0, 2.0, 3.0, 1) };
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
                // while the Speakers page measures, only the speaker on the generator's output plays
                const int out = proc.settings.genOn.load() ? proc.settings.genOutput.load() : 0;
                if (out != lastOut) { std::printf ("  sim: output %d at tick %d written=%llu\n", out, ticks, (unsigned long long) proc.analyzer.samplesWritten()); lastOut = out; }
                // outputs: 1 / 2 main left / right, 3 sub, 4 front fill
                if (out == 1 || out == 2 || out == 4) { for (auto& f : top) y = f (y); if (out == 4) y *= 0.5; }
                else if (out == 3) { for (auto& f : sub) y = f (y); y *= 2.0; }
                else for (auto& f : system) y = f (y);
                line[wp % L] = (float) y;
                const int inputs[] = { 0, 2, 3 };
                for (size_t m = 0; m < paths.size(); ++m)
                {
                    auto& pth = paths[m];
                    // the sub stands 2 ms further from the mics than the tops
                    // the sub stands 2 ms further away, the front fill (output 3) 3 ms closer
                    const int go = proc.settings.genOn.load() ? proc.settings.genOutput.load() : 0;
                    const size_t extra = go == 3 ? 96 : 0;
                    const size_t closer = go == 4 ? 144 : 0;
                    double v = pth.gain * line[(wp - (size_t) pth.delay - extra + closer + L) % L] + pth.gain * pth.refl * line[(wp - (size_t) (pth.delay + pth.reflDelay) - extra + closer + L) % L];
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
        if (ticks == 165) shot ("home");
        if (ticks == 166) editor->showPage (1);
        if (ticks == 182) shot ("mic-setup");
        if (ticks == 183) { proc.settings.selectedMic = la::kAverageMic; editor->showPage (4); }
        if (ticks == 200) shot ("monitor-average");
        if (ticks == 201) { proc.settings.selectedMic = 1; proc.settings.paneCount = 3; editor->setPaneViews (1, 0, 2); }
        if (ticks == 218) shot ("monitor-three");
        if (ticks == 219) { proc.settings.paneCount = 1; editor->showPage (2); }
        if (ticks == 225) shot ("speakers-empty");
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
            editor->showPage (2);
            editor->speakersPage().measure (0);
            stage = 1;
            stageTick = ticks;
        }
        if (stage > 0 && stage < 99 && editor != nullptr) speakersFlow();
    }

    // Measures the simulated top and sub through the Speakers page, finds the crossover, then ends.
    void speakersFlow()
    {
        auto& page = editor->speakersPage();
        const bool timeout = ticks - stageTick > 6000;
        if (stage == 1 && (! page.busy() || timeout))
        {
            const auto& s = proc.system.sources[0];
            std::printf ("status: %s (%d ticks, analysis behind %.1f s)\n", page.statusText().toRawUTF8(), ticks - stageTick, (double) (proc.analyzer.samplesWritten() - proc.analyzer.samplesAnalysed()) / 48000.0);
            std::printf ("measure Main: reliable=%.2f hf=%.1f response=%d points=%d arrival=%d %.3f ms locked=%.3f\n", la::xo::usableReliability (s.response, 0.4), s.response.dbAt (8000.0), (int) s.hasResponse(), (int) s.response.freq.size(),
                         (int) s.hasArrival, s.arrivalMs, proc.system.lockedDelayMs);
            page.measure (2);
            stage = 2;
            stageTick = ticks;
        }
        else if (stage == 1 && ticks == stageTick + 60) shot ("speakers-measuring");
        else if (stage == 2 && (! page.busy() || timeout))
        {
            const auto& s = proc.system.sources[2];
            std::printf ("status: %s (%d ticks, analysis behind %.1f s)\n", page.statusText().toRawUTF8(), ticks - stageTick, (double) (proc.analyzer.samplesWritten() - proc.analyzer.samplesAnalysed()) / 48000.0);
            std::printf ("measure Sub: response=%d points=%d arrival=%d %.3f ms\n", (int) s.hasResponse(), (int) s.response.freq.size(), (int) s.hasArrival, s.arrivalMs);
            for (double f : { 30.0, 60.0, 120.0, 250.0, 1000.0, 5000.0 })
                std::printf ("  %6.0f Hz  main %6.1f dB %6.0f deg   sub %6.1f dB %6.0f deg\n", f, proc.system.sources[0].response.dbAt (f), proc.system.sources[0].response.phaseAt (f),
                             proc.system.sources[2].response.dbAt (f), proc.system.sources[2].response.phaseAt (f));
            page.findCrossover();
            stage = 3;
            stageTick = ticks;
        }
        else if (stage == 3 && (! page.busy() || timeout))
        {
            const auto& r = proc.system.lastResult;
            std::printf ("crossover: valid=%d %s %d dB/oct at %.1f Hz, low delay %.2f ms, invert %d, gain %.1f dB, cost %.2f, eff %.2f, overlap %.0f-%.0f\n",
                         (int) r.valid, la::xo::typeName (r.filter.type), r.filter.slope, r.filter.freq, r.delayLowMs, (int) r.invertLow, r.gainLowDb,
                         r.costDb, r.efficiency, r.highUsableFrom, r.lowUsableTo);
            stage = 4;
            stageTick = ticks;
        }
        else if (stage == 4 && ticks == stageTick + 10)
        {
            shot ("speakers");
            // a front fill on output 3, lined up to the main
            la::SpeakerSource fill;
            fill.name = "Front fill";
            fill.role = (int) la::SpeakerRole::Fill;
            fill.output = 4;
            proc.system.sources.push_back (fill);
            page.refreshChoices();
            page.newAlignment();
            page.measureAlignment (0);
            stage = 5;
            stageTick = ticks;
        }
        else if (stage == 5 && (! page.busy() || timeout))
        {
            const auto& a = proc.system.alignments[0];
            std::printf ("alignment: status '%s' ref=%d align=%d measured=%d refArr=%.3f alignArr=%.3f levels %.1f / %.1f need=%.3f\n",
                         page.statusText().toRawUTF8(), a.ref, a.align, (int) a.measured(), a.refArrivalMs, a.alignArrivalMs, (double) a.refLevelDb,
                         (double) a.alignLevelDb, a.neededMs (proc.system.sources[(size_t) a.ref].proc.delayMs));
            page.applyAlignmentNow();
            std::printf ("fill delay applied: %.3f ms   main R follows: hp %d@%.1f delay %.2f\n", proc.system.sources[3].proc.delayMs,
                         proc.system.sources[1].proc.hp.slope, proc.system.sources[1].proc.hp.freq, proc.system.sources[1].proc.delayMs);
            stage = 6;
            stageTick = ticks;
        }
        else if (stage == 6 && ticks == stageTick + 10)
        {
            shot ("speakers-alignment");
            proc.settings.theme = 1;
            stage = 4;
            stageTick = ticks - 15;
        }
        else if (stage == 4 && ticks == stageTick + 25)
        {
            shot ("speakers-light");
            proc.settings.theme = 0;
            editor->showPage (4);
            proc.settings.paneCount = 1;
            proc.settings.view = 3;
        }
        else if (stage == 4 && ticks == stageTick + 60)
        {
            shot ("monitor-waterfall");
            // room tuning: the three mics at three listening positions, before EQ
            proc.setMicPosition (0, "FOH");
            proc.setMicPosition (1, "Balcony");
            proc.setMicPosition (2, "Row 8");
            editor->showPage (3);
            editor->roomPageForPreview().setMeasuringAfter (false);
            editor->roomPageForPreview().measure();
            stage = 10;
            stageTick = ticks;
        }
        else if (stage == 10 && ticks == stageTick + 40) shot ("room-measuring");
        else if (stage == 10 && (! editor->roomPageForPreview().busy() || timeout))
        {
            auto& rp = editor->roomPageForPreview();
            std::printf ("room before: %s (%d ticks) count=%d spread=%.1f\n", rp.statusText().toRawUTF8(), ticks - stageTick, proc.room.count (false), proc.room.levelSpread (false));
            rp.suggestNow();
            std::printf ("room suggest: %s\n", rp.statusText().toRawUTF8());
            for (const auto& b : proc.room.suggested) std::printf ("  band %.0f Hz %.1f dB Q %.2f\n", b.freq, b.gainDb, b.q);
            rp.applyNow();
            // the processor now has the EQ: put it in the simulated system
            for (const auto& b : proc.system.sources[(size_t) proc.room.eqSpeaker].proc.eq) system.push_back (Biquad::make (fs, b.freq, b.q, b.gainDb, 1));
            shot ("room-suggested");
            rp.measure();
            stage = 11;
            stageTick = ticks;
        }
        else if (stage == 11 && (! editor->roomPageForPreview().busy() || timeout))
        {
            double lo, hi, rb, mb, ra, ma;
            proc.room.eqRange (proc.system, lo, hi);
            proc.room.deviation (proc.room.average (false), lo, hi, rb, mb);
            proc.room.deviation (proc.room.average (true), lo, hi, ra, ma);
            std::printf ("room after: %s count=%d  rms before %.2f (max %.1f) after %.2f (max %.1f) range %.0f-%.0f\n", editor->roomPageForPreview().statusText().toRawUTF8(),
                         proc.room.count (true), rb, mb, ra, ma, lo, hi);
            proc.room.tuned = true;
            proc.room.tunedOn = LiveAlignProcessor::today();
            editor->updateStatuses();
            editor->roomPageForPreview().refresh();
            stage = 12;
            stageTick = ticks;
        }
        else if (stage == 12 && ticks == stageTick + 5)
        {
            shot ("room");
            editor->showPage (2);
        }
        else if (stage == 12 && ticks == stageTick + 10)
        {
            shot ("speakers-with-eq");
            stage = 4;
            stageTick = ticks - 60;
            // venue plan: mics 1 and 3 aimed at the speakers, a named position and a note
            proc.mics.position[1] = "Balcony";
            proc.setMicAim (0, 0);
            proc.plan.items.push_back ({ la::PlanItem::Marker, "Row 3 (alignment)", 0.36f, 0.42f, 0.0f, 0.0f });
            proc.plan.items.push_back ({ la::PlanItem::Note, "Sub under stage lip", 0.82f, 0.12f, 0.0f, 0.0f });
            proc.plan.edited = true;
            editor->showPage (5);
        }
        else if (stage == 4 && ticks == stageTick + 75)
        {
            shot ("plan");
            save (editor->planPageForPreview().getCanvas().exportImage (1600), "plan-export");
            {
                proc.plan.placeMissing (4, proc.settings.micCount.load());
                la::Trace t;
                t.name = "Before tuning (FOH)";
                t.created = "12 Oct 2026 19:40";
                for (double f = 20.0; f <= 20000.0; f *= 1.05) { t.data.freq.push_back (f); t.data.db.push_back (3.0 * std::sin (std::log (f) * 2.0) - (f > 8000.0 ? 4.0 : 0.0)); }
                proc.traces.add (t);
                proc.settings.targetCurve = 4;
                const auto f = dir.getChildFile ("report.pdf");
                const auto err = la::report::writeTuningReport (proc, f, editor->planPageForPreview().getCanvas().exportImage (1800), "J. Ulicki Audio");
                std::printf ("report: err='%s' size=%d\n", err.toRawUTF8(), (int) f.getSize());
            }
            {
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
                editor->showPage (6);
                shot ("locker");
                proc.locker.load();
                editor->showPage (0);
                shot ("home");
            }
        }
        else if (stage == 4 && ticks == stageTick + 80)
        {
            // the speaker system survives a venue save / open
            const auto f = dir.getChildFile ("speakers.lavenue");
            proc.saveProject (f);
            const auto before = proc.system.sources[2].response.freq.size();
            proc.newProject();
            proc.openProject (f);
            std::printf ("venue speakers: sources=%d subPoints=%d (was %d) lp=%d@%.1f locked=%d alignments=%d measured=%d fillCh=%d\n", (int) proc.system.sources.size(),
                         (int) proc.system.sources[2].response.freq.size(), (int) before, proc.system.sources[2].proc.lp.slope,
                         proc.system.sources[2].proc.lp.freq, (int) proc.system.timingLocked, (int) proc.system.alignments.size(),
                         proc.system.alignments.empty() ? -1 : (int) proc.system.alignments[0].measured(), proc.system.sources[0].channel);
            std::printf ("venue room: before=%d after=%d suggested=%d baseEq=%d tuned=%d eqOnMain=%d positions=%s aim(Balcony)=%d\n", proc.room.count (false),
                         proc.room.count (true), (int) proc.room.suggested.size(), (int) proc.room.baseEq.size(), (int) proc.room.tuned,
                         (int) proc.system.sources[0].proc.eq.size(), proc.positions.joinIntoString (",").toRawUTF8(), proc.positionAimOf ("Balcony"));
            f.deleteFile();
            lockerAndUpdateChecks();
            sessionRoundTrip();
            templateAndRecovery();
            stage = 99;
            editor.reset();
            juce::MessageManager::getInstance()->stopDispatchLoop();
        }
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
    std::vector<float> line;
    int stage = 0, stageTick = 0, lastOut = 0;
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
