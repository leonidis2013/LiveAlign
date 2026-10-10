#include "PluginProcessor.h"
#include "PluginEditor.h"

LiveAlignProcessor::LiveAlignProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

LiveAlignProcessor::~LiveAlignProcessor() = default;

bool LiveAlignProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const int in = layouts.getMainInputChannels();
    const int out = layouts.getMainOutputChannels();
    return in >= 1 && in <= kMaxChannels && out >= 1 && out <= kMaxChannels;
}

void LiveAlignProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    gen.prepare (sampleRate);
    const size_t block = (size_t) juce::jmax (512, samplesPerBlock);
    genBuf.assign (block, 0.0f);
    zeros.assign (block, 0.0f);
    rmsCoeff = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));
    micMs = refMs = 0.0;
    analyzer.setSampleRate (sampleRate);
}

void LiveAlignProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();
    const bool standalone = isStandalone();

    gen.setType ((la::GenType) settings.genType.load());
    gen.setLevelDb (settings.genLevelDb.load());
    gen.setFrequency (settings.genFreq.load());
    gen.setSweepSeconds (settings.genSweepSec.load());
    gen.setOn (settings.genOn.load());

    const int micCh = settings.micChannel.load();
    const int refSrc = settings.refSource.load();
    const int genOut = settings.genOutput.load();
    const int chunkMax = (int) genBuf.size();

    float mPeak = 0.0f, rPeak = 0.0f;

    for (int start = 0; start < total; start += chunkMax)
    {
        const int n = juce::jmin (chunkMax, total - start);

        const bool genActive = ! gen.isSilent();
        if (genActive) gen.process (genBuf.data(), n);
        else std::fill (genBuf.begin(), genBuf.begin() + n, 0.0f);

        const float* mic = micCh < numIn ? buffer.getReadPointer (micCh, start) : zeros.data();
        const float* ref = zeros.data();
        if (refSrc == 1) ref = genBuf.data();
        else if (refSrc >= 2 && refSrc - 2 < numIn) ref = buffer.getReadPointer (refSrc - 2, start);

        analyzer.push (mic, ref, n);

        for (int i = 0; i < n; ++i)
        {
            const float m = mic[i], r = ref[i];
            mPeak = juce::jmax (mPeak, std::abs (m));
            rPeak = juce::jmax (rPeak, std::abs (r));
            micMs += rmsCoeff * ((double) m * m - micMs);
            refMs += rmsCoeff * ((double) r * r - refMs);
        }

        // Outputs. The app never passes its inputs through (that would feed the mic back into the PA);
        // the plug-in passes audio through untouched unless the generator is driving a channel.
        for (int ch = 0; ch < numOut; ++ch)
        {
            float* out = buffer.getWritePointer (ch, start);
            const bool genHere = genActive && (genOut == 0 || genOut - 1 == ch);
            if (genHere) std::copy (genBuf.begin(), genBuf.begin() + n, out);
            else if (standalone || ch >= numIn) std::fill (out, out + n, 0.0f);
        }
    }

    // Channels beyond the outputs (in-place buffer) are not heard; nothing else to clear.
    if (mPeak > micPeak.load()) micPeak = mPeak;
    if (rPeak > refPeak.load()) refPeak = rPeak;
    micRms = (float) la::powerToDbfs (micMs);
    refRms = (float) la::powerToDbfs (refMs);
}

juce::String LiveAlignProcessor::loadCalibrationFile (const juce::File& f)
{
    if (! f.existsAsFile()) return "File not found: " + f.getFullPathName();
    auto t = la::parseFreqTable (f.loadFileAsString().toStdString());
    if (t.freq.size() < 3) return "No calibration data found in " + f.getFileName();
    analyzer.setCalibration (t, f.getFileName());
    calPath = f.getFullPathName();
    return {};
}

void LiveAlignProcessor::clearCalibration()
{
    analyzer.clearCalibration();
    calPath.clear();
}

void LiveAlignProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement x ("LiveAlign");
    x.setAttribute ("version", JucePlugin_VersionString);
    settings.save (x);
    x.setAttribute ("calFile", calPath);
    copyXmlToBinary (x, dest);
}

void LiveAlignProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        if (! x->hasTagName ("LiveAlign")) return;
        settings.load (*x);
        const juce::String path = x->getStringAttribute ("calFile");
        if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadCalibrationFile (juce::File (path));
    }
}

juce::AudioProcessorEditor* LiveAlignProcessor::createEditor()
{
    return new LiveAlignEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LiveAlignProcessor();
}
