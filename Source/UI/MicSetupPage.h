#pragma once
// Tab 1: Mic setup and calibration. A guided checklist; each step turns green when it is done (or skipped),
// with the RTA and input meters alongside so you can see what the mic hears.
#include <functional>
#include "Views.h"
#include "SidePanels.h"
#include "../PluginProcessor.h"
#include "../StandaloneHooks.h"

namespace la::ui
{
enum class StepState { Todo, Done, Warn, Skipped };

// One numbered step: status badge, title, explanation, a row of controls and a result line.
class StepCard : public juce::Component
{
public:
    StepCard (int number, juce::String titleText, juce::String helpText)
        : num (number), title (std::move (titleText)), help (std::move (helpText)) {}

    void set (StepState s, const juce::String& resultText)
    {
        if (s == state && resultText == result) return;
        state = s;
        result = resultText;
        repaint();
    }

    StepState getState() const { return state; }
    bool isComplete() const { return state == StepState::Done || state == StepState::Skipped || state == StepState::Warn; }

    // Area for the step's own controls.
    juce::Rectangle<int> controlArea() const { return { kTextX, 62, getWidth() - kTextX - 14, 28 }; }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        const auto r = getLocalBounds().toFloat();
        g.setColour (p.panel);
        g.fillRoundedRectangle (r, 10.0f);
        const auto edge = state == StepState::Done ? p.good : (state == StepState::Warn ? p.warn : p.border);
        g.setColour (state == StepState::Todo || state == StepState::Skipped ? p.border : edge.withAlpha (0.7f));
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, state == StepState::Todo || state == StepState::Skipped ? 1.0f : 1.5f);

        // badge
        const auto badge = juce::Rectangle<float> (16.0f, 16.0f, 30.0f, 30.0f);
        if (state == StepState::Done) { g.setColour (p.good); g.fillEllipse (badge); }
        else if (state == StepState::Warn) { g.setColour (p.warn); g.fillEllipse (badge); }
        else { g.setColour (p.panelRaised); g.fillEllipse (badge); g.setColour (p.border); g.drawEllipse (badge, 1.2f); }

        g.setColour (state == StepState::Done || state == StepState::Warn ? p.onAccent : p.textDim);
        if (state == StepState::Done)
        {
            juce::Path tick;
            tick.startNewSubPath (badge.getX() + 8.5f, badge.getCentreY() + 0.5f);
            tick.lineTo (badge.getX() + 13.0f, badge.getCentreY() + 5.0f);
            tick.lineTo (badge.getRight() - 8.0f, badge.getCentreY() - 5.0f);
            g.strokePath (tick, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        else
        {
            g.setFont (font (14.0f, true));
            g.drawText (state == StepState::Warn ? "!" : juce::String (num), badge, juce::Justification::centred);
        }

        g.setColour (p.text);
        g.setFont (font (15.0f, true));
        g.drawText (title, kTextX, 12, getWidth() - kTextX - 110, 22, juce::Justification::centredLeft);
        if (state == StepState::Skipped)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            g.drawText ("SKIPPED", getWidth() - 110, 12, 96, 22, juce::Justification::centredRight);
        }
        g.setColour (p.textDim);
        g.setFont (font (12.0f));
        g.drawText (help, kTextX, 34, getWidth() - kTextX - 14, 22, juce::Justification::centredLeft, true);

        g.setColour (state == StepState::Warn ? p.warn : (state == StepState::Done ? p.good : p.textDim));
        g.setFont (font (12.0f, state != StepState::Todo));
        g.drawText (result, kTextX, getHeight() - 30, getWidth() - kTextX - 14, 20, juce::Justification::centredLeft, true);
    }

    static constexpr int kTextX = 62, kHeight = 132;

private:
    int num;
    juce::String title, help, result;
    StepState state = StepState::Todo;
};

