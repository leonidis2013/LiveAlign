// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include "../Clock.h"
// Tuning step: Gain and headroom. Pink noise in 3 dB steps through the whole system up to the show level at FOH
// (Venue, Details) plus the headroom wanted, watching the FOH mic: when the level stops rising with the generator the
// system is compressing or limiting, and when the coherence drops it is distorting. Then each speaker alone at the
// same drive (what it gets in the show): one that compresses first is the weak link. The amp gains and limiter
// thresholds go on the processor sheet. Loud: hearing protection, and nobody near the speakers.
#include <functional>
#include "Graph.h"
#include "../ShowChecks.h"
#include "../ProcessorSheet.h"

namespace la::ui
{
class HeadroomPage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onChanged, onOpenVenue, onOpenMics;

    explicit HeadroomPage (LiveAlignProcessor& p) : proc (p)
    {
        runButton.onClick = [this] { clickRun (0); };
        runButton.setTooltip ("Pink noise through the whole system in 3 dB steps, up to the show level plus the headroom wanted");
        eachButton.onClick = [this] { clickRun (1); };
        eachButton.setTooltip ("Each speaker alone at the show drive (as loud as the whole-system run reached), up to the headroom wanted");
        micBox.setTooltip ("The mic at FOH: calibrated, so the levels are in dBA");
        micBox.onChange = [this] { fohMic = juce::jmax (0, micBox.getSelectedId() - 1); };
        venueButton.onClick = [this] { if (onOpenVenue) onOpenVenue(); };
        for (auto* c : std::initializer_list<juce::Component*> { &runButton, &eachButton, &micBox, &venueButton }) addAndMakeVisible (*c);
        lookAndFeelChanged();
        refresh();
        startTimerHz (15);
    }
    ~HeadroomPage() override { if (running()) stopRun ("Stopped."); stopTimer(); }

    void setSnapshot (const Snapshot& s) { snap = &s; }
    void lookAndFeelChanged() override
    {
        runButton.setColour (juce::TextButton::buttonColourId, pal().accent);
        runButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
    }

    bool running() const { return phase != Phase::Idle; }
    bool measured() const { return proc.headroom.find (0) != nullptr && std::isfinite (proc.headroom.find (0)->cleanMax()); }
    int verdictLevel() const
    {
        const auto* r = proc.headroom.find (0);
        if (r == nullptr) return -1;
        int lv = 0;
        headroom::verdict (*r, proc.pref ("targetDbA"), proc.pref ("headroomDb"), splKnown(), lv);
        return lv;
    }
    juce::String nextHint() const
    {
        if (! measured()) return "Run the whole system up to the show level";
        if (verdictLevel() == 2) return "The system falls short of the show level: check the gains";
        return {};
    }

    void refresh()
    {
        micBox.clear (juce::dontSendNotification);
        int foh = -1;
        for (int m = 0; m < proc.settings.micCount.load(); ++m)
        {
            micBox.addItem (proc.mics.nameOf (m) + (proc.mics.position[(size_t) m].isNotEmpty() ? "  at " + proc.mics.position[(size_t) m] : juce::String()), m + 1);
            if (foh < 0 && proc.mics.position[(size_t) m].containsIgnoreCase ("FOH")) foh = m;
        }
        if (! running() && fohMic < 0) fohMic = juce::jmax (0, foh);
        micBox.setSelectedId (juce::jlimit (0, juce::jmax (0, proc.settings.micCount.load() - 1), fohMic) + 1, juce::dontSendNotification);
        // the amp gain and limiter of each output, for the processor sheet
        editors.clear();
        for (int i = 0; i < (int) proc.system.sources.size(); ++i)
        {
            for (int k = 0; k < 2; ++k)
            {
                auto e = std::make_unique<juce::TextEditor>();
                e->setFont (font (12.0f));
                e->setTextToShowWhenEmpty (k == 0 ? "e.g. +32 dB" : "e.g. +12 dBu", pal().textFaint);
                e->setText (k == 0 ? proc.system.sources[(size_t) i].ampGain : proc.system.sources[(size_t) i].limiter, juce::dontSendNotification);
                e->setTooltip (k == 0 ? "The amp's gain (or input sensitivity) on this output, as set" : "The limiter threshold in the processor on this output, as set");
                auto* ep = e.get();
                e->onTextChange = [this, i, k, ep]
                {
                    if (i >= (int) proc.system.sources.size()) return;
                    auto& s = proc.system.sources[(size_t) i];
                    (k == 0 ? s.ampGain : s.limiter) = ep->getText().trim();
                    if (onChanged) onChanged();
                };
                addAndMakeVisible (*e);
                editors.push_back ({ i, k, std::move (e) });
            }
        }
        refreshButtons();
        resized();
        repaint();
    }

