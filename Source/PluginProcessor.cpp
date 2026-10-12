#include "PluginProcessor.h"
#include "PluginEditor.h"

LiveAlignProcessor::LiveAlignProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    traces.importLegacyFolder();
    savedProjectText = createProjectXml()->toString();
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

        analyzer.push (mic, ref, genBuf.data(), n);

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
    return {};
}

void LiveAlignProcessor::clearCalibration()
{
    analyzer.clearCalibration();
}

// The calibration table itself is stored (not just its path), so a project still works on another computer.
void LiveAlignProcessor::saveCalibration (juce::XmlElement& parent) const
{
    const auto cal = analyzer.getCalibration();
    if (cal.empty()) return;
    auto* e = parent.createNewChildElement ("MicCalibration");
    e->setAttribute ("name", analyzer.getCalibrationName());
    if (cal.hasSensFactor) e->setAttribute ("sensFactorDb", cal.sensFactorDb);
    la::FreqTable copy = cal;
    copy.meta.clear();
    e->addTextElement (juce::String (la::writeFrd (copy, {}, false)));
}

void LiveAlignProcessor::loadCalibration (const juce::XmlElement& parent)
{
    if (const auto* e = parent.getChildByName ("MicCalibration"))
    {
        auto t = la::parseFreqTable (e->getAllSubText().toStdString());
        if (e->hasAttribute ("sensFactorDb")) { t.hasSensFactor = true; t.sensFactorDb = e->getDoubleAttribute ("sensFactorDb"); }
        if (t.freq.size() >= 3) { analyzer.setCalibration (t, e->getStringAttribute ("name")); return; }
    }
    // State from 0.1 / 0.2 kept only the file path.
    const juce::String path = parent.getStringAttribute ("calFile");
    if (path.isNotEmpty() && juce::File (path).existsAsFile()) loadCalibrationFile (juce::File (path));
    else analyzer.clearCalibration();
}

std::unique_ptr<juce::XmlElement> LiveAlignProcessor::createProjectXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("LiveAlignProject");
    x->setAttribute ("format", 1);
    x->setAttribute ("appVersion", JucePlugin_VersionString);
    auto* venue = x->createNewChildElement ("Venue");
    venue->setAttribute ("name", venueName);
    venue->setAttribute ("notes", venueNotes);
    auto* st = x->createNewChildElement ("Settings");
    settings.save (*st);
    st->removeAttribute ("theme");   // a preference of this computer, not of the venue
    st->removeAttribute ("currentTab");
    saveCalibration (*x);
    traces.save (*x);
    micChecklist.save (*x);
    return x;
}

void LiveAlignProcessor::loadProjectXml (const juce::XmlElement& x)
{
    if (const auto* venue = x.getChildByName ("Venue"))
    {
        venueName = venue->getStringAttribute ("name", "Untitled venue");
        venueNotes = venue->getStringAttribute ("notes");
    }
    if (const auto* st = x.getChildByName ("Settings"))
    {
        const int theme = settings.theme.load(), tab = settings.currentTab.load();
        settings.load (*st);
        if (! st->hasAttribute ("theme")) settings.theme = theme;
        settings.currentTab = tab;
    }
    settings.genOn = false;
    loadCalibration (x);
    if (! traces.load (x)) traces.clear();
    micChecklist.load (x);
    settings.requestResetAverages = true;
    settings.requestResetLeq = true;
}

juce::String LiveAlignProcessor::saveProject (const juce::File& f)
{
    const auto file = f.withFileExtension (projectExtension());
    auto x = createProjectXml();
    if (! x->writeTo (file)) return "Could not write " + file.getFullPathName();
    projectFile = file;
    savedProjectText = x->toString();
    return {};
}

juce::String LiveAlignProcessor::openProject (const juce::File& f)
{
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName ("LiveAlignProject")) return f.getFileName() + " is not a LiveAlign venue project.";
    loadProjectXml (*x);
    projectFile = f;
    savedProjectText = createProjectXml()->toString();
    return {};
}

void LiveAlignProcessor::newProject()
{
    venueName = "Untitled venue";
    venueNotes.clear();
    projectFile = juce::File();
    traces.clear();
    micChecklist = {};
    settings.delayMs = 0.0f;
    settings.genOn = false;
    settings.requestResetAverages = true;
    settings.requestResetLeq = true;
    savedProjectText = createProjectXml()->toString();
}

bool LiveAlignProcessor::projectChanged() const
{
    return createProjectXml()->toString() != savedProjectText;
}

void LiveAlignProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement x ("LiveAlign");
    x.setAttribute ("version", JucePlugin_VersionString);
    settings.save (x);
    x.setAttribute ("projectFile", projectFile.getFullPathName());
    x.setAttribute ("savedProjectText", savedProjectText);
    if (auto project = createProjectXml()) x.addChildElement (project.release());
    copyXmlToBinary (x, dest);
}

void LiveAlignProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        if (! x->hasTagName ("LiveAlign")) return;
        if (const auto* project = x->getChildByName ("LiveAlignProject"))
        {
            loadProjectXml (*project);
            settings.theme = juce::jlimit (0, 1, x->getIntAttribute ("theme", 0));
            settings.currentTab = juce::jlimit (0, 3, x->getIntAttribute ("currentTab", 0));
            const juce::String path = x->getStringAttribute ("projectFile");
            projectFile = path.isNotEmpty() ? juce::File (path) : juce::File();
            savedProjectText = x->getStringAttribute ("savedProjectText", createProjectXml()->toString());
        }
        else
        {
            // 0.1 / 0.2 state: settings and a calibration file path; traces came from the old folder.
            settings.load (*x);
            loadCalibration (*x);
            savedProjectText = createProjectXml()->toString();
        }
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