//==============================================================================
class MicSetupPage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onLoadCalFile, onNext, onChanged;

    explicit MicSetupPage (LiveAlignProcessor& p)
        : proc (p), settings (p.settings), checklist (p.micChecklist), rta (p.settings, p.traces), inputs (p.settings),
          cards { { 1, "Audio interface", "Choose the interface your measurement mic and loopback are connected to." },
                  { 2, "Inputs", "Pick the mic input and the reference. The reference is a loopback of the signal feeding the PA." },
                  { 3, "Mic calibration file", "Load the file supplied with your measurement mic so its own response is removed." },
                  { 4, "Sound level calibration", "Fit an acoustic calibrator to the mic so levels read true dB SPL." },
                  { 5, "Reference loopback check", "Plays pink noise for 2 seconds from the generator output and checks the loopback." },
                  { 6, "Room noise floor", "With the PA silent, measures the background noise your test signal must beat." } }
    {
        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);
        for (auto& c : cards) content.addAndMakeVisible (c);
        addAndMakeVisible (rta);
        addAndMakeVisible (inputs);

        // 1 interface
        deviceButton.onClick = [] { if (auto& f = StandaloneHooks::showAudioSettings()) f(); };
        add (0, deviceButton);

        // 2 inputs
        micBox.onChange = [this] { settings.micChannel = micBox.getSelectedItemIndex(); settings.requestResetAverages = true; changed(); };
        refBox.onChange = [this]
        {
            settings.refSource = refBox.getSelectedItemIndex();
            checklist.loopbackDone = false;
            settings.requestResetAverages = true;
            changed();
        };
        add (1, micBox);
        add (1, refBox);

        // 3 cal file
        loadButton.onClick = [this] { if (onLoadCalFile) onLoadCalFile(); };
        clearButton.onClick = [this] { proc.clearCalibration(); changed(); };
        noFileButton.setClickingTogglesState (true);
        noFileButton.onClick = [this] { checklist.calFileSkipped = noFileButton.getToggleState(); changed(); };
        add (2, loadButton);
        add (2, clearButton);
        add (2, noFileButton);

        // 4 SPL
        levelBox.addItemList ({ "94 dB", "114 dB" }, 1);
        levelBox.onChange = [this] { settings.calibratorDb = levelBox.getSelectedId() == 2 ? 114.0f : 94.0f; };
        calButton.onClick = [this] { calibrate(); };
        noCalButton.setClickingTogglesState (true);
        noCalButton.onClick = [this] { checklist.splSkipped = noCalButton.getToggleState(); changed(); };
        add (3, levelBox);
        add (3, calButton);
        add (3, noCalButton);

        // 5 loopback
        loopButton.onClick = [this] { startLoopbackCheck(); };
        skipLoopButton.setClickingTogglesState (true);
        skipLoopButton.onClick = [this] { checklist.loopbackSkipped = skipLoopButton.getToggleState(); changed(); };
        add (4, loopButton);
        add (4, skipLoopButton);

        // 6 noise
        noiseButton.onClick = [this] { startNoise(); };
        add (5, noiseButton);

        nextButton.onClick = [this] { if (onNext) onNext(); };
        addAndMakeVisible (nextButton);

        refreshChoices();
        update();
        startTimerHz (10);
    }

    ~MicSetupPage() override { stopTimer(); }

    // Number of finished steps (done, skipped or done with a warning) out of six.
    int completedSteps() const
    {
        int n = 0;
        for (const auto& c : cards) n += c.isComplete() ? 1 : 0;
        return n;
    }
    bool isComplete() const { return completedSteps() == 6; }

    void setSnapshot (const Snapshot& s) { snap = &s; rta.update (s); }

    void setLevels (float micPeak, float micRms, float refPeak, float refRms)
    {
        inputs.setLevels (micPeak, micRms, refPeak, refRms);
        lastMicRms = micRms;
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto right = r.removeFromRight (juce::jmax (360, r.getWidth() * 42 / 100));
        r.removeFromRight (10);
        viewport.setBounds (r);

        auto bottom = right.removeFromBottom (36);
        nextButton.setBounds (bottom.removeFromRight (200));
        right.removeFromBottom (8);
        inputs.setBounds (right.removeFromTop (98));
        right.removeFromTop (8);
        rta.setBounds (right);

        const int w = viewport.getMaximumVisibleWidth() > 0 ? viewport.getMaximumVisibleWidth() : r.getWidth();
        content.setSize (w - 4, 6 * (StepCard::kHeight + 10));
        for (int i = 0; i < 6; ++i)
        {
            cards[i].setBounds (0, i * (StepCard::kHeight + 10), content.getWidth(), StepCard::kHeight);
            layoutControls (i);
        }
    }

    // Reloads routing choices (after a project is opened).
    void refreshChoices()
    {
        const int nIn = juce::jmax (1, proc.numInputs());
        micBox.clear (juce::dontSendNotification);
        refBox.clear (juce::dontSendNotification);
        for (int i = 0; i < nIn; ++i) micBox.addItem ("Mic: input " + juce::String (i + 1), i + 1);
        refBox.addItem ("Reference: off", 1);
        refBox.addItem ("Reference: generator (internal)", 2);
        for (int i = 0; i < nIn; ++i) refBox.addItem ("Reference: input " + juce::String (i + 1) + " (loopback)", i + 3);
        micBox.setSelectedItemIndex (juce::jlimit (0, nIn - 1, settings.micChannel.load()), juce::dontSendNotification);
        refBox.setSelectedItemIndex (juce::jlimit (0, nIn + 1, settings.refSource.load()), juce::dontSendNotification);
        levelBox.setSelectedId (settings.calibratorDb.load() > 104.0f ? 2 : 1, juce::dontSendNotification);
        noFileButton.setToggleState (checklist.calFileSkipped, juce::dontSendNotification);
        noCalButton.setToggleState (checklist.splSkipped, juce::dontSendNotification);
        skipLoopButton.setToggleState (checklist.loopbackSkipped, juce::dontSendNotification);
        nIns = nIn;
    }