    // For the preview: run without the confirmation.
    void runNow (int what) { armed = what; clickRun (what); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, chartCard.toFloat(), "Level at FOH against the generator");
        drawPanel (g, runCard.toFloat(), "Run");
        drawPanel (g, resultCard.toFloat(), "Headroom");
        drawPanel (g, gainCard.toFloat(), "Amp gains and limiters (for the processor sheet)");
        paintChart (g);

        const double target = proc.pref ("targetDbA"), wanted = proc.pref ("headroomDb");
        {
            auto r = runInfo;
            g.setColour (p.textDim);
            g.setFont (font (11.5f));
            juce::String t = "Show level at FOH " + juce::String ((int) target) + " dBA, headroom wanted " + juce::String ((int) wanted) + " dB: steps up to "
                             + juce::String ((int) (target + wanted)) + " dBA, never past the generator's ceiling ("
                             + juce::String ((int) proc.settings.genMaxDb.load()) + " dBFS, Settings).";
            if (! splKnown()) t << " The FOH mic is not calibrated: calibrate it on Mics for dBA.";
            g.drawFittedText (t, r.removeFromTop (48), juce::Justification::topLeft, 3);
            r.removeFromTop (4);
            g.setColour (armed >= 0 ? p.warn : running() ? p.accent : status.isNotEmpty() ? p.text : p.textDim);
            g.setFont (font (12.0f, armed >= 0 || running()));
            g.drawFittedText (armed >= 0 ? juce::String ("This gets loud. Hearing protection on, nobody near the speakers: press again to start.")
                                         : running() ? phaseLine() : status.isNotEmpty() ? status
                                                                   : juce::String ("The processor's EQ, crossover and limiters in place, the amps on as for the show."),
                              r, juce::Justification::topLeft, 4);
        }
        {
            auto r = resultArea;
            const auto* whole = proc.headroom.find (0);
            int lv = 0;
            const auto v = whole != nullptr ? headroom::verdict (*whole, target, wanted, splKnown(), lv) : juce::String ("Run the whole system first.");
            g.setColour (whole == nullptr ? p.textDim : lv == 0 ? p.good : lv == 1 ? p.warn : p.bad);
            g.setFont (font (12.5f, true));
            g.drawText ("Whole system", r.removeFromTop (18), juce::Justification::centredLeft);
            g.setFont (font (12.0f));
            g.drawFittedText (v, r.removeFromTop (50), juce::Justification::topLeft, 3);
            r.removeFromTop (6);
            for (const auto& run : proc.headroom.runs)
            {
                if (run.output == 0 || r.getHeight() < 34) continue;
                const double clean = run.cleanMax();
                const bool bad = run.compressedAt >= 0;
                g.setColour (bad ? p.warn : p.text);
                g.setFont (font (12.0f, true));
                g.drawText (run.name, r.removeFromTop (17), juce::Justification::centredLeft, true);
                g.setColour (bad ? p.warn : p.textDim);
                g.setFont (font (11.5f));
                juce::String t;
                if (bad) t = juce::String (run.distorted ? "Distorts" : "Compresses") + " at " + juce::String (juce::roundToInt (run.steps[(size_t) run.compressedAt].genDb)) + " dBFS drive ("
                             + juce::String (clean, 1) + " dB clean at FOH): the weak link. Check its amp gain and limiter.";
                else t = std::isfinite (clean) ? "Clean up to the show drive plus headroom (" + juce::String (clean, 1) + " at FOH alone)." : juce::String ("Not measured.");
                g.drawFittedText (t, r.removeFromTop (30), juce::Justification::topLeft, 2);
            }
        }
        for (const auto& [row, label] : gainLabels)
        {
            g.setColour (p.text);
            g.setFont (font (12.0f, true));
            g.drawText (label, row, juce::Justification::centredLeft, true);
        }
        g.setColour (p.textFaint);
        g.setFont (font (10.5f, true));
        g.drawText ("OUTPUT", gainHead.withWidth (gainHead.getWidth() / 3), juce::Justification::centredLeft);
        g.drawText ("AMP GAIN", gainHead.withTrimmedLeft (gainHead.getWidth() / 3).withWidth (gainHead.getWidth() / 3), juce::Justification::centredLeft);
        g.drawText ("LIMITER", gainHead.withTrimmedLeft (2 * gainHead.getWidth() / 3), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (400);
        r.removeFromRight (10);
        gainCard = r.removeFromBottom (juce::jmin (260, r.getHeight() / 3));
        r.removeFromBottom (8);
        chartCard = r;
        chartArea = chartCard.reduced (16, 0).withTrimmedTop (40).withTrimmedBottom (16);
        runCard = right.removeFromTop (280);
        right.removeFromTop (8);
        resultCard = right;
        {
            auto c = runCard.reduced (12, 0).withTrimmedTop (38);
            auto row = c.removeFromTop (28);
            micBox.setBounds (row.removeFromLeft (row.getWidth() * 60 / 100));
            row.removeFromLeft (6);
            venueButton.setBounds (row);
            c.removeFromTop (8);
            runButton.setBounds (c.removeFromTop (32));
            c.removeFromTop (6);
            eachButton.setBounds (c.removeFromTop (28));
            c.removeFromTop (8);
            runInfo = c;
        }
        resultArea = resultCard.reduced (12, 0).withTrimmedTop (38).withTrimmedBottom (10);
        {
            auto c = gainCard.reduced (14, 0).withTrimmedTop (36);
            gainHead = c.removeFromTop (18);
            gainLabels.clear();
            const int w = c.getWidth() / 3;
            int last = -1;
            juce::Rectangle<int> row;
            for (auto& ed : editors)
            {
                if (ed.spk != last)
                {
                    row = c.removeFromTop (28);
                    c.removeFromTop (4);
                    last = ed.spk;
                    gainLabels.push_back ({ row.withWidth (w), proc.system.nameOf (ed.spk) });
                }
                const bool fits = row.getBottom() <= gainCard.getBottom() - 6;
                ed.editor->setVisible (fits);
                ed.editor->setBounds (row.withTrimmedLeft (w * (ed.kind + 1)).withWidth (w - 8).reduced (0, 1));
            }
        }
    }

private:
    enum class Phase { Idle, Settle, Average };
    struct Ed { int spk, kind; std::unique_ptr<juce::TextEditor> editor; };

