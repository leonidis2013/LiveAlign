// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StandaloneHooks.h"

// The app takes up to 16 inputs (mics + reference) and 8 outputs; inside a host the plug-in starts as stereo.
juce::AudioProcessor::BusesProperties LiveAlignProcessor::defaultBuses()
{
    const bool app = juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
    return BusesProperties()
        .withInput ("Input", app ? juce::AudioChannelSet::discreteChannels (kMaxInputs) : juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", app ? juce::AudioChannelSet::discreteChannels (kMaxOutputs) : juce::AudioChannelSet::stereo(), true);
}

LiveAlignProcessor::LiveAlignProcessor() : AudioProcessor (defaultBuses())
{
    for (auto& v : micRms) v = -120.0f;
    locker.load();
    resetVenue();
    traces.importLegacyFolder();
    savedProjectText = createProjectXml()->toString();
}

LiveAlignProcessor::~LiveAlignProcessor() = default;

bool LiveAlignProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const int in = layouts.getMainInputChannels();
    const int out = layouts.getMainOutputChannels();
    return in >= 1 && in <= kMaxInputs && out >= 1 && out <= kMaxOutputs;
}

void LiveAlignProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    gen.prepare (sampleRate);
    const size_t block = (size_t) juce::jmax (512, samplesPerBlock);
    genBuf.assign (block, 0.0f);
    zeros.assign (block, 0.0f);
    rmsCoeff = 1.0 - std::exp (-1.0 / (0.3 * sampleRate));
    micMs.fill (0.0);
    refMs = 0.0;
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

    const int count = juce::jlimit (1, la::kMaxMics, settings.micCount.load());
    std::array<int, la::kMaxMics> micCh {};
    for (int m = 0; m < la::kMaxMics; ++m) micCh[(size_t) m] = m < count ? settings.micInput[m].load() : -1;
    const int refSrc = settings.refSource.load();
    const int genOut = settings.genOutput.load();
    const int chunkMax = (int) genBuf.size();

    std::array<float, la::kMaxMics> mPeak {};
    float rPeak = 0.0f;

    for (int start = 0; start < total; start += chunkMax)
    {
        const int n = juce::jmin (chunkMax, total - start);

        const bool genActive = ! gen.isSilent();
        if (genActive) gen.process (genBuf.data(), n);
        else std::fill (genBuf.begin(), genBuf.begin() + n, 0.0f);

        std::array<const float*, la::kMaxMics> mic {};
        for (int m = 0; m < la::kMaxMics; ++m)
        {
            const int ch = micCh[(size_t) m];
            mic[(size_t) m] = ch >= 0 && ch < numIn ? buffer.getReadPointer (ch, start) : zeros.data();
        }
        const float* ref = zeros.data();
        if (refSrc == 1) ref = genBuf.data();
        else if (refSrc >= 2 && refSrc - 2 < numIn) ref = buffer.getReadPointer (refSrc - 2, start);

        analyzer.push (mic.data(), ref, genBuf.data(), n);

        for (int m = 0; m < count; ++m)
        {
            const float* x = mic[(size_t) m];
            double ms = micMs[(size_t) m];
            float pk = mPeak[(size_t) m];
            for (int i = 0; i < n; ++i)
            {
                pk = juce::jmax (pk, std::abs (x[i]));
                ms += rmsCoeff * ((double) x[i] * x[i] - ms);
            }
            micMs[(size_t) m] = ms;
            mPeak[(size_t) m] = pk;
        }
        for (int i = 0; i < n; ++i)
        {
            rPeak = juce::jmax (rPeak, std::abs (ref[i]));
            refMs += rmsCoeff * ((double) ref[i] * ref[i] - refMs);
        }

        // Outputs. The app never passes its inputs through (that would feed the mics back into the PA);
        // the plug-in passes audio through untouched unless the generator is driving a channel.
        for (int ch = 0; ch < numOut; ++ch)
        {
            float* out = buffer.getWritePointer (ch, start);
            const bool genHere = genActive && (genOut == 0 || genOut - 1 == ch);
            if (genHere) std::copy (genBuf.begin(), genBuf.begin() + n, out);
            else if (standalone || ch >= numIn) std::fill (out, out + n, 0.0f);
        }
    }

    for (int m = 0; m < count; ++m)
    {
        if (mPeak[(size_t) m] > micPeak[(size_t) m].load()) micPeak[(size_t) m] = mPeak[(size_t) m];
        micRms[(size_t) m] = (float) la::powerToDbfs (micMs[(size_t) m]);
    }
    if (rPeak > refPeak.load()) refPeak = rPeak;
    refRms = (float) la::powerToDbfs (refMs);
}

