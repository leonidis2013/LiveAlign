// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Settings: a full page with the categories listed on the left (Audio, Measurement, Display, Units, Tuning defaults,
// Generator and safety, Monitoring, Reports and files, Updates) and one category shown at a time, each with its own
// Reset to defaults. The tuning defaults can be overridden for the open venue (the "This venue" column).
#include <functional>
#include <memory>
#include "Theme.h"
#include "../PluginProcessor.h"
#include "../Prefs.h"
#include "../StandaloneHooks.h"

namespace la::ui
{
class SettingsPage : public juce::Component, private juce::Timer
{
public:
    std::function<void()> onThemeChanged, onLayoutChanged, onPrefsChanged, onVenueChanged, onShowTour;

    explicit SettingsPage (LiveAlignProcessor& p) : proc (p), settings (p.settings), updates (p.updates)
    {
        for (int c = 0; c < kNumPrefCats; ++c)
        {
            auto* b = catButtons.add (new juce::TextButton (prefCatName (c)));
            b->setClickingTogglesState (true);
            b->setRadioGroupId (8810);
            b->onClick = [this, c] { if (catButtons[c]->getToggleState()) show (c); };
            addAndMakeVisible (b);
        }
        resetButton.onClick = [this] { resetCategory(); };
        resetButton.setTooltip ("Put every setting in this category back to how LiveAlign comes");
        addAndMakeVisible (resetButton);
        clearVenueButton.onClick = [this] { proc.venuePrefs.values.clear(); changedVenue(); rebuildLater(); };
        clearVenueButton.setTooltip ("Use your defaults for everything in this venue");
        addAndMakeVisible (clearVenueButton);
        view.setViewedComponent (&content, false);
        view.setScrollBarsShown (true, false);
        addAndMakeVisible (view);
        const int last = juce::jlimit (0, kNumPrefCats - 1, UserPrefs::get ("settingsCategory", "0").getIntValue());
        show (visibleCat (last) ? last : 0);
        startTimerHz (2);
    }

    ~SettingsPage() override { stopTimer(); }

    // After a venue opens (its overrides) or the theme changes.
    void refresh() { build(); repaint(); }

    int category() const { return cat; }
    void showCategory (int c) { if (c >= 0 && c < kNumPrefCats && visibleCat (c)) show (c); }

