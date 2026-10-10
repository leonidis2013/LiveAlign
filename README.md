# LiveAlign

Sound system tuning and live monitoring: a dual-channel transfer function (magnitude, phase, coherence), RTA,
impulse response, delay finder, sound level meter and a signal generator. Built with JUCE as a **standalone app**
(for tuning: it drives the PA and measures through your mic) and a **VST3** (for live monitoring in a host such as
LiveProfessor). Windows and macOS.

**Version 0.1.0** (stage 1: the measurement core)

## What you need

- A measurement mic (flat, omni) on an audio interface input, ideally with its calibration file.
- A **reference** signal. The most accurate is a loopback: split the output that feeds the PA (or the DriveRack PA2
  input) back into a second interface input. If you only use the generator, Settings > Reference > *Generator
  (internal copy)* works without a cable.

## Views

| View | What it shows |
|------|---------------|
| **Transfer** (key `2`) | Mic against reference: magnitude (top), phase (bottom) and coherence (pink, top third of the magnitude graph, 0 to 100 %). Points below the coherence blanking threshold are hidden. Hover for a readout; drag up and down to shift the magnitude, double-click or **Centre** to put it on 0 dB |
| **RTA** (key `1`) | The mic in 1/1 to 1/48 octave bands, in dB SPL once calibrated (dBFS before). Optional reference overlay and peak hold |
| **Impulse** (key `3`) | Impulse response, linear or log (ETC), 20 / 100 / 500 ms. Shows the arrival time and distance, and warns when the polarity looks inverted |

Header controls: **Running / Frozen** (`Space`) freezes the graphs (the sound level keeps running), averages,
smoothing (transfer) or bands per octave (RTA), **Reset** (`R`) restarts the averages.

## Right-hand column

- **Sound level:** dBA slow, A fast, C slow, Z slow, LAeq, LCeq and LAF max since the last reset. A, C and Z
  weighting follow the IEC 61672 curves. Calibrate in Settings so these read dB SPL.
- **Inputs:** level of the mic and the reference, with peak hold and a clip light.
- **Delay compensation:** **Find delay** (`D`) listens to the last ~3 seconds, finds how late the mic hears the
  reference and sets the compensation so the phase trace flattens. It also reports polarity. Nudge by 0.1 or 1 ms.
  Distance uses the speed of sound at the temperature set in Settings.
- **Traces:** **Capture** (`C`) saves the transfer function or RTA on screen. Click the colour square to show or
  hide it, double-click to rename, and the `...` menu exports it as an FRD file (for REW, VituixCAD and similar)
  or deletes it. **Import** reads FRD, TXT or CSV files (frequency, dB, optional phase) from other software or a
  speaker manufacturer. Traces are kept in your user folder (`LiveAlign/Traces`).

## Generator

Pink noise, white noise, sine, repeating log sweep (1 to 10 s) and a positive polarity pulse. Level is RMS in dBFS
(a full-scale sine is 0 dBFS). It always starts **off**, fades in and out over 20 ms, and the button turns red while
it plays. Choose the output it plays from. `G` toggles it.

In the **app**, outputs carry only the generator; inputs are never passed through, so the mic cannot feed back into
the PA. In the **VST3**, audio passes through untouched unless the generator is driving a channel.

## Settings

Audio device (app only), measurement mic input, reference source, FFT size (8k to 64k), coherence blanking,
magnitude range, RTA speed, reference overlay, peak hold, mic calibration file, SPL calibration (94 or 114 dB
calibrator, or type the offset), temperature, feet or metres, theme.

**Mic calibration files:** plain text, one `frequency dB [phase]` per line. miniDSP UMIK files (with the
`Sens Factor` header), Dayton and REW-style files, comma or tab separated, and decimal-comma files all load.
The correction is applied to the RTA, the sound level and the transfer function.

**SPL calibration:** fit a calibrator to the mic, switch it on, open Settings and press **Set 94 dB** (or 114 dB).

