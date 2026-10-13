// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Tuning step 5: the feedback test (ring-out). With the PA at show level, a stage mic's gain is brought up slowly
// until it starts to ring; LiveAlign listens through a measurement mic, catches the ringing frequency and suggests a
// narrow cut for it. Repeat for the next ring. The rings found go in the tuning report.
#include <functional>
#include "Graph.h"
#include "NavRail.h"
#include "../PluginProcessor.h"

namespace la::ui
{
class FeedbackPage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onChanged;

    explicit FeedbackPage (LiveAlignProcessor& p) : proc (p), settings (p.settings), test (p.feedback)
    {
        micEditor.setFont (font (13.0f));
        micEditor.setTextToShowWhenEmpty ("e.g. Lectern mic", pal().textFaint);
        micEditor.onTextChange = [this] { test.micName = micEditor.getText().trim(); changed(); };
        micEditor.setTooltip ("The stage mic being rung out: the rings found are noted against it");
        addAndMakeVisible (micEditor);

        listenBox.setTooltip ("The measurement mic that listens for the ring (FOH, or near the stage)");
        listenBox.onChange = [this] { test.listenMic = juce::jmax (0, listenBox.getSelectedItemIndex()); if (listening) settings.selectedMic = test.listenMic; };
        addAndMakeVisible (listenBox);

        listenButton.onClick = [this] { if (listening) stop ("Stopped listening."); else start(); };
        listenButton.setTooltip ("Listen for rings while you bring the stage mic up");
        addAndMakeVisible (listenButton);
        clearButton.onClick = [this] { test.rings.clear(); detector.reset(); changed(); };
        clearButton.setTooltip ("Remove every ring found");
        addAndMakeVisible (clearButton);
        doneButton.onClick = [this] { test.done = ! test.done; changed(); };
        doneButton.setTooltip ("The stage mics are rung out: mark this step as done");
        addAndMakeVisible (doneButton);

        lookAndFeelChanged();
        refresh();
        startTimerHz (20);
    }

    ~FeedbackPage() override { stopTimer(); }

    void setSnapshot (const Snapshot& s) { snap = &s; }
    bool isListening() const { return listening; }
    void stopListening() { if (listening) stop ({}); }

    void lookAndFeelChanged() override { refreshButtons(); }

    void refresh()
    {
        if (! micEditor.hasKeyboardFocus (true)) micEditor.setText (test.micName, juce::dontSendNotification);
        listenBox.clear (juce::dontSendNotification);
        for (int m = 0; m < settings.micCount.load(); ++m)
            listenBox.addItem (proc.mics.nameOf (m) + (proc.mics.position[(size_t) m].isNotEmpty() ? "  -  " + proc.mics.position[(size_t) m] : juce::String()), m + 1);
        test.listenMic = juce::jlimit (0, juce::jmax (0, settings.micCount.load() - 1), test.listenMic);
        listenBox.setSelectedItemIndex (test.listenMic, juce::dontSendNotification);
        refreshButtons();
        repaint();
    }

    juce::String nextHint() const
    {
        if (test.rings.empty()) return "Listen, and bring a stage mic up until it rings";
        if (! test.done) return "Mark the feedback test as done";
        return {};
    }
    void doNext()
    {
        if (test.rings.empty()) { if (! listening) start(); }
        else if (! test.done) { test.done = true; changed(); }
    }

    // Used by the local screenshot tool: rings as if found.
    void addRingForPreview (double f, double prom)
    {
        fb::Ring r;
        r.freq = f;
        r.prominenceDb = prom;
        r.cutDb = fb::suggestedCut (prom);
        add (r);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, howCard.toFloat(), "Ring out a stage mic");
        drawPanel (g, graphCard.toFloat(), "Listening");
        drawPanel (g, ringsCard.toFloat(), "Rings found");

