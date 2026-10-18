// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <atomic>
#include <memory>
#include "PluginProcessor.h"
#include "UI/LALookAndFeel.h"
#include "UI/NavRail.h"
#include "UI/HomePage.h"
#include "UI/MicSetupPage.h"
#include "UI/SpeakersPage.h"
#include "UI/PlanPage.h"
#include "UI/VenuePage.h"
#include "UI/ListenStrip.h"
#include "UI/CoveragePage.h"
#include "UI/HeadroomPage.h"
#include "UI/ListeningPage.h"
#include "UI/Help.h"
#include "UI/RoomPage.h"
#include "UI/GuidedMeasure.h"
#include "UI/FeedbackPage.h"
#include "UI/AcousticsPage.h"
#include "UI/LevelPage.h"
#include "UI/SheetPage.h"
#include "UI/RecheckGuide.h"
#include "UI/MonitorMicPage.h"
#include "UI/ReportPage.h"
#include "UI/MicLockerPanel.h"
#include "UI/SettingsPanel.h"
#include "UI/Pages.h"
#include "UI/MeasurePane.h"
#include "UI/SidePanels.h"
#include "UI/GeneratorStrip.h"

class LiveAlignEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit LiveAlignEditor (LiveAlignProcessor&);
    ~LiveAlignEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override { grabKeyboardFocus(); }

    void showPage (int page);
    void setPaneViews (int a, int b, int c);   // used by the local screenshot tool
    la::ui::SpeakersPage& speakersPage() { return *speakerPage; }
    la::ui::PlanPage& planPageForPreview() { return *planPage; }
    la::ui::RoomPage& roomPageForPreview() { return *roomPage; }
    la::ui::FeedbackPage& feedbackPageForPreview() { return *feedbackPage; }
    la::ui::AcousticsPage& acousticsPageForPreview() { return *acousticsPage; }
    la::ui::LevelPage& levelPageForPreview() { return *levelPage; }
    la::ui::SheetPage& sheetPageForPreview() { return *sheetPage; }
    la::ui::MicSetupPage& micPageForPreview() { return *micPage; }
    la::ui::MicLockerPage& lockerPageForPreview() { return *lockerPage; }
    la::ui::SettingsPage& settingsPageForPreview() { return *settingsPage; }
    la::ui::VenueDetailsPage& venuePageForPreview() { return *venuePage; }
    void applyThemeForPreview() { applyTheme (settings.theme.load()); }
    la::ui::RecheckGuide& recheckForPreview() { return *recheck; }
    void exportReportTo (bool monitoring, const juce::File& f);   // without the file chooser (local screenshot tool)
    void updateStatuses();

private:
    void timerCallback() override;
    void applyTheme (int theme);
    void layoutPanes();
    void refreshMicPicker();

    void openSettings();
    void applyPrefs();
    int prefsSerial = -1, lastPage = 0;
    void refreshUpdate();
    void showUpdate();
    void openMicLocker();
    void loadLockerCal (const juce::String& id, int angle);
    void exportPlan();
    void exportReport (bool monitoring = false);
    juce::Image planImage (int layer);
    void openCalibration (int mic, juce::Component* anchor);
    void loadCalFile (int mic, int angle = -1);
    void capture();
    void importTrace();
    void importSpeaker (int source);
    void exportTrace (int index);
    void renameTrace (int index);
    void deleteTrace (int index);
    void toggleGenerator();
    void toggleFullScreen();
    void showMessage (const juce::String& title, const juce::String& text);
    void showToast (const juce::String& text);   // a short note at the bottom that fades on its own
    juce::String toastText;
    juce::uint32 toastUntil = 0;
    juce::TextButton undoButton, redoButton, helpButton { "?" };
    std::unique_ptr<la::ui::WhatIsThisOverlay> whatIsThis;
    std::unique_ptr<la::ui::TourOverlay> tour;
    void showHelpMenu();
    void showHowTo();
    void startWhatIsThis();
    void helpAt (juce::Point<int> pt);
    void showGlossary();
public:
    void showTour (int step = 0);
    void closeTour();
    bool tourShowing() const { return tour != nullptr; }
