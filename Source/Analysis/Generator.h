#pragma once
// Measurement signal generator: pink noise, white noise, sine, repeating log sweep and a polarity test pulse.
// Level convention (AES17): a full-scale sine is 0 dBFS, and noise at -20 dBFS has the same RMS as a -20 dBFS sine.
// Switching on and off ramps over 20 ms so there are no clicks through the PA.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "Fft.h"

namespace la
{
enum class GenType { Pink = 0, White = 1, Sine = 2, Sweep = 3, Pulse = 4 };

class Generator
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        rampStep = 1.0 / (0.02 * sr);
        rampUpStep = rampStep;
        resetState();
        pinkNorm = measurePinkNorm();
    }

    void setType (GenType newType) { if (newType != type) { type = newType; resetState(); } }
    GenType getType() const { return type; }
    void setLevelDb (double db) { levelDb = std::clamp (db, -90.0, 0.0); }
    void setFrequency (double hz) { freq = std::clamp (hz, 10.0, sr * 0.45); }
    void setSweepSeconds (double s) { sweepSec = std::clamp (s, 0.5, 30.0); }
    void setOn (bool on) { target = on ? 1.0 : 0.0; }
    // How long switching on takes (switching off stays quick, 20 ms).
    void setFadeInSeconds (double s) { rampUpStep = 1.0 / (std::max (0.02, s) * sr); }
    bool isOn() const { return target > 0.5; }
    bool isSilent() const { return target < 0.5 && gain <= 0.0; }

    void process (float* out, int count)
    {
        const double amp = std::pow (10.0, levelDb / 20.0);   // peak of a sine at this level
        const double rms = amp / std::sqrt (2.0);              // RMS for the noises
        for (int i = 0; i < count; ++i)
        {
            if (gain < target) gain = std::min (target, gain + rampUpStep);
            else if (gain > target) gain = std::max (target, gain - rampStep);

            double s = 0.0;
            if (gain > 0.0)
            {
                switch (type)
                {
                    case GenType::Pink:  s = pink() * pinkNorm * rms; break;
                    case GenType::White: s = white() * std::sqrt (3.0) * rms; break;   // uniform: RMS 1/sqrt(3)
                    case GenType::Sine:  s = sine() * amp; break;
                    case GenType::Sweep: s = sweep() * amp; break;
                    case GenType::Pulse: s = pulse() * amp; break;
                }
            }
            out[i] = (float) std::clamp (s * gain, -1.0, 1.0);
        }
    }

private:
    void resetState()
    {
        b0 = b1 = b2 = b3 = b4 = b5 = b6 = 0.0;
        phase = 0.0;
        t = 0;
    }

    double white()   // uniform in [-1, 1)
    {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return (double) (rng >> 11) * (2.0 / 9007199254740992.0) - 1.0;
    }

    double pink()   // Paul Kellet's refined pink filter
    {
        const double w = white();
        b0 = 0.99886 * b0 + w * 0.0555179;
        b1 = 0.99332 * b1 + w * 0.0750759;
        b2 = 0.96900 * b2 + w * 0.1538520;
        b3 = 0.86650 * b3 + w * 0.3104856;
        b4 = 0.55000 * b4 + w * 0.5329522;
        b5 = -0.7616 * b5 - w * 0.0168980;
        const double out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362;
        b6 = w * 0.115926;
        return out;
    }

    double measurePinkNorm()
    {
        const auto saved = rng;
        double sum = 0.0;
        const int count = 1 << 18;
        for (int i = 0; i < 4096; ++i) pink();   // settle
        for (int i = 0; i < count; ++i) { const double v = pink(); sum += v * v; }
        rng = saved;
        resetState();
        return 1.0 / std::sqrt (sum / count);
    }

    double sine()
    {
        const double v = std::sin (phase);
        phase += 2.0 * kPi * freq / sr;
        if (phase > 2.0 * kPi) phase -= 2.0 * kPi;
        return v;
    }

    double sweep()   // exponential sweep 20 Hz to 20 kHz (or 0.45 fs), then 0.5 s of silence, repeating
    {
        const double f1 = 20.0, f2 = std::min (20000.0, sr * 0.45);
        const int64_t len = (int64_t) (sweepSec * sr);
        const int64_t period = len + (int64_t) (0.5 * sr);
        const int64_t pos = t % period;
        ++t;
        if (pos >= len) return 0.0;
        const double time = pos / sr;
        const double k = std::log (f2 / f1);
        const double ph = 2.0 * kPi * f1 * sweepSec / k * (std::exp (time / sweepSec * k) - 1.0);
        const double fade = 0.005 * sr;
        double env = 1.0;
        if (pos < fade) env = 0.5 - 0.5 * std::cos (kPi * pos / fade);
        else if (len - pos < fade) env = 0.5 - 0.5 * std::cos (kPi * (len - pos) / fade);
        return std::sin (ph) * env;
    }

    double pulse()   // positive-going raised-cosine pulse (about 1 ms) twice a second, for polarity checks
    {
        const int64_t period = (int64_t) (0.5 * sr);
        const int64_t pos = t % period;
        ++t;
        const double width = 0.001 * sr;
        if (pos >= width) return 0.0;
        return 0.5 - 0.5 * std::cos (2.0 * kPi * pos / width);
    }

    double sr = 48000.0, levelDb = -20.0, freq = 1000.0, sweepSec = 3.0;
    double gain = 0.0, target = 0.0, rampStep = 0.001, rampUpStep = 0.001;
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, pinkNorm = 1.0;
    double phase = 0.0;
    int64_t t = 0;
    uint64_t rng = 0x9E3779B97F4A7C15ull;
    GenType type = GenType::Pink;
};
} // namespace la
