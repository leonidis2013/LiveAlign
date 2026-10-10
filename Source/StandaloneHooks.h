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
};
} // namespace la
