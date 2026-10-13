// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Guided measuring: a pop-up over the page with the venue plan, the point to measure highlighted (the mic, where it
// goes and how it is aimed), one big button (Measure, then Next point), a short count-in before the noise starts and
// the time left while it measures. Used by Room EQ (the listening positions) and Speaker response (each speaker
// close up). The page does the measuring; this only shows the way through it.
#include <functional>
#include "PlanPage.h"
#include "../UpdateChecker.h"

namespace la::ui
{
// What a page gives the guided pop-up.
class GuidedSource
{
public:
    virtual ~GuidedSource() = default;
    virtual juce::String guidedTitle() const = 0;                 // "Room EQ, before EQ"
    virtual std::vector<GuideSpot> guidedSpots() = 0;             // every point, in order, with its state (may place markers)
    virtual juce::String guidedHowTo() const = 0;                 // one line on how to set the mics up
    virtual bool guidedStart() = 0;                               // start measuring the points now; false: guidedStatus() says why
    virtual void guidedStop() = 0;
    virtual bool guidedBusy() const = 0;
    virtual float guidedProgress() const = 0;                     // 0..1
    virtual juce::String guidedPhase() const = 0;                 // "Playing pink noise..."
    virtual int guidedSerial() const = 0;                         // goes up with every measurement kept
    virtual juce::String guidedStatus() const = 0;                // the result, or why it stopped
    virtual void guidedRedo (const std::vector<GuideSpot>& was) = 0;   // put things back to measure these points again
    virtual bool guidedCanSkip() const { return false; }
    virtual void guidedSkip() {}
    virtual void guidedClosed() {}
    // A mic to pick for the point now (Speaker response, close up). No choices: no picker.
    virtual juce::StringArray guidedMicChoices() const { return {}; }
    virtual int guidedMic() const { return 0; }
    virtual void setGuidedMic (int) {}

    static bool enabled() { return UserPrefs::get ("guidedMeasure", "1") == "1"; }
    static void setEnabled (bool on) { UserPrefs::set ("guidedMeasure", on ? "1" : "0"); }
};

//==============================================================================
class GuidedOverlay : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onClose, onChanged;

    GuidedOverlay (LiveAlignProcessor& p, GuidedSource& s) : proc (p), source (s), canvas (p)
    {
        setWantsKeyboardFocus (true);
        canvas.guide.active = true;
        canvas.onChanged = [this] { if (onChanged) onChanged(); };
        addAndMakeVisible (canvas);

        primary.onClick = [this] { pressPrimary(); };
        addAndMakeVisible (primary);
        again.setButtonText ("Measure again");
        again.onClick = [this] { source.guidedRedo (justDone); state = State::Ready; reload(); startCountIn(); };
        addChildComponent (again);
        skip.setButtonText ("Skip this point");
        skip.onClick = [this] { source.guidedSkip(); reload(); };
        addChildComponent (skip);
        micPicker.onChange = [this]
        {
            source.setGuidedMic (micPicker.getSelectedItemIndex());
            reload();
        };
        addChildComponent (micPicker);
        close.setButtonText ("Close");
        close.onClick = [this] { closeNow(); };
        addAndMakeVisible (close);

        proc.plan.placeMissing ((int) proc.system.sources.size(), proc.tuningMicCount());
        reload();
        startTimerHz (30);
    }

    ~GuidedOverlay() override { stopTimer(); }

    void lookAndFeelChanged() override
    {
        primary.setColour (juce::TextButton::buttonColourId, state == State::Measuring || state == State::CountIn ? pal().bad : pal().accent);
        primary.setColour (juce::TextButton::textColourOffId, pal().onAccent);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.fillAll (juce::Colours::black.withAlpha (0.55f));
        g.setColour (p.panel);
        g.fillRoundedRectangle (card.toFloat(), 12.0f);
        g.setColour (p.border);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 12.0f, 1.0f);

        // header: what, and which point
        g.setColour (p.textDim);
        g.setFont (font (11.0f, true));
        g.drawText ("GUIDED MEASUREMENT", header.withHeight (16), juce::Justification::topLeft);
        g.setColour (p.text);
        g.setFont (font (19.0f, true));
        g.drawText (source.guidedTitle(), header.withTrimmedTop (16), juce::Justification::centredLeft, true);
        g.setColour (p.accent);
        g.setFont (font (15.0f, true));
        g.drawText (pointText(), header.withTrimmedRight (110), juce::Justification::centredRight, true);

