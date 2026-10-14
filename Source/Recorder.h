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
    void start (int samples, int mics)
    {
        active = false;
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
        if (! active.load()) return;
        int p = pos.load();
        const int k = std::min (n, length - p);
        if (k <= 0) return;
        for (int m = 0; m < numMics; ++m) std::copy (mics[m], mics[m] + k, mic[(size_t) m].begin() + p);
        std::copy (reference, reference + k, ref.begin() + p);
        pos = p + k;
    }

    int numMics = 1, length = 0;
    std::array<std::vector<float>, kMaxMics> mic;
    std::vector<float> ref;

private:
    std::atomic<bool> active { false };
    std::atomic<int> pos { 0 };
};
} // namespace la