    void paint (juce::Graphics& g) override
    {
        const auto& p = pal();
        drawPanel (g, menuCard.toFloat(), "Settings");
        drawPanel (g, bodyCard.toFloat(), prefCatName (cat));
        g.setColour (p.textDim);
        g.setFont (font (11.0f));
        g.drawFittedText (catBlurb (cat), blurbArea, juce::Justification::topLeft, 2);
        g.setColour (p.textFaint);
        g.setFont (font (10.5f));
        g.drawText (juce::String (JucePlugin_Name) + " " + JucePlugin_VersionString, menuCard.reduced (14, 10).removeFromBottom (18), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        menuCard = r.removeFromLeft (230);
        r.removeFromLeft (10);
        bodyCard = r;
        auto m = menuCard.reduced (10, 0).withTrimmedTop (40);
        for (int c = 0; c < kNumPrefCats; ++c)
        {
            auto* b = catButtons[c];
            b->setVisible (visibleCat (c));
            if (! b->isVisible()) continue;
            b->setBounds (m.removeFromTop (34));
            m.removeFromTop (4);
        }
        auto body = bodyCard.reduced (16, 0).withTrimmedTop (38);
        auto top = body.removeFromTop (34);
        resetButton.setBounds (top.removeFromRight (170).withSizeKeepingCentre (170, 28));
        top.removeFromRight (8);
        clearVenueButton.setBounds (top.removeFromRight (220).withSizeKeepingCentre (220, 28));
        clearVenueButton.setVisible (cat == (int) PrefCat::Tuning && ! proc.venuePrefs.values.empty());
        blurbArea = top;
        body.removeFromTop (6);
        view.setBounds (body.withTrimmedBottom (10));
        layoutContent();
    }

private:
    // ---- a row: a label (and a line of help) with up to two controls ----
    struct Row
    {
        juce::String label, help;
        juce::Component* a = nullptr;
        juce::Component* b = nullptr;
        bool heading = false;
        juce::String note;   // shown where the second control would be
    };

    class Content : public juce::Component
    {
    public:
        std::vector<Row> rows;
        std::vector<std::unique_ptr<juce::Component>> owned;
        bool twoColumns = false;   // tuning: "Your default" and "This venue"
        juce::String status;       // the updates line
        juce::Colour statusColour;

        static constexpr int kLabelW = 330, kCtrlW = 230, kGap = 12;

        void paint (juce::Graphics& g) override
        {
            const auto& p = pal();
            int y = 0;
            if (twoColumns)
            {
                g.setColour (p.textDim);
                g.setFont (font (10.5f, true));
                g.drawText ("YOUR DEFAULT", kLabelW, 0, kCtrlW, 20, juce::Justification::centredLeft);
                g.drawText ("THIS VENUE", kLabelW + kCtrlW + kGap, 0, kCtrlW, 20, juce::Justification::centredLeft);
                y = 24;
            }
            for (const auto& r : rows)
            {
                if (r.heading)
                {
                    y += y > 0 ? 10 : 0;
                    g.setColour (p.accent);
                    g.fillRoundedRectangle (0.0f, (float) y + 7.0f, 3.0f, 12.0f, 1.5f);
                    g.setColour (p.text);
                    g.setFont (font (13.0f, true));
                    g.drawText (r.label, 10, y, getWidth() - 10, 26, juce::Justification::centredLeft);
                    g.setColour (p.border);
                    g.drawHorizontalLine (y + 27, 0.0f, (float) getWidth());
                    y += 34;
                    continue;
                }
                const int h = rowHeight (r);
                g.setColour (p.text);
                g.setFont (font (12.5f));
                g.drawText (r.label, 0, y + 2, kLabelW - 10, 20, juce::Justification::centredLeft, true);
                if (r.help.isNotEmpty())
                {
                    g.setColour (p.textFaint);
                    g.setFont (font (10.5f));
                    g.drawFittedText (r.help, 0, y + 21, kLabelW - 14, h - 23, juce::Justification::topLeft, 2);
                }
                if (r.note.isNotEmpty())
                {
                    g.setColour (p.textFaint);
                    g.setFont (font (10.5f));
                    g.drawFittedText (r.note, kLabelW + kCtrlW + kGap, y + 2, kCtrlW, h - 4, juce::Justification::centredLeft, 2);
                }
                y += h;
            }
            if (status.isNotEmpty())
            {
                g.setColour (statusColour);
                g.setFont (font (11.5f));
                g.drawFittedText (status, 0, y + 6, getWidth(), 34, juce::Justification::topLeft, 2);
            }
        }

        static int rowHeight (const Row& r) { return r.help.isNotEmpty() ? 52 : 38; }

        int layout()
        {
            int y = twoColumns ? 24 : 0;
            for (auto& r : rows)
            {
                if (r.heading) { y += (y > 0 ? 10 : 0) + 34; continue; }
                const int h = rowHeight (r);
                if (r.a != nullptr) r.a->setBounds (kLabelW, y + 2, kCtrlW, 28);
                if (r.b != nullptr) r.b->setBounds (kLabelW + kCtrlW + kGap, y + 2, kCtrlW, 28);
                y += h;
            }
            return y + (status.isNotEmpty() ? 44 : 10);
        }
    };

    bool visibleCat (int c) const
    {
        if (proc.isStandalone() || proc.uiAsStandalone) return true;
        // the VST3 only monitors: no audio device, no tuning, no generator
        return c != (int) PrefCat::Tuning && c != (int) PrefCat::Audio && c != (int) PrefCat::Generator;
    }

    static juce::String catBlurb (int c)
    {
        switch ((PrefCat) c)
        {
            case PrefCat::Audio: return "The interface and the reference the mics are compared against. The mics themselves are set up on Mics.";
            case PrefCat::Measurement: return "How the transfer function and the RTA are worked out. The defaults suit most tuning; the guided steps pick what they need.";
            case PrefCat::Display: return "What the screen shows and how, and the tour of LiveAlign.";
            case PrefCat::Units: return "Distances and temperatures (the temperature of the day is on Venue, Details).";
            case PrefCat::Tuning: return "Your defaults for every venue. A venue can keep its own value (This venue): it is saved with the venue and listed in the report.";
            case PrefCat::Generator: return "The test signal: where it starts, how loud it may ever get and how gently it comes in.";
            case PrefCat::Monitoring: return "Defaults for new sessions' event level, and how the feedback watch and the delay tracking behave.";
            case PrefCat::Reports: return "What the reports look like, and where files go.";
            case PrefCat::Sensor: return "A temperature and humidity sensor on your network, through an MQTT broker (Home Assistant, Zigbee2MQTT). "
                                         "Its reading is shown on Monitoring and captured on Venue, Details for tuning.";
            case PrefCat::Updates:
            default: return "LiveAlign asks GitHub for the latest release. Nothing is sent but the request.";
        }
    }

    void show (int c)
    {
        cat = c;
        catButtons[c]->setToggleState (true, juce::dontSendNotification);
        UserPrefs::set ("settingsCategory", juce::String (c));
        build();
        resized();
        repaint();
    }

    // ---- building a category ----
    template <class T> T& make()
    {
        auto c = std::make_unique<T>();
        auto& ref = *c;
        content.addAndMakeVisible (ref);
        content.owned.push_back (std::move (c));
        return ref;
    }
    void heading (const juce::String& t) { Row r; r.label = t; r.heading = true; content.rows.push_back (r); }
    void row (const juce::String& label, juce::Component& a, const juce::String& help = {}, juce::Component* b = nullptr, const juce::String& note = {})
    {
        Row r;
        r.label = label; r.help = help; r.a = &a; r.b = b; r.note = note;
        content.rows.push_back (r);
    }

    juce::ComboBox& combo (const juce::StringArray& items, int selectedIndex, std::function<void (int)> onPick)
    {
        auto& c = make<juce::ComboBox>();
        c.addItemList (items, 1);
        c.setSelectedItemIndex (selectedIndex, juce::dontSendNotification);
        auto* cp = &c;
        c.onChange = [cp, onPick] { onPick (cp->getSelectedItemIndex()); };
        return c;
    }

    // A preference: your default, and (tuning) this venue's own value.
    void prefRow (const PrefDef& d, bool withVenue)
    {
        auto& ap = AppPrefs::get();
        juce::StringArray items;
        int sel = 0;
        const double now = ap.value (d.key);
        for (size_t i = 0; i < d.values.size(); ++i)
        {
            items.add (d.text (d.values[i]));
            if (std::abs (d.values[i] - now) < 1e-9) sel = (int) i;
        }
        const juce::String key (d.key);
        auto& def = combo (items, sel, [this, key, &d] (int i)
        {
            AppPrefs::get().set (key, d.values[(size_t) juce::jlimit (0, (int) d.values.size() - 1, i)]);
            changedPrefs();
            rebuildLater();
        });
        def.setTooltip (d.help);
        juce::Component* venueCtrl = nullptr;
        juce::String note;
        if (withVenue && d.venue)
        {
            juce::StringArray vi;
            vi.add ("Your default (" + d.text (now) + ")");
            int vs = 0;
            for (size_t i = 0; i < d.values.size(); ++i)
            {
                vi.add (d.text (d.values[i]));
                if (proc.venuePrefs.has (key) && std::abs (d.values[i] - proc.venuePrefs.value (key)) < 1e-9) vs = (int) i + 1;
            }
            auto& v = combo (vi, vs, [this, key, &d] (int i)
            {
                if (i <= 0) proc.venuePrefs.reset (key);
                else proc.venuePrefs.set (key, d.values[(size_t) juce::jlimit (0, (int) d.values.size() - 1, i - 1)]);
                changedVenue();
                rebuildLater();
            });
            v.setTooltip (proc.venuePrefs.has (key) ? "This venue's own value (a venue override). Pick \"Your default\" to use yours."
                                                    : "This venue uses your default. Pick a value to keep one for this venue only.");
            if (proc.venuePrefs.has (key)) v.setColour (juce::ComboBox::outlineColourId, pal().accent);
            venueCtrl = &v;
        }
        else if (withVenue)
            note = juce::String (d.key) == "eqBands" || juce::String (d.key) == "eqMaxBoost" ? "Each venue: on Room EQ" : "For new venues";
        row (d.label, def, d.help, venueCtrl, note);
    }

    void prefRows (PrefCat c, bool withVenue)
    {
        juce::String group;
        for (const auto& d : prefs::all())
        {
            if (d.cat != c) continue;
            if (group != d.group) { group = d.group; heading (group); }
            prefRow (d, withVenue);
        }
    }

    void build()
    {
        content.rows.clear();
        climateStatus = nullptr;
        content.owned.clear();
        content.twoColumns = false;
        content.status.clear();
        const int nIn = juce::jmax (1, proc.numInputs());
        switch ((PrefCat) cat)
        {
            case PrefCat::Audio:
            {
                if (StandaloneHooks::showAudioSettings())
                {
                    auto& b = make<juce::TextButton>();
                    b.setButtonText ("Audio device...");
                    b.onClick = [] { if (auto& f = StandaloneHooks::showAudioSettings()) f(); };
                    row ("Audio device", b, "The interface, sample rate, buffer size and which channels are used");
                }
                juce::StringArray refs { "Off", "Generator (internal copy)" };
                for (int i = 0; i < nIn; ++i) refs.add ("Input " + juce::String (i + 1) + " (loopback)");
                auto& ref = combo (refs, settings.refSource.load() <= nIn + 1 ? settings.refSource.load() : -1,
                                   [this] (int i) { settings.refSource = i; settings.requestResetAverages = true; });
                if (settings.refSource.load() > nIn + 1) ref.setTextWhenNothingSelected ("Input " + juce::String (settings.refSource.load() - 1) + " (not on this interface)");
                row ("Reference", ref, "A loopback cable from the output that feeds the PA is the most accurate; the generator copy needs no cable");
                auto& coh = combo ({ "Off", "20 %", "40 % (default)", "60 %", "80 % (strict)" }, juce::jlimit (0, 4, (int) std::lround (settings.coherenceBlank.load() * 5.0f)),
                                   [this] (int i) { settings.coherenceBlank = (float) i / 5.0f; });
                row ("Minimum coherence", coh, "Points less reliable than this are hidden and not trusted; raise it in a noisy room");
                if (proc.isStandalone() || proc.uiAsStandalone) listenRows();
                content.status = StandaloneHooks::deviceDescription() ? StandaloneHooks::deviceDescription()() : juce::String ("Inputs and outputs come from the host");
                content.statusColour = pal().textDim;
                break;
            }
            case PrefCat::Measurement:
            {
                heading ("Transfer function");
                static const int sizes[] = { 8192, 16384, 32768, 65536 };
                int fi = 2;
                for (int i = 0; i < 4; ++i) if (sizes[i] == settings.fftSize.load()) fi = i;
                row ("FFT size", combo ({ "8k   (5.9 Hz, fast)", "16k  (2.9 Hz)", "32k  (1.5 Hz)", "64k  (0.7 Hz, slow)" }, fi, [this] (int i) { settings.fftSize = sizes[juce::jlimit (0, 3, i)]; }),
                     "Bigger resolves low frequencies better but reacts more slowly (at 48 kHz)");
                static const int avgs[] = { 1, 2, 4, 8, 16, 32, 64 };
                juce::StringArray ai;
                int as = 3;
                for (int i = 0; i < 7; ++i) { ai.add (juce::String (avgs[i]) + (avgs[i] == 1 ? " (none)" : " frames")); if (avgs[i] == settings.averages.load()) as = i; }
                row ("Averaging", combo (ai, as, [this] (int i) { settings.averages = avgs[juce::jlimit (0, 6, i)]; }), "More is steadier but slower to react");
                static const int smooth[] = { 48, 24, 12, 6, 3 };
                juce::StringArray si;
                int ss = 1;
                for (int i = 0; i < 5; ++i) { si.add ("1/" + juce::String (smooth[i]) + " octave"); if (smooth[i] == settings.smoothing.load()) ss = i; }
                row ("Smoothing", combo (si, ss, [this] (int i) { settings.smoothing = smooth[juce::jlimit (0, 4, i)]; }), "1/24 or 1/12 octave to tune, 1/3 for the overall shape");
                static const float ranges[] = { 12.0f, 20.0f, 30.0f, 40.0f };
                int rs = 1;
                for (int i = 0; i < 4; ++i) if (std::abs (ranges[i] - settings.magRange.load()) < 0.1f) rs = i;
                row ("Magnitude range", combo ({ "+/- 12 dB", "+/- 20 dB", "+/- 30 dB", "+/- 40 dB" }, rs, [this] (int i) { settings.magRange = ranges[juce::jlimit (0, 3, i)]; }));
                heading ("RTA");
                static const int bands[] = { 1, 3, 6, 12, 24, 48 };
                juce::StringArray bi;
                int bs = 1;
                for (int i = 0; i < 6; ++i) { bi.add ("1/" + juce::String (bands[i]) + " octave"); if (bands[i] == settings.rtaBands.load()) bs = i; }
                row ("Resolution", combo (bi, bs, [this] (int i) { settings.rtaBands = bands[juce::jlimit (0, 5, i)]; }));
                row ("Speed", combo ({ "Fast", "Medium", "Slow" }, settings.rtaSpeed.load(), [this] (int i) { settings.rtaSpeed = i; }));
                auto& refT = make<juce::ToggleButton>();
                refT.setButtonText ("Reference");
                refT.setToggleState (settings.rtaShowRef.load(), juce::dontSendNotification);
                refT.onClick = [this, &refT] { settings.rtaShowRef = refT.getToggleState(); };
                auto& peakT = make<juce::ToggleButton>();
                peakT.setButtonText ("Peak hold");
                peakT.setToggleState (settings.rtaPeakHold.load(), juce::dontSendNotification);
                peakT.onClick = [this, &peakT] { settings.rtaPeakHold = peakT.getToggleState(); settings.requestClearPeak = true; };
                row ("Overlays", refT, "The reference's spectrum, and the highest level each band reached", &peakT);
                break;
            }
            case PrefCat::Display:
            {
                heading ("Help");
                {
                    auto& b = make<juce::TextButton>();
                    b.setButtonText ("Show the tour");
                    b.onClick = [this] { if (onShowTour) onShowTour(); };
                    row ("Tour", b, "A few cards on how LiveAlign is laid out. Help for each step is on the ? next to the page title.");
                }
                heading ("Screen");
                row ("Graphs", combo ({ "1 graph", "2 graphs", "3 graphs" }, settings.paneCount.load() - 1,
                                      [this] (int i) { settings.paneCount = i + 1; if (onLayoutChanged) onLayoutChanged(); }),
                     "Live monitoring can show the RTA, transfer function and impulse response together");
                row ("Arrangement", combo ({ "Stacked", "Side by side" }, settings.paneLayout.load(), [this] (int i) { settings.paneLayout = i; if (onLayoutChanged) onLayoutChanged(); }));
                row ("Theme", combo ({ "Dark", "Light" }, settings.theme.load(), [this] (int i)
                {
                    settings.theme = i;
                    juce::Component::SafePointer<SettingsPage> safe (this);
                    juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onThemeChanged) safe->onThemeChanged(); });
                }));
                heading ("Impulse response");
                row ("Display", combo ({ "Linear", "Log (ETC)" }, settings.irMode.load(), [this] (int i) { settings.irMode = i; }));
                row ("Time range", combo ({ "20 ms", "100 ms", "500 ms" }, settings.irRange.load(), [this] (int i) { settings.irRange = i; }));
                heading ("Waterfall");
                row ("History", combo ({ "10 seconds", "30 seconds", "60 seconds" }, settings.wfSeconds.load(), [this] (int i) { settings.wfSeconds = i; }));
                row ("Colour range", combo ({ "40 dB", "60 dB", "80 dB" }, settings.wfRange.load(), [this] (int i) { settings.wfRange = i; }),
                     "From the loudest band down to the darkest colour");
                break;
            }
            case PrefCat::Units:
            {
                const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
                row ("Temperature unit", combo ({ deg + "C  Celsius", deg + "F  Fahrenheit" }, settings.temperatureUnit.load(),
                                                [this] (int i) { settings.temperatureUnit = i; rebuildLater(); }));
                auto& t = make<juce::Label>();
                t.setText ("On Venue, Details (each session keeps its own, with the humidity)", juce::dontSendNotification);
                t.setFont (font (12.0f));
                t.setColour (juce::Label::textColourId, pal().textDim);
                row ("Temperature", t, "The air temperature sets the speed of sound, used to turn delays into distances");
                row ("Distance unit", combo ({ "Feet", "Metres" }, settings.distanceUnit.load(), [this] (int i) { settings.distanceUnit = i; }));
                break;
            }
            case PrefCat::Sensor:
                climateRows();
                break;
            case PrefCat::Tuning:
                content.twoColumns = true;
                prefRows (PrefCat::Tuning, true);
                break;
            case PrefCat::Generator:
                prefRows (PrefCat::Generator, false);
                break;
            case PrefCat::Monitoring:
                prefRows (PrefCat::Monitoring, false);
                break;
            case PrefCat::Reports:
            {
                heading ("Reports");
                auto& author = make<juce::TextEditor>();
                author.setFont (font (13.0f));
                author.setText (UserPrefs::get ("reportAuthor"), juce::dontSendNotification);
                author.setTextToShowWhenEmpty ("Your name or company", pal().textFaint);
                author.onTextChange = [&author] { UserPrefs::set ("reportAuthor", author.getText().trim()); };
                row ("Prepared by", author, "Shown on the tuning report's cover");
                fileRow ("Logo", "reportLogo", false, "A picture (PNG or JPEG) at the top right of the reports' covers");
                for (const auto& d : prefs::all()) if (d.cat == PrefCat::Reports && juce::String (d.group) == "Reports") prefRow (d, false);
                heading ("Files");
                fileRow ("Venues and exports", "venueFolder", true, "Where Open, Save and the exports start (else Documents)");
                for (const auto& d : prefs::all()) if (d.cat == PrefCat::Reports && juce::String (d.group) == "Files") prefRow (d, false);
                break;
            }
            case PrefCat::Updates:
            default:
            {
                auto& check = make<juce::TextButton>();
                check.setButtonText (updates.info().state == UpdateInfo::Available ? "Download " + updates.info().latest + "..." : juce::String ("Check for updates"));
                check.onClick = [this]
                {
                    if (updates.info().state == UpdateInfo::Available) updates.openReleasePage();
                    else { updates.check(); refreshUpdate(); }
                };
                row ("Version " + juce::String (JucePlugin_VersionString), check);
                auto& autoT = make<juce::ToggleButton>();
                autoT.setButtonText ("Check when LiveAlign starts");
                autoT.setToggleState (updates.autoCheck(), juce::dontSendNotification);
                autoT.onClick = [this, &autoT] { updates.setAutoCheck (autoT.getToggleState()); };
                row ("Automatically", autoT, "Needs an internet connection");
                refreshUpdate();
                break;
            }
        }
        if (catButtons[cat] != nullptr) clearVenueButton.setVisible (cat == (int) PrefCat::Tuning && ! proc.venuePrefs.values.empty());
        layoutContent();
        content.repaint();
    }

    // A file or folder: a button that shows the choice, and Clear.
    void fileRow (const juce::String& label, const juce::String& key, bool folder, const juce::String& help)
    {
        const juce::File f (UserPrefs::get (key));
        auto& pick = make<juce::TextButton>();
        pick.setButtonText (f.exists() ? f.getFileName() : juce::String (folder ? "Choose a folder..." : "Choose a picture..."));
        pick.setTooltip (f.exists() ? f.getFullPathName() : help);
        pick.onClick = [this, key, folder]
        {
            chooser = std::make_unique<juce::FileChooser> (folder ? "Choose the folder for venues and exports" : "Choose a logo for the reports",
                                                           juce::File (UserPrefs::get (key)), folder ? juce::String() : juce::String ("*.png;*.jpg;*.jpeg"));
            juce::Component::SafePointer<SettingsPage> safe (this);
            chooser->launchAsync (folder ? juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories
                                         : juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [safe, key] (const juce::FileChooser& fc)
                                  {
                                      if (safe == nullptr || fc.getResult() == juce::File()) return;
                                      UserPrefs::set (key, fc.getResult().getFullPathName());
                                      safe->rebuildLater();
                                  });
        };
        auto& clear = make<juce::TextButton>();
        clear.setButtonText ("Clear");
        clear.setEnabled (f.exists());
        clear.onClick = [this, key] { UserPrefs::set (key, {}); rebuildLater(); };
        row (label, pick, help, &clear);
    }

    // Rebuilding deletes the control whose callback is running: do it once that has returned.
    void rebuildLater()
    {
        juce::Component::SafePointer<SettingsPage> safe (this);
        juce::MessageManager::callAsync ([safe] { if (safe != nullptr) { safe->build(); safe->resized(); } });
    }

    void layoutContent()
    {
        const int w = juce::jmax (Content::kLabelW + 2 * Content::kCtrlW + Content::kGap, view.getWidth() - view.getScrollBarThickness() - 4);
        const int h = content.layout();
        content.setSize (w, juce::jmax (h, view.getHeight() - 4));
    }

    void resetCategory()
    {
        auto& ap = AppPrefs::get();
        switch ((PrefCat) cat)
        {
            case PrefCat::Audio: settings.coherenceBlank = 0.4f; break;
            case PrefCat::Measurement:
                settings.fftSize = 32768; settings.averages = 8; settings.smoothing = 24; settings.magRange = 20.0f;
                settings.rtaBands = 3; settings.rtaSpeed = 1; settings.rtaShowRef = false; settings.rtaPeakHold = false;
                break;
            case PrefCat::Display:
                settings.paneCount = 1; settings.paneLayout = 0; settings.irMode = 0; settings.irRange = 1; settings.wfSeconds = 1; settings.wfRange = 1;
                if (settings.theme.load() != 0) { settings.theme = 0; if (onThemeChanged) onThemeChanged(); }
                if (onLayoutChanged) onLayoutChanged();
                break;
            case PrefCat::Units: settings.temperatureUnit = 1; settings.distanceUnit = 0; break;
            case PrefCat::Reports:
                UserPrefs::set ("reportLogo", {});
                UserPrefs::set ("venueFolder", {});
                [[fallthrough]];
            case PrefCat::Tuning:
            case PrefCat::Generator:
            case PrefCat::Monitoring:
                for (const auto& d : prefs::all()) if ((int) d.cat == cat) ap.reset (d.key);
                changedPrefs();
                break;
            case PrefCat::Sensor: break;   // the connection is yours: Reset leaves it alone
            case PrefCat::Updates:
            default: updates.setAutoCheck (true); break;
        }
        build();
    }

    void changedPrefs() { proc.applyAppPrefs(); if (onPrefsChanged) onPrefsChanged(); }
    void changedVenue() { if (onVenueChanged) onVenueChanged(); resized(); }

    void refreshUpdate()
    {
        if (cat != (int) PrefCat::Updates) return;
        const auto& u = updates.info();
        juce::String t;
        switch (u.state)
        {
            case UpdateInfo::Checking: t = "Checking..."; break;
            case UpdateInfo::UpToDate: t = "Up to date (latest is " + u.latest + ")."; break;
            case UpdateInfo::Available: t = "Version " + u.latest + " is available."; break;
            case UpdateInfo::Failed: t = u.error; break;
            case UpdateInfo::Unknown:
            default: t = "Not checked yet."; break;
        }
        if (t != content.status)
        {
            content.status = t;
            content.statusColour = u.state == UpdateInfo::Available ? pal().accent : u.state == UpdateInfo::Failed ? pal().warn : pal().textDim;
            layoutContent();
            content.repaint();
        }
    }

    void timerCallback() override
    {
        refreshUpdate();
        if (climateStatus != nullptr)
        {
            auto& f = proc.climate;
            juce::String t = f.status();
            const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
            const float tc = f.tempC.load(), rh = f.humidity.load();
            if (std::isfinite (tc) || std::isfinite (rh))
            {
                t << "  |  ";
                if (std::isfinite (tc)) t << (settings.temperatureUnit.load() == 1 ? juce::String (tc * 9.0f / 5.0f + 32.0f, 1) + deg + "F" : juce::String (tc, 1) + deg + "C");
                if (std::isfinite (tc) && std::isfinite (rh)) t << ", ";
                if (std::isfinite (rh)) t << juce::String (juce::roundToInt (rh)) << " %";
                t << " (" << f.ageText() << ")";
            }
            if (t != climateStatus->getText()) climateStatus->setText (t, juce::dontSendNotification);
            climateStatus->setColour (juce::Label::textColourId, f.live() ? pal().good : f.enabled() ? pal().warn : pal().textDim);
        }
    }

    // Live temperature and humidity over MQTT: the broker, the two topics, and how it is doing.
    void climateRows()
    {
        heading ("Connection");
        auto save = [this] (const char* key, const juce::String& v) { UserPrefs::set (key, v); proc.configureClimate(); };
        auto& on = make<juce::ToggleButton>();
        on.setButtonText ("Connect to a sensor");
        on.setToggleState (UserPrefs::get ("mqttOn", "0") == "1", juce::dontSendNotification);
        on.onClick = [this, &on, save] { save ("mqttOn", on.getToggleState() ? "1" : "0"); };
        row ("Room sensor", on, "A temperature and humidity sensor, through an MQTT broker (Home Assistant's Mosquitto, Zigbee2MQTT)");
        auto text = [this, save] (const char* key, const char* def, const juce::String& hint, bool secret = false) -> juce::TextEditor&
        {
            auto& e = make<juce::TextEditor>();
            e.setFont (font (13.0f));
            e.setText (UserPrefs::get (key, def), juce::dontSendNotification);
            e.setTextToShowWhenEmpty (hint, pal().textFaint);
            if (secret) e.setPasswordCharacter ((juce::juce_wchar) 0x2022);
            auto* ep = &e;
            const juce::String k (key);
            // saved when the field is left or Return is pressed, not on every key (each save reconnects)
            e.onReturnKey = [ep, k, save] { save (k.toRawUTF8(), ep->getText().trim()); };
            e.onFocusLost = [ep, k, save] { if (ep->getText().trim() != UserPrefs::get (k)) save (k.toRawUTF8(), ep->getText().trim()); };
            return e;
        };
        auto& host = text ("mqttHost", "", "homeassistant.local or 192.168.1.10");
        auto& port = text ("mqttPort", "1883", "1883");
        port.setInputRestrictions (5, "0123456789");
        row ("Broker", host, "The MQTT broker's name or address on this network (plain MQTT, no TLS)", &port);
        auto& user = text ("mqttUser", "", "User name (if the broker needs one)");
        auto& pass = text ("mqttPass", "", "Password", true);
        row ("Login", user, "Home Assistant: a Home Assistant user, or the Mosquitto add-on's login. Saved on this computer only, not in venue files", &pass);
        heading ("Topics");
        auto& tt = text ("mqttTempTopic", "", "zigbee2mqtt/FOH sensor");
        auto& tk = text ("mqttTempKey", "temperature", "JSON key (temperature)");
        row ("Temperature topic", tt, "The topic the sensor publishes on. A plain number, or JSON read by the key (a.b for nested)", &tk);
        const bool f = UserPrefs::get ("mqttTempUnit", settings.temperatureUnit.load() == 1 ? "F" : "C") == "F";
        const auto deg = juce::String (juce::CharPointer_UTF8 ("\xc2\xb0"));
        row ("Sensor's unit", combo ({ deg + "C  Celsius", deg + "F  Fahrenheit" }, f ? 1 : 0, [save] (int i) { save ("mqttTempUnit", i == 1 ? "F" : "C"); }),
             "The unit the sensor sends the temperature in");
        auto& ht = text ("mqttHumTopic", "", "Same topic, or its own");
        auto& hk = text ("mqttHumKey", "humidity", "JSON key (humidity)");
        row ("Humidity topic", ht, "Relative humidity in %", &hk);
        heading ("Status");
        auto& st = make<juce::Label>();
        st.setFont (font (12.0f));
        climateStatus = &st;
        auto& again = make<juce::TextButton>();
        again.setButtonText ("Reconnect");
        again.onClick = [this] { proc.climate.stop(); proc.configureClimate(); };
        row ("Status", st, "A reading older than ten minutes is shown as stale and is not captured", &again);
        timerCallback();
    }
    juce::Label* climateStatus = nullptr;

    // Listening on headphones: what to hear and where. The level and mute are on the main screen (bottom of the menu).
    void listenRows()
    {
        heading ("Listening (headphones)");
        juce::StringArray srcs { "Off", "The reference" };
        const int mics = juce::jmax (1, settings.micCount.load());
        for (int m = 0; m < mics; ++m) srcs.add (proc.mics.nameOf (m));
        const int nOut = juce::jmax (0, proc.numOutputs());
        listenTargets.clear();
        juce::StringArray outs;
        for (int c = 0; c < nOut; ++c)
        {
            juce::String feeds;
            for (int i = 0; i < (int) proc.system.sources.size(); ++i)
                if (proc.system.sources[(size_t) i].output == c + 1) feeds = proc.system.nameOf (i);
            outs.add ("This interface: output " + juce::String (c + 1) + (feeds.isNotEmpty() ? "  (feeds " + feeds + "!)" : juce::String()));
            listenTargets.add ("#" + juce::String (c));
        }
        for (const auto& d : proc.listenDevice.deviceNames())
        {
            outs.add (d);
            listenTargets.add (d);
        }
        int sel = -1;
        const int ch = settings.listenChannel.load();
        if (ch >= 0) sel = listenTargets.indexOf ("#" + juce::String (ch));
        else sel = listenTargets.indexOf (proc.listenDeviceName);
        auto apply = [this] (int source, int target)
        {
            const auto t = target >= 0 && target < listenTargets.size() ? listenTargets[target] : juce::String();
            if (t.startsWith ("#")) proc.setListening (source, t.substring (1).getIntValue(), {});
            else proc.setListening (source, -1, t);
            rebuildLater();
        };
        auto& out = combo (outs, sel, [this, apply] (int i) { apply (settings.listenSource.load(), i); });
        if (sel < 0) out.setTextWhenNothingSelected (outs.isEmpty() ? "No outputs found" : "Pick where to listen");
        auto* outPtr = &out;
        row ("Listen to", combo (srcs, juce::jlimit (0, srcs.size() - 1, settings.listenSource.load()),
                                 [apply, outPtr] (int i) { apply (i, outPtr->getSelectedItemIndex()); }),
             "Hear the reference (what the console sends the PA) or a mic, on headphones");
        row ("Play on", out, "A spare output of the interface (never one that feeds the PA), or another device such as this computer's headphone jack");
        auto& st = make<juce::Label>();
        st.setFont (font (12.0f));
        juce::String t;
        if (settings.listenSource.load() == 0) t = "Off";
        else if (proc.listenError.isNotEmpty()) t = proc.listenError;
        else if (ch >= 0) t = "On output " + juce::String (ch + 1) + " of the interface";
        else if (proc.listenDevice.isOpen()) t = "On " + proc.listenDevice.description();
        else t = "Pick where to listen";
        t << ".  The level and mute are on the main screen, at the bottom of the menu.";
        st.setText (t, juce::dontSendNotification);
        st.setColour (juce::Label::textColourId, proc.listenError.isNotEmpty() ? pal().warn : pal().textDim);
        row ("Status", st);
    }
    juce::StringArray listenTargets;

    LiveAlignProcessor& proc;
    Settings& settings;
    UpdateChecker& updates;
    juce::OwnedArray<juce::TextButton> catButtons;
    juce::TextButton resetButton { "Reset to defaults" }, clearVenueButton { "Clear this venue's own values" };
    juce::Viewport view;
    Content content;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::Rectangle<int> menuCard, bodyCard, blurbArea;
    int cat = 0;
};
} // namespace la::ui
