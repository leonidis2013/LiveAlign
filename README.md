# LiveAlign

Sound system tuning and live monitoring: a dual-channel transfer function (magnitude, phase, coherence), RTA,
impulse response, delay finder, sound level meter and a signal generator. Built with JUCE as a **standalone app**
(for tuning: it drives the PA and measures through your mic) and a **VST3** (for live monitoring in a host such as
LiveProfessor). Windows and macOS.

**Version 0.3.0**: guided workflow tabs, the Mic setup checklist, venue project files, JUCE 9 and ASIO on Windows

## What you need

- A measurement mic (flat, omni) on an audio interface input, ideally with its calibration file.
- A **reference** signal. The most accurate is a loopback: split the output that feeds the PA (or your speaker processor's
  input) back into a second interface input. If you only use the generator, Settings > Reference > *Generator
  (internal copy)* works without a cable.

## Workflow

LiveAlign walks through setting up and tuning a live sound system in four tabs. You can jump to any tab at any time;
each one shows its progress and has Back / Next buttons.

| Tab | Status | What it does |
|-----|--------|--------------|
| **1 Mic setup** | Built | A checklist: audio interface, inputs, mic calibration file, sound level calibration, reference loopback check, room noise floor |
| **2 Speakers** | Next stage | Build the system, speaker library, measure and overlay each speaker or group, crossover recommendation, settings summary |
| **3 Room tuning** | Later | Several mic positions averaged, target curve and suggested EQ, alignment at the seats, ring-out, room acoustics |
| **4 Monitoring** | Built | RTA, transfer function, impulse response, sound level, delay finder and traces, for use during the event |

The **VST3** shows the Monitoring tab only, since it runs inside a host during the event.

### 1 Mic setup

Each step turns green when done (amber when done with a warning). Steps you cannot do can be skipped.

1. **Audio interface:** pick the interface (app only).
2. **Inputs:** mic input and reference. It checks the mic input is live and not the same input as the reference.
3. **Mic calibration file:** load the file that came with the mic, or mark that it has none.
4. **Sound level calibration:** 94 or 114 dB calibrator, with a live reading; or carry on in dBFS.
5. **Reference loopback check:** plays pink noise at -20 dBFS for 2 seconds from the generator output and finds it on
   the reference input. Reports the interface round trip, polarity (warns if inverted) and level (warns below -36
   or above -3 dBFS). Turn the amps down if you don't want to hear it. Not needed when the reference is the
   generator's internal copy.
6. **Room noise floor:** with the generator off, measures 5 seconds of background noise (LAeq and LCeq), says how
   loud the test signal should be (15 dB above it), and keeps the spectrum as the RTA trace "Room noise floor".

The RTA and input meters sit beside the checklist so you can see what the mic hears.

### Venue projects

**Project** (top bar) > New venue, Open venue, Save (`Cmd/Ctrl+S`), Save as, Venue name and notes. A `.lavenue` file
holds the venue name and notes, the settings, the mic calibration (the data itself, so it works on another computer),
the traces, the target curve and the checklist. The theme is not stored in projects. The top bar shows the venue name
and "Unsaved changes" when there is something to save. The app also remembers the open project between sessions.

Traces now live in the project. Traces saved by version 0.1 or 0.2 (in `LiveAlign/Traces`) are loaded once.

## Monitoring tab

### Layout

The Monitoring tab shows **one, two or three graphs** (Settings > Screen), stacked or side by side. Each graph has
its own RTA / Transfer / Impulse buttons, so the RTA, transfer function and impulse response can all be on screen at
once. **Full screen** (app only, key `F`, `Esc` to leave) fills the whole display.

## Views

| View | What it shows |
|------|---------------|
| **Transfer** (key `2`) | Mic against reference: magnitude (top), phase (bottom) and coherence (pink, top third of the magnitude graph, 0 to 100 %). Points below the coherence blanking threshold are hidden. Hover for a readout; drag up and down to shift the magnitude, double-click or **Centre** to put it on 0 dB |
| **RTA** (key `1`) | The mic in 1/1 to 1/48 octave bands, in dB SPL once calibrated (dBFS before). Optional reference overlay and peak hold |
| **Impulse** (key `3`) | Impulse response, linear or log (ETC), 20 / 100 / 500 ms. Shows the arrival time and distance, and warns when the polarity looks inverted |

Top bar on this tab: **Running** (green) / **Frozen** (amber) with `Space` freezes the graphs (the sound level keeps
running), **Reset avg** (`R`) restarts the averages. Keys `1`, `2`, `3` switch the top graph.

## Right-hand column

- **Sound level:** dBA slow, A fast, C slow, Z slow, LAeq, LCeq and LAF max since the last reset. A, C and Z
  weighting follow the IEC 61672 curves. **Calibrate** opens the calibration pop-up (below) so these read dB SPL.
- **Inputs:** level of the mic and the reference, with peak hold and a clip light.
- **Delay compensation:** **Find delay** (`D`) listens to the last ~3 seconds, finds how late the mic hears the
  reference and sets the compensation so the phase trace flattens. It also reports polarity. Nudge by 0.1 or 1 ms.
  Distance uses the speed of sound at the temperature set in Settings (Celsius or Fahrenheit).
