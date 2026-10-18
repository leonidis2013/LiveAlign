#pragma once
// Lets the editor reach the standalone app's audio device (set by StandaloneApp.cpp; empty in the VST3).
#include <functional>
#include <juce_core/juce_core.h>

namespace la
{
struct StandaloneHooks
{
    static std::function<void()>& showAudioSettings() { static std::function<void()> f; return f; }
    static std::function<juce::String()>& deviceDescription() { static std::function<juce::String()> f; return f; }
    static std::function<juce::String()>& deviceName() { static std::function<juce::String()> f; return f; }
    static std::function<void()>& toggleFullScreen() { static std::function<void()> f; return f; }
    static std::function<bool()>& isFullScreen() { static std::function<bool()> f; return f; }
    // The audio interface that was chosen but is gone (unplugged, switched off): its name, or empty when all is well.
    static std::function<juce::String()>& interfaceLost() { static std::function<juce::String()> f; return f; }
};
} // namespace la