private:
    void askText (const juce::String& title, const juce::String& message, const juce::String& initial,
                  std::function<void (const juce::String&)> done);

    // venue and sessions
    void saveProject (bool askForFile, std::function<void()> then = {});
    void openProject();
    void newProject();
    void newSession();
    void afterProjectLoaded();
    void confirmDiscard (std::function<void()> then);

    LiveAlignProcessor& processor;
    la::Settings& settings;
    la::ui::LALookAndFeel laf;
    const bool standalone;

    la::Snapshot snap;
    juce::uint64 snapSerial = 0;

    la::ui::NavRail nav;
    std::unique_ptr<la::ui::HomePage> homePage;
    std::unique_ptr<la::ui::MicSetupPage> micPage;
    std::unique_ptr<la::ui::SpeakersPage> speakerPage;
    std::unique_ptr<la::ui::RoomPage> roomPage;
    std::unique_ptr<la::ui::PlanPage> planPage;
    std::unique_ptr<la::ui::VenueDetailsPage> venuePage;
    std::unique_ptr<la::ui::CoveragePage> coveragePage;
    std::unique_ptr<la::ui::HeadroomPage> headroomPage;
    std::unique_ptr<la::ui::ListeningPage> listeningPage;

    // undo / redo and the change history (see trackChanges)
    std::vector<std::unique_ptr<juce::XmlElement>> undoStack, redoStack;
    std::unique_ptr<juce::XmlElement> committedState, committedProc;
    juce::String committedFp, pendingFp;
    // When the user last clicked, dragged or scrolled anywhere in the app: the undo fingerprint (the venue's editable
    // state, serialised) is taken three times a second only around input, every two seconds otherwise.
    struct InputWatch : juce::MouseListener
    {
        std::atomic<juce::uint32> last { 0 };
        void touch() { last = juce::Time::getMillisecondCounter(); }
        void mouseDown (const juce::MouseEvent&) override { touch(); }
        void mouseUp (const juce::MouseEvent&) override { touch(); }
        void mouseDrag (const juce::MouseEvent&) override { touch(); }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override { touch(); }
        bool recent() const { return juce::Time::getMillisecondCounter() - last.load() < 3000; }
    } inputWatch;
    int pendingTicks = 0, pendingPage = -1;
    void resetUndo();
    void trackChanges();
    void logChanges (const juce::XmlElement& procBefore, const juce::String& why);
    void restoreState (const juce::XmlElement& state, const juce::String& why);
    void refreshAllPages();
public:
    void undo();
    void redo();
    bool canUndo() const { return ! undoStack.empty(); }
    void undoTickForPreview() { trackChanges(); }
    bool pageInSession (int page) const;
    void afterProjectLoadedForPreview() { afterProjectLoaded(); }
    bool listenStripShownForPreview() const { return listenStrip != nullptr && listenStrip->isVisible() && listenStrip->getParentComponent() != nullptr && listenStrip->listening(); }
    void applySessionKind();
    void startSession (const juce::String& name, int kind);
    bool unsavedForPreview() const { return unsaved; }
    void startVenue (const juce::String& name, int kind);   // a new venue with its first session of this kind   // a new session of this kind (0 tuning, 1 monitoring), opened
    bool canRedo() const { return ! redoStack.empty(); }
    void rollBackTo (int historyEntry);
    la::ui::CoveragePage& coveragePageForPreview() { return *coveragePage; }
    la::ui::HeadroomPage& headroomPageForPreview() { return *headroomPage; }
    la::ui::ListeningPage& listeningPageForPreview() { return *listeningPage; }
private:
    std::unique_ptr<la::ui::MicLockerPage> lockerPage;
    std::unique_ptr<la::ui::SettingsPage> settingsPage;
    std::unique_ptr<la::ui::FeedbackPage> feedbackPage;
    std::unique_ptr<la::ui::AcousticsPage> acousticsPage;
    std::unique_ptr<la::ui::LevelPage> levelPage;
    std::unique_ptr<la::ui::SheetPage> sheetPage;
    std::unique_ptr<la::ui::RecheckGuide> recheck;
    void exportSheet (bool csv);
    std::unique_ptr<la::ui::MonitorMicPage> monitorMicPage;
    std::unique_ptr<la::ui::ReportPage> tuningReportPage, monitorReportPage;
    std::unique_ptr<la::ui::GuidedOverlay> guidedOverlay;   // the guided measuring pop-up, while it is open
public:
    void openGuided (la::ui::GuidedSource& source);
    static std::vector<int> anglesFor (const juce::Array<juce::File>& files, int angle, const la::MicCalSet* existing = nullptr, std::vector<bool>* guessed = nullptr);
    la::ui::GuidedOverlay* guided() { return guidedOverlay.get(); }   // for the preview
private:
    static constexpr int kMonitorMic = -2;   // the monitoring mic, for the calibration pop-ups
    double lastLogSecond = 0.0;

    // Monitoring page
    std::unique_ptr<la::ui::MeasurePane> panes[3];
    la::ui::SplPanel splPanel;
    la::ui::InputPanel inputPanel;
    la::ui::DelayPanel delayPanel;
    la::ui::TracesPanel tracesPanel;
    la::ui::EventPanel eventPanel;
    la::ui::FeedbackWatchPanel fbWatch;
    la::ui::TunedPanel tunedPanel;
    la::ui::ConditionsPanel condPanel;   // the room sensor's reading and its effect (shown when a sensor is set up)
    std::unique_ptr<la::ui::ListenStrip> listenStrip;   // the listening level, above Settings on every page
    void updateConditionsPanel();
    void updateTunedPanel();
    la::fb::Detector liveFb { { 80.0, 12000.0, 15.0, 18, 3, 1.0 / 12.0 } };   // stricter than the feedback test: music has peaks too
    juce::uint64 liveFbFrame = 0;
    juce::uint32 lastTrackRequest = 0;
    juce::ComboBox micPicker;

    la::ui::GeneratorStrip generator;

    juce::TextButton runButton, resetButton { "RESET AVG" };
    void refreshNext();                     // the next step: marked in the navigation and on Home
    void offerRecovery();
    juce::TooltipWindow tooltip { this, 600 };

    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> textWindow;

    int appliedTheme = -1, tick = 0, laidOutCount = -1, laidOutLayout = -1, shownPage = -1, pickerMics = -1;
    bool unsaved = false, monitorMicChosen = false;
    juce::uint32 overloadUntil = 0;
    juce::Rectangle<int> paneArea, pageArea, titleArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAlignEditor)
};