- **Traces:** **Capture** (`C`) saves the transfer function or RTA on screen. Click the colour square to show or
  hide it, double-click to rename, and the `...` menu exports it as an FRD file (for REW, VituixCAD and similar)
  or deletes it. **Import** reads FRD, TXT or CSV files (frequency, dB, optional phase) from other software or a
  speaker manufacturer. Traces are kept in your user folder (`LiveAlign/Traces`).
- **Target:** draws a target curve with a +/-3 dB band over the transfer function and the RTA, lined up with the
  measurement's 500 Hz to 2 kHz average. Built in: Flat, Speech, Gentle tilt (-1 dB/octave), Worship / live music,
  Concert / high energy, Cinema X-curve (SMPTE ST 202) and Harman-style in-room (approx.). Only the X-curve is a
  published standard; the others are common starting points, not rules.

## Generator

Pink noise, white noise, sine, repeating log sweep (1 to 10 s) and a positive polarity pulse. Level is RMS in dBFS
(a full-scale sine is 0 dBFS). It always starts **off**, fades in and out over 20 ms, and the button turns red while
it plays. Choose the output it plays from. `G` toggles it.

In the **app**, outputs carry only the generator; inputs are never passed through, so the mic cannot feed back into
the PA. In the **VST3**, audio passes through untouched unless the generator is driving a channel.

## Settings

Everything is in one Settings window, in sections:

- **Inputs:** audio device (app only), measurement mic input, reference source, mic calibration.
- **Transfer function:** FFT size (8k to 64k), averaging, smoothing, coherence blanking, magnitude range.
- **RTA:** resolution (1/1 to 1/48 octave), speed, reference overlay, peak hold.
- **Screen:** number of graphs, stacked or side by side, theme.
- **Impulse response:** linear or log (ETC), time range.
- **Environment:** temperature (Celsius or Fahrenheit), distance in feet or metres.

## Calibration

**Calibrate** in the Sound level box (or Settings > Mic calibration) opens the calibration pop-up:

- **Sound level:** fit a calibrator to the mic and switch it on, pick its level (94 dB, 114 dB or another value),
  wait for the live reading to settle, then press **Calibrate**. The offset can also be set by hand.
- **Mic calibration file:** plain text, one `frequency dB [phase]` per line. miniDSP UMIK files (with the
  `Sens Factor` header), Dayton and REW-style files, comma or tab separated, and decimal-comma files all load.
  The correction is applied to the RTA, the sound level and the transfer function.

## Keys

`Cmd/Ctrl+S` save project, `G` generator, `F` full screen, `Esc` leave full screen. On the Monitoring tab:
`1` / `2` / `3` top graph view, `Space` freeze, `R` reset averages, `D` find delay, `C` capture (whatever the graphs
show).

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
  (ASIO, WASAPI or DirectSound).
- VST3: copy `LiveAlign.vst3` to `C:\Program Files\Common Files\VST3` and rescan in your host.

## Building

GitHub Actions builds the Windows and macOS (universal) app and VST3 on every push to main and publishes them on the
Releases page. Local build (CMake 3.22 or newer; JUCE 9.0.3 is downloaded automatically):

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
meter, generator levels and pink-noise slope, calibration / FRD / CSV file parsing, and the target curves. CI runs them with address and
undefined-behaviour sanitizers before building.

`-DLA_BUILD_PREVIEW=ON` builds a local-only tool that feeds a simulated PA and room through the processor and saves
screenshots of every view.

## Known limits

- The transfer function uses one FFT size for all frequencies (8k to 64k). Low frequencies need the bigger sizes;
  a multi-window FFT like Smaart's is a possible later improvement.
- Pink noise uses Paul Kellet's filter, designed for 44.1 kHz; at 96 kHz the slope below about 20 Hz differs slightly.
- ASIO on Windows uses the ASIO headers bundled with JUCE 9; Steinberg's licence terms for them apply.
- The VST3 can only measure what the host routes to its inputs (input 1 mic, input 2 reference by default).
- Built with JUCE 9, which is licensed under AGPLv3 or the commercial JUCE 9 licence. Keeping LiveAlign for your own
  use is unaffected; if you ever distribute or sell it, you either release the source under AGPLv3 or need a
  commercial JUCE licence. See [JUCE's licensing page](https://juce.com/get-juce/#licensing-faq).

## Roadmap

- **Tab 2, Speakers:** any speakers and any speaker management system. Speaker library (spec sheet values, onboard
  DSP settings, imported EASE metadata, FRD / CSV / CLF data and in-app measurements), measuring and overlaying each
  speaker or group, a crossover designer that can be limited to the filter types and slopes your processor offers,
  top / sub phase alignment, and a settings summary to enter in your processor.
- **Tab 3, Room tuning:** multi-position averaging, target curves with suggested EQ, alignment at the seats,
  feedback ring-out, room acoustics (RT60, EDT, C50/C80, room modes).
- **Tab 4, Monitoring additions:** spectrogram, delay tracking, trace offsets, SPL logging with alarms, PDF reports.
