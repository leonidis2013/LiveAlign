// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The standalone app's audio interface, kept: when the interface goes away (unplugged, switched off), JUCE would open
// the computer's own audio in its place, and measuring there, or playing pink noise out of the laptop, by surprise is
// worse than stopping. So nothing else is opened: the audio is closed, the app says the interface is gone, and it is
// opened again as it was when it comes back. A change made in the Audio interface window is the new choice.
#include <functional>
#include <memory>
#include <juce_audio_devices/juce_audio_devices.h>

namespace la
{
class InterfaceGuard : private juce::ChangeListener
{
public:
    // `saved`: the last choice as saved (the "audioSetup" setting): if it is not here at start, JUCE has opened
    // another device instead. `userChanging`: the Audio interface window is open (a change there is the new choice).
    InterfaceGuard (juce::AudioDeviceManager& d, std::unique_ptr<juce::XmlElement> saved, int ins, int outs, std::function<bool()> userChanging)
        : dm (d), numIns (ins), numOuts (outs), choosing (std::move (userChanging)), wanted (std::move (saved))
    {
        dm.addChangeListener (this);
        check();
    }
    ~InterfaceGuard() override { dm.removeChangeListener (this); }

    juce::String lostName() const { return lost; }
    // The choice to keep saved while the interface is gone (JUCE would save the fallback, or nothing).
    std::unique_ptr<juce::XmlElement> savedChoice() const { return lost.isNotEmpty() && wanted != nullptr ? std::make_unique<juce::XmlElement> (*wanted) : nullptr; }

    // Looks again (on every change of the device manager: a device opened or closed, the list of devices changed).
    void check()
    {
        if (busy) return;
        const juce::ScopedValueSetter<bool> guard (busy, true);
        update();
    }

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override { check(); }

    juce::String wantedName() const
    {
        if (wanted == nullptr) return {};
        const auto in = wanted->getStringAttribute ("audioInputDeviceName"), out = wanted->getStringAttribute ("audioOutputDeviceName");
        return in.isNotEmpty() ? in : out;
    }

    bool wantedHere() const
    {
        if (wanted == nullptr) return true;
        const auto type = wanted->getStringAttribute ("deviceType");
        const auto in = wanted->getStringAttribute ("audioInputDeviceName"), out = wanted->getStringAttribute ("audioOutputDeviceName");
        for (auto* t : dm.getAvailableDeviceTypes())
        {
            if (type.isNotEmpty() && t->getTypeName() != type) continue;
            const auto ins = t->getDeviceNames (true), outs = t->getDeviceNames (false);
            if ((in.isEmpty() || ins.contains (in)) && (out.isEmpty() || outs.contains (out))) return true;
        }
        return false;
    }

    bool currentIsWanted() const
    {
        auto* dev = dm.getCurrentAudioDevice();
        if (dev == nullptr || wanted == nullptr) return false;
        const auto setup = dm.getAudioDeviceSetup();
        const auto in = wanted->getStringAttribute ("audioInputDeviceName"), out = wanted->getStringAttribute ("audioOutputDeviceName");
        return (in.isEmpty() || setup.inputDeviceName == in) && (out.isEmpty() || setup.outputDeviceName == out);
    }

    void update()
    {
        auto* dev = dm.getCurrentAudioDevice();
        // the person picked a device in the Audio interface window: that is the choice now
        if (choosing && choosing())
        {
            if (dev != nullptr) { wanted = dm.createStateXml(); lost.clear(); }
            return;
        }
        if (wanted == nullptr || wantedName().isEmpty())
        {
            if (dev != nullptr) wanted = dm.createStateXml();
            return;
        }
        if (! wantedHere())
        {
            // gone: close whatever JUCE opened in its place, and say so
            if (dev != nullptr && ! currentIsWanted()) dm.closeAudioDevice();
            else if (dev != nullptr) return;   // still open (the list is being rebuilt)
            lost = wantedName();
            reopened = false;
            return;
        }
        if (lost.isNotEmpty())
        {
            // back: open it as it was (once each time it appears: a failed open is not retried in a loop)
            if (! currentIsWanted() && ! reopened)
            {
                reopened = true;
                dm.closeAudioDevice();
                dm.initialise (numIns, numOuts, wanted.get(), false);
            }
            if (currentIsWanted()) lost.clear();
            return;
        }
        if (dev != nullptr) wanted = dm.createStateXml();   // channels, rate, buffer as they are now
    }

    juce::AudioDeviceManager& dm;
    const int numIns, numOuts;
    std::function<bool()> choosing;
    std::unique_ptr<juce::XmlElement> wanted;
    juce::String lost;
    bool busy = false, reopened = false;
};
} // namespace la
