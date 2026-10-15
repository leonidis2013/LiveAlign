// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <array>
#include <map>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Settings.h"
#include "Analyzer.h"
#include "Recorder.h"
#include "TraceStore.h"
#include "Checklist.h"
#include "SpeakerSystem.h"
#include "MicLocker.h"
#include "MicCalibration.h"
#include "VenuePlan.h"
#include "RoomTuning.h"
#include "Monitoring.h"
#include "TuningExtras.h"
#include "LevelBalance.h"
#include "UpdateChecker.h"
#include "Analysis/Generator.h"

// LiveAlign runs as a standalone app (system setup and tuning: drives the PA with the generator and measures through
// up to eight mics) and as a VST3 (monitoring in a host such as LiveProfessor). The audio thread only reads inputs,
// runs the generator and hands samples to the analysis thread.
class LiveAlignProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxInputs = 16, kMaxOutputs = 8;

    LiveAlignProcessor();
    ~LiveAlignProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    bool isStandalone() const { return wrapperType == wrapperType_Standalone; }
    bool uiAsStandalone = false;   // local screenshot tool only: show every page
    int numInputs() const { return getTotalNumInputChannels(); }
    int numOutputs() const { return getTotalNumOutputChannels(); }

    // ---- mics (message thread) ----
    // Each mic can have a 0 degree (at the speaker) and a 90 degree (pointing up) file; angle -1 = from the file name.
    juce::String loadCalibrationFile (int mic, const juce::File&, int angle = -1);   // returns an error message, or empty
    void clearCalibration (int mic, int angle = -1);                                  // -1: both
    const la::MicCalSet& calibrations (int mic) const { return cals[(size_t) juce::jlimit (0, la::kMaxMics - 1, mic)]; }
    // The aim (and so the calibration file) is set by what is measured: close to a speaker the mic points at it
    // (speakerAim, 0 degrees); in the room it points up (roomAim, 90 degrees when that file is loaded, else the nearest
    // angle loaded). Each can be changed in the guided pop-up, to the angles the mic has files for.
    void setMicAim (int mic, int aim);          // 0 = at the speaker, 1 = pointing up
    void swapCalibrationAngle (int mic);        // a single file that was taken for the wrong angle
    void setMicPosition (int mic, const juce::String& position);
    int positionAimOf (const juce::String& position) const;   // -1 when not set
    void setPositionAim (const juce::String& position, int aim);
    std::map<juce::String, int> positionAim;  // venue: aim at each named position
    int roomAim = la::Cal90, speakerAim = la::Cal0;   // venue
    // A page that needs a particular aim while it measures (Speakers: at the speaker). -1 = the room aim.
    void setAimOverride (int aim);
    int aimOverride() const { return aimOverrideValue; }
    int effectiveAim (int mic) const;
    int ownAim (int mic) const;                 // the aim of the mic's position (or its own), ignoring any page override
    int activeCalAngle (int mic) const { return calibrations (mic).pick (effectiveAim (mic)); }   // -1: no file
    void addMic();
    void removeMic (int mic);

    // ---- mic locker (message thread) ----
    juce::String saveMicToLocker (int mic, const juce::String& name);   // returns the locker id
    void assignLockerMic (int mic, const juce::String& lockerId);
    void unlinkLockerMic (int mic) { mics.lockerId[(size_t) juce::jlimit (0, la::kMaxMics - 1, mic)].clear(); }
    bool syncLocker();          // linked mics' calibrations changed: update the locker (true when it was saved)
    void lockerEntryChanged (const juce::String& lockerId);   // edited on the Mic locker page: update the rows using it
    juce::String micModelSerial (int mic) const;              // from the locker, for tags and reports
    la::MicLocker locker;
    la::UpdateChecker updates;

    // Input meters: peak since the last read, and a smoothed RMS (dBFS)
    float takeMicPeak (int mic) { return micPeak[(size_t) juce::jlimit (0, la::kMaxMics - 1, mic)].exchange (0.0f); }
    float micRmsDb (int mic) const { return micRms[(size_t) juce::jlimit (0, la::kMaxMics - 1, mic)].load(); }
    float takeRefPeak() { return refPeak.exchange (0.0f); }
    float refRmsDb() const { return refRms.load(); }

    // ---- venue and sessions (message thread) ----
    std::unique_ptr<juce::XmlElement> createProjectXml() const;
    void loadProjectXml (const juce::XmlElement&);
    juce::String saveProject (const juce::File&);
    juce::String openProject (const juce::File&);
    void newProject();
    // A new venue that starts from this one: same mics, speakers (names, models, outputs, crossover filters), positions and
    // plan; nothing measured, no delays, EQ or alignments, one new session.
    void newVenueFrom (const juce::String& name);
    static juce::String projectExtension() { return ".lavenue"; }
    bool projectChanged() const;   // differs from the last save / open

    // ---- autosave and crash recovery (standalone) ----
    // Every few minutes the open venue, if changed, is copied to a recovery file (the venue file itself is only written
    // by Save). A flag file marks LiveAlign as running; finding it at the next start means it did not close cleanly.
    static juce::File recoveryFile();
    static juce::File runningFlag();
    void autosave();
    juce::String restoreRecovery();   // returns an error message, or empty
    void clearRecovery();

    void newSession (const juce::String& name);
    void switchSession (int index);
    void deleteSession (int index);
    void renameSession (int index, const juce::String& name);
    void compareWithSession (int index);   // shows that session's traces beside the open one's
    int currentSession() const { return current; }
    const std::vector<la::SessionInfo>& getSessions() const { return sessions; }

    // Interface and wiring as text, so a change since the last checks can be spotted.
    juce::String setupFingerprint() const;
    // What differs from the setup this session's checks were done with, in words ("the interface: was X, now Y").
    juce::StringArray setupChanges() const;
    bool interfaceChanged() const;   // the device or sample rate, not just a mic's input
    static juce::String today() { return juce::Time::getCurrentTime().formatted ("%d %b %Y"); }

    juce::String venueName { "Untitled venue" }, venueNotes;
    juce::File projectFile;
    la::VenueMics mics;
    juce::StringArray positions;
    // Positions are one list, kept the same everywhere: every position has its marker on the venue plan (placed for
    // you when it has none) and every marker on the plan is a position. Rename and remove go through here.
    void syncPositionsWithPlan();
    void renamePosition (const juce::String& from, const juce::String& to);
    void removePosition (const juce::String& name);
    la::SpeakerStatus speakers;
    la::SpeakerSystem system;
    la::VenuePlan plan;                 // sketch of the setup (venue)           // sources, responses and crossover (venue)
    la::SessionChecks checks;           // the open session's checks
    la::RoomTuning room;                // the open session's room tuning
    la::FeedbackTest feedback;          // the open session's feedback test
    la::AsFound asFound;                // the open session's processor settings before anything was changed
    la::LevelBalance level;             // the open session's level balance
    la::Recheck recheck;                // the open session's quick re-check
    la::SplLog splLog;                  // the open session's sound level log (monitoring)
    la::MonitorMic monitorMic;          // the monitoring mic (venue)

    // ---- monitoring mode ----
    // Monitoring uses its own mic: while it is on, analysis slot 1 is the monitor mic (its input, calibration,
    // sound level and delay) and the tuning mics are put aside, then restored. The VST3 is always in this mode.
    void setMonitorMode (bool on);
    bool isMonitorMode() const { return monitorModeOn; }
    int tuningMicCount() const { return monitorModeOn ? tuningSlot.count : settings.micCount.load(); }
    void applyMonitorMic();            // after the monitor mic was edited (while in monitoring mode)
    void takeMonitorMicFromTuning (int mic = -1);   // copy a tuning mic (default: the one at FOH, else mic 1)
    juce::String loadMonitorCalibration (const juce::File&, int angle = -1);
    void assignMonitorFromLocker (const juce::String& lockerId);
    void syncMonitorFromSlot();        // the monitor mic's delay and sound level, as changed while monitoring
    // Remember the system as found, the first time something is about to change it in this session.
    void noteAsFound() { if (! asFound.taken()) asFound.take (system, today() + juce::Time::getCurrentTime().formatted (" %H:%M")); }

    la::Settings settings;
    la::Analyzer analyzer { settings };
    la::Recorder recorder;              // a plain recording of the mics and the reference (sweeps for room acoustics)
    la::TraceStore traces;              // the open session's traces

private:
    static BusesProperties defaultBuses();
    std::unique_ptr<juce::XmlElement> sessionBody() const;
    void loadSessionBody (const juce::XmlElement*);
    void saveMicCalibrations (juce::XmlElement&) const;
    void loadMicCalibrations (const juce::XmlElement&);
    void resetVenue();

    la::Generator gen;
    std::vector<float> genBuf, zeros;
    double rmsCoeff = 0.0;
    std::array<double, la::kMaxMics> micMs {};
    double refMs = 0.0;
    std::array<std::atomic<float>, la::kMaxMics> micPeak {}, micRms {};
    std::atomic<float> refPeak { 0.0f }, refRms { -120.0f };

    void applyCalibration (int mic);
    std::array<la::MicCalSet, la::kMaxMics> cals;
    int aimOverrideValue = -1;

    bool monitorModeOn = false;
    struct TuningSlot { int count = 1, selected = 0, input = 0; float delay = 0.0f, offset = 120.0f; bool calibrated = false; } tuningSlot;

    std::vector<la::SessionInfo> sessions;
    int current = 0;
    juce::String savedProjectText;
    int lastAutosaveHash = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAlignProcessor)
};
