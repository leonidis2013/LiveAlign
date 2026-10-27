#include <algorithm>
// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "StandaloneHooks.h"
#include "VenueLibrary.h"

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
    if (wrapperType != wrapperType_Standalone) setMonitorMode (true);   // the VST3 only monitors
    applyAppPrefs();
    settings.genLevelDb = (float) la::AppPrefs::get().value ("genStartDb");
    configureClimate();
    if (wrapperType == wrapperType_Standalone) loadListening();
    savedProjectText = createProjectXml()->toString();
    changedCheckedAt = 0;
    startTimer (100);
}

// The show's watchers: the sound level log, the feedback watch and the delay tracking. The analysis thread measures
// (whole intervals, the ring detector, the delay); here their results go into the show log and the tracking is asked
// for, with or without a window.
void LiveAlignProcessor::timerCallback()
{
    const auto& ap = la::AppPrefs::get();
    analyzer.setLogging (monitorModeOn, splLog.interval);
    analyzer.setFeedbackWatch (feedbackWatching.load() && (monitorModeOn || monitorPageShown.load()));
    analyzer.setFeedbackSensitivity (juce::roundToInt (ap.value ("fbSense")));
    const bool sensor = climate.live();   // the room sensor's reading goes in the log (typed-in conditions do not change)
    for (const auto& p : analyzer.takeLogPoints())
        splLog.addInterval (p.t, p.la, p.lc, p.laMax, sensor ? climate.tempC.load() : NAN, sensor ? climate.humidity.load() : NAN, p.lcPeak);
    for (const auto& h : analyzer.takeFeedbackHits())
    {
        splLog.addHit (h.t, (float) h.freq, (float) h.cutDb);
        if (newHits.size() >= 64) newHits.erase (newHits.begin());
        newHits.push_back (h);
    }
    // delay tracking: find the delay on the programme every few seconds
    if (delayTracking.load() && settings.refSource.load() >= 1 && la::clock::ms32() - lastTrackRequest > (juce::uint32) (1000.0 * ap.value ("trackSec")))
    {
        lastTrackRequest = la::clock::ms32();
        analyzer.requestTrackDelay();
    }
}

// This computer's preferences that live in the audio settings (the generator's ceiling and fade-in).
void LiveAlignProcessor::applyAppPrefs()
{
    const auto& ap = la::AppPrefs::get();
    settings.genMaxDb = (float) ap.value ("genMaxDb");
    settings.genFadeMs = (float) ap.value ("genFadeMs");
}

LiveAlignProcessor::~LiveAlignProcessor() { stopTimer(); }

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
    health.prepare (sampleRate, juce::jmax (4096, 2 * samplesPerBlock));
    health.setTimingChecks (isStandalone() && ! timingChecksOff);
}

void LiveAlignProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numIn = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();
    const int total = buffer.getNumSamples();
    const bool standalone = isStandalone();

    gen.setType ((la::GenType) settings.genType.load());
    gen.setLevelDb (juce::jmin (settings.genLevelDb.load(), settings.genMaxDb.load()));
    gen.setFadeInSeconds (settings.genFadeMs.load() * 0.001);
    gen.setFrequency (settings.genFreq.load());
    gen.setSweepSeconds (settings.genSweepSec.load());
    gen.setOn (settings.genOn.load() && ! settings.genLocked.load());

    const int count = juce::jlimit (1, la::kMaxMics, settings.micCount.load());
    std::array<int, la::kMaxMics> micCh {};
    for (int m = 0; m < la::kMaxMics; ++m) micCh[(size_t) m] = m < count ? settings.micInput[m].load() : -1;
    const int refSrc = settings.refSource.load();
    const int genOut = settings.genOutput.load();
    const int chunkMax = (int) genBuf.size();
    const int listenSrc = settings.listenSource.load(), listenCh = settings.listenChannel.load();
    const float listenGain = settings.listenMute.load() || listenSrc == 0 ? 0.0f : juce::Decibels::decibelsToGain (settings.listenGainDb.load(), -90.0f);
    listenDevice.bridge.setSourceRate (getSampleRate());

    std::array<float, la::kMaxMics> mPeak {};
    float rPeak = 0.0f;

    // audio health: each mic in use and the console feed (an input), the whole block at once
    {
        std::array<const float*, la::AudioHealth::kMaxChannels> watch {};
        int nw = 0;
        for (int m = 0; m < count && nw < la::AudioHealth::kMaxChannels - 1; ++m)
            watch[(size_t) nw++] = micCh[(size_t) m] >= 0 && micCh[(size_t) m] < numIn ? buffer.getReadPointer (micCh[(size_t) m]) : nullptr;
        healthMics.store (nw, std::memory_order_relaxed);
        if (refSrc >= 2 && refSrc - 2 < numIn) watch[(size_t) nw++] = buffer.getReadPointer (refSrc - 2);
        health.process (watch.data(), nw, total, juce::Time::getMillisecondCounterHiRes());
    }

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
        recorder.push (mic.data(), ref, n);

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

        // Listening: the reference or a mic, at its level, for headphones (never on an output the generator uses)
        const float* lsn = listenSrc == 1 ? ref : (listenSrc >= 2 && listenSrc - 2 < la::kMaxMics ? mic[(size_t) (listenSrc - 2)] : nullptr);
        if (lsn != nullptr && listenCh == -1 && standalone) listenDevice.bridge.push (lsn, n, listenGain);

        // Outputs. The app never passes its inputs through (that would feed the mics back into the PA);
        // the plug-in passes audio through untouched unless the generator is driving a channel.
        for (int ch = 0; ch < numOut; ++ch)
        {
            float* out = buffer.getWritePointer (ch, start);
            const bool listenHere = lsn != nullptr && standalone && ch == listenCh;
            const bool genHere = genActive && ! listenHere && (genOut == 0 || genOut - 1 == ch);
            if (genHere) std::copy (genBuf.begin(), genBuf.begin() + n, out);
            else if (listenHere) { for (int i = 0; i < n; ++i) out[i] = lsn[i] * listenGain; }
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
    const int a = angle < 0 ? la::MicCalSet::angleFromName (f.getFileName()) : juce::jlimit (0, la::kCalSlots - 1, angle);
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
    // two files the wrong way round: swap them (with more than two, set each file's angle instead)
    auto& c = cals[(size_t) mic];
    int first = -1, second = -1;
    for (int a = 0; a < la::kCalSlots; ++a) if (c.has (a)) { if (first < 0) first = a; else if (second < 0) second = a; }
    if (first < 0) return;
    if (second < 0) second = first == la::Cal0 ? la::Cal90 : la::Cal0;   // one file: it is really for the other usual angle
    std::swap (c.table[first], c.table[second]);
    std::swap (c.name[first], c.name[second]);
    applyCalibration (mic);
}