        // the steps
        {
            auto r = howText;
            const char* steps[] = {
                "Put the stage mic where it is used, with the PA at show level and the console's channel EQ flat.",
                "Pick the measurement mic that listens and press Start listening.",
                "Bring the stage mic's gain up slowly until it just starts to ring, then pull it back 3 dB.",
                "Put the suggested cut in the console or processor, then bring the gain up again for the next ring.",
                "Stop after 3 to 5 rings, or when it rings at many frequencies at once: the mic is then near its limit." };
            for (int i = 0; i < 5; ++i)
            {
                auto line = r.removeFromTop (54);
                const auto badge = line.removeFromLeft (24).removeFromTop (22).toFloat().withSizeKeepingCentre (20.0f, 20.0f);
                g.setColour (p.accent);
                g.drawEllipse (badge.reduced (1.0f), 1.4f);
                g.setFont (font (11.0f, true));
                g.drawText (juce::String (i + 1), badge, juce::Justification::centred);
                line.removeFromLeft (8);
                g.setColour (p.text);
                g.setFont (font (12.0f));
                g.drawFittedText (steps[i], line, juce::Justification::topLeft, 3, 1.0f);
            }
        }
        for (const auto& l : labels)
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            g.drawText (l.first.toUpperCase(), l.second, juce::Justification::centredLeft);
        }
        g.setColour (listening ? p.accent : p.textDim);
        g.setFont (font (12.0f, listening));
        g.drawFittedText (status, statusArea, juce::Justification::topLeft, 3);

        paintSpectrum (g);
        paintRings (g);
    }

    void resized() override
    {
        labels.clear();
        auto r = getLocalBounds();
        howCard = r.removeFromLeft (300);
        r.removeFromLeft (10);
        ringsCard = r.removeFromRight (360);
        r.removeFromRight (10);
        graphCard = r;
        {
            auto c = howCard.reduced (14, 0).withTrimmedTop (38);
            auto row = [&] (const juce::String& name, juce::Component& comp)
            {
                labels.push_back ({ name, c.removeFromTop (16) });
                comp.setBounds (c.removeFromTop (28));
                c.removeFromTop (10);
            };
            row ("Stage mic", micEditor);
            row ("Listen with", listenBox);
            listenButton.setBounds (c.removeFromTop (36));
            c.removeFromTop (8);
            statusArea = c.removeFromTop (48);
            c.removeFromTop (8);
            howText = c;
        }
        {
            auto c = graphCard.reduced (12, 0).withTrimmedTop (34).withTrimmedBottom (12);
            plotArea = c;
        }
        {
            auto c = ringsCard.reduced (12, 0).withTrimmedTop (36);
            auto bottom = c.removeFromBottom (44).withSizeKeepingCentre (c.getWidth(), 30);
            doneButton.setBounds (bottom.removeFromRight (200));
            bottom.removeFromRight (8);
            clearButton.setBounds (bottom.removeFromLeft (110));
            ringsArea = c;
        }
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        for (int i = 0; i < (int) test.rings.size(); ++i)
        {
            const auto row = ringRow (i);
            if (! row.contains (e.getPosition())) continue;
            if (e.x > row.getRight() - 28) test.rings.erase (test.rings.begin() + i);   // the x
            else test.rings[(size_t) i].applied = ! test.rings[(size_t) i].applied;    // in the console / processor now
            changed();
            return;
        }
    }

