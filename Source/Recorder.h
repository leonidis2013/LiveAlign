// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// A plain recording of the mics and the reference for a set time (a sweep for room acoustics). The message thread
// arms it with start(); the audio thread fills it; done() says when it is full. No locking on the audio thread.
#include <algorithm>
#include <array>
#include <atomic>
#include <vector>
#include "Settings.h"

namespace la
{
class Recorder
{
public:
    // Message thread. The audio thread may be inside push() when this is called: it is told to stop, given a moment to
    // leave (push() marks itself busy), and only then are the buffers resized.
    void start (int samples, int mics)
    {
        active = false;
        for (int spins = 0; busy.load() && spins < 2000; ++spins) juce::Thread::sleep (1);
        numMics = std::max (1, std::min (mics, kMaxMics));
        for (int m = 0; m < numMics; ++m) mic[(size_t) m].assign ((size_t) samples, 0.0f);
        ref.assign ((size_t) samples, 0.0f);
        length = samples;
        pos = 0;
        active = true;
    }
    void stop() { active = false; }
    bool running() const { return active.load() && pos.load() < length; }
    bool done() const { return length > 0 && pos.load() >= length; }
    float progress() const { return length > 0 ? (float) pos.load() / (float) length : 0.0f; }

    // Audio thread.
    void push (const float* const* mics, const float* reference, int n)
    {
        struct Busy { std::atomic<bool>& b; explicit Busy (std::atomic<bool>& x) : b (x) { b = true; } ~Busy() { b = false; } } guard (busy);
        if (! active.load()) return;
        const int len = length.load(), nm = numMics.load();
        int p = pos.load();
        const int k = std::min (n, len - p);
        if (k <= 0) return;
        for (int m = 0; m < nm; ++m) std::copy (mics[m], mics[m] + k, mic[(size_t) m].begin() + p);
        std::copy (reference, reference + k, ref.begin() + p);
        pos = p + k;
    }

    std::atomic<int> numMics { 1 }, length { 0 };
    std::atomic<bool> busy { false };
    std::array<std::vector<float>, kMaxMics> mic;
    std::vector<float> ref;

private:
    std::atomic<bool> active { false };
    std::atomic<int> pos { 0 };
};
} // namespace la
