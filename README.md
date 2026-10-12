# LiveAlign

Sound system tuning and live monitoring: a dual-channel transfer function (magnitude, phase, coherence), RTA,
impulse response, delay finder, sound level meter and a signal generator. Built with JUCE as a **standalone app**
(for tuning: it drives the PA and measures through your mic) and a **VST3** (for live monitoring in a host such as
LiveProfessor). Windows and macOS.

**Version 0.5.0**: the Speakers step (measure each speaker with one mic or import its response, crossover designer for any processor, settings summary), a waterfall view on Monitoring, amber outlines on required steps that are not done yet, and a slimmer navigation pane

## What you need

- One or more measurement mics (flat, omni; up to eight) on audio interface inputs, ideally with their calibration files.
- A **reference** signal. The most accurate is a loopback: split the output that feeds the PA (or your speaker processor's
  input) back into a second interface input. If you only use the generator, Settings > Reference > *Generator
  (internal copy)* works without a cable.

## Workflow

Everything runs from the **navigation pane** on the left. Each step has its own icon and a live status: a green tick
when it is ready, an amber `!` when it needs attention, a progress ring while it is under way, and a dash for steps
that come in a later stage. The venue and session are at the bottom of the pane, with Settings and Full screen.
Steps that are required but not done yet have an amber outline (on Home and on the Mic setup checklist); click
anywhere on a step on the Home page to open it. The generator strip only appears on the pages that play test
signals (Speakers, Room tuning, Monitoring); leaving for the other pages stops it.

| Page | Status | What it does |
|------|--------|--------------|
| **Home** | Built | Venue name and notes, the venue's sessions, and where each step stands |
| **1 Mic setup** | Built | Mic table and a checklist: interface, reference, mics connected, calibration files, sound level, loopback check, room noise floor |
| **2 Speakers** | Built | Any speakers and any speaker management system: build the system, measure (one mic) or import each speaker, overlay them with the predicted sum, crossover recommendation, settings summary |
| **3 Room tuning** | Later | All mic positions averaged, target curve and suggested EQ, alignment at the seats, ring-out, room acoustics |
| **4 Monitoring** | Built | RTA, transfer function, impulse response, waterfall, sound level, delay finder and traces |

The **VST3** shows the Monitoring page only, since it runs inside a host during the event.

### Home: venues and sessions

A **venue file** (`.lavenue`) holds what stays with the room and the system, and a list of **sessions**. A session is
one visit.

| Belongs to the venue (carries over) | Belongs to each session |
|------|------|
| Venue name and notes | Traces |
| Mics: names, inputs, positions, calibration files, SPL calibration | Sound level confirmed for this session |
| Named positions (FOH, Centre, Rear, Balcony ...) | Loopback check |
| Speaker setup, and whether it has been marked as changed | Room noise floor |
| Analysis settings, target curve | Delay compensation for each mic |

- **New session** starts a visit: the setup carries over and the measurements start fresh. Mic setup then asks for
  a quick re-check: confirm each mic's sound level ("Still correct" with the calibrator on, or calibrate again), rerun
  the loopback check and the noise floor.
- **Open** makes another session the open one. **Compare** shows the selected session's traces beside the open
  session's (they are not copied into it). **Rename** and **Delete** do what they say.
- **Speakers** stay "ready" from session to session unless you press **Mark as changed** on the Home page.
- **Change detection:** if the interface, sample rate, reference or a mic's input differs from when this session's
  checks were done, Mic setup turns amber and Home says what to re-check.
- New venue, Open, Save (`Cmd/Ctrl+S`) and Save as are on the Home page. The pane shows "Unsaved changes" when there
  is something to save. Files from 0.3 open as a venue with one session.

### 1 Mic setup

**Mic table** (up to 8 mics): name, input, position (pick one or type a new name), calibration file (load, clear, or
mark that the mic has none), sound level (Calibrate, or Re-check level when it was calibrated in an earlier session),
and a live level meter. **+ Add mic** adds a row; the `x` removes one. Use several mics at once on separate inputs,
or move one mic between named positions.

**Checklist** (each turns green when done, amber when done with a warning):