    bool splKnown() const { return proc.settings.micSplCalibrated[juce::jlimit (0, kMaxMics - 1, juce::jmax (0, fohMic))].load() && ! proc.mics.noCalibrator; }

    juce::String phaseLine() const
    {
        juce::String t = "Running " + runName + ": " + juce::String (juce::roundToInt (genNow)) + " dBFS";
        if (const auto* run = proc.headroom.find (runOutput); run != nullptr && ! run->steps.empty() && std::isfinite (run->steps.back().splDb))
            t << ", last step " << juce::String (run->steps.back().splDb, 1) << (splKnown() ? " dBA" : " dBFS(A)");
        return t + (phase == Phase::Settle ? "  (settling)" : "  (measuring)");
    }

    // Two presses: the first arms (a warning), the second starts. Pressing while running stops.
    void clickRun (int what)
    {
        if (running()) { stopRun ("Stopped."); return; }
        if (armed != what) { armed = what; armedAt = la::clock::ms32(); repaint(); return; }
        armed = -1;
        if (proc.settings.refSource.load() == 0 && proc.settings.micCount.load() == 0) { status = "Set up a mic on Mics first."; repaint(); return; }
        queue.clear();
        if (what == 0) queue.push_back (0);
        else
        {
            const auto* whole = proc.headroom.find (0);
            if (whole == nullptr || whole->steps.empty()) { status = "Run the whole system first: each speaker is then run at the same drive."; repaint(); return; }
            for (int i = 0; i < (int) proc.system.sources.size(); ++i)
            {
                const auto& s = proc.system.sources[(size_t) i];
                if (s.output <= 0) continue;
                bool dup = false;
                for (int o : queue) dup = dup || o == s.output;
                if (! dup) queue.push_back (s.output);
            }
            if (queue.empty()) { status = "Each speaker needs its own output (Venue, Speakers) to run it alone."; repaint(); return; }
        }
        savedSel = proc.settings.selectedMic.load();
        savedLevel = proc.settings.genLevelDb.load();
        savedOutput = proc.settings.genOutput.load();
        savedType = proc.settings.genType.load();
        nextRun();
    }