        // where the mics go
        auto r = instr;
        g.setFont (font (14.5f, true));
        for (const auto& sp : spots)
        {
            if (sp.state != GuideSpot::Now && sp.state != GuideSpot::JustDone) continue;
            if (r.getHeight() < 20) break;
            auto row = r.removeFromTop (24);
            const auto col = sp.mic >= 0 ? micColour (sp.mic) : p.accent;
            g.setColour (col);
            g.fillEllipse (row.removeFromLeft (14).toFloat().withSizeKeepingCentre (10.0f, 10.0f));
            row.removeFromLeft (8);
            g.setColour (p.text);
            const bool measured = sp.state == GuideSpot::JustDone;
            juce::String t = (sp.mic >= 0 ? proc.mics.nameOf (sp.mic) + (measured ? "  at  " : "  to  ") : juce::String()) + sp.label;
            g.drawText (t, row.removeFromLeft (juce::jmin (row.getWidth() / 2, 360)), juce::Justification::centredLeft, true);
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            if (measured) g.setColour (p.good);
            g.drawText (measured ? juce::String ("measured")
                        : sp.how.isNotEmpty() ? sp.how
                                        : sp.up ? juce::String (juce::CharPointer_UTF8 ("pointing straight up (90\xc2\xb0 calibration)"))
                                                : juce::String (juce::CharPointer_UTF8 ("aimed at the speaker (0\xc2\xb0 calibration)")),
                        row, juce::Justification::centredLeft, true);
            g.setFont (font (14.5f, true));
        }
        g.setColour (p.textDim);
        g.setFont (font (12.5f));
        g.drawFittedText (source.guidedHowTo(), r.withTrimmedTop (2), juce::Justification::topLeft, 2);