int LiveAlignProcessor::effectiveAim (int mic) const
{
    juce::ignoreUnused (mic);
    return aimOverrideValue >= 0 ? aimOverrideValue : roomAim;
}

int LiveAlignProcessor::ownAim (int mic) const
{
    juce::ignoreUnused (mic);
    return roomAim;
}

int LiveAlignProcessor::positionAimOf (const juce::String& position) const
{
    if (position.isEmpty()) return -1;
    const auto it = positionAim.find (position);
    return it != positionAim.end() ? it->second : -1;
}

void LiveAlignProcessor::setPositionAim (const juce::String& position, int aim)
{
    if (position.isEmpty()) return;
    aim = juce::jlimit (0, la::kCalSlots - 1, aim);
    positionAim[position] = aim;
    for (int m = 0; m < la::kMaxMics; ++m)
        if (mics.position[(size_t) m] == position) { mics.aim[(size_t) m] = aim; applyCalibration (m); }
}

// The aim of the mics in the room (pointing up, or an angle): one setting for every room measurement.
void LiveAlignProcessor::setMicAim (int mic, int aim)
{
    juce::ignoreUnused (mic);
    roomAim = juce::jlimit (0, la::kCalSlots - 1, aim);
    for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
}

void LiveAlignProcessor::setMicPosition (int mic, const juce::String& position)
{
    mic = juce::jlimit (0, la::kMaxMics - 1, mic);
    const auto p = position.trim();
    mics.position[(size_t) mic] = p;
    if (p.isNotEmpty() && ! positions.contains (p)) positions.add (p);
    // a new position starts with the aim this mic had
    if (p.isNotEmpty() && positionAimOf (p) < 0) positionAim[p] = mics.aim[(size_t) mic];
    applyCalibration (mic);
}

void LiveAlignProcessor::setAimOverride (int aim)
{
    aimOverrideValue = aim < 0 ? -1 : juce::jlimit (0, la::kCalSlots - 1, aim);
    for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
}

// Gives the analysis the file that matches how the mic is aimed now.
void LiveAlignProcessor::applyCalibration (int mic)
{
    if (monitorModeOn && mic == 0) { applyMonitorMic(); return; }
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

void LiveAlignProcessor::syncPositionsWithPlan()
{
    // every point on the plan is a tuning point, a monitoring point, or both
    for (const auto& it : plan.items)
        if (it.kind == la::PlanItem::Marker && it.text.isNotEmpty() && ! positions.contains (it.text) && ! monitorPoints.contains (it.text)) positions.add (it.text);
    if (monitorMic.position.isNotEmpty()) monitorPoints.addIfNotAlreadyThere (monitorMic.position);
    for (const auto& p : positions) plan.ensureMarker (p);
    for (const auto& p : monitorPoints) plan.ensureMarker (p);
    // each tuning mic sits on its position's marker
    for (int m = 0; m < juce::jmin (settings.micCount.load(), la::kMaxMics); ++m)
    {
        const int k = plan.markerOf (mics.position[(size_t) m]);
        if (k < 0) continue;
        plan.mics[(size_t) m].x = plan.items[(size_t) k].x;
        plan.mics[(size_t) m].y = plan.items[(size_t) k].y;
        plan.mics[(size_t) m].placed = true;
    }
}

void LiveAlignProcessor::renamePosition (const juce::String& from, const juce::String& to)
{
    const auto t = to.trim();
    if (from == t || t.isEmpty()) return;
    const int i = positions.indexOf (from);
    if (i >= 0) { if (positions.contains (t)) positions.remove (i); else positions.set (i, t); }
    for (auto& p : mics.position) if (p == from) p = t;
    if (const int k = monitorPoints.indexOf (from); k >= 0) { if (monitorPoints.contains (t)) monitorPoints.remove (k); else monitorPoints.set (k, t); }
    if (auto it = positionAim.find (from); it != positionAim.end()) { const int a = it->second; positionAim.erase (it); positionAim[t] = a; }
    for (auto& m : room.meas) if (m.position == from) m.position = t;
    for (auto& w : room.sweeps) if (w.position == from) w.position = t;
    if (room.skipped.contains (from)) { room.skipped.removeString (from); room.skipped.addIfNotAlreadyThere (t); }
    if (monitorMic.position == from) monitorMic.position = t;
    for (auto& a : system.alignments) if (a.spot == from) a.spot = t;
    for (auto& it : plan.items) if (it.kind == la::PlanItem::Marker && it.text == from) it.text = t;
}

void LiveAlignProcessor::removePosition (const juce::String& name)
{
    positions.removeString (name);
    monitorPoints.removeString (name);
    if (monitorMic.position == name) monitorMic.position.clear();
    for (auto& p : mics.position) if (p == name) p.clear();
    positionAim.erase (name);
    room.meas.erase (std::remove_if (room.meas.begin(), room.meas.end(), [&name] (const la::RoomMeasurement& m) { return m.position == name; }), room.meas.end());
    room.skipped.removeString (name);
    room.sweeps.erase (std::remove_if (room.sweeps.begin(), room.sweeps.end(), [&name] (const la::SweepMeasurement& w) { return w.position == name; }), room.sweeps.end());
    plan.items.erase (std::remove_if (plan.items.begin(), plan.items.end(), [&name] (const la::PlanItem& it) { return it.kind == la::PlanItem::Marker && it.text == name; }),
                      plan.items.end());
}

juce::StringArray LiveAlignProcessor::setupChanges() const
{
    juce::StringArray out;
    if (checks.setup.isEmpty()) return out;
    const auto now = setupFingerprint();
    if (now == checks.setup) return out;
    juce::StringArray a, b;
    a.addTokens (checks.setup, "|", {});
    b.addTokens (now, "|", {});
    auto part = [] (const juce::StringArray& x, int i) { return i < x.size() ? x[i] : juce::String(); };
    if (part (a, 0) != part (b, 0)) out.add ("the interface: was " + (part (a, 0).isNotEmpty() ? part (a, 0) : juce::String ("none")) + ", now "
                                             + (part (b, 0).isNotEmpty() ? part (b, 0) : juce::String ("none")));
    if (part (a, 1) != part (b, 1)) out.add ("the sample rate: was " + part (a, 1) + " Hz, now " + part (b, 1) + " Hz");
    if (part (a, 2) != part (b, 2)) out.add ("the reference");
    for (int k = 3; k < juce::jmax (a.size(), b.size()); ++k)
        if (part (a, k) != part (b, k))
        {
            const int m = k - 3;
            if (part (b, k).isEmpty()) out.add (mics.nameOf (m) + " was removed");
            else if (part (a, k).isEmpty()) out.add (mics.nameOf (m) + " was added");
            else out.add (mics.nameOf (m) + "'s input: was " + juce::String (part (a, k).substring (1).getIntValue() + 1) + ", now "
                          + juce::String (part (b, k).substring (1).getIntValue() + 1));
        }
    if (out.isEmpty()) out.add ("the wiring");
    return out;
}

bool LiveAlignProcessor::interfaceChanged() const
{
    if (checks.setup.isEmpty()) return false;
    juce::StringArray a, b;
    a.addTokens (checks.setup, "|", {});
    b.addTokens (setupFingerprint(), "|", {});
    return a[0] != b[0] || a[1] != b[1];
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
    if (entry->name == entry->modelSerial()) entry->name.clear();
    entry->cals = cals[(size_t) mic];
    entry->noCalFile = mics.noCalFile[(size_t) mic];
    entry->hasSpl = settings.micSplCalibrated[mic].load();
    entry->splOffset = settings.micSplOffset[mic].load();
    if (entry->hasSpl && entry->splDate.isEmpty()) { entry->splDate = today(); entry->splRefDb = settings.calibratorDb.load(); }
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
    mics.name[(size_t) mic] = e->label();
    cals[(size_t) mic] = e->cals;
    applyCalibration (mic);
    mics.noCalFile[(size_t) mic] = ! e->cals.any() && e->noCalFile;
    settings.micSplCalibrated[mic] = e->hasSpl;
    if (e->hasSpl) settings.micSplOffset[mic] = e->splOffset;
    checks.splVerified[(size_t) mic] = false;
    settings.requestResetAverages = true;
}

void LiveAlignProcessor::lockerEntryChanged (const juce::String& lockerId)
{
    const auto* e = locker.find (lockerId);
    if (e == nullptr) return;
    for (int m = 0; m < la::kMaxMics; ++m)
    {
        if (mics.lockerId[(size_t) m] != lockerId) continue;
        mics.name[(size_t) m] = e->label();
        cals[(size_t) m] = e->cals;
        mics.noCalFile[(size_t) m] = ! e->cals.any() && e->noCalFile;
        if (e->hasSpl) { settings.micSplCalibrated[m] = true; settings.micSplOffset[m] = e->splOffset; }
        applyCalibration (m);
    }
}

juce::String LiveAlignProcessor::micModelSerial (int mic) const
{
    const auto* e = locker.find (mics.lockerId[(size_t) juce::jlimit (0, la::kMaxMics - 1, mic)]);
    return e != nullptr ? e->modelSerial() : juce::String();
}

bool LiveAlignProcessor::syncLocker()
{
    if (monitorModeOn) return false;   // slot 1 is the monitor mic just now
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
            e->splTime = juce::Time::getCurrentTime().formatted ("%H:%M");
            e->splRefDb = settings.calibratorDb.load();
            changed = true;
        }
        if (mics.name[(size_t) m].isNotEmpty() && mics.name[(size_t) m] != e->label()) { e->name = mics.name[(size_t) m]; changed = true; }
    }
    if (changed) locker.save();
    return changed;
}