    void nextRun()
    {
        if (queue.empty()) { stopRun (status.isNotEmpty() ? status : juce::String ("Done.")); return; }
        runOutput = queue.front();
        queue.erase (queue.begin());
        runName = runOutput == 0 ? juce::String ("Whole system") : nameOfOutput (runOutput);
        auto& run = proc.headroom.get (runOutput, runName);
        run.steps.clear();
        run.compressedAt = -1;
        run.distorted = run.ceiling = run.reached = run.stopped = false;
        run.when = LiveAlignProcessor::today() + juce::Time::getCurrentTime().formatted (" %H:%M");
        // where to stop: the whole system at the show level plus headroom; a speaker alone at the drive that got
        // the whole system there, plus headroom
        const double ceiling = juce::jmin (0.0, (double) proc.settings.genMaxDb.load());
        if (runOutput == 0) { stopGen = ceiling; stopSpl = proc.pref ("targetDbA") + proc.pref ("headroomDb"); genNow = -30.0; }
        else
        {
            const auto* whole = proc.headroom.find (0);
            double drive = whole->steps.back().genDb;
            for (const auto& s : whole->steps) if (std::isfinite (s.splDb) && s.splDb >= proc.pref ("targetDbA")) { drive = s.genDb; break; }
            stopGen = juce::jmin (ceiling, drive + proc.pref ("headroomDb"));
            stopSpl = 1e9;
            genNow = juce::jmin (-30.0, stopGen - 18.0);
        }
        // the coherence where this output plays (a sub's is only good low down)
        cohLo = 200.0; cohHi = 5000.0;
        if (runOutput != 0)
            for (int i = 0; i < (int) proc.system.sources.size(); ++i)
                if (proc.system.sources[(size_t) i].output == runOutput) { proc.system.playRange (i, cohLo, cohHi); break; }
        auto& st = proc.settings;
        st.selectedMic = fohMic;
        st.genType = 0;
        st.genOutput = runOutput;
        st.genLevelDb = (float) genNow;
        st.genOn = true;
        st.running = true;
        beginStep();
        refreshButtons();
    }

    void beginStep()
    {
        phase = Phase::Settle;
        phaseStart = la::clock::ms();
        sumPow = 0.0; sumCoh = 0.0; nSpl = 0; nCoh = 0; lastLeq = NAN;
        lastSerial = 0;
    }

    void stopRun (const juce::String& why)
    {
        auto& st = proc.settings;
        st.genOn = false;
        st.genLevelDb = savedLevel;
        st.genOutput = savedOutput;
        st.genType = savedType;
        st.selectedMic = savedSel;
        if (phase != Phase::Idle && ! queue.empty()) queue.clear();
        phase = Phase::Idle;
        status = why;
        refreshButtons();
        repaint();
        if (onChanged) onChanged();
    }

    void timerCallback() override
    {
        if (armed >= 0 && la::clock::ms32() - armedAt > 5000) { armed = -1; repaint(); }
        if (! running()) return;
        if (! proc.settings.genOn.load()) { stopRun ("Stopped: the generator was turned off."); return; }
        const double el = la::clock::ms() - phaseStart;
        if (phase == Phase::Settle)
        {
            if (el > 2000.0)
            {
                // the level from here on: the A-weighted Leq over the averaging time (a slow meter would still carry the step before)
                phase = Phase::Average;
                phaseStart = la::clock::ms();
                proc.settings.requestResetAverages = true;
                proc.settings.requestResetLeq = true;
            }
        }
        else if (phase == Phase::Average && snap != nullptr)
        {
            if (snap->serial != lastSerial)
            {
                lastSerial = snap->serial;
                // the whole system in dBA (the show level); a speaker alone in dBC (a sub barely shows in dBA)
                const float leq = runOutput == 0 ? snap->leqA : snap->leqC;
                if (el > 800.0 && leq > -150.0f && snap->leqSeconds > 0.5) { lastLeq = leq; ++nSpl; }
                double c = 0.0;
                int n = 0;
                for (size_t i = 0; i < snap->tfFreq.size() && i < snap->tfCoh.size(); ++i)
                    if (snap->tfFreq[i] >= cohLo && snap->tfFreq[i] <= cohHi && std::isfinite (snap->tfCoh[i])) { c += snap->tfCoh[i]; ++n; }
                if (n > 0 && el > 800.0) { sumCoh += c / n; ++nCoh; }
            }
            if (el > 3000.0) finishStep();
        }
        repaint (runCard);
        repaint (chartCard);
    }