private:
    void start()
    {
        if (! proc.isStandalone() && ! proc.uiAsStandalone) return;
        savedSelected = settings.selectedMic.load();
        settings.selectedMic = test.listenMic;
        settings.genOn = false;     // the programme or the room is the source here, not the generator
        settings.running = true;
        detector.reset();
        listening = true;
        lastFrame = 0;
        status = "Listening at " + proc.mics.nameOf (test.listenMic) + ". Bring " + (test.micName.isNotEmpty() ? test.micName : juce::String ("the stage mic"))
                 + " up slowly until it rings.";
        refreshButtons();
        repaint();
    }

    void stop (const juce::String& why)
    {
        listening = false;
        settings.selectedMic = savedSelected;
        if (why.isNotEmpty()) status = why;
        refreshButtons();
        repaint();
    }

    void add (const fb::Ring& r)
    {
        // a ring close to one already found replaces it (it rang again)
        for (auto& e : test.rings)
            if (std::abs (std::log2 (e.ring.freq / r.freq)) < 1.0 / 12.0) { e.ring = r; e.time = juce::Time::getCurrentTime().formatted ("%H:%M"); changed(); return; }
        FeedbackTest::Entry e;
        e.ring = r;
        e.mic = test.micName;
        e.time = juce::Time::getCurrentTime().formatted ("%H:%M");
        test.rings.push_back (e);
        std::sort (test.rings.begin(), test.rings.end(), [] (const FeedbackTest::Entry& a, const FeedbackTest::Entry& b) { return a.ring.freq < b.ring.freq; });
        changed();
    }

    void timerCallback() override
    {
        if (! listening || snap == nullptr) return;
        if (snap->wfFrame == lastFrame || snap->wfLine.empty()) { repaint (plotArea); return; }
        lastFrame = snap->wfFrame;
        for (const auto& r : detector.push (snap->wfFreq, snap->wfLine))
        {
            add (r);
            status = "Ring at " + freqLabel (r.freq) + " Hz: pull the gain back, put in the cut (" + juce::String (juce::roundToInt (r.cutDb)) + " dB, Q "
                     + juce::String (juce::roundToInt (r.q)) + "), then go again.";
        }
        repaint();
    }

    void changed()
    {
        refreshButtons();
        if (onChanged) onChanged();
        repaint();
    }

    void refreshButtons()
    {
        listenButton.setButtonText (listening ? "Stop listening" : "Start listening");
        listenButton.setColour (juce::TextButton::buttonColourId, listening ? pal().bad : pal().accent);
        listenButton.setColour (juce::TextButton::textColourOffId, pal().onAccent);
        listenButton.setEnabled (proc.isStandalone() || proc.uiAsStandalone);
        clearButton.setEnabled (! test.rings.empty());
        doneButton.setButtonText (test.done ? "Rung out (undo)" : "Mark the feedback test done");
    }

    juce::Rectangle<int> ringRow (int i) const { return ringsArea.withTop (ringsArea.getY() + 22 + i * 34).withHeight (30); }

    void paintSpectrum (juce::Graphics& g)
    {
        juce::Graphics::ScopedSaveState keep (g);
        const auto& p = pal();
        auto plot = plotArea.toFloat().withTrimmedLeft (34.0f).withTrimmedBottom (18.0f);
        g.setColour (p.bg.withAlpha (0.5f));
        g.fillRect (plot);
        FreqAxis ax { 50.0, 16000.0, plot };
        drawFreqGrid (g, ax, plot, plot.withY (plot.getBottom()).withHeight (18.0f));
        const float top = 0.0f, range = 90.0f;
        auto yDb = [&] (double db) { return plot.getY() + (float) (top - db) / range * plot.getHeight(); };
        g.setFont (font (10.0f));
        for (float db = top; db >= top - range; db -= 15.0f)
        {
            g.setColour (p.grid);
            g.drawHorizontalLine ((int) yDb (db), plot.getX(), plot.getRight());
            g.setColour (p.textDim);
            g.drawText (juce::String ((int) db), juce::Rectangle<float> (plot.getX() - 34.0f, yDb (db) - 7.0f, 30.0f, 14.0f), juce::Justification::centredRight);
        }
        // rings found
        for (const auto& e : test.rings)
        {
            const float x = ax.x (e.ring.freq);
            g.setColour ((e.applied ? p.good : p.warn).withAlpha (0.6f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setFont (font (10.5f, true));
            g.drawText (freqLabel (e.ring.freq), juce::Rectangle<float> (x - 30.0f, plot.getY() + 4.0f, 60.0f, 14.0f), juce::Justification::centred);
        }
        if (snap == nullptr || snap->wfLine.empty() || ! listening)
        {
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawFittedText (listening ? "Waiting for sound..." : "Press Start listening, then bring the stage mic up slowly.",
                              plot.reduced (40.0f).toNearestInt(), juce::Justification::centred, 2);
            return;
        }
        const auto& f = snap->wfFreq;
        const auto& v = snap->wfLine;
        auto path = tracePath (f.size(), [&] (size_t i) { return ax.x (f[i]); }, [&] (size_t i) { return yDb (v[i]); },
                               [&] (size_t i) { return f[i] >= 50.0 && f[i] <= 16000.0; });
        g.reduceClipRegion (plot.toNearestInt());
        strokeTrace (g, path, micColour (test.listenMic), 1.6f);
    }

    void paintRings (juce::Graphics& g)
    {
        const auto& p = pal();
        auto head = ringsArea.withHeight (18);
        g.setColour (p.textDim);
        g.setFont (font (10.5f, true));
        const int cw[] = { 70, 60, 70, 46 };
        const char* heads[] = { "FREQUENCY", "ABOVE", "CUT", "Q" };
        for (int c = 0; c < 4; ++c) g.drawText (heads[c], head.removeFromLeft (cw[c]), juce::Justification::centredLeft);
        g.drawText ("IN", head, juce::Justification::centredLeft);
        if (test.rings.empty())
        {
            g.setColour (p.textFaint);
            g.setFont (font (12.0f));
            g.drawFittedText ("None yet. Each ring LiveAlign catches is listed here with a cut to put in.", ringsArea.withTrimmedTop (26).withHeight (40),
                              juce::Justification::topLeft, 2);
        }
        for (int i = 0; i < (int) test.rings.size(); ++i)
        {
            auto row = ringRow (i);
            if (row.getBottom() > ringsArea.getBottom()) break;
            const auto& e = test.rings[(size_t) i];
            g.setColour (p.panelRaised);
            g.fillRoundedRectangle (row.toFloat(), 5.0f);
            auto r = row.reduced (6, 0);
            g.setFont (monoFont (12.0f, true));
            g.setColour (p.text);
            g.drawText (freqLabel (e.ring.freq) + " Hz", r.removeFromLeft (cw[0] - 6), juce::Justification::centredLeft);
            g.setFont (monoFont (12.0f));
            g.setColour (p.textDim);
            g.drawText (juce::String (juce::roundToInt (e.ring.prominenceDb)) + " dB", r.removeFromLeft (cw[1]), juce::Justification::centredLeft);
            g.setColour (p.warn);
            g.drawText (juce::String (juce::roundToInt (e.ring.cutDb)) + " dB", r.removeFromLeft (cw[2]), juce::Justification::centredLeft);
            g.setColour (p.textDim);
            g.drawText (juce::String (juce::roundToInt (e.ring.q)), r.removeFromLeft (cw[3]), juce::Justification::centredLeft);
            const auto box = r.removeFromLeft (20).toFloat().withSizeKeepingCentre (14.0f, 14.0f);
            g.setColour (e.applied ? p.good : p.textDim);
            if (e.applied) drawStatusBadge (g, box, StepStatus::Ready);
            else g.drawRoundedRectangle (box, 3.0f, 1.3f);
            g.setColour (p.textFaint);
            g.setFont (font (11.0f));
            g.drawText (e.mic + (e.time.isNotEmpty() ? "  " + e.time : juce::String()), r.withTrimmedRight (24).withTrimmedLeft (6), juce::Justification::centredLeft, true);
            g.drawText ("x", row.removeFromRight (24), juce::Justification::centred);
        }
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        const auto below = ringsArea.withTop (ringRow ((int) test.rings.size()).getY() + 6).withHeight (48);
        if (! test.rings.empty() && below.getBottom() < ringsArea.getBottom())
            g.drawFittedText ("Click a ring to tick it once the cut is in; x removes it. Cuts are narrow bells (Q 10, about 1/7 octave).",
                              below, juce::Justification::topLeft, 3);
    }

    LiveAlignProcessor& proc;
    Settings& settings;
    FeedbackTest& test;
    const Snapshot* snap = nullptr;
    fb::Detector detector;
    juce::TextEditor micEditor;
    juce::ComboBox listenBox;
    juce::TextButton listenButton { "Start listening" }, clearButton { "Clear" }, doneButton;
    juce::Rectangle<int> howCard, graphCard, ringsCard, howText, statusArea, plotArea, ringsArea;
    std::vector<std::pair<juce::String, juce::Rectangle<int>>> labels;
    juce::String status { "Not listening." };
    bool listening = false;
    int savedSelected = 0;
    juce::uint64 lastFrame = 0;
};
} // namespace la::ui