// The calibration tables themselves are stored (not just their paths), so a venue file works on another computer.
void LiveAlignProcessor::saveMicCalibrations (juce::XmlElement& parent) const
{
    for (int m = 0; m < la::kMaxMics; ++m)
        for (int a = 0; a < la::kCalSlots; ++a)
        {
            const auto& c = cals[(size_t) m];
            if (! c.has (a)) continue;
            auto* e = parent.createNewChildElement ("MicCalibration");
            e->setAttribute ("mic", m);
            e->setAttribute ("angle", la::calSlotDegrees (a));
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
        const int a = e->hasAttribute ("angle") ? la::calSlotFromDegrees (e->getIntAttribute ("angle")) : la::MicCalSet::angleFromName (name);
        cals[(size_t) m].table[a] = std::move (t);
        cals[(size_t) m].name[a] = name;
    }
    for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
    // 0.1 / 0.2 state kept only the file path
    const juce::String path = parent.getStringAttribute ("calFile");
    if (path.isNotEmpty() && juce::File (path).existsAsFile() && ! cals[0].any())
        loadCalibrationFile (0, juce::File (path));
}

// ---------------------------------------------------------------------------- monitoring mode

void LiveAlignProcessor::setMonitorMode (bool on)
{
    if (on == monitorModeOn) return;
    if (on)
    {
        if (! monitorMic.configured) takeMonitorMicFromTuning();
        tuningSlot = { settings.micCount.load(), settings.selectedMic.load(), settings.micInput[0].load(), settings.micDelayMs[0].load(),
                       settings.micSplOffset[0].load(), settings.micSplCalibrated[0].load() };
        monitorModeOn = true;
        applyMonitorMic();
    }
    else
    {
        // what changed while monitoring belongs to the monitor mic
        monitorMic.delayMs = settings.micDelayMs[0].load();
        monitorMic.splOffset = settings.micSplOffset[0].load();
        monitorMic.splCalibrated = settings.micSplCalibrated[0].load();
        monitorModeOn = false;
        settings.micCount = tuningSlot.count;
        settings.selectedMic = tuningSlot.selected;
        settings.micInput[0] = tuningSlot.input;
        settings.micDelayMs[0] = tuningSlot.delay;
        settings.micSplOffset[0] = tuningSlot.offset;
        settings.micSplCalibrated[0] = tuningSlot.calibrated;
        applyCalibration (0);
    }
    settings.requestResetAverages = true;
    settings.requestResetLeq = true;
}

void LiveAlignProcessor::applyMonitorMic()
{
    if (! monitorModeOn) return;
    settings.micCount = 1;
    settings.selectedMic = 0;
    settings.micInput[0] = juce::jlimit (0, kMaxInputs - 1, monitorMic.input);
    settings.micDelayMs[0] = monitorMic.delayMs;
    settings.micSplOffset[0] = monitorMic.splOffset;
    settings.micSplCalibrated[0] = monitorMic.splCalibrated;
    const int a = monitorMic.cal.pick (monitorMic.aim);
    if (a < 0) { if (! analyzer.getCalibration (0).empty()) analyzer.clearCalibration (0); }
    else analyzer.setCalibration (0, monitorMic.cal.table[a], monitorMic.cal.name[a]);
}

void LiveAlignProcessor::takeMonitorMicFromTuning (int mic)
{
    int m = 0;
    const int count = monitorModeOn ? tuningSlot.count : settings.micCount.load();
    if (mic >= 0 && mic < count) m = mic;
    else
        for (int k = 0; k < count; ++k)
            if (mics.position[(size_t) k].containsIgnoreCase ("FOH")) { m = k; break; }
    const bool slot0 = monitorModeOn && m == 0;
    monitorMic.name = mics.nameOf (m);
    // where it stands for the show: the monitoring point already chosen; else the first monitoring point (FOH). Not
    // the tuning point the mic was last at: that would turn a tuning point into a monitoring point behind the
    // user's back, and the plan would show the mic somewhere it is not.
    if (monitorMic.position.isEmpty() || ! monitorPoints.contains (monitorMic.position))
        monitorMic.position = monitorPoints.isEmpty() ? juce::String ("FOH") : monitorPoints[0];
    monitorMic.lockerId = mics.lockerId[(size_t) m];
    monitorMic.input = slot0 ? tuningSlot.input : settings.micInput[m].load();
    monitorMic.cal = cals[(size_t) m];
    monitorMic.noCalFile = mics.noCalFile[(size_t) m];
    monitorMic.aim = ownAim (m);
    monitorMic.splCalibrated = slot0 ? tuningSlot.calibrated : settings.micSplCalibrated[m].load();
    monitorMic.splOffset = slot0 ? tuningSlot.offset : settings.micSplOffset[m].load();
    monitorMic.delayMs = slot0 ? tuningSlot.delay : settings.micDelayMs[m].load();
    applyMonitorMic();
}