        // progress: count-in, measuring with the time left, or the result
        auto pr = progressArea;
        auto bar = pr.removeFromBottom (8).toFloat();
        juce::String line;
        juce::Colour lineCol = p.textDim;
        float fill = 0.0f;
        if (state == State::CountIn)
        {
            const int left = juce::jmax (1, (int) std::ceil (countInLeft()));
            line = "Starting in " + juce::String (left) + "...  stand clear of the mic";
            lineCol = p.accent;
            fill = 1.0f - (float) (countInLeft() / kCountIn);
        }
        else if (state == State::Measuring)
        {
            fill = source.guidedProgress();
            line = source.guidedPhase();
            const double el = (juce::Time::getMillisecondCounterHiRes() - measureStart) / 1000.0;
            if (fill > 0.08f && el > 1.0)
                line << "   about " << juce::jmax (1, juce::roundToInt (el * (1.0 - fill) / fill)) << " s left";
            lineCol = p.accent;
        }
        else if (state == State::Done)
        {
            fill = 1.0f;
            line = source.guidedStatus();
            lineCol = resultOk ? p.good : p.warn;
        }
        else if (allDone())
        {
            line = "Every point is measured.";
            lineCol = p.good;
        }
        else
        {
            line = message.isNotEmpty() ? message : juce::String ("Put the mic in place, then press Measure.");
            lineCol = message.isNotEmpty() ? p.warn : p.textDim;
        }
        g.setColour (lineCol);
        g.setFont (font (14.0f, state != State::Ready));
        g.drawFittedText (line, pr, juce::Justification::centredLeft, 2);
        g.setColour (p.border);
        g.fillRoundedRectangle (bar, 4.0f);
        if (fill > 0.0f)
        {
            g.setColour (state == State::Done ? (resultOk ? p.good : p.warn) : p.accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, fill)), 4.0f);
        }

        if (micPicker.isVisible())
        {
            g.setColour (p.textDim);
            g.setFont (font (11.0f, true));
            g.drawText ("MIC", micLabel, juce::Justification::centredLeft);
        }

        // a dot for every point
        auto dots = dotsArea;
        const int n = (int) spots.size();
        const float step = juce::jmin (22.0f, (float) dots.getWidth() / (float) juce::jmax (1, n));
        float x = (float) dots.getX();
        for (const auto& sp : spots)
        {
            const auto c = juce::Point<float> (x + step * 0.5f, (float) dots.getCentreY());
            const bool done = sp.state == GuideSpot::Done || sp.state == GuideSpot::JustDone;
            g.setColour (done ? p.good : sp.state == GuideSpot::Now ? p.accent : p.border);
            if (sp.state == GuideSpot::ToDo) g.drawEllipse (juce::Rectangle<float> (10.0f, 10.0f).withCentre (c), 1.5f);
            else g.fillEllipse (juce::Rectangle<float> (sp.state == GuideSpot::Now ? 12.0f : 10.0f, sp.state == GuideSpot::Now ? 12.0f : 10.0f).withCentre (c));
            x += step;
        }
    }

    void resized() override
    {
        auto b = getLocalBounds();
        card = b.withSizeKeepingCentre (juce::jmin (1040, b.getWidth() - 60), juce::jmin (780, b.getHeight() - 50));
        auto c = card.reduced (22, 18);
        header = c.removeFromTop (44);
        close.setBounds (header.withTrimmedLeft (header.getWidth() - 96).withSizeKeepingCentre (96, 30));
        c.removeFromTop (10);
        auto buttons = c.removeFromBottom (46);
        c.removeFromBottom (12);
        dotsArea = c.removeFromBottom (16);
        c.removeFromBottom (8);
        progressArea = c.removeFromBottom (44);
        c.removeFromBottom (10);
        int nowCount = 0;
        for (const auto& sp : spots) nowCount += sp.state == GuideSpot::Now || sp.state == GuideSpot::JustDone ? 1 : 0;
        instr = c.removeFromBottom (juce::jmin (4, juce::jmax (1, nowCount)) * 24 + 24);
        c.removeFromBottom (10);
        canvas.setBounds (c);

        primary.setBounds (buttons.removeFromRight (240));
        buttons.removeFromRight (10);
        again.setBounds (buttons.removeFromRight (170).withSizeKeepingCentre (170, 38));
        if (micPicker.isVisible())
        {
            micLabel = buttons.removeFromLeft (36);
            micPicker.setBounds (buttons.removeFromLeft (220).withSizeKeepingCentre (220, 34));
            buttons.removeFromLeft (12);
        }
        skip.setBounds (buttons.removeFromLeft (150).withSizeKeepingCentre (150, 34));
    }

    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { closeNow(); return true; }
        if (k == juce::KeyPress::spaceKey || k == juce::KeyPress::returnKey) { pressPrimary(); return true; }
        return true;   // the page underneath does not get keys while this is open
    }

    void closeNow()
    {
        if (state == State::Measuring) source.guidedStop();
        stopTimer();
        source.guidedClosed();
        if (onClose) onClose();
    }

    // For the preview: the states to show.
    void pressPrimaryForTest() { pressPrimary(); }
    void skipCountIn() { if (state == State::CountIn) countInStart -= kCountIn * 1000.0; }

