// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Listening: hear the reference (or a mic) on headphones. Either on a spare output of the measurement interface (the
// processor writes it there), or on another audio device altogether (the computer's headphone jack, a USB headset),
// so the measurement interface keeps its inputs and its outputs for the PA and the test signals.
//
// Two devices run on two clocks: ListenBridge carries the audio from the measurement interface's callback to the
// listening device's, through a lock-free FIFO, and resamples it, nudging the rate by a hair to keep the FIFO near
// half full (no clicks from the clocks drifting apart; a gap only if a device stalls).
#include <atomic>
#include <cmath>
#include <vector>
#include <juce_core/juce_core.h>
#include <juce_audio_devices/juce_audio_devices.h>

namespace la
{
class ListenBridge
{
public:
    ListenBridge() : fifo (kFifo)
    {
        data.assign ((size_t) kFifo, 0.0f);
        pending.reserve ((size_t) kFifo * 2);   // never grows on the audio thread (at most the FIFO plus four targets)
    }

    // Measurement interface's audio thread.
    void setSourceRate (double r) { if (r > 0.0) srcRate = r; }
    void push (const float* x, int n, float gain)
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        for (int i = 0; i < n1; ++i) data[(size_t) (s1 + i)] = x[i] * gain;
        for (int i = 0; i < n2; ++i) data[(size_t) (s2 + i)] = x[n1 + i] * gain;
        fifo.finishedWrite (n1 + n2);   // when full, the newest is dropped: the reader catches up below
    }

    // Listening device's audio thread: `n` samples at `dstRate`.
    void pull (float* out, int n, double dstRate)
    {
        // take everything waiting (after a reset, what was waiting is old: dropped)
        const int avail = fifo.getNumReady();
        if (avail > 0 && drain.exchange (false))
        {
            int s1, n1, s2, n2;
            fifo.prepareToRead (avail, s1, n1, s2, n2);
            fifo.finishedRead (n1 + n2);
        }
        else if (avail > 0)
        {
            int s1, n1, s2, n2;
            fifo.prepareToRead (avail, s1, n1, s2, n2);
            for (int i = 0; i < n1; ++i) pending.push_back (data[(size_t) (s1 + i)]);
            for (int i = 0; i < n2; ++i) pending.push_back (data[(size_t) (s2 + i)]);
            fifo.finishedRead (n1 + n2);
        }
        const double target = juce::jmax (1024.0, srcRate * 0.04);   // about 40 ms buffered
        // far behind (a device stalled, or the source rate is much higher): skip ahead to the target
        if ((double) pending.size() > target * 4.0) { pending.erase (pending.begin(), pending.end() - (long) target); pos = 0.0; }
        // waiting to fill after a gap: silence until half the target is there
        if (refilling && (double) pending.size() < target * 0.5) { std::fill (out, out + n, 0.0f); ++underruns; return; }
        refilling = false;
        // a gentle pull toward the target fill: at most 0.05 % faster or slower, inaudible
        const double fill = ((double) pending.size() - target) / target;
        const double step = srcRate / juce::jmax (1.0, dstRate) * (1.0 + 0.0005 * juce::jlimit (-1.0, 1.0, fill));
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) pos;
            if (k + 1 >= pending.size()) { std::fill (out + i, out + n, 0.0f); refilling = true; ++underruns; break; }
            const double f = pos - (double) k;
            out[i] = (float) (pending[k] * (1.0 - f) + pending[k + 1] * f);
            pos += step;
        }
        const auto used = juce::jmin ((size_t) pos, pending.size());
        pending.erase (pending.begin(), pending.begin() + (long) used);
        pos -= (double) used;
        lastFill = (int) pending.size();
    }

    // From the listening side only (the writer keeps writing): start again from fresh audio.
    void reset() { pending.clear(); pos = 0.0; refilling = true; drain = true; }
    int bufferedSamples() const { return lastFill; }
    std::atomic<int> underruns { 0 };

private:
    static constexpr int kFifo = 1 << 16;   // 1.4 s at 48 kHz
    juce::AbstractFifo fifo;
    std::vector<float> data;
    std::vector<float> pending;   // listening thread only
    double pos = 0.0, srcRate = 48000.0;
    bool refilling = true;
    std::atomic<bool> drain { true };
    std::atomic<int> lastFill { 0 };
};

// The listening device: another output device opened on its own (output only), fed from the bridge. Opened and
// closed on the message thread.
class ListenDevice : private juce::AudioIODeviceCallback
{
public:
    ListenBridge bridge;
    ~ListenDevice() { close(); }

    // The output devices on this computer, as "Type: Name" (the measurement interface among them too).
    juce::StringArray deviceNames()
    {
        juce::StringArray out;
        for (auto* t : dm.getAvailableDeviceTypes())
        {
            t->scanForDevices();
            for (const auto& n : t->getDeviceNames (false)) out.add (t->getTypeName() + ": " + n);
        }
        return out;
    }

    // Opens "Type: Name" (empty closes). Returns an error, or empty.
    juce::String open (const juce::String& typeAndName, double preferredRate)
    {
        close();
        if (typeAndName.isEmpty()) return {};
        const auto type = typeAndName.upToFirstOccurrenceOf (": ", false, false), name = typeAndName.fromFirstOccurrenceOf (": ", false, false);
        dm.setCurrentAudioDeviceType (type, true);
        if (dm.getCurrentDeviceTypeObject() == nullptr) return "No " + type + " audio on this computer";
        auto err = dm.initialise (0, 2, nullptr, false);
        if (err.isNotEmpty()) return err;
        juce::AudioDeviceManager::AudioDeviceSetup setup = dm.getAudioDeviceSetup();
        setup.outputDeviceName = name;
        setup.inputDeviceName = {};
        setup.useDefaultInputChannels = false;
        setup.inputChannels.clear();
        setup.useDefaultOutputChannels = true;
        if (preferredRate > 0.0) setup.sampleRate = preferredRate;   // the same rate if it can (no rate change, only the drift)
        err = dm.setAudioDeviceSetup (setup, true);
        if (err.isNotEmpty()) { dm.closeAudioDevice(); return err; }
        if (dm.getCurrentAudioDevice() == nullptr) return "Could not open " + name;
        bridge.reset();
        dm.addAudioCallback (this);
        openName = typeAndName;
        return {};
    }
    void close()
    {
        dm.removeAudioCallback (this);
        dm.closeAudioDevice();
        openName.clear();
    }
    bool isOpen() const { return openName.isNotEmpty(); }
    juce::String description()
    {
        auto* d = dm.getCurrentAudioDevice();
        if (d == nullptr || openName.isEmpty()) return {};
        return d->getName() + ", " + juce::String (juce::roundToInt (d->getCurrentSampleRate() / 100.0) / 10.0) + " kHz";
    }

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n, const juce::AudioIODeviceCallbackContext&) override
    {
        if (numOut <= 0) return;
        bridge.pull (out[0], n, rate);
        for (int c = 1; c < numOut; ++c) std::copy (out[0], out[0] + n, out[c]);   // the same in both ears
    }
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override { rate = d->getCurrentSampleRate(); bridge.reset(); }
    void audioDeviceStopped() override {}

    juce::AudioDeviceManager dm;
    juce::String openName;
    double rate = 48000.0;
};
} // namespace la
