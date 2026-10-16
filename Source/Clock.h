// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The clock every timed step reads (count-ins, settling, averaging, timeouts). Real time in the app; the screenshot
// tool sets it from the audio it has fed, so a run does not depend on how fast the computer is.
#include <atomic>
#include <juce_core/juce_core.h>

namespace la::clock
{
inline std::atomic<double>& simulated() { static std::atomic<double> v { -1.0 }; return v; }

// Milliseconds, as juce::Time::getMillisecondCounterHiRes().
inline double ms()
{
    const double s = simulated().load();
    return s >= 0.0 ? s : juce::Time::getMillisecondCounterHiRes();
}
// Milliseconds, as juce::Time::getMillisecondCounter().
inline juce::uint32 ms32() { return (juce::uint32) (juce::int64) ms(); }

// The screenshot tool: from now on the clock is this (and moves only when it says so).
inline void setSimulated (double milliseconds) { simulated() = milliseconds; }
} // namespace la::clock
