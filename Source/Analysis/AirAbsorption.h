// SPDX-License-Identifier: AGPL-3.0-only
// Copyright (C) 2026 Jerod Ulicki

#pragma once
// The air between the speaker and a far seat: its absorption (ISO 9613-1) at the temperature and humidity of the day,
// and how much the delays drift when the temperature moves after they were set.
#include <cmath>

namespace la::air
{
// Absorption of sound by the air (dB per metre) at frequency f (Hz), temperature tC (Celsius), relative humidity rh (%)
// and pressure kPa (ISO 9613-1).
inline double absorptionDbPerM (double f, double tC, double rh, double kPa = 101.325)
{
    const double T = tC + 273.15, T0 = 293.15, T01 = 273.16, pr = 101.325;
    const double pa = kPa / pr;
    const double C = -6.8346 * std::pow (T01 / T, 1.261) + 4.6151;
    const double h = std::fmax (0.0, rh) * std::pow (10.0, C) / pa;
    const double frO = pa * (24.0 + 4.04e4 * h * (0.02 + h) / (0.391 + h));
    const double frN = pa * std::pow (T / T0, -0.5) * (9.0 + 280.0 * h * std::exp (-4.170 * (std::pow (T / T0, -1.0 / 3.0) - 1.0)));
    const double f2 = f * f;
    return 8.686 * f2 * (1.84e-11 / pa * std::sqrt (T / T0)
                         + std::pow (T / T0, -2.5) * (0.01275 * std::exp (-2239.1 / T) / (frO + f2 / frO)
                                                      + 0.1068 * std::exp (-3352.0 / T) / (frN + f2 / frN)));
}

inline double speedOfSound (double tC) { return 331.3 * std::sqrt (1.0 + tC / 273.15); }

// A delay set at one temperature, at another: the path difference it makes up for stays, the speed of sound does not.
// Returns the change (ms; positive: it now needs more delay).
inline double delayDriftMs (double delayMs, double setAtC, double nowC)
{
    if (! std::isfinite (delayMs) || ! std::isfinite (setAtC) || ! std::isfinite (nowC)) return 0.0;
    return delayMs * (speedOfSound (setAtC) / speedOfSound (nowC) - 1.0);
}
} // namespace la::air
