#pragma once
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Settings.h"
#include "Analyzer.h"
#include "TraceStore.h"
#include "Checklist.h"
#include "Analysis/Generator.h"

// LiveAlign runs as a standalone app (system tuning: drives the PA with the generator and measures through the mic)
// and as a VST3 (live monitoring in a host such as LiveProfessor). The audio thread only reads inputs, runs the
// generator and hands samples to the analysis thread.
class LiveAlignProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kMaxChannels = 8;

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
    bool uiAsStandalone = false;   // local screenshot tool only: show every tab
    int numInputs() const { return getTotalNumInputChannels(); }
    int numOutputs() const { return getTotalNumOutputChannels(); }

    // Loads a microphone calibration file. Returns an error message, or empty on success.
    juce::String loadCalibrationFile (const juce::File&);
    void clearCalibration();

    // Input meters: peak since the last read, and a smoothed RMS (dBFS)
    float takeMicPeak() { return micPeak.exchange (0.0f); }
    float takeRefPeak() { return refPeak.exchange (0.0f); }
    float micRmsDb() const { return micRms.load(); }
    float refRmsDb() const { return refRms.load(); }

    // ---- venue project (message thread) ----
    // A project holds the venue details, the settings, the mic calibration, the traces and the checklist.
    std::unique_ptr<juce::XmlElement> createProjectXml() const;
    void loadProjectXml (const juce::XmlElement&);
    juce::String saveProject (const juce::File&);   // returns an error message, or empty
    juce::String openProject (const juce::File&);
    void newProject();
    static juce::String projectExtension() { return ".lavenue"; }
    // True when the project differs from the last save / open.
    bool projectChanged() const;

    juce::String venueName { "Untitled venue" }, venueNotes;
    juce::File projectFile;
    la::MicChecklist micChecklist;

    la::Settings settings;
    la::Analyzer analyzer { settings };
    la::TraceStore traces;

private:
    la::Generator gen;
    std::vector<float> genBuf, zeros;
    double rmsCoeff = 0.0, micMs = 0.0, refMs = 0.0;
    std::atomic<float> micPeak { 0.0f }, refPeak { 0.0f }, micRms { -120.0f }, refRms { -120.0f };
    juce::String savedProjectText;
    void saveCalibration (juce::XmlElement&) const;
    void loadCalibration (const juce::XmlElement&);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveAlignProcessor)
};