// ---------------------------------------------------------------------------- mics

juce::String LiveAlignProcessor::loadCalibrationFile (int mic, const juce::File& f, int angle)
{
    if (! f.existsAsFile()) return "File not found: " + f.getFullPathName();
    auto t = la::parseFreqTable (f.loadFileAsString().toStdString());
    if (t.freq.size() < 3) return "No calibration data found in " + f.getFileName();
    mic = juce::jlimit (0, la::kMaxMics - 1, mic);
    const int a = angle < 0 ? la::MicCalSet::angleFromName (f.getFileName()) : (angle & 1);
    cals[(size_t) mic].table[a] = std::move (t);
    cals[(size_t) mic].name[a] = f.getFileName();
    mics.noCalFile[(size_t) mic] = false;
    applyCalibration (mic);
    return {};
}

void LiveAlignProcessor::clearCalibration (int mic, int angle)
{
    mic = juce::jlimit (0, la::kMaxMics - 1, mic);
    if (angle < 0) cals[(size_t) mic].clearAll();
    else cals[(size_t) mic].clear (angle);
    applyCalibration (mic);
}

void LiveAlignProcessor::swapCalibrationAngle (int mic)
{
    mic = juce::jlimit (0, la::kMaxMics - 1, mic);
    auto& c = cals[(size_t) mic];
    std::swap (c.table[0], c.table[1]);
    std::swap (c.name[0], c.name[1]);
    applyCalibration (mic);
}

int LiveAlignProcessor::effectiveAim (int mic) const
{
    return aimOverrideValue >= 0 ? aimOverrideValue : mics.aim[(size_t) juce::jlimit (0, la::kMaxMics - 1, mic)];
}

void LiveAlignProcessor::setMicAim (int mic, int aim)
{
    mic = juce::jlimit (0, la::kMaxMics - 1, mic);
    mics.aim[(size_t) mic] = aim == 0 ? 0 : 1;
    applyCalibration (mic);
}

void LiveAlignProcessor::setAimOverride (int aim)
{
    aimOverrideValue = aim < 0 ? -1 : (aim == 0 ? 0 : 1);
    for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
}

// Gives the analysis the file that matches how the mic is aimed now.
void LiveAlignProcessor::applyCalibration (int mic)
{
    const auto& c = cals[(size_t) mic];
    const int a = c.pick (effectiveAim (mic));
    if (a < 0) { if (! analyzer.getCalibration (mic).empty()) analyzer.clearCalibration (mic); return; }
    if (analyzer.getCalibrationName (mic) == c.name[a] && analyzer.getCalibration (mic).freq == c.table[a].freq
        && analyzer.getCalibration (mic).db == c.table[a].db) return;
    analyzer.setCalibration (mic, c.table[a], c.name[a]);
}

void LiveAlignProcessor::addMic()
{
    const int n = settings.micCount.load();
    if (n >= la::kMaxMics) return;
    // first input not used by another mic or the reference
    int input = 0;
    for (; input < kMaxInputs; ++input)
    {
        bool used = settings.refSource.load() - 2 == input;
        for (int m = 0; m < n; ++m) used = used || settings.micInput[m].load() == input;
        if (! used) break;
    }
    settings.micInput[n] = juce::jmin (input, kMaxInputs - 1);
    settings.micDelayMs[n] = 0.0f;
    mics.name[(size_t) n] = {};
    mics.position[(size_t) n] = {};
    mics.noCalFile[(size_t) n] = false;
    mics.lockerId[(size_t) n].clear();
    mics.aim[(size_t) n] = 1;
    checks.splVerified[(size_t) n] = false;
    cals[(size_t) n].clearAll();
    applyCalibration (n);
    settings.micCount = n + 1;
    settings.requestResetAverages = true;
}