1. **Audio interface** (app only).
2. **Reference:** the loopback input (or the generator's internal copy). Warns if a mic is on the reference input.
3. **Mics connected:** every mic shows signal, and no two share an input.
4. **Calibration files:** every mic has its file, or switch on **No calibration files** (the step then turns grey,
   "not used"). Single mics can be marked from their Calibration file button.
5. **Sound level:** every mic calibrated or confirmed this session (the **Sound level** button in each mic's row),
   or switch on **No calibrator (dBFS)**.
6. **Loopback check:** pink noise at -20 dBFS for 2 seconds from the generator output, found on the reference input.
   Reports the interface round trip, polarity (warns if inverted) and level (warns below -36 or above -3 dBFS).
7. **Room noise floor:** with the generator off, 5 seconds on every mic (LAeq and LCeq), how loud the test signal
   should be (15 dB above the loudest mic's noise), and each mic's spectrum kept as an RTA trace.

The RTA beside the checklist shows every mic in its own colour.

### 2 Speakers

Set up once per venue, for any speakers and any speaker management system.

- **Speakers:** up to 8 sources (a top, a sub, fills, delays, more subs ...), each with a name, a role and the
  interface output that feeds it while it is measured. The dot at the right of a row shows or hides it on the graph.
- **Measure** (app): pick the **one mic** used for every speaker measurement (Measurement card), then measure each
  speaker on its own with your processor's crossover filters bypassed. LiveAlign plays pink noise from that speaker's
  output, finds its arrival, and averages its response. The first measurement sets the timing and **locks** it, so
  every later speaker keeps its true arrival relative to the first; keep the mic where it is. **Re-time** after moving
  the mic, then measure every speaker again.
- **Import response...** uses a manufacturer's or another analyser's response instead (FRD, CSV or TXT: frequency,
  dB, optional phase). Without phase, LiveAlign uses minimum phase; enter how much later it arrives than the measured
  speakers with **Arrival**.
- **Graph:** each speaker's magnitude and phase in its own colour and the **predicted sum** (white), with or without
  the suggested processing.
- **Crossover:** pick the high and low speaker of the pair and the range to search (sub to top 40 - 250 Hz, woofer
  to mid, mid to high). Switch on only the filter types (Butterworth, Linkwitz-Riley, Bessel) and slopes (6 to
  48 dB/octave) your processor offers. **Find best crossover** tries each of them across the overlap where both
  speakers are within 6 dB of their passband, with every delay (+/-15 ms) and both polarities, and level-matches the
  low speaker to the high one. It scores how flat the sum is (against what the two speakers give on their own, so a
  room notch both share does not count) and how much in phase they meet. **Adjust by hand** shows and edits the
  result: frequency, filter, slope, low speaker delay, gain and polarity, with a live readout of the sum.
- **Settings for your processor:** a table of every speaker's high-pass, low-pass, delay, polarity and gain, and for
  fills and delays the delay that makes them arrive with the main at the mic. **Copy settings** puts it on the
  clipboard as text. **Mark speakers set up** once the settings are in your processor: the step stays ready in later
  sessions until **Mark as changed** on the Home page.

The speakers, their responses and the settings are saved in the venue file.

## Monitoring

### Layout

The **mic picker** at the top chooses which mic the graphs and the sound level show, or the **average of all mics**
(power average of level, mean coherence; phase is not averaged). The Monitoring page shows **one, two or three graphs** (Settings > Screen), stacked or side by side. Each graph has
its own RTA / Transfer / Impulse buttons, so the RTA, transfer function and impulse response can all be on screen at
once. **Full screen** (app only, key `F`, `Esc` to leave) fills the whole display.

## Views

| View | What it shows |
|------|---------------|
| **Transfer** (key `2`) | Mic against reference: magnitude (top), phase (bottom) and coherence (pink, top third of the magnitude graph, 0 to 100 %). Points below the coherence blanking threshold are hidden. Hover for a readout; drag up and down to shift the magnitude, double-click or **Centre** to put it on 0 dB |
| **RTA** (key `1`) | The mic in 1/1 to 1/48 octave bands, in dB SPL once calibrated (dBFS before). Optional reference overlay and peak hold |
| **Impulse** (key `3`) | Impulse response, linear or log (ETC), 20 / 100 / 500 ms. Shows the arrival time and distance, and warns when the polarity looks inverted |
| **Waterfall** (key `4`) | The selected mic's spectrum over time (1/24 octave, newest at the top), as colour: for feedback, ringing and changes during the event. History of 10, 30 or 60 s and a 40 / 60 / 80 dB colour range in Settings > Waterfall. Hover for frequency, level and time; it stops while Frozen |

Top bar on this tab: **Running** (green) / **Frozen** (amber) with `Space` freezes the graphs (the sound level keeps
running), **Reset avg** (`R`) restarts the averages. Keys `1` to `4` switch the top graph.

## Right-hand column

- **Sound level:** dBA slow, A fast, C slow, Z slow, LAeq, LCeq and LAF max since the last reset. A, C and Z
  weighting follow the IEC 61672 curves. **Calibrate** opens the calibration pop-up (below) so these read dB SPL.
- **Inputs:** level of the selected mic and the reference, with peak hold and a clip light.
- **Delay compensation:** **Find delay** (`D`) listens to the last ~3 seconds, finds how late each mic hears the
  reference and sets each mic's own compensation so the phase trace flattens. The panel shows the selected mic. It also reports polarity. Nudge by 0.1 or 1 ms.
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

Settings (bottom of the navigation pane) holds the options that are not part of a step:

- **Inputs:** audio device (app only; this replaces the app's Options menu), reference. Mics are set up on the Mic
  setup page.
- **Transfer function:** FFT size (8k to 64k), averaging, smoothing, coherence blanking, magnitude range.
- **RTA:** resolution (1/1 to 1/48 octave), speed, reference overlay, peak hold.
- **Screen:** number of graphs, stacked or side by side, theme.
- **Impulse response:** linear or log (ETC), time range.
- **Waterfall:** history length and colour range.
- **Environment:** temperature (Celsius or Fahrenheit), distance in feet or metres.

## Calibration

Each mic is calibrated on its own, from its **Sound level** button in the mic table (or **Calibrate** in the Sound level
box on the Monitoring page, for the selected mic):

- **Calibrate:** fit the calibrator, pick its level (94 dB, 114 dB or another value), wait for the live reading to
  settle and press Calibrate.
- **Still correct:** in a later session, if the calibrator reads correctly with last time's calibration, confirm it.
- The offset can also be set by hand.
- **Mic calibration file:** plain text, one `frequency dB [phase]` per line. miniDSP UMIK files (with the
  `Sens Factor` header), Dayton and REW-style files, comma or tab separated, and decimal-comma files all load. The
  correction is applied to that mic's RTA, sound level and transfer function. The file's data is stored in the venue,
  so the venue file works on another computer.

## Look

LiveAlign uses the Live series look shared with LiveVoice, LiveMeter and BroadcastMaster: the same dark and light
palettes, cards with an accent bar beside the title, pill switches, slim sliders and the capitalised product name.

## Keys

`Cmd/Ctrl+S` save the venue, `G` generator, `F` full screen, `Esc` leave full screen. On the Monitoring page:
`1` to `4` top graph view, `Space` freeze, `R` reset averages, `D` find delay, `C` capture (whatever the graphs
show).

## Installing

Download the latest version from the [Releases page](https://github.com/leonidis2013/LiveAlign/releases/latest) (each
version is its own release, the newest marked Latest): `LiveAlign-macOS-App.zip`, `LiveAlign-macOS-VST3.zip` (universal: Apple silicon and Intel),
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
meter, generator levels and pink-noise slope, calibration / FRD / CSV file parsing, the target curves, and the
crossover maths (Linkwitz-Riley, Butterworth and Bessel responses against theory, minimum phase from magnitude, the
timing of measured responses, and the optimiser on a known top and sub, also with a room notch). CI runs them with address and
undefined-behaviour sanitizers before building.

`-DLA_BUILD_PREVIEW=ON` builds a local-only tool that feeds a simulated PA and room through the processor and saves
screenshots of every view, including measuring a simulated top and sub and finding their crossover.

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

- **Room tuning (next):** all mic positions averaged, target curves with suggested EQ, alignment at the seats, feedback
  ring-out, room acoustics (RT60, EDT, C50/C80, room modes).
- **Speakers additions:** EQ suggestions per speaker, more than one crossover pair at a time, CLF / GLL data
  (these formats are closed or binary; FRD / CSV exports from the manufacturer's software work today).
- **Monitoring additions:** delay tracking, trace offsets, SPL logging with alarms, PDF reports.
