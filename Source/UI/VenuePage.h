// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Venue, Details: everything about the place itself, in one spot. Its name and notes, what the room is used for and
// how big it is, the conditions of the day (temperature and humidity, per session) and the targets (Room EQ's tonal
// balance, the show level at FOH and the headroom wanted). The other pages show these read-only, with a link here.
#include "../PluginProcessor.h"
#include "../Analysis/AirAbsorption.h"
#include "Theme.h"

namespace la::ui
{
class VenueDetailsPage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onChanged, onRecheck;

    explicit VenueDetailsPage (LiveAlignProcessor& p) : proc (p)
    {
        auto editor = [this] (juce::TextEditor& e, const juce::String& tip, bool multi = false)
        {
            e.setMultiLine (multi, true);
            e.setReturnKeyStartsNewLine (multi);
            e.setTooltip (tip);
            addAndMakeVisible (e);
        };
        editor (nameEditor, "The venue's name: on the reports, the file and the venue list");
        nameEditor.onTextChange = [this] { const auto t = nameEditor.getText().trim(); if (t.isNotEmpty()) { proc.venueName = t; changed(); } };
        editor (notesEditor, "Anything worth knowing next time: access, power, the house desk, who to call", true);
        notesEditor.onTextChange = [this] { proc.venueNotes = notesEditor.getText(); changed(); };

        useBox.addItemList (RoomTuning::useNames(), 1);
        useBox.setTooltip ("What the room is mostly used for: sets the reverberation time and intelligibility Room acoustics compares with");
        useBox.onChange = [this] { proc.room.use = useBox.getSelectedItemIndex(); changed(); };
        addAndMakeVisible (useBox);
        for (int i = 0; i < 4; ++i)
        {
            static const char* tips[] = { "The room's inside length (front to back)", "The room's inside width", "The average ceiling height",
                                          "The volume, if the room is not a box (it wins over the three sizes). Leave empty if you do not know it." };
            editor (sizeEditors[i], tips[i]);
            sizeEditors[i].setInputRestrictions (8, "0123456789.,");
            sizeEditors[i].onTextChange = [this] { readSizes(); };
        }

        auto slider = [this] (juce::Slider& s, const juce::String& tip)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 24);
            s.setTooltip (tip);
            addAndMakeVisible (s);
        };
        slider (tempSlider, "The air temperature in the room now: sets the speed of sound (delays into distances) and the air absorption. "
                            "With a room sensor (Settings, Room sensor), Capture takes its reading");
        captureButton.setTooltip ("Take the room sensor's temperature and humidity as this session's conditions (Cmd/Ctrl+Z undoes it)");
        captureButton.onClick = [this] { captureFromSensor(); };
        addChildComponent (captureButton);
        tempSlider.onValueChange = [this]
        {
            const double v = tempSlider.getValue();
            proc.setConditions ((float) (fahrenheit() ? (v - 32.0) * 5.0 / 9.0 : v), proc.settings.humidity.load());
            capturedNote.clear();
            changed();
        };
        slider (humiditySlider, "The relative humidity in the room now: dry air takes more of the high end off at the back");
        humiditySlider.setRange (5.0, 100.0, 1.0);
        humiditySlider.setTextValueSuffix (" %");
        humiditySlider.onValueChange = [this] { proc.setConditions (proc.settings.temperatureC.load(), (float) humiditySlider.getValue()); capturedNote.clear(); changed(); };
        recheckButton.setTooltip ("The quick re-check: each speaker close up, the level at FOH and the delays, against the tuning");
        recheckButton.onClick = [this] { if (onRecheck) onRecheck(); };
        addChildComponent (recheckButton);

        for (const auto& t : targets::list()) curveBox.addItem (t.name, curveBox.getNumItems() + 1);
        curveBox.setTooltip ("The tonal balance Room EQ tunes toward");
        curveBox.onChange = [this] { proc.room.target = curveBox.getSelectedItemIndex(); proc.room.suggested.clear(); changed(); };
        addAndMakeVisible (curveBox);
        prefBox (levelBox, "targetDbA", "The loudest the show should run at FOH: Gain and headroom checks the system gets there with room to spare");
        prefBox (headroomBox, "headroomDb", "How far above the target level the system should go before it compresses or limits");
        prefBox (coverageBox, "coverageTol", "How evenly each speaker should cover its area (Aim and coverage)");
        refresh();
        startTimerHz (2);
    }

    // The room sensor's reading becomes this session's conditions (an edit: undo takes it back).
    void captureFromSensor()
    {
        auto& f = proc.climate;
        if (! f.live()) return;
        const float t = f.tempC.load(), h = f.humidity.load();
        proc.setConditions (std::isfinite (t) ? t : proc.settings.temperatureC.load(), std::isfinite (h) ? h : proc.settings.humidity.load());
        capturedNote = "Captured from the sensor at " + juce::Time::getCurrentTime().formatted ("%H:%M");
        refresh();
        changed();
    }

    void refresh()
    {
        if (! nameEditor.hasKeyboardFocus (true)) nameEditor.setText (proc.venueName, juce::dontSendNotification);
        if (! notesEditor.hasKeyboardFocus (true)) notesEditor.setText (proc.venueNotes, juce::dontSendNotification);
        useBox.setSelectedItemIndex (juce::jlimit (0, RoomTuning::useNames().size() - 1, proc.room.use), juce::dontSendNotification);
        const auto& pl = proc.plan;
        const double k = feet() ? 3.28084 : 1.0;
        const double vals[4] = { pl.lengthM * k, pl.widthM * k, pl.heightM * k, pl.volumeM3 * k * k * k };
        for (int i = 0; i < 4; ++i)
            if (! sizeEditors[i].hasKeyboardFocus (true))
                sizeEditors[i].setText (vals[i] > 0.0 ? juce::String (vals[i], i == 3 ? 0 : 1) : juce::String(), juce::dontSendNotification);
        const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        tempSlider.setRange (fahrenheit() ? 14.0 : -10.0, fahrenheit() ? 113.0 : 45.0, 1.0);
        tempSlider.setTextValueSuffix (" " + deg + (fahrenheit() ? "F" : "C"));
        const double c = proc.settings.temperatureC.load();
        tempSlider.setValue (fahrenheit() ? c * 9.0 / 5.0 + 32.0 : c, juce::dontSendNotification);
        humiditySlider.setValue (proc.settings.humidity.load(), juce::dontSendNotification);
        curveBox.setSelectedItemIndex (juce::jlimit (0, targets::count() - 1, proc.room.target), juce::dontSendNotification);
        for (auto* b : { &levelBox, &headroomBox, &coverageBox }) syncPrefBox (*b);
        recheckButton.setVisible (driftWorthRecheck() && ! proc.monitoringSession());   // it plays test signals: a tuning session's
        syncCapture();
        resized();
        repaint();
    }

    // How far the conditions have moved since the tuning was set (for Home and the re-check prompt).
    static juce::String driftText (const LiveAlignProcessor& p, bool& worth)
    {
        worth = false;
        const auto& ch = p.checks;
        if (! std::isfinite (ch.tunedTempC)) return {};
        const double now = p.settings.temperatureC.load(), dt = now - ch.tunedTempC;
        double maxDelay = 0.0;
        for (const auto& s : p.system.sources) maxDelay = juce::jmax (maxDelay, s.proc.delayMs);
        const double drift = air::delayDriftMs (maxDelay, ch.tunedTempC, now);
        const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        const bool f = p.settings.temperatureUnit.load() == 1;
        const double shown = f ? dt * 9.0 / 5.0 : dt;
        juce::String t;
        if (std::abs (dt) < 0.5) t = "Same temperature as when tuned";
        else t = juce::String (juce::roundToInt (std::abs (shown))) + deg + (f ? "F " : "C ") + (dt > 0.0 ? "warmer" : "cooler") + " than when tuned";
        if (ch.tunedConditionsOn.isNotEmpty()) t << " (" << ch.tunedConditionsOn << ")";
        if (maxDelay > 0.05) t << ": the longest delay drifts " << juce::String (std::abs (drift), 2) << " ms";
        if (std::isfinite (ch.tunedHumidity) && std::abs (p.settings.humidity.load() - ch.tunedHumidity) >= 20.0f)
            t << "; humidity " << juce::String (juce::roundToInt (ch.tunedHumidity)) << " % then, " << juce::String (juce::roundToInt (p.settings.humidity.load())) << " % now";
        worth = std::abs (drift) >= p.pref ("recheckDelay") * 0.5 || std::abs (dt) >= 5.0
                || (std::isfinite (ch.tunedHumidity) && std::abs (p.settings.humidity.load() - ch.tunedHumidity) >= 20.0f);
        return t + ".";
    }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, venueCard.toFloat(), "Venue");
        drawPanel (g, roomCard.toFloat(), "Room");
        drawPanel (g, condCard.toFloat(), "Conditions (this session)");
        drawPanel (g, targetCard.toFloat(), "Targets");
        for (const auto& l : labels)
        {
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawText (l.second, l.first, juce::Justification::centredLeft, true);
        }
        g.setFont (font (12.0f));
        g.setColour (p.textDim);
        g.drawFittedText (roomInfo(), roomInfoArea, juce::Justification::topLeft, 3);
        g.drawFittedText (conditionsInfo(), condInfoArea, juce::Justification::topLeft, 5);
        bool worth = false;
        const auto drift = driftText (proc, worth);
        g.setColour (worth ? p.warn : p.textDim);
        g.drawFittedText (drift.isNotEmpty() ? drift : juce::String ("The tuning has not been set yet: the conditions are noted when the delays or the EQ are."),
                          driftArea, juce::Justification::topLeft, 3);
        g.setColour (p.textFaint);
        g.setFont (font (11.0f));
        g.drawFittedText ("Defaults come from Settings, Tuning defaults; a value picked here is this venue's own.", targetNote, juce::Justification::topLeft, 2);
    }

    void resized() override
    {
        labels.clear();
        auto r = getLocalBounds();
        auto left = r.removeFromLeft ((r.getWidth() - 12) / 2);
        r.removeFromLeft (12);
        auto right = r;
        venueCard = left.removeFromTop (juce::jmin (260, left.getHeight() / 2));
        left.removeFromTop (12);
        roomCard = left.removeFromTop (juce::jmin (250, left.getHeight()));
        condCard = right.removeFromTop (juce::jmin (270, right.getHeight() / 2 + 20));
        right.removeFromTop (12);
        targetCard = right.removeFromTop (juce::jmin (200, right.getHeight()));

        auto field = [this] (juce::Rectangle<int>& c, const juce::String& name, juce::Component& comp, int h = 28, int labelW = 130)
        {
            auto row = c.removeFromTop (h);
            labels.push_back ({ row.removeFromLeft (labelW), name });
            comp.setBounds (row);
            c.removeFromTop (8);
        };
        {
            auto c = venueCard.reduced (14, 0).withTrimmedTop (36).withTrimmedBottom (12);
            field (c, "Name", nameEditor, 28, 70);
            auto row = c;
            labels.push_back ({ row.removeFromLeft (70).withHeight (24), "Notes" });
            notesEditor.setBounds (row);
        }
        {
            auto c = roomCard.reduced (14, 0).withTrimmedTop (36);
            field (c, "Used for", useBox);
            const auto u = juce::String (feet() ? " (ft)" : " (m)");
            auto row = c.removeFromTop (28);
            labels.push_back ({ row.removeFromLeft (130), "Length x width" + u });
            sizeEditors[0].setBounds (row.removeFromLeft ((row.getWidth() - 8) / 2));
            row.removeFromLeft (8);
            sizeEditors[1].setBounds (row);
            c.removeFromTop (8);
            field (c, "Ceiling height" + u, sizeEditors[2]);
            field (c, juce::String ("Or volume") + (feet() ? " (cu ft)" : " (m3)"), sizeEditors[3]);
            roomInfoArea = c.removeFromTop (40);
        }
        {
            captureButton.setBounds (condCard.getRight() - 14 - 170, condCard.getY() + 6, 170, 24);
            auto c = condCard.reduced (14, 0).withTrimmedTop (36);
            field (c, "Temperature", tempSlider);
            field (c, "Humidity", humiditySlider);
            condInfoArea = c.removeFromTop (72);
            c.removeFromTop (4);
            driftArea = c.removeFromTop (48);
            recheckButton.setBounds (c.removeFromTop (28).withWidth (170));
        }
        {
            auto c = targetCard.reduced (14, 0).withTrimmedTop (36);
            field (c, "Room EQ target", curveBox, 28, 150);
            field (c, "Level at FOH", levelBox, 28, 150);
            field (c, "Headroom wanted", headroomBox, 28, 150);
            field (c, "Coverage within", coverageBox, 28, 150);
            targetNote = c.removeFromTop (32);
        }
    }