juce::String LiveAlignProcessor::loadMonitorCalibration (const juce::File& f, int angle)
{
    if (! f.existsAsFile()) return "File not found: " + f.getFullPathName();
    auto t = la::parseFreqTable (f.loadFileAsString().toStdString());
    if (t.freq.size() < 3) return "No calibration data found in " + f.getFileName();
    const int a = angle < 0 ? la::MicCalSet::angleFromName (f.getFileName()) : juce::jlimit (0, la::kCalSlots - 1, angle);
    monitorMic.cal.table[a] = std::move (t);
    monitorMic.cal.name[a] = f.getFileName();
    monitorMic.noCalFile = false;
    monitorMic.configured = true;
    applyMonitorMic();
    return {};
}

void LiveAlignProcessor::assignMonitorFromLocker (const juce::String& id)
{
    const auto* e = locker.find (id);
    if (e == nullptr) return;
    syncMonitorFromSlot();
    monitorMic.lockerId = id;
    monitorMic.name = e->label();
    monitorMic.cal = e->cals;
    monitorMic.noCalFile = e->noCalFile;
    if (e->hasSpl)
    {
        monitorMic.splCalibrated = true;
        monitorMic.splOffset = e->splOffset;
        monitorMic.splDate = e->splDate;
        monitorMic.splTime = e->splTime;
        monitorMic.splRefDb = e->splRefDb;
    }
    monitorMic.configured = true;
    applyMonitorMic();
}

void LiveAlignProcessor::syncMonitorFromSlot()
{
    if (! monitorModeOn) return;
    monitorMic.delayMs = settings.micDelayMs[0].load();
    monitorMic.splOffset = settings.micSplOffset[0].load();
    monitorMic.splCalibrated = settings.micSplCalibrated[0].load();
}

// ---------------------------------------------------------------------------- audio health

LiveAlignProcessor::HealthLine LiveAlignProcessor::healthLine (double nowSec, double delayDriftMsPerMin) const
{
    HealthLine h;
    const auto& ev = health.events();
    const double recent = nowSec - 600.0;   // what counts now: the last ten minutes
    auto at = [] (double t) { return juce::Time ((juce::int64) (t * 1000.0)).formatted ("%H:%M"); };
    auto rateText = [this]
    {
        const double r = health.rate();
        if (r <= 0.0) return juce::String();
        const double ppm = health.ppm();
        return juce::String (r, 1) + " Hz (" + (ppm >= 0.0 ? "+" : "") + juce::String (juce::roundToInt (ppm)) + " ppm)";
    };
    // the totals since the audio started, for the tooltip
    {
        juce::StringArray d;
        for (int k = la::AudioHealth::Dropout; k <= la::AudioHealth::DriverOverload; ++k)
        {
            const int n = health.count (k);
            if (n > 0) d.add (juce::String (n) + " " + la::AudioHealth::kindName (k) + (n == 1 ? "" : "s"));
        }
        h.detail = "Since the audio started: " + (d.isEmpty() ? juce::String ("nothing found") : d.joinIntoString (", ")) + "."
                   + (rateText().isNotEmpty() ? " The interface's real rate: " + rateText() + "." : juce::String())
                   + " Watched all the time from what the inputs carry: missing audio (the sample count), digital silence or repeated "
                     "blocks inside a signal, and clicks at a steady interval (two clocks not locked).";
    }
    if (health.isStopped()) { h.text = "Audio: not running"; return h; }
    // a clock not locked: the worst, and the clearest
    for (int c = 0; c < la::AudioHealth::kMaxChannels; ++c)
        if (const double iv = health.syncInterval (c); iv > 0.0)
        {
            h.text = healthChannelName (c) + ": a click every " + juce::String (iv, 1) + " s, its clock is not locked to the interface's";
            h.level = 2;
            return h;
        }
    if (std::abs (delayDriftMsPerMin) >= 0.2)
    {
        h.text = "The delay to the console feed is drifting " + juce::String (std::abs (delayDriftMsPerMin), 2) + " ms a minute: the clocks may not be locked";
        h.level = 2;
        return h;
    }
    int drops = 0, gaps = 0;
    double lastDrop = 0.0, lastGap = 0.0;
    juce::String gapWhere;
    for (const auto& e : ev)
    {
        if (e.t < recent) continue;
        if (e.kind == la::AudioHealth::Dropout || e.kind == la::AudioHealth::DriverOverload) { ++drops; lastDrop = e.t; }
        if (e.kind == la::AudioHealth::ZeroGap || e.kind == la::AudioHealth::Repeat) { ++gaps; lastGap = e.t; gapWhere = healthChannelName (e.channel); }
    }
    if (drops > 0)
    {
        h.text = juce::String (drops) + (drops == 1 ? " dropout" : " dropouts") + " in the last 10 min (last " + at (lastDrop) + "): the computer, driver or USB missed audio";
        h.level = 1;
        return h;
    }
    if (gaps > 0)
    {
        h.text = gapWhere + ": " + juce::String (gaps) + (gaps == 1 ? " gap" : " gaps") + " in the audio in the last 10 min (last " + at (lastGap) + "): the interface dropped it";
        h.level = 1;
        return h;
    }
    if (std::abs (health.ppm()) >= 1000.0)
    {
        h.text = "Clock " + rateText() + ": far from " + juce::String (juce::roundToInt (health.nominalRate())) + " Hz, check the sample rate";
        h.level = 1;
        return h;
    }
    h.text = "Audio clean" + (rateText().isNotEmpty() ? juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + rateText() : juce::String (" so far"));
    return h;
}

// ---------------------------------------------------------------------------- listening

void LiveAlignProcessor::setListening (int source, int channel, const juce::String& deviceName)
{
    settings.listenSource = juce::jlimit (0, la::kMaxMics + 1, source);
    settings.listenChannel = juce::jmax (-1, channel);
    listenError.clear();
    const bool wantDevice = source > 0 && channel < 0 && deviceName.isNotEmpty();
    if (! wantDevice) { listenDevice.close(); }
    else if (! listenDevice.isOpen() || deviceName != listenDeviceName) listenError = listenDevice.open (deviceName, getSampleRate());
    listenDeviceName = deviceName;
    la::UserPrefs::set ("listenSource", juce::String (settings.listenSource.load()));
    la::UserPrefs::set ("listenChannel", juce::String (settings.listenChannel.load()));
    la::UserPrefs::set ("listenDevice", deviceName);
}