void LiveAlignProcessor::removeMic (int mic)
{
    const int n = settings.micCount.load();
    if (n <= 1 || mic < 0 || mic >= n) return;
    // shift the later mics down one slot
    for (int m = mic; m < n - 1; ++m)
    {
        settings.micInput[m] = settings.micInput[m + 1].load();
        settings.micDelayMs[m] = settings.micDelayMs[m + 1].load();
        settings.micSplCalibrated[m] = settings.micSplCalibrated[m + 1].load();
        settings.micSplOffset[m] = settings.micSplOffset[m + 1].load();
        mics.name[(size_t) m] = mics.name[(size_t) m + 1];
        mics.position[(size_t) m] = mics.position[(size_t) m + 1];
        mics.noCalFile[(size_t) m] = mics.noCalFile[(size_t) m + 1];
        checks.splVerified[(size_t) m] = checks.splVerified[(size_t) m + 1];
        mics.lockerId[(size_t) m] = mics.lockerId[(size_t) m + 1];
        mics.aim[(size_t) m] = mics.aim[(size_t) m + 1];
        cals[(size_t) m] = cals[(size_t) m + 1];
        applyCalibration (m);
    }
    plan.micRemoved (mic);
    cals[(size_t) n - 1].clearAll();
    applyCalibration (n - 1);
    mics.lockerId[(size_t) n - 1].clear();
    settings.micCount = n - 1;
    if (settings.selectedMic.load() >= n - 1 && settings.selectedMic.load() != la::kAverageMic) settings.selectedMic = 0;
    settings.requestResetAverages = true;
}

juce::String LiveAlignProcessor::setupFingerprint() const
{
    juce::String s = la::StandaloneHooks::deviceName() ? la::StandaloneHooks::deviceName()() : juce::String ("host");
    s << "|" << juce::roundToInt (getSampleRate()) << "|ref" << settings.refSource.load();
    for (int m = 0; m < settings.micCount.load(); ++m) s << "|m" << settings.micInput[m].load();
    return s;
}

// ---------------------------------------------------------------------------- mic locker

juce::String LiveAlignProcessor::saveMicToLocker (int mic, const juce::String& name)
{
    mic = juce::jlimit (0, la::kMaxMics - 1, mic);
    auto* entry = locker.find (mics.lockerId[(size_t) mic]);
    if (entry == nullptr)
    {
        la::LockerMic m;
        m.id = juce::Uuid().toString();
        locker.mics.push_back (m);
        entry = &locker.mics.back();
    }
    entry->name = name.isNotEmpty() ? name : mics.nameOf (mic);
    entry->cals = cals[(size_t) mic];
    entry->noCalFile = mics.noCalFile[(size_t) mic];
    entry->hasSpl = settings.micSplCalibrated[mic].load();
    entry->splOffset = settings.micSplOffset[mic].load();
    if (entry->hasSpl && entry->splDate.isEmpty()) entry->splDate = today();
    entry->savedOn = today();
    mics.lockerId[(size_t) mic] = entry->id;
    if (mics.name[(size_t) mic].isEmpty()) mics.name[(size_t) mic] = entry->name;
    locker.save();
    return entry->id;
}

// Puts a locker mic in a slot of the venue's mic table: its name, calibration file and last sound level calibration.
// The sound level still has to be confirmed this session (the interface gain may differ).
void LiveAlignProcessor::assignLockerMic (int mic, const juce::String& lockerId)
{
    mic = juce::jlimit (0, la::kMaxMics - 1, mic);
    const auto* e = locker.find (lockerId);
    if (e == nullptr) return;
    // a mic can only be in one slot
    for (int m = 0; m < la::kMaxMics; ++m) if (m != mic && mics.lockerId[(size_t) m] == lockerId) mics.lockerId[(size_t) m].clear();
    mics.lockerId[(size_t) mic] = lockerId;
    mics.name[(size_t) mic] = e->name;
    cals[(size_t) mic] = e->cals;
    applyCalibration (mic);
    mics.noCalFile[(size_t) mic] = ! e->cals.any() && e->noCalFile;
    settings.micSplCalibrated[mic] = e->hasSpl;
    if (e->hasSpl) settings.micSplOffset[mic] = e->splOffset;
    checks.splVerified[(size_t) mic] = false;
    settings.requestResetAverages = true;
}