private:
    bool feet() const { return proc.settings.distanceUnit.load() == 0; }
    bool fahrenheit() const { return proc.settings.temperatureUnit.load() == 1; }
    void changed() { repaint(); if (onChanged) onChanged(); }
    void timerCallback() override
    {
        if (! isShowing()) return;
        syncCapture();
        const bool w = driftWorthRecheck();
        if (recheckButton.isVisible() != (w && ! proc.monitoringSession())) recheckButton.setVisible (w && ! proc.monitoringSession());
        repaint();
    }
    bool driftWorthRecheck() const { bool w = false; driftText (proc, w); return w; }

    // The room sensor's button: shown when one is set up, its reading on it, usable when the reading is fresh.
    void syncCapture()
    {
        auto& f = proc.climate;
        const bool on = f.enabled();
        if (captureButton.isVisible() != on) captureButton.setVisible (on);
        if (! on) return;
        juce::String t;
        if (f.live())
        {
            const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            const float tc = f.tempC.load(), rh = f.humidity.load();
            t = "Capture ";
            if (std::isfinite (tc)) t << juce::roundToInt (fahrenheit() ? tc * 9.0f / 5.0f + 32.0f : tc) << deg << (fahrenheit() ? "F" : "C");
            if (std::isfinite (tc) && std::isfinite (rh)) t << ", ";
            if (std::isfinite (rh)) t << juce::roundToInt (rh) << " %";
        }
        else t = f.connected.load() ? "Sensor: no reading yet" : "Sensor: not connected";
        if (captureButton.getButtonText() != t) captureButton.setButtonText (t);
        captureButton.setEnabled (f.live());
    }

    void readSizes()
    {
        const double k = feet() ? 3.28084 : 1.0;
        auto v = [this] (int i) { return juce::jmax (0.0, sizeEditors[i].getText().replaceCharacter (',', '.').getDoubleValue()); };
        proc.plan.lengthM = v (0) / k;
        proc.plan.widthM = v (1) / k;
        proc.plan.heightM = v (2) / k;
        proc.plan.volumeM3 = v (3) / (k * k * k);
        changed();
    }

    juce::String roomInfo() const
    {
        const double vol = proc.plan.volume();
        if (vol <= 0.0) return "The size gives Room acoustics the Schroeder frequency (where room modes stop) and each speaker's critical distance.";
        const bool f = feet();
        return "Volume " + juce::String (juce::roundToInt (f ? vol * 35.3147 : vol)) + (f ? " cu ft" : " m3")
               + ". Room acoustics uses it for the Schroeder frequency and the critical distance.";
    }

    // The distance to the back of the room, for the air absorption (the room's length, else a typical 30 m).
    double farMetres() const { return proc.plan.lengthM > 1.0 ? proc.plan.lengthM : 30.0; }

    juce::String conditionsInfo() const
    {
        const double t = proc.settings.temperatureC.load(), rh = proc.settings.humidity.load();
        const double c = air::speedOfSound (t);
        const bool f = feet();
        const double far = farMetres();
        const double a8 = air::absorptionDbPerM (8000.0, t, rh) * far, a4 = air::absorptionDbPerM (4000.0, t, rh) * far,
                     a16 = air::absorptionDbPerM (16000.0, t, rh) * far;
        juce::String s = capturedNote.isNotEmpty() ? capturedNote + ". " : juce::String();
        s << "Speed of sound " + (f ? juce::String (juce::roundToInt (c * 3.28084)) + " ft/s" : juce::String (juce::roundToInt (c)) + " m/s") + ". ";
        s << "At " << (f ? juce::String (juce::roundToInt (far * 3.28084)) + " ft" : juce::String (juce::roundToInt (far)) + " m")
          << (proc.plan.lengthM > 1.0 ? " (the back of the room)" : "") << " the air takes " << juce::String (a4, 1) << " dB off 4 kHz, "
          << juce::String (a8, 1) << " dB off 8 kHz and " << juce::String (a16, 1) << " dB off 16 kHz";
        s << (a8 >= 3.0 ? ": expected at the back, so Room EQ leaves it alone" : "") << " (ISO 9613-1).";
        return s;
    }

    void prefBox (juce::ComboBox& b, const char* key, const juce::String& tip)
    {
        b.setTooltip (tip);
        b.getProperties().set ("key", key);
        b.onChange = [this, &b, k = juce::String (key)]
        {
            const int id = b.getSelectedId();
            const auto* d = prefs::find (k);
            if (d == nullptr) return;
            if (id == 1) proc.venuePrefs.reset (k);
            else if (id >= 2 && id - 2 < (int) d->values.size()) proc.venuePrefs.set (k, d->values[(size_t) (id - 2)]);
            changed();
        };
        addAndMakeVisible (b);
    }
    void syncPrefBox (juce::ComboBox& b)
    {
        const auto k = b.getProperties()["key"].toString();
        const auto* d = prefs::find (k);
        if (d == nullptr) return;
        b.clear (juce::dontSendNotification);
        b.addItem ("Default (" + d->text (AppPrefs::get().value (k)) + ")", 1);
        for (size_t i = 0; i < d->values.size(); ++i) b.addItem (d->text (d->values[i]), (int) i + 2);
        int sel = 1;
        if (proc.venuePrefs.has (k))
            for (size_t i = 0; i < d->values.size(); ++i)
                if (std::abs (d->values[i] - proc.venuePrefs.value (k)) < 1e-6) sel = (int) i + 2;
        b.setSelectedId (sel, juce::dontSendNotification);
    }

    LiveAlignProcessor& proc;
    juce::TextEditor nameEditor, notesEditor, sizeEditors[4];
    juce::ComboBox useBox, curveBox, levelBox, headroomBox, coverageBox;
    juce::Slider tempSlider, humiditySlider;
    juce::TextButton captureButton;
    juce::String capturedNote;   // when the conditions were last captured from the sensor
    juce::TextButton recheckButton { "Quick re-check..." };
    juce::Rectangle<int> venueCard, roomCard, condCard, targetCard, roomInfoArea, condInfoArea, driftArea, targetNote;
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> labels;
};
} // namespace la::ui
