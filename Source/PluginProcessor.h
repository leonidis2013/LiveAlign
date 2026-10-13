// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
#include <array>
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Settings.h"
#include "Analyzer.h"
#include "TraceStore.h"
#include "Checklist.h"
#include "SpeakerSystem.h"
#include "MicLocker.h"
#include "MicCalibration.h"
#include "VenuePlan.h"
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
    void setMicAim (int mic, int aim);
    void swapCalibrationAngle (int mic);        // a single file that was taken for the wrong angle          // 0 = at the speaker, 1 = pointing up
    // A page that needs a particular aim while it measures (Speakers: at the speaker). -1 = each mic's own aim.
    void setAimOverride (int aim);
    int aimOverride() const { return aimOverrideValue; }
    int effectiveAim (int mic) const;
    int activeCalAngle (int mic) const { return calibrations (mic).pick (effectiveAim (mic)); }   // -1: no file
    void addMic();
    void removeMic (int mic);

    // ---- mic locker (message thread) ----
    juce::String saveMicToLocker (int mic, const juce::String& name);   // returns the locker id
    void assignLockerMic (int mic, const juce::String& lockerId);
    void unlinkLockerMic (int mic) { mics.lockerId[(size_t) juce::jlimit (0, la::kMaxMics - 1, mic)].clear(); }
    bool syncLocker();          // linked mics' calibrations changed: update the locker (true when it was saved)
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
    static juce::String projectExtension() { return ".lavenue"; }
    bool projectChanged() const;   // differs from the last save / open

    void newSession (const juce::String& name);
    void switchSession (int index);
    void deleteSession (int index);
    void renameSession (int index, const juce::String& name);
    void compareWithSession (int index);   // shows that session's traces beside the open one's
    int currentSession() const { return current; }
    const std::vector<la::SessionInfo>& getSessions() const { return sessions; }

    // Interface and wiring as text, so a change since the last checks can be spotted.
    juce::String setupFingerprint() const;
    static juce::String today() { return juce::Time::getCurrentTime().formatted ("%d %b %Y"); }

    juce::String venueName { "Untitled venue" }, venueNotes;
    juce::File projectFile;
    la::VenueMics mics;
    juce::StringArray positions;
    la::SpeakerStatus speakers;
    la::SpeakerSystem system;
    la::VenuePlan plan;                 // sketch of the setup (venue)           // sources, responses and crossover (venue)
    la::SessionChecks checks;           // the open session's checks

    la::Settings settings;
    la::Analyzer analyzer { settings };
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

    std::vector<la::SessionInfo> sessions;
    int current = 0;
    juce::String savedProjectText;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAlignProcessor)
};