bool LiveAlignProcessor::syncLocker()
{
    bool changed = false;
    for (int m = 0; m < settings.micCount.load(); ++m)
    {
        auto* e = locker.find (mics.lockerId[(size_t) m]);
        if (e == nullptr) continue;
        const bool spl = settings.micSplCalibrated[m].load();
        const float off = settings.micSplOffset[m].load();
        const bool noFile = mics.noCalFile[(size_t) m];
        const auto& c = cals[(size_t) m];
        if (c.name[0] != e->cals.name[0] || c.name[1] != e->cals.name[1] || c.has (0) != e->cals.has (0) || c.has (1) != e->cals.has (1))
        {
            e->cals = c;
            changed = true;
        }
        if (noFile != e->noCalFile) { e->noCalFile = noFile; changed = true; }
        if (spl && (! e->hasSpl || std::abs (off - e->splOffset) > 0.005f))
        {
            e->hasSpl = true;
            e->splOffset = off;
            e->splDate = today();
            changed = true;
        }
        if (mics.name[(size_t) m].isNotEmpty() && mics.name[(size_t) m] != e->name) { e->name = mics.name[(size_t) m]; changed = true; }
    }
    if (changed) locker.save();
    return changed;
}

// The calibration tables themselves are stored (not just their paths), so a venue file works on another computer.
void LiveAlignProcessor::saveMicCalibrations (juce::XmlElement& parent) const
{
    for (int m = 0; m < la::kMaxMics; ++m)
        for (int a = 0; a < 2; ++a)
        {
            const auto& c = cals[(size_t) m];
            if (! c.has (a)) continue;
            auto* e = parent.createNewChildElement ("MicCalibration");
            e->setAttribute ("mic", m);
            e->setAttribute ("angle", a == la::Cal90 ? 90 : 0);
            e->setAttribute ("name", c.name[a]);
            la::MicCalSet::saveTable (*e, c.table[a]);
        }
}

void LiveAlignProcessor::loadMicCalibrations (const juce::XmlElement& parent)
{
    for (auto& c : cals) c.clearAll();
    for (const auto* e : parent.getChildWithTagNameIterator ("MicCalibration"))
    {
        const int m = e->getIntAttribute ("mic", 0);   // 0.3 files have one, without "mic"
        if (m < 0 || m >= la::kMaxMics) continue;
        auto t = la::MicCalSet::loadTable (*e);
        if (t.freq.size() < 3) continue;
        const auto name = e->getStringAttribute ("name");
        // before 0.9 there was one file per mic: its name says which angle it is for
        const int a = e->hasAttribute ("angle") ? (e->getIntAttribute ("angle") == 90 ? la::Cal90 : la::Cal0) : la::MicCalSet::angleFromName (name);
        cals[(size_t) m].table[a] = std::move (t);
        cals[(size_t) m].name[a] = name;
    }
    for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
    // 0.1 / 0.2 state kept only the file path
    const juce::String path = parent.getStringAttribute ("calFile");
    if (path.isNotEmpty() && juce::File (path).existsAsFile() && ! cals[0].any())
        loadCalibrationFile (0, juce::File (path));
}

// ---------------------------------------------------------------------------- sessions

std::unique_ptr<juce::XmlElement> LiveAlignProcessor::sessionBody() const
{
    auto b = std::make_unique<juce::XmlElement> ("Body");
    auto* d = b->createNewChildElement ("Delays");
    for (int m = 0; m < la::kMaxMics; ++m) d->setAttribute ("mic" + juce::String (m), settings.micDelayMs[m].load());
    checks.save (*b);
    traces.save (*b);
    return b;
}

void LiveAlignProcessor::loadSessionBody (const juce::XmlElement* b)
{
    checks = {};
    traces.clear();
    for (int m = 0; m < la::kMaxMics; ++m) settings.micDelayMs[m] = 0.0f;
    if (b == nullptr) return;
    if (const auto* d = b->getChildByName ("Delays"))
        for (int m = 0; m < la::kMaxMics; ++m)
            settings.micDelayMs[m] = (float) d->getDoubleAttribute ("mic" + juce::String (m), 0.0);
    checks.load (*b);
    traces.load (*b);
    settings.requestResetAverages = true;
    settings.requestResetLeq = true;
}

