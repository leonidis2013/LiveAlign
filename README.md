# LiveAlign

Sound system tuning and live monitoring: a dual-channel transfer function (magnitude, phase, coherence), RTA,
impulse response, delay finder, sound level meter and a signal generator. Built with JUCE as a **standalone app**
(for tuning: it drives the PA and measures through your mic) and a **VST3** (for live monitoring in a host such as
LiveProfessor). Windows and macOS.

**Version 0.14.0**: guided measuring: a pop-up with the venue plan showing where each mic goes and how to aim it, one Measure button with a count-in and the time left, then Next point (Room EQ and Speaker response). **0.13.0**: a new navigation (Setup, Tuning, Monitoring, Reports) with Speaker response, Crossover and Time alignment as their own steps; close-up speaker measurements with a picture of where to put the mic; a time alignment wizard (play the main, play the sub, apply, check) that matches phase at the crossover; Room EQ in rounds with several mics or one mic moved between positions; a feedback test; room acoustics (RT60, EDT, C50 / C80, STI, room modes, coverage, NC / NR); a dedicated monitoring mic and event panel; tuning and live monitoring reports in chapters with the settings as found and as left; seating and two mic layers on the Venue plan; a new logo and Home page; true full screen on macOS. **0.12.1**: the crossover pair follows the speakers' roles (top above, sub below), with a warning and a Swap button when a sub is set above a top. **0.12.0**: Room tuning (measure the listening positions with several mics or one mic moved between them, average against a target, suggested parametric EQ within your processor's limits, check after), the mic aim and calibration file following the position, a Next button on every page, advanced analysis settings tucked away, autosave with crash recovery, and new venues based on an existing one. **0.11.0**: a PDF tuning report, the Mic locker as its own page (model and serial number for each mic), speakers as individual cabinets with left / right pairs, the system's own window buttons, and the navigation grouped into Configuration, Tuning and Monitoring. **0.10.0**: the Venue plan, a sketch of where the speakers and mics are. **0.9.0**: 0 and 90 degree calibration files for each mic, with an Aim setting. **0.8.0**: a mic locker that remembers your mics' calibration across venues, and Check for updates. **0.7.0**: signal reliability (coherence) throughout the Speakers step with an adjustable minimum, and the Speakers step laid out as guided steps (Measure, Crossover, Time alignment) instead of tabs. **0.6.0** added time alignment of fills, delays and left / right mains at the seats where they meet, and a channel (mono, left, centre, right, L+R) for every speaker. **0.5.0** added the Speakers step (measure each speaker with one mic or import its response, crossover designer for any processor, settings summary), a waterfall view on Monitoring, amber outlines on required steps that are not done yet, and a slimmer navigation pane

## What you need

- One or more measurement mics (flat, omni; up to eight) on audio interface inputs, ideally with their calibration files.
- A **reference** signal. The most accurate is a loopback: split the output that feeds the PA (or your speaker processor's
  input) back into a second interface input. If you only use the generator, Settings > Reference > *Generator
  (internal copy)* works without a cable.

## Workflow

Everything runs from the **navigation pane** on the left (it scrolls with the mouse wheel), under the LiveAlign logo:

| Group | Page | What it does |
|------|------|--------------|
| | **Home** | Venue name and notes, sessions, and every page of the workflow with where it stands |
| **Setup** | **Mics** (Mic setup) | Mic table and checklist: interface, reference, mics connected, calibration files, sound level, loopback check, room noise floor |
| | **Speakers** | The system: cabinets, roles, channels (Mono, Left, Centre, Right), outputs, models; how the speakers are measured |
| | **Venue plan** | A sketch of the room, seating, speakers, and the tuning and monitoring mics |
| **Tuning** | **1 Speaker response** | Measure each speaker close up (or from one spot), or import its response |
| | **2 Crossover** | Find the best crossover your processor can do, or adjust by hand |
| | **3 Time alignment** | Line up the sub to the main, fills and delays: play one, play the other, apply, check |
| | **4 Room EQ** | Listening positions in rounds, average against a target, suggested parametric EQ, check after |
| | **5 Feedback test** | Raise the gain on an open mic and catch the frequencies that start to ring |
| | **6 Room acoustics** | RT60, EDT, clarity, speech intelligibility, room modes, coverage, background noise |
| **Monitoring** | **Monitor mic** | The mic used during the event (usually one at FOH): input, calibration, sound level, where it is |
| | **Live monitoring** | RTA, transfer function, impulse response, waterfall, sound level, delay finder and traces |
| **Reports** | **Tuning report** | Pick the chapters and export the PDF |
| | **Monitoring report** | The event's sound level against the limit, the monitoring mic and traces |