void LiveAlignProcessor::loadListening()
{
    settings.listenGainDb = (float) juce::jlimit (-60.0, 12.0, la::UserPrefs::get ("listenGainDb", "-12").getDoubleValue());
    settings.listenMute = la::UserPrefs::get ("listenMute", "0") == "1";
    const int src = la::UserPrefs::get ("listenSource", "0").getIntValue();
    if (src > 0) setListening (src, la::UserPrefs::get ("listenChannel", "-1").getIntValue(), la::UserPrefs::get ("listenDevice"));
}

void LiveAlignProcessor::saveListenLevel()
{
    la::UserPrefs::set ("listenGainDb", juce::String (settings.listenGainDb.load(), 1));
    la::UserPrefs::set ("listenMute", settings.listenMute.load() ? "1" : "0");
}

// ---------------------------------------------------------------------------- sessions

std::unique_ptr<juce::XmlElement> LiveAlignProcessor::sessionBody() const
{
    auto b = std::make_unique<juce::XmlElement> ("Body");
    auto* d = b->createNewChildElement ("Delays");
    for (int m = 0; m < la::kMaxMics; ++m) d->setAttribute ("mic" + juce::String (m), settings.micDelayMs[m].load());
    checks.save (*b);
    traces.save (*b);
    room.save (*b);
    level.save (*b);
    recheck.save (*b);
    feedback.save (*b);
    asFound.save (*b);
    splLog.save (*b);
    coverage.save (*b);
    headroom.save (*b);
    listening.save (*b);
    if (monitorModeOn) d->setAttribute ("mic0", tuningSlot.delay);   // the tuning mic's, not the monitor mic's
    return b;
}

void LiveAlignProcessor::loadSessionBody (const juce::XmlElement* b)
{
    // while monitoring, slot 1 holds the monitor mic: its delay stays, the session's goes to the tuning mic
    struct KeepMonitorDelay
    {
        LiveAlignProcessor& p;
        float d;
        explicit KeepMonitorDelay (LiveAlignProcessor& pp) : p (pp), d (pp.settings.micDelayMs[0].load()) {}
        ~KeepMonitorDelay()
        {
            if (! p.monitorModeOn) return;
            p.tuningSlot.delay = p.settings.micDelayMs[0].load();
            p.settings.micDelayMs[0] = d;
        }
    } keep (*this);
    checks = {};
    traces.clear();
    room.load (b);
    level.load (b);
    recheck.load (b);
    feedback.load (b);
    asFound.load (b);
    splLog.load (b);
    coverage.load (b);
    headroom.load (b);
    listening.load (b);
    for (int m = 0; m < la::kMaxMics; ++m) settings.micDelayMs[m] = 0.0f;
    if (b == nullptr) return;
    if (const auto* d = b->getChildByName ("Delays"))
        for (int m = 0; m < la::kMaxMics; ++m)
            settings.micDelayMs[m] = (float) d->getDoubleAttribute ("mic" + juce::String (m), 0.0);
    checks.load (*b);
    if (std::isfinite (checks.tempC)) settings.temperatureC = juce::jlimit (-20.0f, 50.0f, checks.tempC);
    if (std::isfinite (checks.humidity)) settings.humidity = juce::jlimit (0.0f, 100.0f, checks.humidity);
    traces.load (*b);
    settings.requestResetAverages = true;
    settings.requestResetLeq = true;
}

void LiveAlignProcessor::newSession (const juce::String& name, int kind)
{
    if (! sessionStarted())
    {
        // the venue's placeholder becomes its first session (it holds nothing of a visit yet)
        auto& c = sessions[(size_t) current];
        c.name = name.isNotEmpty() ? name : juce::String ("Session 1");
        c.created = today();
        c.kind = juce::jlimit (0, 1, kind);
        c.started = true;
        sessionKindChanged();
        return;
    }
    traces.clearComparisons();
    sessions[(size_t) current].body = sessionBody();
    la::SessionInfo s;
    s.id = juce::Uuid().toString();
    s.name = name.isNotEmpty() ? name : "Session " + juce::String (sessions.size() + 1);
    s.created = today();
    s.kind = juce::jlimit (0, 1, kind);
    // a new visit starts with the same delays (the mics are often in the same places) but no traces or checks
    auto keepDelays = sessionBody();
    sessions.push_back (std::move (s));
    current = (int) sessions.size() - 1;
    const auto tunedC = checks.tunedTempC, tunedH = checks.tunedHumidity;   // the conditions the tuning was done in carry on
    const auto tunedOn = checks.tunedConditionsOn;
    loadSessionBody (nullptr);
    checks.tunedTempC = tunedC;
    checks.tunedHumidity = tunedH;
    checks.tunedConditionsOn = tunedOn;
    if (const auto* d = keepDelays->getChildByName ("Delays"))
        for (int m = 0; m < la::kMaxMics; ++m)
            settings.micDelayMs[m] = (float) d->getDoubleAttribute ("mic" + juce::String (m), 0.0);
    sessionKindChanged();
}

// The session's kind sets what may play: a monitoring session never runs the generator. The tuning it is compared with
// is the venue's latest tuning session that measured the room.
void LiveAlignProcessor::sessionKindChanged()
{
    settings.genLocked = monitoringSession();
    if (monitoringSession()) settings.genOn = false;
    tuningRef.reset();
    if (! monitoringSession()) return;
    for (int i = (int) sessions.size(); --i >= 0;)
    {
        if (i == current || sessions[(size_t) i].kind != 0 || sessions[(size_t) i].body == nullptr) continue;
        auto r = std::make_unique<la::RoomTuning>();
        r->load (sessions[(size_t) i].body.get());
        if (r->meas.empty()) continue;
        tuningRef = std::move (r);
        tuningRefName = sessions[(size_t) i].name;
        break;
    }
}

const la::RoomTuning& LiveAlignProcessor::tuningRoom() const
{
    return monitoringSession() && tuningRef != nullptr ? *tuningRef : room;
}

void LiveAlignProcessor::switchSession (int index)
{
    if (index < 0 || index >= (int) sessions.size() || index == current) return;
    traces.clearComparisons();
    sessions[(size_t) current].body = sessionBody();
    current = index;
    loadSessionBody (sessions[(size_t) current].body.get());
    sessions[(size_t) current].body.reset();
    sessionKindChanged();
}