void LiveAlignProcessor::newSession (const juce::String& name)
{
    traces.clearComparisons();
    sessions[(size_t) current].body = sessionBody();
    la::SessionInfo s;
    s.id = juce::Uuid().toString();
    s.name = name.isNotEmpty() ? name : "Session " + juce::String (sessions.size() + 1);
    s.created = today();
    // a new visit starts with the same delays (the mics are often in the same places) but no traces or checks
    auto keepDelays = sessionBody();
    sessions.push_back (std::move (s));
    current = (int) sessions.size() - 1;
    loadSessionBody (nullptr);
    if (const auto* d = keepDelays->getChildByName ("Delays"))
        for (int m = 0; m < la::kMaxMics; ++m)
            settings.micDelayMs[m] = (float) d->getDoubleAttribute ("mic" + juce::String (m), 0.0);
}

void LiveAlignProcessor::switchSession (int index)
{
    if (index < 0 || index >= (int) sessions.size() || index == current) return;
    traces.clearComparisons();
    sessions[(size_t) current].body = sessionBody();
    current = index;
    loadSessionBody (sessions[(size_t) current].body.get());
    sessions[(size_t) current].body.reset();
}

void LiveAlignProcessor::deleteSession (int index)
{
    if (sessions.size() <= 1 || index < 0 || index >= (int) sessions.size()) return;
    if (index == current)
    {
        const int next = index == 0 ? 1 : index - 1;
        switchSession (next);
    }
    traces.clearComparisons();
    sessions.erase (sessions.begin() + index);
    if (current > index) --current;
}

void LiveAlignProcessor::renameSession (int index, const juce::String& name)
{
    if (index >= 0 && index < (int) sessions.size() && name.trim().isNotEmpty())
        sessions[(size_t) index].name = name.trim();
}

void LiveAlignProcessor::compareWithSession (int index)
{
    if (index < 0 || index >= (int) sessions.size() || index == current || sessions[(size_t) index].body == nullptr) return;
    traces.addComparison (*sessions[(size_t) index].body, sessions[(size_t) index].name);
}

// ---------------------------------------------------------------------------- venue file

void LiveAlignProcessor::resetVenue()
{
    venueName = "Untitled venue";
    venueNotes.clear();
    projectFile = juce::File();
    mics = {};
    for (auto& c : cals) c.clearAll();
    aimOverrideValue = -1;
    for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
    positions = { "FOH", "Centre", "Rear", "Balcony" };
    speakers = {};
    system = la::SpeakerSystem::defaults();
    plan = la::VenuePlan::defaults();
    checks = {};
    traces.clear();
    sessions.clear();
    la::SessionInfo s;
    s.id = juce::Uuid().toString();
    s.name = "Session 1";
    s.created = today();
    sessions.push_back (std::move (s));
    current = 0;
    for (int m = 0; m < la::kMaxMics; ++m) settings.micDelayMs[m] = 0.0f;
}

std::unique_ptr<juce::XmlElement> LiveAlignProcessor::createProjectXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("LiveAlignProject");
    x->setAttribute ("format", 2);
    x->setAttribute ("appVersion", JucePlugin_VersionString);
    auto* venue = x->createNewChildElement ("Venue");
    venue->setAttribute ("name", venueName);
    venue->setAttribute ("notes", venueNotes);

    auto* st = x->createNewChildElement ("Settings");
    settings.save (*st);
    for (auto* a : { "theme", "currentTab" }) st->removeAttribute (a);   // preferences of this computer
    for (int m = 0; m < la::kMaxMics; ++m) st->removeAttribute ("micDelayMs" + juce::String (m));   // per session

    mics.save (*x);
    saveMicCalibrations (*x);
    auto* pos = x->createNewChildElement ("Positions");
    for (const auto& p : positions) pos->createNewChildElement ("Position")->setAttribute ("name", p);
    speakers.save (*x);
    system.save (*x);
    plan.save (*x);

    auto* list = x->createNewChildElement ("Sessions");
    list->setAttribute ("current", current);
    for (size_t i = 0; i < sessions.size(); ++i)
    {
        const auto& s = sessions[i];
        auto* e = list->createNewChildElement ("Session");
        e->setAttribute ("id", s.id);
        e->setAttribute ("name", s.name);
        e->setAttribute ("created", s.created);
        e->setAttribute ("notes", s.notes);
        auto body = (int) i == current ? sessionBody() : (s.body != nullptr ? std::make_unique<juce::XmlElement> (*s.body) : nullptr);
        if (body != nullptr)
            for (auto* child : body->getChildIterator()) e->addChildElement (new juce::XmlElement (*child));
    }
    return x;
}

