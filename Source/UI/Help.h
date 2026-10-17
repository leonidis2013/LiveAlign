// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Help: a glossary of every number and term LiveAlign shows, a how-to for each step, "What is this?" (click anything
// for its explanation and the terms it uses), and the first-run tour. Opened from the "?" next to the page title.
#include <functional>
#include "NavRail.h"
#include "Theme.h"

namespace la::help
{
struct Term { const char* name; const char* text; };

inline const std::vector<Term>& glossary()
{
    static const std::vector<Term> t {
        { "Absorption (air)", "High frequencies lose energy travelling through air, more when it is dry. At 30 m (100 ft) 8 kHz can be several dB down: normal at the back of a room, and not something to EQ. Worked out as in ISO 9613-1." },
        { "ALcons", "Articulation loss of consonants (%), from the STI (Farrel Becker's relation): the share of consonants likely to be missed. Under 5 % is good, over 15 % is poor." },
        { "As found / as left", "The processor settings and responses before this session changed anything, and after. The reports show both." },
        { "Bass ratio", "The reverberation at 125 - 250 Hz against 500 Hz - 1 kHz. Above 1 the room is warm; much above 1.3 it can sound boomy." },
        { "C50", "Clarity for speech: the sound energy in the first 50 ms against the rest (dB, ISO 3382-1). Above 0 dB speech is clear; below -3 dB it starts to blur." },
        { "C80", "Clarity for music: the first 80 ms against the rest (dB, ISO 3382-1). About -2 to +4 dB suits most music." },
        { "Calibration file", "A mic's own frequency response, measured by its maker, that LiveAlign takes out of every measurement. A 0 degree file is for a mic aimed at the speaker, a 90 degree file for one pointing up." },
        { "Coherence", "How much of what the mic hears is the signal you are playing (1) rather than noise or the room (0). Below about 0.6 a reading is not to be trusted, so it is greyed out." },
        { "Compression", "When a system is pushed, the amps or limiters stop the level rising as fast as the signal: 3 dB more in gives less than 3 dB more out. Gain and headroom looks for it." },
        { "Coverage", "How evenly a speaker reaches the seats it is meant for. The spec (plus or minus 3 dB by default) is how much the level may vary across its area; AVIXA A102.01 (Audio Coverage Uniformity) rates a system the same way." },
        { "Critical distance", "Where the direct sound from a speaker and the room's reverberant sound are equally loud. Beyond it the room dominates and EQ helps less. Depends on the speaker's directivity (Q) and the room." },
        { "Crossover", "Where the sub hands over to the tops: a low-pass on the sub and a high-pass on the tops, at the same frequency, so they add up to a flat response." },
        { "dBA, dBC, dBZ", "Sound level weighted like the ear at moderate levels (A), with the low end included (C), or flat (Z), as IEC 61672-1 defines them. Show limits are usually LAeq (A) or LCeq (C)." },
        { "dBFS", "Decibels against digital full scale: 0 dBFS is the loudest a signal can be. Used when a mic has no sound-level calibration." },
        { "Delay", "Time added to a speaker in the processor so its sound arrives together with another's. 1 ms is about 34 cm (13.5 in) at 20 C." },
        { "Directivity (Q)", "How narrowly a speaker radiates: 2 for nearly all round (a sub), around 9 for 90 x 60 degrees, 30 for a line array. From the data sheet's coverage angles." },
        { "Distortion", "Sound that is not in the signal: the amps or drivers running out. With pink noise it shows as the coherence dropping as the level goes up." },
        { "Echo", "A reflection late and loud enough to be heard as a repeat (usually more than 50 ms after the direct sound): a far wall, a balcony front." },
        { "EDT", "Early decay time: the reverberation time from the first 10 dB of the decay (ISO 3382-1), which is what the ear notices most. Compare it with RT." },
        { "EQ (parametric)", "A filter at a frequency, with a gain and a width (Q). Room EQ uses broad cuts and gentle boosts toward the target." },
        { "First reflection", "The delay between the direct sound and the first strong reflection (the initial time delay gap). Short gaps add intimacy; very short ones colour the sound." },
        { "Flutter echo", "A rapid ringing between two parallel hard surfaces. A clap in the room shows it." },
        { "Generator", "LiveAlign's test signal: pink noise, a sweep or a sine, out of the outputs you pick. It always starts off, and never goes above the ceiling in Settings." },
        { "Group", "Speakers of a kind that share one set of settings: Mains (left, centre, right), Subs, Fills, Delays. The group's filters, level, polarity and EQ are set once; each member keeps its own delay and level trim, and is still measured and checked on its own. The crossover reads each side as its group (the members averaged)." },
        { "Headroom", "How far the system can go above the show level before it compresses or limits. Peaks in music need several dB (6 dB is a common aim)." },
        { "Impulse response", "What the mic hears from an instant click through the system and the room: the direct sound, then the reflections and the decay. Everything about the room comes from it." },
        { "LAeq", "The A-weighted sound level averaged over a time (15 minutes, an hour ...), per IEC 61672-1. Most show limits are an LAeq (the WHO and many local rules quote 15 minute or 1 hour LAeq)." },
        { "Level balance", "Each speaker's level against the others where the audience hears them: left and right equal, fills a little under the mains, the sub as the target asks." },
        { "Limiter", "A ceiling in the processor or amp that protects the speakers. The threshold is its setting (dBu or dBFS)." },
        { "Loopback", "A cable from an interface output straight back to an input, carrying the signal that feeds the PA. The reference every mic is compared with, and the interface's own delay." },
        { "MQTT", "A small messaging protocol home-automation sensors publish on (Home Assistant's Mosquitto broker, Zigbee2MQTT). With a room sensor set up (Settings, Room sensor), Venue captures its temperature and humidity for tuning, and Monitoring shows them live with their effect on the delays and the top end at the back." },
        { "NC, NR", "Noise criterion (ANSI S12.2) and noise rating (ISO 1996): one number for the background noise in a room (air handling, traffic), from its octave bands. Under 30 is quiet; over 40 starts to mask speech." },
        { "Phase", "Where in its cycle a frequency arrives. Two speakers that add up in phase reinforce; out of phase they cancel." },
        { "Pink noise", "Noise with equal energy in every octave: it sounds even, and it is the usual test signal for system tuning (IEC 60268-1 describes it as the programme simulation noise for loudspeaker testing)." },
        { "Polarity", "Whether a speaker pushes when the signal pushes (normal) or pulls (inverted). A wrong polarity cancels low end where speakers overlap." },
        { "Processor sheet", "Every setting to enter in your processor, output by output: filters, delay, polarity, gain, EQ. Tick each output once it is in." },
        { "Q", "The width of an EQ band: higher Q is narrower. Room EQ keeps cuts at Q 4 or wider, boosts wider still." },
        { "Reference", "The signal the system is fed, which every mic is compared with: the loopback, or the generator's own copy." },
        { "Reverberation time (RT)", "How long the sound takes to die away by 60 dB (RT60), in seconds, per octave, measured from the decay as T20 or T30 (ISO 3382-1 / ISO 3382-2). Speech wants under about 1 s; music a little more." },
        { "RTA", "Real-time analyser: the level of what the mic hears in each band (1/3 octave or finer), with no reference." },
        { "Schroeder frequency", "Below it, single room modes (standing waves) rule: peaks and dips that change from seat to seat. EQ there only helps where most seats agree." },
        { "Seat-to-seat variance", "How much the tonal balance differs from point to point (dB). Under 2 dB is very even; EQ cannot fix differences between seats, aim and coverage can." },
        { "SPL", "Sound pressure level (dB re 20 µPa). A mic calibrated with an acoustic calibrator (IEC 60942 class 1 or 2, usually 94 or 114 dB at 1 kHz) reads true SPL." },
        { "Speed of sound", "About 343 m/s (1125 ft/s) at 20 C, faster when warmer. Delays set at one temperature drift when the room warms up." },
        { "STI", "Speech transmission index (IEC 60268-16), 0 to 1: how well speech gets through the system and the room (with the noise). Above 0.6 is good, under 0.45 poor; emergency and voice-alarm systems usually ask for 0.5 or more (ISO 7240-19, NFPA 72)." },
        { "Sweep", "A tone that glides from low to high. Sweeps give long, clean impulse responses for the room acoustics." },
        { "Target curve", "The tonal balance to tune toward: usually a gentle tilt with more low end, as people prefer for music and speech." },
        { "Time alignment", "Delaying the speaker that arrives first so two speakers arrive together where they overlap." },
        { "Transfer function", "What the system does to the signal: the mic against the reference, level (magnitude) and phase per frequency. The main view for tuning (a dual-channel FFT, as in Smaart or SIM)." },
        { "Treble ratio", "The reverberation at 2 - 4 kHz against 500 Hz - 1 kHz. Low is a dull room, high a bright one." },
        { "Trim", "A small level change on an output (from Level balance), on top of its gain." },
        { "Ts", "Centre time (ISO 3382-1): the 'centre of gravity' of the impulse response (ms). Lower is clearer." },
        { "Waterfall", "The spectrum over time, as a 3D landscape: ringing frequencies (feedback, room modes) stand out as ridges." },
        { "Standards", "Where LiveAlign follows a published standard: room acoustics per ISO 3382-1 (RT, EDT, C50, C80, Ts) and the sweep method of ISO 18233; STI per IEC 60268-16; sound level weighting and Leq per IEC 61672-1; NC per ANSI S12.2 and NR per ISO 1996; air absorption per ISO 9613-1; coverage uniformity in the spirit of AVIXA A102.01; calibrators per IEC 60942. The numbers are comparable with other tools that follow the same standards." } };
    return t;
}

// The terms each page uses (for "What is this?" on a page and the how-to).
inline juce::StringArray termsFor (int page)
{
    using ui::Page;
    switch ((Page) page)
    {
        case Page::Mics: return { "Calibration file", "Reference", "Loopback", "SPL", "dBFS", "NC, NR" };
        case Page::Venue: return { "Speed of sound", "Absorption (air)", "MQTT", "Target curve", "Headroom", "Coverage" };
        case Page::Speakers: return { "Group", "Directivity (Q)", "Polarity", "Critical distance" };
        case Page::Plan: return { "Coverage" };
        case Page::Response: return { "Transfer function", "Polarity", "Phase", "Coherence", "Calibration file" };
        case Page::Crossover: return { "Crossover", "Phase", "Polarity", "Delay" };
        case Page::Level: return { "Level balance", "Trim" };
        case Page::Coverage: return { "Coverage", "Directivity (Q)" };
        case Page::Align: return { "Time alignment", "Delay", "Impulse response", "Speed of sound" };
        case Page::Acoustics: return { "Reverberation time (RT)", "EDT", "C50", "C80", "Ts", "STI", "ALcons", "Bass ratio", "Treble ratio", "Schroeder frequency",
                                       "Critical distance", "First reflection", "Echo", "Flutter echo", "NC, NR", "Sweep", "Standards" };
        case Page::Room: return { "Target curve", "EQ (parametric)", "Q", "Seat-to-seat variance", "Schroeder frequency", "Absorption (air)" };
        case Page::Headroom: return { "Headroom", "Compression", "Distortion", "Limiter", "dBA, dBC, dBZ" };
        case Page::Listening: return { "EQ (parametric)", "Target curve" };
        case Page::Feedback: return { "Waterfall", "EQ (parametric)", "Q" };
        case Page::MonitorMic: return { "Calibration file", "LAeq", "dBA, dBC, dBZ" };
        case Page::Monitor: return { "Transfer function", "RTA", "Impulse response", "Waterfall", "LAeq", "Coherence", "Standards" };
        case Page::Sheet: return { "Processor sheet", "Delay", "Polarity", "Q", "Limiter" };
        case Page::Home: case Page::TuningReport: case Page::MonitorReport: case Page::Locker: case Page::Settings:
        default: return { "As found / as left" };
    }
}

// What to do on each page, in a few steps.
inline juce::StringArray howTo (int page)
{
    using ui::Page;
    switch ((Page) page)
    {
        case Page::Home: return { "Pick a venue from the list, or start a new one.", "Follow NEXT: the navigation marks the step to do now.",
                                  "A session is one visit; for a system already tuned, the quick re-check finds what has drifted." };
        case Page::Venue: return { "Name the venue and note anything worth remembering.", "Pick what the room is used for and enter its size (for the acoustics).",
                                   "Enter the temperature and humidity of the day.", "Set the targets: the Room EQ curve, the show level at FOH, the headroom." };
        case Page::Speakers: return { "Add the speakers: a left / right pair, subs, fills, delays.", "Give each its output and channel, and how widely it covers.",
                                      "Measure them on Speaker response." };
        case Page::Plan: return { "Drag the speakers to where they are, and turn them the way they face.", "Add the points to measure at (FOH, a few seats); each is for tuning, monitoring or both.",
                                  "Pick the mic at each point (Mic here)." };
        case Page::Mics: return { "Add a row per mic and pick its input.", "Load each mic's calibration file, or link it to the locker.",
                                  "Pick the reference (the loopback), then run the pre-flight: loopback, room noise, sound level." };
        case Page::Locker: return { "Every measurement mic you own, with its serial number, calibration files and sound level.", "Link a mic on This venue's mics to its locker entry." };
        case Page::Response: return { "Put a mic close to the speaker (30 cm, 1 ft), on axis.", "Measure it: its response and its polarity come from the same capture.",
                                      "Fix an inverted polarity in the processor and measure again." };
        case Page::Crossover: return { "Find the crossover: LiveAlign suggests the filters, the delay and the polarity.", "Enter them in the processor (Processor sheet).",
                                       "Measure the sub and the tops together to check they add up." };
        case Page::Level: return { "Tick the centre (FOH) and a seat in each fill's area.", "Measure each speaker alone at each point.",
                                   "Put the trims on the sheet, enter them, and measure again to check." };
        case Page::Coverage: return { "Each speaker's level across its area comes from Level balance.", "A speaker that falls off more than the spec: re-aim it as the hint says.",
                                      "Measure it again; the report shows before and after." };
        case Page::Align: return { "Pick a spot where two speakers overlap (a fill and the mains).", "Measure each, set the delay, enter it.", "Measure them together to check." };
        case Page::Acoustics: return { "A quiet room: sweep at two or three points.", "Read the verdict: whether EQ can help, or the room needs treatment.",
                                       "Room EQ allows for what it finds (the switch on Room EQ)." };
        case Page::Room: return { "Measure the points as found.", "Suggest EQ, enter it in the processor, then I've entered this EQ.",
                                  "Measure a check at the same points; refine until it is close enough.", "Then the listening check." };
        case Page::Headroom: return { "Right after every speaker is verified: the gain structure comes before any tuning.", "Hearing protection on, nobody near the speakers.",
                                      "Run the whole system: pink noise in steps up to the show level plus headroom.",
                                      "Then each speaker alone at the same drive: the one that compresses first is the weak link.", "Note the amp gains and limiters for the sheet." };
        case Page::Listening: return { "Play music you know, at show level.", "At each point: Good or A problem, and a note.", "Compare with the EQ bypassed; then call it done." };
        case Page::Feedback: return { "The PA at show level, the stage mic in place.", "Bring the mic up slowly; LiveAlign catches each ring.", "Notch the rings on the mic's channel." };
        case Page::MonitorMic: return { "Pick the mic for the show and where it stands (a monitoring point).", "Calibrate it, set the limit and the window.", "Then Live monitoring." };
        case Page::Monitor: return { "The graphs show the programme at the monitor mic.", "The event panel logs the level against the limit.", "Against the tuning shows how the show differs from the tuning." };
        case Page::Sheet: return { "Enter each output's settings in the processor.", "Tick it once it is in.", "History lists every change; roll back to any of them." };
        case Page::TuningReport: case Page::MonitorReport: return { "Pick the sections, then save the PDF." };
        case Page::Settings: default: return { "Your preferences for every venue; Tuning has this venue's own values too." };
    }
}

inline const Term* find (const juce::String& name)
{
    for (const auto& t : glossary()) if (name.equalsIgnoreCase (t.name)) return &t;
    return nullptr;
}

// The glossary terms mentioned in a piece of text (a tooltip, a label).
inline juce::StringArray termsIn (const juce::String& text)
{
    juce::StringArray out;
    for (const auto& t : glossary())
    {
        const juce::String n (t.name);
        const auto key = n.upToFirstOccurrenceOf (" (", false, false).upToFirstOccurrenceOf (",", false, false);
        if (key.length() >= 2 && text.containsIgnoreCase (key)) out.add (n);
    }
    return out;
}
} // namespace la::help