**Mic locker** and **Settings** sit at the bottom of the pane, above the venue and session. Each page has its own icon
and a live status: a green tick when it is ready, an amber `!` when it needs attention, a progress ring while it is
under way (Live monitoring has no tick: it is never "done"). Required steps not done yet have an amber outline on
Home; click a step there to open it. The generator strip only appears on the pages that play test signals.

**Window:** on macOS the window uses the standard red / yellow / green buttons, and green (or `F`) gives true full
screen. On Windows the usual minimise / maximise / close.

**Next button:** the top right of every page shows the one thing to do next and, once a page is done, **Continue to**
the next one. On Home it starts where you left off.

**Autosave:** every two minutes, unsaved changes are copied to a recovery file in LiveAlign's user folder (your
venue file is only written by Save). If LiveAlign did not close normally, the next start offers to restore them.

**New venue... > Based on this one** (Home) starts a venue with the same mics, speakers (names, models, outputs,
crossover filters), positions and plan, and nothing measured: handy for a touring rig or a similar room.

The **VST3** opens on Live monitoring with the monitoring mic, since it runs inside a host during the event.

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

### Venue plan

A simple drawing of how the setup is laid out, seen from above, **not to scale**. It is kept with the venue, and
**Save as picture...** writes it as a PNG (white page, title block and legend) to print or share.

- **Speakers** come from the Speakers step, each tagged with its name, channel, output and delay, drawn as a cabinet
  (a square for subs) with an arrow for the way it faces.
- **Mics** come from the Mic setup table, numbered in their colour and tagged with name, position and input. A mic
  aimed at the speaker shows an arrow; a mic pointing up shows a dot in the circle. Selecting a mic also lets you
  change its aim (the same setting as on Mic setup), which picks its 0 or 90 degree calibration.