void LiveAlignProcessor::loadProjectXml (const juce::XmlElement& x)
{
    const int theme = settings.theme.load(), tab = settings.currentTab.load();
    resetVenue();
    if (const auto* venue = x.getChildByName ("Venue"))
    {
        venueName = venue->getStringAttribute ("name", "Untitled venue");
        venueNotes = venue->getStringAttribute ("notes");
    }
    if (const auto* st = x.getChildByName ("Settings"))
    {
        settings.load (*st);
        if (! st->hasAttribute ("theme")) settings.theme = theme;
        settings.currentTab = tab;
    }
    settings.genOn = false;
    mics.load (x);
    loadMicCalibrations (x);
    if (const auto* pos = x.getChildByName ("Positions"))
    {
        positions.clear();
        for (const auto* p : pos->getChildWithTagNameIterator ("Position")) positions.add (p->getStringAttribute ("name"));
    }
    speakers.load (x);
    system.load (x);
    plan.load (x);
    // mics linked to the locker pick up their latest calibration from it
    for (int m = 0; m < la::kMaxMics; ++m)
        if (const auto* e = locker.find (mics.lockerId[(size_t) m]))
        {
            if (e->cals.any()) { cals[(size_t) m] = e->cals; applyCalibration (m); }
            if (e->hasSpl) { settings.micSplCalibrated[m] = true; settings.micSplOffset[m] = e->splOffset; }
        }

    if (const auto* list = x.getChildByName ("Sessions"))
    {
        sessions.clear();
        for (const auto* e : list->getChildWithTagNameIterator ("Session"))
        {
            la::SessionInfo s;
            s.id = e->getStringAttribute ("id", juce::Uuid().toString());
            s.name = e->getStringAttribute ("name", "Session");
            s.created = e->getStringAttribute ("created");
            s.notes = e->getStringAttribute ("notes");
            s.body = std::make_unique<juce::XmlElement> ("Body");
            for (const auto* child : e->getChildIterator()) s.body->addChildElement (new juce::XmlElement (*child));
            sessions.push_back (std::move (s));
        }
        if (sessions.empty()) resetVenue();
        current = juce::jlimit (0, (int) sessions.size() - 1, list->getIntAttribute ("current", 0));
        loadSessionBody (sessions[(size_t) current].body.get());
        sessions[(size_t) current].body.reset();
    }
    else
    {
        // format 1 (0.3): one implicit session holding the traces and checks
        auto body = std::make_unique<juce::XmlElement> ("Body");
        for (const auto* child : x.getChildIterator()) body->addChildElement (new juce::XmlElement (*child));
        auto* d = body->createNewChildElement ("Delays");
        if (const auto* st = x.getChildByName ("Settings")) d->setAttribute ("mic0", st->getDoubleAttribute ("delayMs", 0.0));
        loadSessionBody (body.get());
    }
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
    if (x == nullptr || ! x->hasTagName ("LiveAlignProject")) return f.getFileName() + " is not a LiveAlign venue file.";
    loadProjectXml (*x);
    projectFile = f;
    savedProjectText = createProjectXml()->toString();
    return {};
}

void LiveAlignProcessor::newProject()
{
    resetVenue();
    settings.genOn = false;
    settings.requestResetAverages = true;
    settings.requestResetLeq = true;
    savedProjectText = createProjectXml()->toString();
}

bool LiveAlignProcessor::projectChanged() const
{
    return createProjectXml()->toString() != savedProjectText;
}

// ---------------------------------------------------------------------------- state

void LiveAlignProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement x ("LiveAlign");
    x.setAttribute ("version", JucePlugin_VersionString);
    x.setAttribute ("theme", settings.theme.load());
    x.setAttribute ("currentTab", settings.currentTab.load());
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
        settings.theme = juce::jlimit (0, 1, x->getIntAttribute ("theme", 0));
        settings.currentTab = juce::jlimit (0, 5, x->getIntAttribute ("currentTab", 0));
        if (const auto* project = x->getChildByName ("LiveAlignProject"))
        {
            loadProjectXml (*project);
            const juce::String path = x->getStringAttribute ("projectFile");
            projectFile = path.isNotEmpty() ? juce::File (path) : juce::File();
            savedProjectText = x->getStringAttribute ("savedProjectText", createProjectXml()->toString());
        }
        else
        {
            // 0.1 / 0.2 state: settings and a calibration file path; traces came from the old folder
            settings.load (*x);
            loadMicCalibrations (*x);
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