void LiveAlignProcessor::deleteSession (int index)
{
    if (index < 0 || index >= (int) sessions.size()) return;
    if (sessions.size() == 1)
    {
        // a venue always has a session: the only one is replaced by an empty placeholder (New session starts the next)
        la::SessionInfo s;
        s.id = juce::Uuid().toString();
        s.name = "No session yet";
        s.created = today();
        s.kind = sessions.front().kind;
        s.started = false;
        sessions.clear();
        sessions.push_back (std::move (s));
        current = 0;
        loadSessionBody (nullptr);
        traces.clearComparisons();
        sessionKindChanged();
        return;
    }
    if (index == current)
    {
        const int next = index == 0 ? 1 : index - 1;
        switchSession (next);
    }
    traces.clearComparisons();
    sessions.erase (sessions.begin() + index);
    if (current > index) --current;
    sessionKindChanged();
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
    const bool wasMonitoring = monitorModeOn;
    setMonitorMode (false);
    monitorMic = {};
    feedback = {};
    asFound = {};
    splLog.load (nullptr);
    coverage = {};
    headroom = {};
    listening = {};
    history.entries.clear();
    venueName = "Untitled venue";
    venueNotes.clear();
    loopbackOutput = 0;
    projectFile = juce::File();
    mics = {};
    for (auto& c : cals) c.clearAll();
    aimOverrideValue = -1;
    for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
    positions.clear();       // a new venue: no points until they are added (or a layout loaded) on the plan
    monitorPoints.clear();
    positionAim.clear();
    roomAim = la::Cal90;
    speakerAim = la::Cal0;
    level = {};
    recheck = {};
    venuePrefs = {};
    speakers = {};
    system = {};   // a new venue starts with no speakers: they are added on the Venue page
    // and with nothing attached: one mic slot, no input link, no calibration, no sound level, the default reference
    settings.micCount = 1;
    for (int m = 0; m < la::kMaxMics; ++m)
    {
        settings.micInput[m] = m == 0 ? 0 : m + 1;
        settings.micSplCalibrated[m] = false;
        settings.micSplOffset[m] = 120.0f;
    }
    settings.refSource = 3;
    settings.selectedMic = 0;
    plan = la::VenuePlan::defaults();
    checks = {};
    settings.temperatureC = 20.0f;   // a new venue: the usual conditions until they are entered or captured
    settings.humidity = 50.0f;
    room.load (nullptr);   // a new venue: this computer's Room EQ defaults
    traces.clear();
    sessions.clear();
    la::SessionInfo s;
    s.id = juce::Uuid().toString();
    s.name = "No session yet";
    s.created = today();
    s.started = false;      // a new venue has no session until New session (what it is for: tuning or a show)
    sessions.push_back (std::move (s));
    current = 0;
    sessionKindChanged();
    for (int m = 0; m < la::kMaxMics; ++m) settings.micDelayMs[m] = 0.0f;
    if (wasMonitoring) setMonitorMode (true);
}

std::unique_ptr<juce::XmlElement> LiveAlignProcessor::createProjectXml() const
{
    auto x = std::make_unique<juce::XmlElement> ("LiveAlignProject");
    x->setAttribute ("format", 2);
    x->setAttribute ("appVersion", JucePlugin_VersionString);
    auto* venue = x->createNewChildElement ("Venue");
    venue->setAttribute ("name", venueName);
    venue->setAttribute ("notes", venueNotes);
    // read by the venue list on Home without opening the whole file
    venue->setAttribute ("sessions", (int) sessions.size());
    venue->setAttribute ("tuned", room.tuned);
    venue->setAttribute ("loopbackOutput", loopbackOutput);

    auto* st = x->createNewChildElement ("Settings");
    settings.save (*st);
    for (const auto& a : la::Settings::userKeys()) st->removeAttribute (a);   // preferences of this computer
    for (int m = 0; m < la::kMaxMics; ++m) st->removeAttribute ("micDelayMs" + juce::String (m));   // per session
    if (monitorModeOn)   // the tuning mics as they are outside monitoring
    {
        st->setAttribute ("micCount", tuningSlot.count);
        st->setAttribute ("selectedMic", tuningSlot.selected);
        st->setAttribute ("micInput0", tuningSlot.input);
        st->setAttribute ("micSplOffset0", tuningSlot.offset);
        st->setAttribute ("micSplCalibrated0", tuningSlot.calibrated);
    }
    {
        auto mm = monitorMic;
        if (monitorModeOn)
        {
            mm.delayMs = settings.micDelayMs[0].load();
            mm.splOffset = settings.micSplOffset[0].load();
            mm.splCalibrated = settings.micSplCalibrated[0].load();
        }
        mm.save (*x);
    }

    mics.save (*x);
    saveMicCalibrations (*x);
    auto* pos = x->createNewChildElement ("Positions");
    pos->setAttribute ("roomAim", la::calSlotDegrees (roomAim));
    pos->setAttribute ("speakerAim", la::calSlotDegrees (speakerAim));
    for (const auto& p : positions)
    {
        auto* e = pos->createNewChildElement ("Position");
        e->setAttribute ("name", p);
    }
    for (const auto& p : monitorPoints)
    {
        auto* e = pos->createNewChildElement ("MonitorPoint");
        e->setAttribute ("name", p);
    }
    speakers.save (*x);
    system.save (*x);
    plan.save (*x);
    venuePrefs.save (*x);
    history.save (*x);

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
        e->setAttribute ("kind", s.kind == 1 ? "monitoring" : "tuning");
        if (! s.started) e->setAttribute ("started", false);
        auto body = (int) i == current ? sessionBody() : (s.body != nullptr ? std::make_unique<juce::XmlElement> (*s.body) : nullptr);
        if (body != nullptr)
            for (auto* child : body->getChildIterator()) e->addChildElement (new juce::XmlElement (*child));
    }
    return x;
}