- **Area...** adds a stage, FOH, seating, balcony, choir ... (resize from the corner), **Mic position...** a named
  position (one of the venue's positions, a time alignment spot, or a new one) and **Note...** a note.
- Drag to move, drag the small dot to turn (15 degree steps; hold Alt for any angle, or use `[` and `]`),
  double-click to rename, Delete to remove. New speakers and mics are placed for you; **Place missing speakers and
  mics** puts back any that are not on the drawing.
- **Seating:** a seating area can show chairs in plain rows, blocks with aisles, or curved rows (pick a style when it is
  selected), so the drawing reads like the room.
- **Two mic layers, one drawing:** **Tuning mics** shows the mics and positions used for tuning; **Monitoring mic** shows
  the event's monitoring mic on the same room and speakers. The tuning report uses the tuning layer, the monitoring
  report the monitoring layer, each on its own landscape page.

### Mic setup

**Mic table** (up to 8 mics): name, input, position (pick one or type a new name), calibration files (load, clear, or
mark that the mic has none), aim, sound level (Calibrate, or Re-check level when it was calibrated in an earlier session),
and a live level meter. **+ Add mic** adds a row; the `x` removes one. Use several mics at once on separate inputs,
or move one mic between named positions.

**Mics and positions are separate.** A *mic* is the hardware (input, calibration files, sound level); a *position* is
a place in the room (FOH, Row 8, Balcony). One mic can measure many positions one after another, and a position
keeps its measurements whichever mic took them. The **aim** belongs to the position: set FOH to 90 degrees (pointing
up) and any mic placed there uses its 90 degree file; move it to a position aimed at the speaker and it switches to
its 0 degree file automatically. Each measurement records the mic and the calibration file it used.

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

**0 and 90 degree calibration:** many mics come with two files: **0 degrees** for the mic pointed at the speaker,
and **90 degrees** for the mic pointing up at the ceiling (sound arriving from the side, the usual way to measure in
the seats). They differ mostly above about 5 kHz. **Load cal file(s)...** takes both at once; each goes to its
angle by its name (miniDSP's `..._90deg.txt`, "90 deg", "-90" ...), and the file menu can load, clear or swap each
angle by hand. The **Aim** column (enabled once a mic has both files) picks which one is used: *at speaker* (0
degrees) or *pointing up* (90 degrees, the default). The **Speakers** step switches to the 0 degree file while it
measures (point the mic at each speaker) and back afterwards; the measurement records which file it used. A mic
with one file uses it either way. The Monitoring status line shows the file in use (CAL 0 or 90 degrees). Both
files are kept in the venue and in the mic locker; venues and lockers from earlier versions keep their one file,
placed by its name.

**Mic locker** (button at the bottom of the navigation pane): the mics you own, saved on this computer (not in a
venue). Each has a model, serial number, optional name, its 0 and 90 degree calibration files, its last sound level
calibration and notes. **+ Add mic** adds one; **Save a mic from this venue...** copies a mic from the mic table. On
Mic setup, the **Locker** button beside a mic's name (it reads **Linked** for a mic from the locker) offers:

- **Pick a saved mic** to put it in that row: its name, calibration file and sound level offset come with it. The
  sound level still needs a quick "Still correct" check each session, since the interface gain may differ.
- **Save this mic to the locker...** after loading its calibration file and calibrating it, with a name you will
  recognise (model and serial number).
- A row that came from the locker stays **linked** (the arrow is tinted): a new calibration file or sound level
  calibration updates the locker too, and opening a venue picks up the mic's latest calibration from the locker.
  **Unlink** stops that for the row.
- **Mic locker...** (beside + Add mic) lists the saved mics, which row of this venue uses each one, and Rename /
  Remove. The locker is the file `MicLocker.xml` in LiveAlign's user folder.

### Speakers and Tuning steps 1 - 3

**Speakers are individual cabinets or outputs.** **Add speaker** offers a *left / right pair* (Main L and Main R,
the right one sharing the left one's settings), a sub, a front fill, a delay speaker, a centre or another speaker.
Each has a name, **model**, role, channel, output and **count** (cabinets on that output). **Same as ...** makes a
speaker share another's filters, level and polarity (and delay, unless a time alignment gives it its own): it needs
no measurement of its own. A new venue starts with Main L, Main R (same as Main L) and a sub.

Set up once per venue, for any speakers and any speaker management system. The **Speakers** page builds the system;
**Speaker response**, **Crossover** and **Time alignment** are the first three Tuning steps, each its own page in the
navigation. Each turns green when done and amber while it is still needed.

**How the speakers are measured** (Speakers page): **Close up** (the default) puts the measurement mic about 12 in
(30 cm) from each speaker's cone, on its axis, so the room hardly shows; the page draws where the mic goes for the
selected speaker and says which mic. **From one spot** keeps the mic in one place for every speaker (venues from
earlier versions). Close-up measurements do not carry the speakers' true arrival times, so the time alignment step
is then needed for the crossover pair.

**Is the signal reliable?** Coherence says how much of what the mic hears is the test signal rather than noise or
late reflections. LiveAlign uses it everywhere in this step:

- **Signal quality** (Measure step): a live bar of how much of the range is reliable at the measurement mic while a
  speaker plays (good from 80 %, fair from 60 %, below that raise the level or reduce the noise).
- **Minimum coherence** (Measure step, same setting as Settings > Transfer function): how reliable a point must be
  to count. 40 % by default; raise it in a noisy room to be stricter.
- **Reliability of each measurement:** the share of each speaker's useful range measured above the minimum, shown in
  the speaker list and the Measure step. Below 80 % the status says to measure that speaker again.
- **Graph:** each speaker's trace is faded where its measurement is not reliable, and the selected speaker's
  coherence is drawn across the top (pink, with the minimum as a dashed line).
- **Crossover search** ignores unreliable points, prefers a crossover region that was measured reliably, and warns
  when less than 70 % of it was. The live readout says how reliable the region is when it is below 80 %.
- **Dropped audio:** if the computer cannot keep up and audio is dropped during a measurement, the average restarts
  (and stops with a message after three tries), so a broken average is never kept.
- On **Monitoring**, the status line shows how much of the transfer function is reliable right now.

- **Speakers:** up to 8 sources (a top, a sub, fills, delays, more subs ...), each with a name, a role, a channel
  (Mono, Left, Centre or Right; a stereo main is a left / right pair) and the interface output that feeds it while it is measured. The dot at the right of a row shows or hides it on the graph.
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
- **Crossover:** the high and low speaker of the pair come from the roles: the first main / top above, the first sub
  below, wherever they are in the list (pick others, or swap them with the arrows between the two boxes). If a sub
  is set above a top, a warning offers **Swap**, and Find best crossover waits until the pair is the right way up;
  deliberate pairs of the same kind (sub to sub, top to top) are left alone. Pick the range to search (sub to top
  40 - 250 Hz, woofer to mid, mid to high). Switch on only the filter types (Butterworth, Linkwitz-Riley, Bessel) and slopes (6 to
  48 dB/octave) your processor offers. **Find best crossover** tries each of them across the overlap where both
  speakers are within 6 dB of their passband, with every delay (+/-15 ms) and both polarities, and level-matches the
  low speaker to the high one. It scores how flat the sum is (against what the two speakers give on their own, so a
  room notch both share does not count) and how much in phase they meet. **Adjust by hand** shows and edits the
  result: frequency, filter, slope, low speaker delay, gain and polarity, with a live readout of the sum.
- **Time alignment** (step 3) is a guided wizard. For the **sub to the main**: put the mic where both are heard
  (usually FOH), press **Play the main** (pink noise from the main only), then **Play the sub** (the main muted, the
  sub on). LiveAlign matches their phase across the crossover region (half to twice the crossover frequency) and gives
  the delay for the speaker that arrives first, taking into account any delay already set in the processor when you
  measured. **Apply** puts it in the settings (swapping which speaker is delayed if needed), then **Check** plays both
  together and confirms they add up in phase at the crossover (the sum should gain about 6 dB over each on its own).
  **Redo** starts again. Other alignments line up a fill, a delay or one side of a left / right pair with
  another speaker where their coverage meets: the front rows for a front fill, under the balcony for a delay, the
  centre line for left / right (not FOH, which is usually off to one side). Pick the speaker to delay, the one it
  lines up to and the spot, move the measurement mic there, set both speakers' processor delays to 0, and press
  **Measure both**: each plays pink noise in turn and LiveAlign finds when it arrives and how loud it is there. It then
  gives the delay (adding any delay the reference speaker already has, such as from the crossover), plus an optional
  **Extra** few ms so the sound still seems to come from the main (often 5 - 10 ms for delay speakers, 0 for left /
  right). **Apply** puts it in the settings table. If the speaker you picked already arrives later, it offers to swap
  the pair. Levels at the spot are compared too; a stereo pair should match within 1 dB. Up to 8 alignments.
- **Settings for your processor:** a table of every speaker's channel, output, high-pass, low-pass, delay, polarity
  and gain, and each time alignment with its spot and whether it has been applied. **Copy settings** puts it on the
  clipboard as text. **Mark speakers set up** once the settings are in your processor: the step stays ready in later
  sessions until **Mark as changed** on the Home page.

The speakers, their responses and the settings are saved in the venue file.

## Monitoring

### Layout

The generator is not shown here: monitoring listens to the programme (the transfer function works on music and
speech too). The **mic picker** follows the mic whose position is FOH until you pick another one, and the Sound level
box says which mic it is reading.

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

### 4 Room EQ

Measures the whole system at the listening positions and tunes it toward a target.

- **Listening positions** (left): every position with its aim and whether it has a before / after measurement.
  **+ Position** adds one. **Where the mics are now** says where each mic is; with one mic, **Next position** moves
  it to the next position not measured yet (and LiveAlign does that by itself after each measurement).
- **Rounds:** tick the planned positions to measure. With as many mics as positions, they are all measured at once;
  with fewer mics (two mics for six positions, say) LiveAlign measures in rounds and tells you which mic to **move**
  where before the next one. Mics left at a position already measured sit the next round out. Positions can be
  skipped.
- **1 Measure:** plays pink noise (whole system or one output), finds each mic's arrival, averages, and stores each
  mic's response at its position. Points below the minimum coherence are left out. Measuring a position again
  replaces its earlier measurement of the same round (before or after EQ).
- **2 Target and EQ:** the target curve, the speaker the EQ goes on (speakers that share its settings, like Main R,
  follow), how many bands your processor has free and how much boost to allow. **Suggest EQ** fits parametric
  (bell) bands to the average of the positions, each levelled to its own 500 Hz - 4 kHz so a far seat counts for its
  tonal balance, not its loudness. Cuts are preferred; boosts are small and only for wide dips (a narrow dip is
  usually a reflection EQ cannot fill); tiny or narrow bands that would barely help are left out. The range is set
  by the speaker's crossover filters. **Copy to the speaker settings** puts the bands in the speaker's settings
  (Speakers summary and report), added to any EQ that was already on it when you measured.
- **3 Check:** enter the EQ in your processor, measure again with **After EQ**, and compare: deviation from the target
  (RMS and worst) before and after, and the level spread across positions. **Mark the room as tuned** when done.

The graph shows each position (thin), the before and after averages, the target with a +/-3 dB band, and the before
average with the suggested EQ (dashed).

### Guided measuring

With **Guided** switched on (beside Measure on Room EQ and Speaker response; on by default, remembered on this
computer), Measure opens a pop-up that walks through the points one at a time:

- **The venue plan** with the point to measure now pulsing, the mic on it (its number and colour) and how it is aimed:
  a dot for pointing straight up, an arrow toward the speaker for aimed. Points already measured have a tick; the
  rest are outlined. A position that is not on the plan yet is placed for you (on an area of the same name, such as
  FOH, or spread across the seating); drag the pulsing point to where the mic really is and the plan keeps it.
- **Where each mic goes** in words underneath ("Mic 1 to Row 8, pointing straight up"), with one line on how to set up.
- **Measure:** a three-second count-in ("stand clear of the mic"), then the progress with the time left. **Stop**
  cancels.
- **Next point** moves on (or **Measure again**; **Try again** if nothing reliable was measured). Room EQ also has
  **Skip this point**. The dots along the bottom show how far through you are. **Close** or `Esc` leaves at any
  time; `Space` or `Enter` presses the big button.

Room EQ measures as many positions at once as there are mics, so with three mics and five positions it is two
rounds; a mic left at a position already measured sits the round out. Speaker response has one point per speaker
that needs its own measurement (speakers set "Same as" another are skipped), with the mic about 12 in from the cone
(or, measuring from one spot, where it stays for every speaker) and the speaker that plays ringed on the plan.

### 5 Feedback test

With a microphone open on stage and the system running, slowly raise the gain. LiveAlign watches the spectrum and
flags a frequency that stands 12 dB or more above its neighbours (within a third of an octave) for several frames in
a row, the start of a ring. Pull the gain back, put in the suggested cut (a narrow bell, Q 10) and go again for the
next ring; tick each one once its cut is in. The rings, with the stage mic they were found on, go in the tuning report.

### 6 Room acoustics

Uses the impulse responses captured at each listening position during Room EQ (and the noise floor from Mic setup):

- **RT60** (from T20 or T30, T15 for a short or noisy response) and **EDT** per octave band, 63 Hz - 8 kHz.
- **C50**, **C80** and **D50** (clarity and definition).
- **STI** per position (speech intelligibility, IEC 60268-16 indirect method from the impulse response, male weights).
- **Room modes**: peaks that the positions share below about 300 Hz.
- **Coverage**: level spread across the positions.
- **NC** and **NR** ratings of the background noise.

These are estimates from a live-sound measurement, not a certified acoustic survey.

## Monitoring mic

The mic used during the event, usually just one at FOH, set up on its own: its input, calibration files (or pick it from
the Mic locker, or take one of the tuning mics), sound level calibration and where it is on the Venue plan. Live
monitoring and the VST3 use it; the tuning mics are kept for the Tuning steps.

## Live monitoring additions

- **Event panel:** the sound level logged every 5 seconds through the event, the rolling LAeq and a limit you set.
- **Target:** the target curve is anchored where you put it instead of following the measurement; **Re-level**
  lines it up with the current measurement again.

## Reports

Two reports, each its own page under **Reports**: pick the chapters (remembered on this computer), who prepared it,
and export the PDF. The cover lists the chapters in bold, numbered; every page is numbered "page x of y"; the venue
plan has its own landscape page.

- **Tuning report:** summary with the settings **as found** (when the session started) beside **as left**, the venue
  plan (tuning mics), speakers and processor settings, crossover, time alignment, room EQ, feedback test, room
  acoustics, mics and measurements.
- **Live monitoring report:** sound level of the event against the limit, the monitoring mic and its calibration, the
  venue plan (monitoring mic) and the traces captured during the event.

## Tuning report (details)

**Tuning report (PDF)...** (on Home and on the Venue plan page) writes a PDF of the open session: a cover with the
venue, session, date, who prepared it (Settings > Reports) and the analysis settings; the venue plan; the speakers
with model, channel, output and count; the settings for the processor (high-pass, low-pass, delay, polarity, gain),
the crossover, the parametric EQ and the time alignments; the speakers' responses with the processing and their predicted sum; the
mics with model and serial number, position, input, calibration files, aim and sound level calibration; this
session's checks (reference, loopback, noise floor); room tuning (target, before / after deviation, level spread
across positions, the average against the target, each measurement with its mic and calibration file, and the
suggested EQ); and the captured transfer functions (with the target) and
RTA traces. Monitoring is not part of it.

## Updates

LiveAlign checks GitHub for a newer release once each time it starts (Settings > Updates > *Check when LiveAlign
starts* turns this off; only the request is sent). When there is one, an **UPDATE** button appears in the
navigation pane above the venue: it shows what's new and opens the download page in your browser. Settings >
Updates > **Check for updates** checks by hand. Installing a new version keeps your venues, traces and mic locker.

## Settings

Settings (bottom of the navigation pane) holds the options that are not part of a step. The analysis details
(transfer function, RTA, impulse response and waterfall) are hidden until **Show advanced analysis settings** is on;
the defaults suit most tuning.

- **Inputs:** audio device (app only; this replaces the app's Options menu), reference. Mics are set up on the Mic
  setup page.
- **Minimum coherence** (always shown): how reliable a point must be to count.
- **Transfer function** (advanced): FFT size (8k to 64k), averaging, smoothing, magnitude range.
- **RTA** (advanced): resolution (1/1 to 1/48 octave), speed, reference overlay, peak hold.
- **Screen:** number of graphs, stacked or side by side, theme.
- **Impulse response:** linear or log (ETC), time range.
- **Waterfall:** history length and colour range.
- **Environment:** temperature (Celsius or Fahrenheit), distance in feet or metres.
- **Updates:** check now, and whether to check when LiveAlign starts.

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
timing of measured responses, and the optimiser on a known top and sub, also with a room notch), the crossover pair
following the speakers' roles, the room acoustics (RT60, EDT, clarity and STI of a synthetic decay with a known
reverb time, NC / NR) and the feedback detector (a ring found, music and noise left alone). CI runs them with address and
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

- **Room acoustics additions:** coverage maps on the venue plan, acoustics from a dedicated sweep.
- **Speakers additions:** EQ suggestions per speaker, more than one crossover pair at a time, CLF / GLL data
  (these formats are closed or binary; FRD / CSV exports from the manufacturer's software work today).
- **Monitoring additions:** delay tracking, trace offsets, sound level alarms.