## Keys

`1` / `2` / `3` views, `Space` freeze, `R` reset averages, `D` find delay, `C` capture, `G` generator.

## Installing

Download the latest build from the [Releases page](https://github.com/leonidis2013/LiveAlign/releases/tag/latest)
("Latest build"): `LiveAlign-macOS-App.zip`, `LiveAlign-macOS-VST3.zip` (universal: Apple silicon and Intel),
`LiveAlign-Windows-App.zip` and `LiveAlign-Windows-VST3.zip`.

### macOS

The builds are ad-hoc signed, not notarised, so remove the download quarantine flag once in Terminal.

App:

1. Unzip `LiveAlign-macOS-App.zip` and move `LiveAlign.app` to `/Applications`.
2. Run `xattr -dr com.apple.quarantine /Applications/LiveAlign.app`
3. Open it. Allow microphone access when macOS asks (System Settings > Privacy & Security > Microphone if you
   missed the prompt).
4. Settings > **Audio device...** to pick your interface, sample rate and channels.

VST3:

1. Unzip `LiveAlign-macOS-VST3.zip` and copy `LiveAlign.vst3` to `~/Library/Audio/Plug-Ins/VST3`.
2. Run `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/LiveAlign.vst3`
3. Rescan plug-ins in your host.

### Windows

- App: unzip `LiveAlign-Windows-App.zip` and run `LiveAlign.exe`. Allow microphone access for desktop apps in
  Windows Settings > Privacy & security > Microphone. Choose the interface under Settings > **Audio device...**
  (WASAPI or DirectSound; see Known limits).
- VST3: copy `LiveAlign.vst3` to `C:\Program Files\Common Files\VST3` and rescan in your host.

## Building

GitHub Actions builds the Windows and macOS (universal) app and VST3 on every push to main and publishes them on the
Releases page. Local build (CMake 3.22 or newer; JUCE 8.0.8 is downloaded automatically):

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target LiveAlign_Standalone LiveAlign_VST3
```

## Tests

The measurement engine is plain C++ without JUCE and is tested with known signals:

```
g++ -std=c++17 -O2 -ISource Tests/AnalysisTests.cpp -o analysis_tests && ./analysis_tests
```

They cover the FFT, RTA levels against a calibrated sine and white noise, the transfer function of a known gain and
delay and of a Butterworth filter (magnitude and phase against theory), coherence at 0 dB signal-to-noise, the delay
finder (full range, inverted polarity, a sub-like band-limited path, silence), A and C weighting, the sound level
meter, generator levels and pink-noise slope, and calibration / FRD / CSV file parsing. CI runs them with address and
undefined-behaviour sanitizers before building.

`-DLA_BUILD_PREVIEW=ON` builds a local-only tool that feeds a simulated PA and room through the processor and saves
screenshots of every view.

## Known limits

- The transfer function uses one FFT size for all frequencies (8k to 64k). Low frequencies need the bigger sizes;
  a multi-window FFT like Smaart's is a possible later improvement.
- Pink noise uses Paul Kellet's filter, designed for 44.1 kHz; at 96 kHz the slope below about 20 Hz differs slightly.
- On Windows the app uses WASAPI or DirectSound. ASIO is not included.
- The VST3 can only measure what the host routes to its inputs (input 1 mic, input 2 reference by default).
- JUCE is licensed under AGPLv3 or a commercial licence. That matters if you distribute LiveAlign to others.

## Roadmap

- **Stage 2, tuning tools:** speaker library (spec sheet values, onboard DSP settings, imported FRD / CSV / CLF
  data and in-app measurements), crossover designer limited to the filters the DriveRack PA2 offers, phase
  alignment of tops and subs, target curves with suggested EQ, and a PA2 settings page in its menu order.
- **Stage 3, live and room tools:** feedback detection and ring-out, room acoustics (RT60, EDT, C50/C80, room
  modes, noise floor), SPL logging with alarms, multi-position averaging, and PDF reports.