    void finishStep()
    {
        auto& run = proc.headroom.get (runOutput, runName);
        HeadroomStep s;
        s.genDb = genNow;
        s.splDb = nSpl > 0 ? lastLeq : NAN;
        s.coherence = nCoh > 0 ? sumCoh / nCoh : NAN;
        run.steps.push_back (s);
        const int j = headroom::judge (run.steps);
        if (j > 0)
        {
            run.compressedAt = (int) run.steps.size() - 1;
            run.distorted = j == 2;
            status = runName + (j == 2 ? ": the coherence dropped (distortion)." : ": the level stopped rising with the generator (compression or limiting).");
            nextRun();
            return;
        }
        if (std::isfinite (s.splDb) && s.splDb >= stopSpl) { run.reached = true; status = runName + ": reached the show level plus headroom, clean."; nextRun(); return; }
        // the next step: 3 dB, or a bigger jump while far below the target
        double step = 3.0;
        const double target = proc.pref ("targetDbA");
        if (runOutput == 0 && std::isfinite (s.splDb) && s.splDb < target - 15.0) step = juce::jlimit (3.0, 12.0, std::floor ((target - 12.0 - s.splDb) / 3.0) * 3.0);
        if (genNow + step > stopGen + 0.01)
        {
            if (runOutput == 0) { run.ceiling = true; status = runName + ": the generator reached its ceiling."; }
            else { run.reached = true; status = runName + ": clean up to the show drive plus headroom."; }
            nextRun();
            return;
        }
        genNow += step;
        proc.settings.genLevelDb = (float) genNow;
        beginStep();
    }

    juce::String nameOfOutput (int out) const
    {
        juce::StringArray n;
        for (int i = 0; i < (int) proc.system.sources.size(); ++i) if (proc.system.sources[(size_t) i].output == out) n.add (proc.system.nameOf (i));
        return n.isEmpty() ? "Output " + juce::String (out) : n.joinIntoString (" + ");
    }