namespace la::ui
{
// Text in a call-out: a title, a body, and glossary terms below.
class HelpCard : public juce::Component
{
public:
    HelpCard (const juce::String& t, const juce::String& b, const juce::StringArray& terms, int width = 420) : title (t), body (b)
    {
        for (const auto& n : terms) if (const auto* g = help::find (n)) entries.push_back (g);
        layout = juce::TextLayout();
        juce::AttributedString a;
        a.setWordWrap (juce::AttributedString::byWord);
        if (body.isNotEmpty()) a.append (body + "\n", font (13.0f), pal().text);
        for (const auto* e : entries)
        {
            a.append ("\n" + juce::String (e->name) + "\n", font (12.5f, true), pal().accent);
            a.append (juce::String (e->text) + "\n", font (12.0f), pal().textDim);
        }
        layout.createLayout (a, (float) width - 28.0f);
        setSize (width, juce::jmin (560, (int) layout.getHeight() + 50));
    }
    void paint (juce::Graphics& g) override
    {
        g.setColour (pal().text);
        g.setFont (font (14.5f, true));
        g.drawText (title, getLocalBounds().reduced (14, 10).removeFromTop (20), juce::Justification::centredLeft, true);
        layout.draw (g, getLocalBounds().reduced (14, 0).withTrimmedTop (36).toFloat());
    }
private:
    juce::String title, body;
    std::vector<const help::Term*> entries;
    juce::TextLayout layout;
};

// The glossary: search box and the list.
class GlossaryView : public juce::Component
{
public:
    GlossaryView()
    {
        search.setTextToShowWhenEmpty ("Search the glossary", pal().textFaint);
        search.setFont (font (13.0f));
        search.onTextChange = [this] { fill(); };
        addAndMakeVisible (search);
        text.setMultiLine (true, true);
        text.setReadOnly (true);
        text.setCaretVisible (false);
        text.setScrollbarsShown (true);
        text.setColour (juce::TextEditor::backgroundColourId, juce::Colours::transparentBlack);
        text.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (text);
        setSize (520, 560);
        fill();
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (12);
        search.setBounds (r.removeFromTop (30));
        r.removeFromTop (8);
        text.setBounds (r);
    }
private:
    void fill()
    {
        const auto q = search.getText().trim();
        text.clear();
        for (const auto& t : help::glossary())
        {
            if (q.isNotEmpty() && ! juce::String (t.name).containsIgnoreCase (q) && ! juce::String (t.text).containsIgnoreCase (q)) continue;
            text.setFont (font (13.0f, true));
            text.setColour (juce::TextEditor::textColourId, pal().accent);
            text.insertTextAtCaret (juce::String (t.name) + "\n");
            text.setFont (font (12.5f));
            text.setColour (juce::TextEditor::textColourId, pal().text);
            text.insertTextAtCaret (juce::String (t.text) + "\n\n");
        }
        text.setCaretPosition (0);
    }
    juce::TextEditor search, text;
};

// "What is this?": covers the window; the next click shows help for what is under it.
class WhatIsThisOverlay : public juce::Component
{
public:
    std::function<void (juce::Point<int>)> onPick;   // in the parent's coordinates
    std::function<void()> onCancel;
    WhatIsThisOverlay() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setWantsKeyboardFocus (true); }
    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        g.setColour (p.accent.withAlpha (0.06f));
        g.fillAll();
        auto b = juce::Rectangle<int> (460, 38).withCentre ({ getWidth() / 2 + NavRail::kWidth / 2, 30 });
        g.setColour (p.accent);
        g.fillRoundedRectangle (b.toFloat(), 8.0f);
        g.setColour (p.onAccent);
        g.setFont (font (13.0f, true));
        g.drawText ("Click anything for help on it.   Esc to stop.", b, juce::Justification::centred);
    }
    void mouseUp (const juce::MouseEvent& e) override { if (onPick) onPick (e.getPosition() + getPosition()); }
    bool keyPressed (const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey && onCancel) { onCancel(); return true; } return false; }
};