void LiveAlignProcessor::loadProjectXml (const juce::XmlElement& x)
{
    const bool wasMonitoring = monitorModeOn;
    setMonitorMode (false);
    const int theme = settings.theme.load(), tab = settings.currentTab.load();
    resetVenue();
    if (const auto* venue = x.getChildByName ("Venue"))
    {
        venueName = venue->getStringAttribute ("name", "Untitled venue");
        venueNotes = venue->getStringAttribute ("notes");
        loopbackOutput = juce::jlimit (0, kMaxOutputs, venue->getIntAttribute ("loopbackOutput", 0));
    }
    if (const auto* st = x.getChildByName ("Settings"))
    {
        settings.loadVenue (*st);   // the mics, inputs and reference; not this computer's analysis and display preferences
        settings.theme = theme;
        settings.currentTab = tab;
    }
    settings.genOn = false;
    mics.load (x);
    loadMicCalibrations (x);
    if (const auto* pos = x.getChildByName ("Positions"))
    {
        positions.clear();
        // earlier versions kept an aim per position: the aim is now set by what is measured (room points up)
        roomAim = la::calSlotFromDegrees (pos->getIntAttribute ("roomAim", 90));
        speakerAim = la::calSlotFromDegrees (pos->getIntAttribute ("speakerAim", 0));
        for (const auto* p : pos->getChildWithTagNameIterator ("Position"))
            positions.add (p->getStringAttribute ("name"));
        monitorPoints.clear();
        for (const auto* p : pos->getChildWithTagNameIterator ("MonitorPoint"))
            monitorPoints.add (p->getStringAttribute ("name"));
        for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
    }
    speakers.load (x);
    system.load (x);
    plan.load (x);
    venuePrefs.load (x);
    history.load (x);
    monitorMic.load (x);
    // venues from before 0.19: the monitor mic's place (or FOH) is the monitoring point
    if (monitorPoints.isEmpty() && monitorMic.position.isNotEmpty()) monitorPoints.add (monitorMic.position);
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
            s.started = e->getBoolAttribute ("started", true);
            s.body = std::make_unique<juce::XmlElement> ("Body");
            for (const auto* child : e->getChildIterator()) s.body->addChildElement (new juce::XmlElement (*child));
            if (e->hasAttribute ("kind")) s.kind = e->getStringAttribute ("kind") == "monitoring" ? 1 : 0;
            else
            {
                // before 0.23 a session was both: one that only logged a show (no room or speaker work) is a monitoring one
                const auto* log = s.body->getChildByName ("SplLog");
                const auto* rt = s.body->getChildByName ("RoomTuning");
                const bool logged = log != nullptr && log->getAllSubText().trim().isNotEmpty();
                const bool tuned = rt != nullptr && rt->getChildByName ("Measurement") != nullptr;
                s.kind = logged && ! tuned ? 1 : 0;
            }
            sessions.push_back (std::move (s));
        }
        if (sessions.empty()) resetVenue();
        current = juce::jlimit (0, (int) sessions.size() - 1, list->getIntAttribute ("current", 0));
        loadSessionBody (sessions[(size_t) current].body.get());
        sessions[(size_t) current].body.reset();
        sessionKindChanged();
        // venues from before 0.18 kept the deepest cut with Room EQ: it is a venue preference now
        if (! venuePrefs.has ("eqMaxCut") && std::abs (room.maxCutDb - 12.0) > 0.01) venuePrefs.set ("eqMaxCut", room.maxCutDb);
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
    if (wasMonitoring) setMonitorMode (true);
}

juce::String LiveAlignProcessor::saveProject (const juce::File& f)
{
    const auto file = f.withFileExtension (projectExtension());
    auto x = createProjectXml();
    const auto text = x->toString();
    if (auto* v = x->getChildByName ("Venue")) v->setAttribute ("savedOn", juce::String (juce::Time::getCurrentTime().toMilliseconds()));
    if (! x->writeTo (file)) return "Could not write " + file.getFullPathName();
    projectFile = file;
    savedProjectText = text;
    changedCheckedAt = 0;
    la::venues::noteUsed (file);
    return {};
}

juce::String LiveAlignProcessor::openProject (const juce::File& f)
{
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName ("LiveAlignProject")) return f.getFileName() + " is not a LiveAlign venue file.";
    loadProjectXml (*x);
    projectFile = f;
    savedProjectText = createProjectXml()->toString();
    changedCheckedAt = 0;
    la::venues::noteUsed (f);
    venueOpen = true;
    return {};
}

void LiveAlignProcessor::newProject()
{
    resetVenue();
    venueOpen = true;
    settings.genOn = false;
    settings.requestResetAverages = true;
    settings.requestResetLeq = true;
    savedProjectText = createProjectXml()->toString();
    changedCheckedAt = 0;
}

void LiveAlignProcessor::newVenueFrom (const juce::String& name)
{
    const auto keepMics = mics;
    const auto keepCals = cals;
    const auto keepPositions = positions;
    const auto keepMonitorPoints = monitorPoints;
    const auto keepAim = positionAim;
    const int keepRoomAim = roomAim, keepSpeakerAim = speakerAim, keepLoopback = loopbackOutput;
    const auto keepPlan = plan;
    auto keepMonitor = monitorMic;
    if (monitorModeOn) { keepMonitor.delayMs = settings.micDelayMs[0].load(); keepMonitor.splOffset = settings.micSplOffset[0].load(); }
    auto keepSystem = system;
    juce::XmlElement keepSettings ("Settings");   // the mics' inputs, sound level and the reference
    settings.save (keepSettings);
    resetVenue();
    settings.loadVenue (keepSettings);
    for (int m = 0; m < la::kMaxMics; ++m) settings.micDelayMs[m] = 0.0f;
    monitorMic = keepMonitor;
    applyMonitorMic();
    venueName = name.trim().isNotEmpty() ? name.trim() : juce::String ("Untitled venue");
    mics = keepMics;
    cals = keepCals;
    for (int m = 0; m < la::kMaxMics; ++m) applyCalibration (m);
    positions = keepPositions;
    monitorPoints = keepMonitorPoints;
    positionAim = keepAim;
    roomAim = keepRoomAim;
    speakerAim = keepSpeakerAim;
    loopbackOutput = keepLoopback;
    plan = keepPlan;
    for (auto& s : keepSystem.sources)
    {
        s.response = {};
        s.imported = false;
        s.source.clear();
        s.delayUsedMs = s.offsetMs = s.arrivalMs = 0.0;
        s.hasArrival = false;
        s.proc.delayMs = 0.0;
        s.proc.eq.clear();
        s.nearField = false;
        s.trimDb = 0.0;
        s.sheetEntered.clear();
        s.asFound = {};
        s.asFoundWhen.clear();
        s.polarity.clear();
    }
    keepSystem.alignments.clear();
    keepSystem.lastResult = {};
    keepSystem.timingLocked = false;
    keepSystem.lockedDelayMs = 0.0;
    system = std::move (keepSystem);
    settings.genOn = false;
    settings.requestResetAverages = true;
    settings.requestResetLeq = true;
    savedProjectText = createProjectXml()->toString();
    venueOpen = true;
    changedCheckedAt = 0;
}

// Serialising the whole venue is not free (every trace, measurement and the level log), so the answer is kept for a
// second and a half: the pages ask several times a second. projectChangedNow() asks afresh (before a save or close).
bool LiveAlignProcessor::projectChanged() const
{
    const auto now = la::clock::ms32();
    if (changedCheckedAt != 0 && now - changedCheckedAt < 1500) return changedCache;
    return projectChangedNow();
}
bool LiveAlignProcessor::projectChangedNow() const
{
    changedCache = createProjectXml()->toString() != savedProjectText;
    changedCheckedAt = la::clock::ms32();
    return changedCache;
}

// ---------------------------------------------------------------------------- autosave

juce::File LiveAlignProcessor::recoveryFile() { return la::TraceStore::userFolder().getChildFile ("Autosave").getChildFile ("Recovery" + projectExtension()); }
juce::File LiveAlignProcessor::runningFlag() { return la::TraceStore::userFolder().getChildFile ("Autosave").getChildFile ("running.flag"); }

