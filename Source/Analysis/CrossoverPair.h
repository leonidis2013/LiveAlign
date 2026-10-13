// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// Which two speakers the crossover is between, chosen from their roles (not their place in the list): the high
// side is the first main / top, the low side the first sub. Plain C++, unit tested.
#include <vector>

namespace la::xo
{
// Same numbering as la::SpeakerRole.
constexpr int kRoleMain = 0, kRoleSub = 1, kRoleFill = 2, kRoleDelay = 3, kRoleOther = 4;

struct Pair
{
    int high = -1, low = -1;
    bool valid (int count) const { return high != low && high >= 0 && low >= 0 && high < count && low < count; }
};

inline int firstWithRole (const std::vector<int>& roles, int role, int skip = -1)
{
    for (int i = 0; i < (int) roles.size(); ++i)
        if (roles[(size_t) i] == role && i != skip) return i;
    return -1;
}

// The usual pair for these speakers: first main / top above, first sub below. Without a main, the first speaker
// that is not a sub plays above; without a sub, the next speaker that is not the high one plays below.
inline Pair defaultPair (const std::vector<int>& roles)
{
    Pair p;
    const int n = (int) roles.size();
    if (n < 2) return { 0, n > 1 ? 1 : 0 };
    p.high = firstWithRole (roles, kRoleMain);
    p.low = firstWithRole (roles, kRoleSub);
    if (p.high < 0)
        for (int i = 0; i < n && p.high < 0; ++i)
            if (roles[(size_t) i] != kRoleSub && i != p.low) p.high = i;
    if (p.high < 0) p.high = p.low == 0 ? 1 : 0;   // only subs: sub to sub
    if (p.low < 0 || p.low == p.high)
    {
        p.low = -1;
        for (int i = 0; i < n && p.low < 0; ++i)
            if (i != p.high) p.low = i;
    }
    return p;
}

// The pair is upside down: a sub is above the crossover and a speaker that is not a sub below it.
// Deliberate pairs of the same kind (sub to sub, top to top) are fine.
inline bool pairInverted (const std::vector<int>& roles, const Pair& p)
{
    if (! p.valid ((int) roles.size())) return false;
    return roles[(size_t) p.high] == kRoleSub && roles[(size_t) p.low] != kRoleSub;
}
} // namespace la::xo