// The first-run tour: a few cards, each pointing at a part of the window.
class TourOverlay : public juce::Component
{
public:
    struct Step { juce::String title, text; std::function<juce::Rectangle<int>()> area; };
    std::function<void()> onClose;

    explicit TourOverlay (std::vector<Step> s) : steps (std::move (s))
    {
        for (auto* b : { &backButton, &nextButton, &skipButton }) addAndMakeVisible (*b);
        backButton.onClick = [this] { if (index > 0) { --index; update(); } };
        nextButton.onClick = [this] { if (index + 1 < (int) steps.size()) { ++index; update(); } else if (onClose) onClose(); };
        skipButton.onClick = [this] { if (onClose) onClose(); };
        setWantsKeyboardFocus (true);
    }
    void setStep (int i) { index = juce::jlimit (0, (int) steps.size() - 1, i); update(); }
    void resized() override { update(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { if (onClose) onClose(); return true; }
        if (k == juce::KeyPress::rightKey || k == juce::KeyPress::returnKey) { nextButton.triggerClick(); return true; }
        if (k == juce::KeyPress::leftKey) { backButton.triggerClick(); return true; }
        return false;
    }
    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        juce::Path dim;
        dim.addRectangle (getLocalBounds().toFloat());
        if (! hole.isEmpty()) { dim.addRoundedRectangle (hole.toFloat().expanded (4.0f), 8.0f); dim.setUsingNonZeroWinding (false); }
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillPath (dim);
        if (! hole.isEmpty())
        {
            g.setColour (p.accent);
            g.drawRoundedRectangle (hole.toFloat().expanded (4.0f), 8.0f, 2.0f);
        }
        g.setColour (p.panel);
        g.fillRoundedRectangle (card.toFloat(), 10.0f);
        g.setColour (p.accent);
        g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 10.0f, 1.5f);
        auto c = card.reduced (18, 14);
        g.setColour (p.textFaint);
        g.setFont (font (10.5f, true));
        g.drawText ("TOUR  " + juce::String (index + 1) + " OF " + juce::String ((int) steps.size()), c.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (p.text);
        g.setFont (font (16.0f, true));
        g.drawText (steps[(size_t) index].title, c.removeFromTop (26), juce::Justification::centredLeft, true);
        g.setColour (p.textDim);
        g.setFont (font (13.0f));
        g.drawFittedText (steps[(size_t) index].text, c.withTrimmedBottom (40), juce::Justification::topLeft, 6);
    }
private:
    void update()
    {
        if (steps.empty()) return;
        hole = steps[(size_t) index].area ? steps[(size_t) index].area() : juce::Rectangle<int>();
        const int w = 400, h = 200;
        // the card beside what it points at (right of it, else left, else centred)
        juce::Rectangle<int> cr;
        if (hole.isEmpty()) cr = juce::Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre());
        else if (hole.getRight() + 24 + w < getWidth()) cr = juce::Rectangle<int> (w, h).withPosition (hole.getRight() + 24, juce::jlimit (12, getHeight() - h - 12, hole.getY()));
        else cr = juce::Rectangle<int> (w, h).withPosition (juce::jmax (12, hole.getX() - 24 - w), juce::jlimit (12, getHeight() - h - 12, hole.getY()));
        card = cr;
        auto b = card.reduced (18, 14).removeFromBottom (30);
        skipButton.setBounds (b.removeFromLeft (110));
        nextButton.setBounds (b.removeFromRight (100));
        b.removeFromRight (8);
        backButton.setBounds (b.removeFromRight (80));
        backButton.setEnabled (index > 0);
        nextButton.setButtonText (index + 1 < (int) steps.size() ? "Next" : "Done");
        skipButton.setButtonText (index + 1 < (int) steps.size() ? "Skip the tour" : "Close");
        repaint();
    }
    std::vector<Step> steps;
    int index = 0;
    juce::Rectangle<int> hole, card;
    juce::TextButton backButton { "Back" }, nextButton { "Next" }, skipButton { "Skip the tour" };
};
} // namespace la::ui