private:
    enum class State { Ready, CountIn, Measuring, Done };
    static constexpr double kCountIn = 3.0;

    double countInLeft() const { return juce::jmax (0.0, kCountIn - (juce::Time::getMillisecondCounterHiRes() - countInStart) / 1000.0); }

    bool allDone() const
    {
        for (const auto& sp : spots) if (sp.state == GuideSpot::Now) return false;
        return true;
    }

    juce::String pointText() const
    {
        int done = 0, now = 0;
        for (const auto& sp : spots)
        {
            done += sp.state == GuideSpot::Done || sp.state == GuideSpot::JustDone ? 1 : 0;
            now += sp.state == GuideSpot::Now ? 1 : 0;
        }
        const int n = (int) spots.size();
        if (now == 0) return juce::String (done) + " of " + juce::String (n) + " measured";
        if (now == 1) return "Point " + juce::String (done + 1) + " of " + juce::String (n);
        return "Points " + juce::String (done + 1) + juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")) + juce::String (done + now) + " of " + juce::String (n);
    }

    void reload()
    {
        spots = source.guidedSpots();
        if (state == State::Done && resultOk)
            for (auto& sp : spots)
            {
                if (sp.state == GuideSpot::Now) sp.state = GuideSpot::ToDo;   // the next ones show after Next point
                for (const auto& j : justDone)
                    if (j.label == sp.label) { sp.state = GuideSpot::JustDone; sp.mic = j.mic; sp.how = j.how; sp.up = j.up; }
            }
        canvas.guide.spots = spots;
        refreshButtons();
        resized();
        repaint();
    }

    void refreshButtons()
    {
        const bool finished = allDone() && state != State::Done;
        switch (state)
        {
            case State::Ready:     primary.setButtonText (finished ? "Done" : "Measure"); break;
            case State::CountIn:
            case State::Measuring: primary.setButtonText ("Stop"); break;
            case State::Done:      primary.setButtonText (resultOk ? (nextExists() ? juce::String (juce::CharPointer_UTF8 ("Next point  \xe2\x86\x92")) : juce::String ("Done"))
                                                                   : juce::String ("Try again"));
                                   break;
        }
        again.setVisible (state == State::Done && resultOk);
        skip.setVisible (state == State::Ready && ! finished && source.guidedCanSkip());
        const auto mics = source.guidedMicChoices();
        const bool pick = mics.size() > 1 && state == State::Ready && ! finished;
        micPicker.setVisible (pick);
        if (pick)
        {
            micPicker.clear (juce::dontSendNotification);
            micPicker.addItemList (mics, 1);
            micPicker.setSelectedItemIndex (source.guidedMic(), juce::dontSendNotification);
        }
        resized();
        int now = 0;
        for (const auto& sp : spots) now += sp.state == GuideSpot::Now ? 1 : 0;
        skip.setButtonText (now > 1 ? "Skip these points" : "Skip this point");
        lookAndFeelChanged();
    }

    bool nextExists()
    {
        for (const auto& sp : source.guidedSpots()) if (sp.state == GuideSpot::Now) return true;
        return false;
    }

    void pressPrimary()
    {
        switch (state)
        {
            case State::Ready:
                if (allDone()) { closeNow(); return; }
                startCountIn();
                break;
            case State::CountIn:
                state = State::Ready;
                message = "Stopped.";
                refreshButtons();
                repaint();
                break;
            case State::Measuring:
                source.guidedStop();
                state = State::Ready;
                message = "Stopped.";
                refreshButtons();
                repaint();
                break;
            case State::Done:
                if (! resultOk) { source.guidedRedo (justDone); state = State::Ready; reload(); startCountIn(); return; }
                if (! nextExists()) { closeNow(); return; }
                state = State::Ready;
                justDone.clear();
                message.clear();
                reload();
                break;
        }
    }

    void startCountIn()
    {
        message.clear();
        state = State::CountIn;
        countInStart = juce::Time::getMillisecondCounterHiRes();
        refreshButtons();
        repaint();
    }

    void timerCallback() override
    {
        canvas.guide.pulse = std::fmod (canvas.guide.pulse + 1.0f / 45.0f, 1.0f);
        canvas.repaint();
        if (state == State::CountIn && countInLeft() <= 0.0)
        {
            nowSpots.clear();
            for (const auto& sp : spots) if (sp.state == GuideSpot::Now) nowSpots.push_back (sp);
            serialAtStart = source.guidedSerial();
            if (source.guidedStart())
            {
                state = State::Measuring;
                measureStart = juce::Time::getMillisecondCounterHiRes();
            }
            else
            {
                state = State::Ready;
                message = source.guidedStatus();
            }
            refreshButtons();
        }
        else if (state == State::Measuring && ! source.guidedBusy())
        {
            resultOk = source.guidedSerial() != serialAtStart;
            justDone = nowSpots;
            state = State::Done;
            reload();
            if (onChanged) onChanged();
        }
        if (state == State::CountIn || state == State::Measuring) repaint (progressArea.expanded (4));
    }

    LiveAlignProcessor& proc;
    GuidedSource& source;
    PlanCanvas canvas;
    juce::TextButton primary, again, skip, close;
    juce::ComboBox micPicker;
    juce::Rectangle<int> micLabel;
    juce::Rectangle<int> card, header, instr, progressArea, dotsArea;
    std::vector<GuideSpot> spots, nowSpots, justDone;
    State state = State::Ready;
    juce::String message;
    double countInStart = 0.0, measureStart = 0.0;
    int serialAtStart = 0;
    bool resultOk = false;
};
} // namespace la::ui