private:
    void add (int card, juce::Component& c)
    {
        cards[card].addAndMakeVisible (c);
        controls[card].push_back (&c);
    }

    void layoutControls (int card)
    {
        auto area = cards[card].controlArea();
        static const int widths[6][3] = { { 150, 0, 0 }, { 190, 280, 0 }, { 90, 80, 170 }, { 100, 120, 150 }, { 130, 170, 0 }, { 170, 0, 0 } };
        for (size_t i = 0; i < controls[card].size(); ++i)
        {
            controls[card][i]->setBounds (area.removeFromLeft (widths[card][i]));
            area.removeFromLeft (8);
        }
    }

    void changed() { update(); if (onChanged) onChanged(); }

    void timerCallback() override
    {
        if (proc.numInputs() != nIns) refreshChoices();
        pollLoopback();
        pollNoise();
        update();
    }

    // ---- step logic ----
    void update()
    {
        const bool standalone = (bool) StandaloneHooks::showAudioSettings();
        deviceButton.setVisible (standalone);

        // 1 interface
        if (! standalone) cards[0].set (StepState::Done, "Running inside a host: it provides the audio inputs and outputs.");
        else
        {
            const auto d = StandaloneHooks::deviceDescription() ? StandaloneHooks::deviceDescription()() : juce::String();
            const bool ok = d.isNotEmpty() && d != "No audio device";
            cards[0].set (ok ? StepState::Done : StepState::Todo, ok ? d : juce::String ("No audio device is open yet."));
        }

        // 2 inputs
        {
            const int ref = settings.refSource.load();
            const bool heard = lastMicRms > -100.0f;
            if (ref == 0) cards[1].set (StepState::Todo, "Choose a reference. Without one, only the RTA and sound level work.");
            else if (! heard) cards[1].set (StepState::Todo, "No signal on the mic input yet. Check the cable and phantom power.");
            else if (ref - 2 == settings.micChannel.load()) cards[1].set (StepState::Warn, "The mic and the reference are the same input.");
            else cards[1].set (StepState::Done, "Mic input " + juce::String (settings.micChannel.load() + 1) + " is live ("
                                                    + juce::String (juce::roundToInt (lastMicRms)) + " dBFS). Reference: "
                                                    + (ref == 1 ? juce::String ("generator copy") : "input " + juce::String (ref - 1)) + ".");
        }

        // 3 calibration file
        {
            const auto name = proc.analyzer.getCalibrationName();
            clearButton.setEnabled (name.isNotEmpty());
            noFileButton.setEnabled (name.isEmpty());
            if (name.isNotEmpty())
            {
                const auto cal = proc.analyzer.getCalibration();
                double lo = 0.0, hi = 0.0;
                for (double v : cal.db) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
                cards[2].set (StepState::Done, name + "   (" + juce::String (cal.freq.size()) + " points, "
                                                   + juce::String (lo, 1) + " to +" + juce::String (hi, 1) + " dB)");
            }
            else if (checklist.calFileSkipped) cards[2].set (StepState::Skipped, "No file: the mic is treated as flat.");
            else cards[2].set (StepState::Todo, "No file loaded.");
        }

        // 4 SPL
        {
            const float z = snap != nullptr ? snap->rawZSlowDbfs : -200.0f;
            juce::String live = "Mic now: " + (z < -150.0f ? juce::String ("no signal") : juce::String (z, 1) + " dBFS");
            if (settings.splCalibrated.load())
            {
                if (z > -150.0f) live << " = " << juce::String (z + settings.splOffset.load(), 1) << " dB SPL";
                cards[3].set (StepState::Done, "Calibrated.   " + live);
            }
            else if (checklist.splSkipped) cards[3].set (StepState::Skipped, "Levels show as dBFS.   " + live);
            else cards[3].set (StepState::Todo, live + (calMessage.isNotEmpty() ? "   " + calMessage : juce::String()));
            noCalButton.setEnabled (! settings.splCalibrated.load());
        }

        // 5 loopback
        {
            const int ref = settings.refSource.load();
            loopButton.setEnabled (ref >= 2 && loopPhase == 0);
            loopButton.setButtonText (loopPhase != 0 ? "Checking..." : "Run check");
            if (ref == 1) cards[4].set (StepState::Done, "Not needed: the reference is the generator's internal copy.");
            else if (ref == 0) cards[4].set (StepState::Todo, "Choose a reference input in step 2 first.");
            else if (loopPhase != 0) cards[4].set (StepState::Todo, "Playing pink noise and listening on the reference input...");
            else if (checklist.loopbackDone)
            {
                const float lvl = checklist.loopbackLevelDbfs;
                const bool levelOk = lvl > -36.0f && lvl < -3.0f;
                juce::String t = "Found it: " + juce::String (checklist.loopbackLatencyMs, 1) + " ms round trip, polarity "
                                 + (checklist.loopbackInverted ? "INVERTED" : "normal") + ", level " + juce::String (lvl, 0) + " dBFS";
                if (checklist.loopbackInverted) t << ". Swap the loopback wiring or flip polarity on that input.";
                else if (! levelOk) t << (lvl <= -36.0f ? ". Low: raise the input gain." : ". Hot: lower the input gain.");
                cards[4].set (checklist.loopbackInverted || ! levelOk ? StepState::Warn : StepState::Done, t);
            }
            else if (checklist.loopbackSkipped) cards[4].set (StepState::Skipped, "Not checked.");
            else cards[4].set (StepState::Todo, loopMessage.isNotEmpty() ? loopMessage
                                                                         : juce::String ("Turn the amps down if you don't want to hear it."));
        }

        // 6 noise
        {
            if (noiseActive) cards[5].set (StepState::Todo, "Measuring... " + juce::String (juce::roundToInt (noiseProgress * 100.0f)) + " %   Keep the room quiet.");
            else if (checklist.noiseDone)
            {
                const juce::String unit = checklist.noiseIsSpl ? " dBA" : " dBFS(A)";
                cards[5].set (StepState::Done, "Noise floor " + juce::String (checklist.noiseA, 1) + unit + " (C: "
                                                   + juce::String (checklist.noiseC, 1) + "). Measure at "
                                                   + juce::String (juce::roundToInt (checklist.noiseA + 15.0f)) + unit
                                                   + " or louder. Saved as the trace \"Room noise floor\".");
            }
            else cards[5].set (StepState::Todo, "The generator is switched off while this runs.");
            noiseButton.setEnabled (! noiseActive);
            noiseButton.setButtonText (noiseActive ? "Measuring..." : "Measure (5 seconds)");
        }

        const int done = completedSteps();
        nextButton.setButtonText (done == 6 ? "Next: Speakers" : "Next: Speakers  (" + juce::String (done) + "/6 done)");
    }

    void calibrate()
    {
        const float z = snap != nullptr ? snap->rawZSlowDbfs : -200.0f;
        if (z < -80.0f) { calMessage = "No signal from the calibrator yet."; changed(); return; }
        settings.splOffset = settings.calibratorDb.load() - z;
        settings.splCalibrated = true;
        settings.requestResetLeq = true;
        calMessage.clear();
        changed();
    }

    void startLoopbackCheck()
    {
        saved = { settings.genOn.load(), settings.genType.load(), settings.genLevelDb.load() };
        settings.genType = 0;          // pink noise
        settings.genLevelDb = -20.0f;
        settings.genOn = true;
        loopPhase = 1;
        loopStart = juce::Time::getMillisecondCounter();
        loopSerial = proc.analyzer.readLoopbackReport().serial;
        loopMessage.clear();
        update();
    }

    void pollLoopback()
    {
        if (loopPhase == 0) return;
        const auto now = juce::Time::getMillisecondCounter();
        if (loopPhase == 1 && now - loopStart > 2000)
        {
            proc.analyzer.requestLoopbackCheck();
            loopPhase = 2;
        }
        else if (loopPhase == 2)
        {
            const auto rep = proc.analyzer.readLoopbackReport();
            if (rep.serial != loopSerial || now - loopStart > 6000)
            {
                settings.genOn = saved.on;
                settings.genType = saved.type;
                settings.genLevelDb = saved.level;
                loopPhase = 0;
                if (rep.serial != loopSerial && rep.valid)
                {
                    checklist.loopbackDone = true;
                    checklist.loopbackLatencyMs = rep.latencyMs;
                    checklist.loopbackInverted = rep.inverted;
                    checklist.loopbackLevelDbfs = rep.refLevelDbfs;
                }
                else
                {
                    checklist.loopbackDone = false;
                    loopMessage = rep.refLevelDbfs < -90.0f ? "Nothing arrived on the reference input. Check the loopback cable and the output."
                                                            : "Signal on the reference input, but not the generator. Is the loopback on the generator's output?";
                }
                changed();
            }
        }
    }

    void startNoise()
    {
        settings.genOn = false;
        noiseSerial = proc.analyzer.readNoiseReport().serial;
        proc.analyzer.requestNoiseMeasurement (5.0);
        noiseActive = true;
        noiseProgress = 0.0f;
        update();
    }

    void pollNoise()
    {
        if (! noiseActive) return;
        const auto rep = proc.analyzer.readNoiseReport();
        noiseProgress = rep.progress;
        if (rep.done && rep.serial > noiseSerial + 1)
        {
            noiseActive = false;
            checklist.noiseDone = true;
            checklist.noiseA = rep.laeq;
            checklist.noiseC = rep.lceq;
            checklist.noiseIsSpl = rep.isSpl;

            // keep the noise spectrum as an RTA trace (replacing an older one)
            for (int i = proc.traces.size() - 1; i >= 0; --i)
                if (proc.traces.all()[(size_t) i].name == "Room noise floor") proc.traces.remove (i);
            Trace t;
            t.kind = Trace::Rta;
            t.name = "Room noise floor";
            for (size_t i = 0; i < rep.bandFreq.size(); ++i) { t.data.freq.push_back (rep.bandFreq[i]); t.data.db.push_back (rep.bandDb[i]); }
            proc.traces.add (std::move (t));
            changed();
        }
    }

    LiveAlignProcessor& proc;
    Settings& settings;
    MicChecklist& checklist;
    const Snapshot* snap = nullptr;

    juce::Viewport viewport;
    juce::Component content;
    RtaView rta;
    InputPanel inputs;
    StepCard cards[6];
    std::vector<juce::Component*> controls[6];

    juce::TextButton deviceButton { "Audio device..." }, loadButton { "Load..." }, clearButton { "Clear" },
        noFileButton { "My mic has no file" }, calButton { "Calibrate" }, noCalButton { "No calibrator (dBFS)" },
        loopButton { "Run check" }, skipLoopButton { "Skip this check" }, noiseButton { "Measure (5 seconds)" },
        nextButton { "Next: Speakers" };
    juce::ComboBox micBox, refBox, levelBox;

    struct SavedGen { bool on = false; int type = 0; float level = -20.0f; } saved;
    int loopPhase = 0, nIns = 0;
    juce::uint32 loopStart = 0;
    juce::uint64 loopSerial = 0, noiseSerial = 0;
    bool noiseActive = false;
    float noiseProgress = 0.0f, lastMicRms = -120.0f;
    juce::String calMessage, loopMessage;
};
} // namespace la::ui