void LiveAlignProcessor::autosave()
{
    auto x = createProjectXml();
    const auto text = x->toString();
    if (text == savedProjectText)   // nothing unsaved: nothing to recover
    {
        if (lastAutosaveHash != 0) recoveryFile().deleteFile();
        lastAutosaveHash = 0;
        return;
    }
    if (text.hashCode() == lastAutosaveHash) return;
    x->setAttribute ("recoveredFrom", projectFile.getFullPathName());
    x->setAttribute ("autosaved", juce::Time::getCurrentTime().formatted ("%d %b %Y %H:%M"));
    const auto f = recoveryFile();
    f.getParentDirectory().createDirectory();
    const auto tmp = f.getSiblingFile ("Recovery.tmp");
    if (x->writeTo (tmp) && tmp.moveFileTo (f)) lastAutosaveHash = text.hashCode();
}

juce::String LiveAlignProcessor::restoreRecovery()
{
    const auto f = recoveryFile();
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName ("LiveAlignProject")) return "The autosaved venue could not be read.";
    loadProjectXml (*x);
    const auto from = x->getStringAttribute ("recoveredFrom");
    projectFile = from.isNotEmpty() && juce::File::isAbsolutePath (from) ? juce::File (from) : juce::File();
    savedProjectText.clear();   // restored work is unsaved until the user saves it
    changedCheckedAt = 0;
    venueOpen = true;
    return {};
}

void LiveAlignProcessor::clearRecovery()
{
    recoveryFile().deleteFile();
    lastAutosaveHash = 0;
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
        settings.currentTab = juce::jlimit (0, 16, x->getIntAttribute ("currentTab", 0));
        if (const auto* project = x->getChildByName ("LiveAlignProject"))
        {
            loadProjectXml (*project);
            const juce::String path = x->getStringAttribute ("projectFile");
            projectFile = path.isNotEmpty() ? juce::File (path) : juce::File();
            savedProjectText = x->getStringAttribute ("savedProjectText", createProjectXml()->toString());
            changedCheckedAt = 0;
        }
        else
        {
            // 0.1 / 0.2 state: settings and a calibration file path; traces came from the old folder
            settings.load (*x);
            loadMicCalibrations (*x);
            savedProjectText = createProjectXml()->toString();
            changedCheckedAt = 0;
        }
    }
    if (! startLevelApplied) { settings.genLevelDb = (float) la::AppPrefs::get().value ("genStartDb"); startLevelApplied = true; }
}

juce::AudioProcessorEditor* LiveAlignProcessor::createEditor()
{
    return new LiveAlignEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LiveAlignProcessor();
}

// ---- undo ----
namespace
{
void saveUndoCommon (const LiveAlignProcessor& p, juce::XmlElement& x)
{
    auto* v = x.createNewChildElement ("Venue");
    v->setAttribute ("name", p.venueName);
    v->setAttribute ("notes", p.venueNotes);
    v->setAttribute ("tempC", p.settings.temperatureC.load());
    v->setAttribute ("humidity", p.settings.humidity.load());
    v->setAttribute ("roomTarget", p.room.target);
    v->setAttribute ("roomUse", p.room.use);
    v->setAttribute ("lengthM", p.plan.lengthM);
    auto* pos = x.createNewChildElement ("Positions");
    pos->setAttribute ("tuning", p.positions.joinIntoString ("|"));
    pos->setAttribute ("monitoring", p.monitorPoints.joinIntoString ("|"));
    pos->setAttribute ("monitorMic", p.monitorMic.position);
    for (int m = 0; m < la::kMaxMics; ++m) pos->setAttribute ("mic" + juce::String (m), p.mics.position[(size_t) m]);
    p.plan.save (x);
    p.venuePrefs.save (x);
}
} // namespace

juce::String LiveAlignProcessor::undoFingerprint() const
{
    juce::XmlElement x ("U");
    saveUndoCommon (*this, x);
    x.addChildElement (la::ChangeLog::snapshot (system).release());
    auto* d = x.createNewChildElement ("Spk");
    for (const auto& s : system.sources)
        d->createNewChildElement ("S")->setAttribute ("d", s.name + "|" + s.model + "|" + juce::String (s.role) + "|" + juce::String (s.channel) + "|"
                                                         + juce::String (s.output) + "|" + juce::String (s.count) + "|" + juce::String (s.linkTo) + "|"
                                                         + juce::String (s.directivityQ));
    return juce::String::toHexString (x.toString (juce::XmlElement::TextFormat().singleLine()).hashCode64());
}

std::unique_ptr<juce::XmlElement> LiveAlignProcessor::undoState() const
{
    auto x = std::make_unique<juce::XmlElement> ("Undo");
    saveUndoCommon (*this, *x);
    system.save (*x);
    return x;
}

void LiveAlignProcessor::restoreUndoState (const juce::XmlElement& x)
{
    if (const auto* v = x.getChildByName ("Venue"))
    {
        venueName = v->getStringAttribute ("name", venueName);
        venueNotes = v->getStringAttribute ("notes");
        settings.temperatureC = (float) v->getDoubleAttribute ("tempC", settings.temperatureC.load());
        settings.humidity = (float) v->getDoubleAttribute ("humidity", settings.humidity.load());
        room.target = v->getIntAttribute ("roomTarget", room.target);
        room.use = v->getIntAttribute ("roomUse", room.use);
    }
    if (const auto* pos = x.getChildByName ("Positions"))
    {
        positions.clear();
        positions.addTokens (pos->getStringAttribute ("tuning"), "|", {});
        positions.removeEmptyStrings();
        monitorPoints.clear();
        monitorPoints.addTokens (pos->getStringAttribute ("monitoring"), "|", {});
        monitorPoints.removeEmptyStrings();
        monitorMic.position = pos->getStringAttribute ("monitorMic");
        for (int m = 0; m < la::kMaxMics; ++m) mics.position[(size_t) m] = pos->getStringAttribute ("mic" + juce::String (m));
    }
    plan.load (x);
    venuePrefs.load (x);
    la::SpeakerSystem was;
    was.load (x);
    if (was.sources.size() == system.sources.size())
    {
        // the same speakers: their set-up and processing come back; what was measured since stays
        for (size_t i = 0; i < was.sources.size(); ++i)
        {
            auto& to = was.sources[i];
            const auto& now = system.sources[i];
            to.response = now.response;
            to.imported = now.imported;
            to.source = now.source;
            to.delayUsedMs = now.delayUsedMs;
            to.offsetMs = now.offsetMs;
            to.hasArrival = now.hasArrival;
            to.arrivalMs = now.arrivalMs;
            to.nearField = now.nearField;
            to.measureMic = now.measureMic;
            to.polarity = now.polarity;
            to.asFound = now.asFound;
            to.asFoundProc = now.asFoundProc;
            to.asFoundWhen = now.asFoundWhen;
        }
        was.alignments = system.alignments;
    }
    system = was;
    syncPositionsWithPlan();
}