    void paintChart (juce::Graphics& g)
    {
        const auto& p = pal();
        auto r = chartArea;
        const double target = proc.pref ("targetDbA"), wanted = proc.pref ("headroomDb");
        double gLo = -36.0, gHi = 0.0, sLo = target - 30.0, sHi = target + wanted + 6.0;
        for (const auto& run : proc.headroom.runs)
            for (const auto& s : run.steps)
            {
                gLo = std::min (gLo, s.genDb - 3.0);
                if (std::isfinite (s.splDb)) { sLo = std::min (sLo, s.splDb - 3.0); sHi = std::max (sHi, s.splDb + 3.0); }
            }
        auto plot = r.withTrimmedLeft (44).withTrimmedBottom (24);
        auto x = [&] (double gd) { return (float) plot.getX() + (float) ((gd - gLo) / (gHi - gLo)) * (float) plot.getWidth(); };
        auto y = [&] (double db) { return (float) plot.getBottom() - (float) ((db - sLo) / (sHi - sLo)) * (float) plot.getHeight(); };
        g.setColour (p.grid);
        g.setFont (font (10.5f));
        for (double gd = std::ceil (gLo / 6.0) * 6.0; gd <= gHi; gd += 6.0)
        {
            g.drawVerticalLine ((int) x (gd), (float) plot.getY(), (float) plot.getBottom());
            g.setColour (p.textFaint);
            g.drawText (juce::String ((int) gd), juce::Rectangle<float> (x (gd) - 20.0f, (float) plot.getBottom() + 4.0f, 40.0f, 14.0f), juce::Justification::centred);
            g.setColour (p.grid);
        }
        for (double db = std::ceil (sLo / 10.0) * 10.0; db <= sHi; db += 10.0)
        {
            g.drawHorizontalLine ((int) y (db), (float) plot.getX(), (float) plot.getRight());
            g.setColour (p.textFaint);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> ((float) r.getX(), y (db) - 7.0f, 38.0f, 14.0f), juce::Justification::centredRight);
            g.setColour (p.grid);
        }
        g.setColour (p.textFaint);
        g.drawText ("Generator (dBFS)", plot.withY (plot.getBottom() + 4).withHeight (14), juce::Justification::centredRight);
        // the target and the headroom wanted
        g.setColour (p.accent.withAlpha (0.12f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom ((float) plot.getX(), y (target + wanted), (float) plot.getRight(), y (target)));
        g.setColour (p.accent);
        g.drawHorizontalLine ((int) y (target), (float) plot.getX(), (float) plot.getRight());
        g.setFont (font (11.0f, true));
        g.drawText ("Show level " + juce::String ((int) target) + " dBA", juce::Rectangle<float> ((float) plot.getX() + 6.0f, y (target) - 16.0f, 200.0f, 14.0f), juce::Justification::centredLeft);
        g.setColour (p.textDim);
        g.drawText ("+ headroom", juce::Rectangle<float> ((float) plot.getX() + 6.0f, y (target + wanted), 200.0f, 14.0f), juce::Justification::centredLeft);
        int colour = 0;
        for (const auto& run : proc.headroom.runs)
        {
            const auto c = run.output == 0 ? p.text : traceColour (colour++);
            juce::Path path;
            bool started = false;
            for (int i = 0; i < (int) run.steps.size(); ++i)
            {
                const auto& s = run.steps[(size_t) i];
                if (! std::isfinite (s.splDb)) continue;
                const juce::Point<float> pt (x (s.genDb), y (s.splDb));
                if (! started) { path.startNewSubPath (pt); started = true; } else path.lineTo (pt);
                const bool bad = run.compressedAt >= 0 && i >= run.compressedAt;
                g.setColour (bad ? p.bad : c);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre (pt));
            }
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (run.output == 0 ? 2.2f : 1.5f));
            if (started)
            {
                const auto& last = run.steps.back();
                g.setFont (font (11.0f, run.output == 0));
                g.drawText (run.name, juce::Rectangle<float> (x (last.genDb) + 6.0f, y (last.splDb) - 7.0f, 160.0f, 14.0f), juce::Justification::centredLeft, true);
            }
        }
        if (proc.headroom.runs.empty())
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText ("Each step plays 5 s of pink noise and reads the level at FOH (dBA for the whole system, dBC for a speaker alone). A clean system rises 3 dB for every 3 dB of generator; "
                              "a step that rises less is compressing.", plot.reduced (40), juce::Justification::centred, 3);
        }
    }

    void refreshButtons()
    {
        const bool standalone = proc.isStandalone() || proc.uiAsStandalone;
        runButton.setButtonText (running() && runOutput == 0 ? "Stop" : armed == 0 ? "Press again to start" : "Run the whole system");
        eachButton.setButtonText (running() && runOutput != 0 ? "Stop" : armed == 1 ? "Press again to start" : "Then each speaker alone");
        runButton.setEnabled (standalone && (! running() || runOutput == 0));
        eachButton.setEnabled (standalone && measured() && (! running() || runOutput != 0));
        venueButton.setButtonText (juce::String (juce::CharPointer_UTF8 ("Show level  \xe2\x86\x92")));
        venueButton.setTooltip ("The show level at FOH and the headroom wanted are set on Venue, Details");
    }

    LiveAlignProcessor& proc;
    const Snapshot* snap = nullptr;
    juce::TextButton runButton, eachButton, venueButton;
    juce::ComboBox micBox;
    std::vector<Ed> editors;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> gainLabels;
    juce::Rectangle<int> chartCard, chartArea, runCard, runInfo, resultCard, resultArea, gainCard, gainHead;
    juce::String status, runName;
    std::vector<int> queue;
    Phase phase = Phase::Idle;
    double lastLeq = NAN, cohLo = 200.0, cohHi = 5000.0;
    double phaseStart = 0.0, genNow = -30.0, stopGen = 0.0, stopSpl = 0.0, sumPow = 0.0, sumCoh = 0.0;
    int nSpl = 0, nCoh = 0, runOutput = 0, fohMic = -1, armed = -1, savedSel = 0, savedOutput = 0, savedType = 0;
    float savedLevel = -20.0f;
    juce::uint32 armedAt = 0;
    juce::uint64 lastSerial = 0;
};
} // namespace la::ui
