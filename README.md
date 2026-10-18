# LiveAlign

Sound system tuning and live monitoring: a dual-channel transfer function (magnitude, phase, coherence), RTA,
impulse response, delay finder, sound level meter and a signal generator. Built with JUCE as a **standalone app**
(for tuning: it drives the PA and measures through your mic) and a **VST3** (for live monitoring in a host such as
LiveProfessor). Windows and macOS.

**Version 0.26.0**: **show log**. While monitoring, **Mark part** names each part of the event as it starts (Pre-service, Worship, Sermon ...); the log keeps a row per minute (LAeq, LCeq, LC - LA, LAF max, the rolling Leq against the limit, the feedback caught, the room sensor's temperature and humidity) and a summary per part. Both are in the monitoring report and export as **CSV** for a spreadsheet (Reports, Monitoring report). A **Level** graph (the sound level over time, with the limit and the parts) joins RTA, Transfer, Impulse and Waterfall, and Live monitoring starts with **three graphs** (Transfer, RTA, Waterfall). The report's cover and mic chapter give the mic's model and serial and its **sound level calibration**: the calibrator's level (94 or 114 dB), when, and the result. Turning the feedback watch off takes its marks off the graphs; polarity is no longer flagged while monitoring (the programme says nothing about it). If the **audio interface is unplugged**, LiveAlign no longer switches to the computer's own audio: it stops, shows a red warning, and opens the interface again when it is plugged back in. **0.25.0**: a cleaner **Home**. The right side is the session (its name and kind, NEXT, anything that needs attention) with the **venue's details** under it, editable there: Venue details is no longer a menu item of its own. **New venue** asks for the name and whether the first session is tuning or a show, and Home follows it from the start; a new venue starts at the usual conditions (20 °C, 50 %) and with nothing unsaved. **0.24.2**: the headphones (what to hear, where it plays) are set up in the Audio interface window too; any venue can be deleted from Home, including the open one and one never saved (it closes, back to the start); a venue's only session can be deleted (an empty one takes its place). **0.24.1**: at start the app shows Home only (and Settings) until a venue is opened, started or restored. **0.24.0**: **listening on headphones**: hear the reference or a mic (Settings > Audio), on a spare output of the measurement interface or on another device altogether (the computer's headphone jack, a USB headset), with the level and mute on the main screen. **0.23.1**: the venue plan opens on the session's layer (a show's monitoring points). **0.23.0**: **sessions are for tuning or for a show**. New session asks which; the menu and Home show only that kind's pages (a show session: Venue, the mic locker, Monitoring, Reports), a show session never plays the generator, and its monitoring compares with the venue's latest tuning session. **0.22.0**: **speaker groups**. Speakers of a kind are a group (Mains: left, centre and right; Subs; Fills; Delays), sharing one set of filters, level, polarity and EQ, with each member's own delay and trim; each is still measured and checked on its own. The **crossover** reads each side as its group (the members averaged), reads a sub's level where it plays fully (40 - 80 Hz, not where a protective high-pass rolls it off), and warns when the two were measured more than 15 dB apart. The **processor sheet** works for either kind of processor: one output per speaker, or a group (matrix) stage with each group's line entered once and each output's delay and trim under it. **0.21.0**: a **room sensor** over MQTT (Home Assistant's Mosquitto, Zigbee2MQTT or any broker; Settings > Room sensor): **Capture** on Venue, Details takes its temperature and humidity for tuning, and Monitoring shows it live with what it does to the sound against the tuning (how far the delays are off, how much more the air takes off the top at the back). Smoother: Room EQ's graph keeps each seat's smoothed curve and the averages until a measurement changes, undo looks for changes only around input, and Find delay works out the reference's spectrum once for every mic. Builds use the current GitHub Actions (Node 24) and test the MQTT feed against a real Mosquitto broker. **0.20.0**: the navigation is groups that open: **Venue** (Details, Speakers, Plan and points), **Mics** (this venue's mics, the locker), **Tuning** (the steps), **Monitoring** and **Reports**, each with its state on one line when closed; the tabs across the page top are gone. **Gain and headroom** moves to step 2, right after every speaker is verified, so the gain structure is set before anything is tuned. The **Speakers** tab is the set-up only (the list, the fields, and the system as a table, output by output); the tuning steps show the speakers as a picker with their measuring state. A **mono sub** added from the menu is fed from both sides (All outputs). **Time alignment** shows the two speakers' **impulse responses** one above the other on the same time base, each with a Play button and its arrival marked, and the offset drawn between them. Standards are called out where a measure follows one (ISO 3382-1, ISO 18233, IEC 60268-16, IEC 61672-1, ANSI S12.2, ISO 1996, ISO 9613-1, IEC 60942, AVIXA A102.01) in the glossary, on the pages and on the report. Under the hood, a reliability pass: the unsaved check and the step statuses are throttled instead of serialising the venue every tick, the waterfall is drawn from a cached image, the recorder no longer races the audio thread when a measurement starts, time alignment validates its queue, polarity is Unknown rather than wrong when a capture has no phase, and the sweep thread and update check cannot call back into a closed window; the whole flow runs clean under AddressSanitizer and UBSan. **0.19.0**: the venue in one place, and the steps between tuning and the show. **Setup** is two pages: **Venue** (tabs: **Details** with the name, notes, what the room is used for, its size, the temperature and humidity of the day and the targets; **Speakers**, now with how widely each one covers; **Plan and points**) and **Mics** (the venue's mics, with the **Mic locker** as a tab). The plan shows **measurement points only**: each is for tuning, monitoring or both, and shows the mic at it ("FOH · Mic 1"); **Mic here** links a mic to the selected point (the Mics page's At follows). A **new venue** starts blank. **Home lists your venues** (the venues folder and the recent ones: last saved, sessions, tuned), with search, open, rename, duplicate, delete and show in folder. Three new tuning steps: **Aim and coverage** (each speaker across the seats it covers, against a plus or minus 3 dB spec, with hints such as "tilt it down" from the plan, re-measured after re-aiming, before and after in the report), **Gain and headroom** (pink noise in steps up to the show level at FOH, per speaker and together, watching for compression and distortion; the amp gains and limiters go on the processor sheet) and the **Listening check** (music you know at each point, notes, EQ in against bypassed; a problem point can be measured again). The temperature and humidity are kept per session, with the air absorption at the back of the room and a re-check prompt when the delays have drifted; Room EQ shows the **seat-to-seat variance** as found and as left, and Live monitoring compares the show with the tuning. **Undo and redo** (`Cmd/Ctrl+Z`) cover EQ, trims, delays, points, speakers and venue values, and the processor sheet has the **history** of every change per output with a roll back. **Help:** a **?** beside each page title (how to, What is this?, the glossary) and a first-run tour. Builds are now checked on Linux too: tests of the tuning logic, a screenshot run of the whole flow with pass / fail checks, and pluginval. **0.18.0**: more room acoustics, a Room EQ that allows for them, and a new Settings page. Room acoustics adds the bass and treble ratios, the 63 Hz band, reverberation in 1/3 octaves, centre time (Ts), STI corrected for the room noise and the speech level, %ALcons, echo and flutter echo detection, the energy-time curve with the first reflection, the spread across the points, the coverage at 2 and 4 kHz, and (with the room's size) the Schroeder frequency and each speaker's critical distance. Room EQ's **Allow for the room acoustics** (on by default) uses them: below the Schroeder frequency the seats' median and cuts only on room modes most seats share, sized to how long they ring, with no boosts into nulls; long reverb corrected from the direct sound; the target's low end eased where the room rings long; nothing suggested where the noise is too close; no high boosts when clarity is poor; shallow cuts when the room needs treatment. **Settings** is a full page with a category menu, a Reset to defaults for each, and new tuning defaults (that a venue can override), generator safety (a ceiling and a fade-in), monitoring defaults (limit, A or C weighting, feedback watch sensitivity, delay tracking) and reports and files (paper size, a logo, the default folder, autosave). **0.17.0**: a smoother tuning flow. The steps run Speaker response, Crossover, **Level balance** (new), Time alignment, Room acoustics, Room EQ, Feedback test, and the navigation pane is the one way between them (it marks the next step; Home is a dashboard with the next step, what needs attention and a **quick re-check** of a tuned system). Every measurement goes through the guided pop-up (`Space` measure, `Enter` next, `Esc` stop), with the mutes, mic moves and wiring changes as its "First:" line. Listening points are made on the Venue plan; the mic's aim follows what is measured (at the speaker, or pointing up), limited to the angles it has files for. Calibration files are added in the mic locker, each one's angle read from its name. Polarity comes from every speaker's own capture, a speaker set "Same as" another is measured and compared with it ("matches Main L", "OPPOSITE to Main L"), and an inversion needs two tests to agree. The Room EQ target follows what the speakers can play. Room acoustics comes before Room EQ, with a decision tree. One **processor sheet** lists every setting to enter, ticked off as you go, with CSV and PDF export. **0.16.1**: speakers sharing an output (a pair on All outputs) are measured by muting the others at the console or processor: LiveAlign says which to mute and asks you to confirm. **0.16.0**: a new Mics page (mic positions on top with the mic attached from your locker, the mics below, the checklist beside them); calibration files at any angle (0, 30, 45, 60, 90, 180 degrees); Room EQ as a loop (as found, enter the EQ, check, refine, as left) on the whole system, with gentler bands; room acoustics measured with a sweep; time alignment with pink noise, a sweep or music from the console; a feedback watch and delay tracking while monitoring; a crossover only when there is a sub, Automatic or By hand; the app icon; and fixes (outputs shared by two speakers, the interface change, signal quality, the top + sub sum). **0.15.0**: polarity of every speaker and every cabinet, checked while capturing speaker responses, with a quick check for speakers set "Same as" another and for each cabinet on a shared output. **0.14.1**: close up, each speaker can be measured with its own mic (picked on Speaker response or in the guided pop-up). **0.14.0**: guided measuring: a pop-up with the venue plan showing where each mic goes and how to aim it, one Measure button with a count-in and the time left, then Next point (Room EQ and Speaker response). **0.13.0**: a new navigation (Setup, Tuning, Monitoring, Reports) with Speaker response, Crossover and Time alignment as their own steps; close-up speaker measurements with a picture of where to put the mic; a time alignment wizard (play the main, play the sub, apply, check) that matches phase at the crossover; Room EQ in rounds with several mics or one mic moved between positions; a feedback test; room acoustics (RT60, EDT, C50 / C80, STI, room modes, coverage, NC / NR); a dedicated monitoring mic and event panel; tuning and live monitoring reports in chapters with the settings as found and as left; seating and two mic layers on the Venue plan; a new logo and Home page; true full screen on macOS. **0.12.1**: the crossover pair follows the speakers' roles (top above, sub below), with a warning and a Swap button when a sub is set above a top. **0.12.0**: Room tuning (measure the listening positions with several mics or one mic moved between them, average against a target, suggested parametric EQ within your processor's limits, check after), the mic aim and calibration file following the position, a Next button on every page, advanced analysis settings tucked away, autosave with crash recovery, and new venues based on an existing one. **0.11.0**: a PDF tuning report, the Mic locker as its own page (model and serial number for each mic), speakers as individual cabinets with left / right pairs, the system's own window buttons, and the navigation grouped into Configuration, Tuning and Monitoring. **0.10.0**: the Venue plan, a sketch of where the speakers and mics are. **0.9.0**: 0 and 90 degree calibration files for each mic, with an Aim setting. **0.8.0**: a mic locker that remembers your mics' calibration across venues, and Check for updates. **0.7.0**: signal reliability (coherence) throughout the Speakers step with an adjustable minimum, and the Speakers step laid out as guided steps (Measure, Crossover, Time alignment) instead of tabs. **0.6.0** added time alignment of fills, delays and left / right mains at the seats where they meet, and a channel (mono, left, centre, right, L+R) for every speaker. **0.5.0** added the Speakers step (measure each speaker with one mic or import its response, crossover designer for any processor, settings summary), a waterfall view on Monitoring, amber outlines on required steps that are not done yet, and a slimmer navigation pane

## What you need

- One or more measurement mics (flat, omni; up to eight) on audio interface inputs, ideally with their calibration files.
- A **reference** signal. The most accurate is a loopback: split the output that feeds the PA (or your speaker processor's
  input) back into a second interface input. If you only use the generator, Settings > Reference > *Generator
  (internal copy)* works without a cable.

## Workflow

Everything runs from the **navigation pane** on the left, under the LiveAlign logo. Home sits on its own; the rest are
groups that open when you click them (the open page's group stays open, the others close to one line with their
state: "3 of 10 done", or what needs attention). Clicking a closed group goes to its next step.

| Group | Page | What it does |
|------|------|--------------|
| | **Home** | Your venues and the open one's sessions; on the right, the next step and anything that needs attention, then the venue's details (name, notes, room use and size, the conditions of the day, the targets) |
| **Venue** | **Speakers** | The set-up only: cabinets, roles, channels, outputs, models, coverage, and the system as a table |
| | **Plan and points** | The measurement points (tuning, monitoring or both) with the mic at each, and the speakers, on a sketch |
| **Mics** | **This venue's mics** | Input, the point each is at, sound level, the checklist and the pre-flight run |
| | **Mic locker** | Every mic you own: model, serial, calibration files, sound level |
| **Tuning** | **1 Speaker response** | Each speaker close up, its polarity from the same capture; or import its response |
| | **2 Gain and headroom** | Pink noise in steps up to the show level at FOH: the headroom, and the speaker that runs out first |
| | **3 Crossover** | Find the best crossover your processor can do, or adjust by hand |
| | **4 Level balance** | Each speaker alone at the listening points: left against right, fills against the mains, the sub against the tops |
| | **5 Aim and coverage** | Each speaker across the seats it covers, against the coverage spec, with a hint for one that falls off |
| | **6 Time alignment** | Two impulse responses on one time base: play each alone, see the offset, apply the delay, check both together |
| | **7 Room acoustics** | A sweep at the points: RT60, EDT, clarity, intelligibility, modes; a decision tree says whether EQ can help |
| | **8 Room EQ** | As found, enter the suggested EQ, check, refine, as left: the system as a whole against a target |
| | **9 Listening check** | Music you know at each point, a note at each, the EQ in and bypassed |
| | **10 Feedback test** | Raise the gain on an open mic and catch the frequencies that start to ring |
| **Monitoring** | **Monitor mic** | The mic used during the event (usually one at FOH): from the locker, input, sound level, its point on the plan |
| | **Live monitoring** | Three graphs, each RTA, transfer function, impulse response, waterfall or sound level over time; the sound level, the event and its parts, feedback watch, delay tracking, traces |
| **Reports** | **Processor sheet** | Every setting to enter, output by output, ticked off as entered; copy, CSV or PDF |
| | **Tuning report** | Pick the chapters and export the PDF |
| | **Monitoring report** | The event's sound level against the limit, the show log (each part, each minute; also as CSV), the monitoring mic with its calibration, and traces |

**Settings** sits at the bottom of the pane, above the venue and session. Each page has its own icon
and a live status: a green tick when it is ready, an amber `!` when it needs attention, a progress ring while it is
under way (Live monitoring has no tick: it is never "done"). Required steps not done yet have an amber outline on
Home; click a step there to open it. The generator strip only appears on the pages that play test signals.

**Window:** on macOS the window uses the standard red / yellow / green buttons, and green (or `F`) gives true full
screen. On Windows the usual minimise / maximise / close.

**The next step:** the navigation pane is the one way between the steps. The step to do next has an accent outline,
**NEXT**, and what to do there as its second line (hover for more); Home shows the same at the top, with a **Go to**
button and a line listing anything that needs attention. A step is green only once it is verified (Room acoustics
from a real sweep, not the Room EQ estimate; polarity only red when two tests agree).

**Autosave:** every two minutes, unsaved changes are copied to a recovery file in LiveAlign's user folder (your
venue file is only written by Save). If LiveAlign did not close normally, the next start offers to restore them.

**New venue...** (Home) starts a blank venue: no speakers, one mic slot with nothing attached, no calibration or sound
level, the default reference, the default plan and points (FOH, Centre, Rear, Balcony; FOH for monitoring too), and no
venue values of its own. **New venue... > Based on this one** starts one with the same mics, speakers (names, models,
outputs, crossover filters), points and plan, and nothing measured: handy for a touring rig or a similar room.

**Undo and redo:** `Cmd/Ctrl+Z` (and `Cmd/Ctrl+Shift+Z`, or the arrows beside the page title) undo what you change by
hand: EQ entered, trims, delays, polarity, points and speakers moved or removed, the venue's details and values.
Measurements are not undone.

**Help:** the **?** beside each page title has **How to** (the step in a few lines, also `F1`), **What is this?** (click
anything on the screen for what it is and the terms it uses), the **Glossary** of every number and term, and the
**tour**, shown the first time LiveAlign runs (again from Settings > Display).

The **VST3** opens on Live monitoring with the monitoring mic, since it runs inside a host during the event.

### Home: venues and sessions

**At start** LiveAlign shows Home only (and Settings): open a venue from the list, **Open...** one from elsewhere,
start a **New venue...**, or restore unsaved work after a crash. The rest of the menu appears once a venue is open.

**Venues:** Home lists every venue in the venues folder (Settings > Reports and files, else `Documents/LiveAlign
Venues`) and the ones opened recently from anywhere else, the last used first: name, when it was saved and used, its
sessions, and whether it was marked tuned. The open venue is marked. Click one to open it (LiveAlign asks to save the
open one first); search by name; **...** (or right-click) has Open, Rename, Duplicate, Show in folder and Delete (to the
trash, after asking). A new venue saves into the venues folder by default.

A **venue file** (`.lavenue`) holds what stays with the room and the system, and a list of **sessions**. A session is
one visit, of one of two kinds, chosen when it is made (**New session**):

- **Tuning:** the tuning steps, with test signals. The menu shows Venue, Mics, Tuning and Reports (the processor sheet
  and the tuning report); Home shows the setup and the ten steps, and the quick re-check.
- **Show monitoring:** live monitoring only. The menu shows Venue, the mic locker, Monitoring and Reports (the processor
  sheet and the monitoring report); Home shows the monitor mic and live monitoring. The venue plan opens on its
  monitoring points (a tuning session on its tuning points; the other layer is a click away). **The generator never plays** in a
  show session, so nothing can reach the PA mid-show. Against the tuning and the monitoring report compare with the
  venue's latest tuning session.

Doing both on one day (a re-tune at sound check, then the service) is two sessions. A new session defaults to Show
monitoring once the venue is tuned. Each session in the list carries a TUNING or SHOW tag. Sessions from before 0.23
open as tuning sessions, or as show sessions when they only logged a show.

| Belongs to the venue (carries over) | Belongs to each session |
|------|------|
| Venue name and notes, the room's use and size, the targets | Traces |
| Mics: names, inputs, positions, calibration files, SPL calibration | Sound level confirmed for this session |
| Named positions (FOH, Centre, Rear, Balcony ...) | Loopback check |
| Speaker setup, and whether it has been marked as changed | Room noise floor |
| Analysis settings, target curve | Delay compensation for each mic |
| The change history of every output | The temperature and humidity of the day |

- **Quick re-check** (top right of the steps): for a system already tuned, one guided pass that measures each speaker
  close up, then each one and the whole system at FOH, and flags only what has drifted: a polarity that flipped, a
  level more than 2 dB off, a delay that is not what was set (against the level balance readings), or the tonal balance
  moved since Room EQ. The result is listed on Home.
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

### Venue: Details

Everything about the place itself, in one spot (the other pages show it read-only, with **Edit on Venue**):

- **Venue:** name and notes.
- **Room:** what it is used for (speech, amplified music, choir, concerts: Room acoustics compares with it) and its
  size (length, width and ceiling height, or the volume) for the Schroeder frequency and the critical distance.
- **Conditions (this session):** the temperature and humidity of the day. Each session keeps its own. LiveAlign shows
  the speed of sound and how much the air takes off the top at the back of the room (dry air takes more; Room EQ leaves
  it alone), and remembers the conditions when the delays and the EQ were set: when the room has warmed up or cooled
  down enough for the delays to drift (or the humidity has moved a lot), it says by how much and offers the quick
  re-check (Home lists it too). With a **room sensor** set up (Settings > Room sensor), the card has a **Capture**
  button showing the sensor's reading: it takes the temperature and humidity as this session's conditions for tuning
  (an edit like any other, so `Cmd/Ctrl+Z` undoes it).
- **Targets:** the Room EQ target curve, the show level at FOH (dBA), the headroom wanted above it, and the coverage
  spec. Each starts at your default (Settings > Tuning defaults) and can be this venue's own.

### Venue: Plan and points

A simple drawing of how the setup is laid out, seen from above, **not to scale**. It is kept with the venue, and
**Save as picture...** writes it as a PNG (white page, title block and legend) to print or share.

- **Speakers** come from the Speakers tab, each tagged with its name, channel, output and delay, drawn as a cabinet
  (a square for subs) with an arrow for the way it faces. Drag to move, drag the small dot to turn (15 degree steps;
  hold Alt for any angle, or use `[` and `]`). **Place missing speakers** appears when one is not on the drawing yet.
- **Measurement points** are the only mic places: **+ Measurement point...** adds one (FOH, Row 8, Balcony ...); drag
  it where it is, double-click to rename (everywhere), Delete to remove (with its measurements). Each point is **for
  tuning, for monitoring, or both** (set when it is added, from the layer it is added on, and changed on the selected
  point). FOH starts as both.
- **Mic here:** select a point and pick the mic at it (None, Mic 1 ...). Picking a mic moves it there (a mic is at one
  point at a time); the Mics page's **At** shows the same. Each point is drawn with its mic, "Front Left · Mic 2", or
  "no mic". The guided pop-up says "Mic 2 to Front Left". Venues from earlier versions keep their links.
- **Two layers, one drawing:** **Tuning points** (Room EQ, Room acoustics, Level balance and the re-check measure at
  these) and **Monitoring points** (where the monitoring mic can stand; the Monitor mic page picks one). The tuning
  report uses the tuning layer, the monitoring report the monitoring layer, each on its own landscape page.
- **Area...** adds a stage, FOH, seating, balcony, choir ... (resize from the corner) and **Note...** a note.
  **Seating:** a seating area can show chairs in plain rows, blocks with aisles, or curved rows.

### Mic setup (the Mics page)

**Mics:** the venue's mics, each picked from your **mic locker** (**+ Add mic** offers the locker's mics first, or a new
one), with its input, **At** (the listening point it stands at now, or **Moves (guided)** for a mic the guided pop-up
walks from point to point), its calibration files, its sound level (Calibrate, or Re-check level when it was
calibrated in an earlier session) and a live level meter. The locker owns a locker mic's calibration files and sound
level: its Calibration files button opens it in the locker. A *mic* is the hardware; a *point* is a place in the room,
made on Venue, Plan and points.

**Aim:** set by what is measured, not per mic or per point. Close to a speaker the mic points at it (its 0 degree
file); in the room it points straight up (its 90 degree file, or the nearest angle it has a file for). The guided pop-up
says how to aim it and which file is used, and its **Aim** picker offers only the angles the mic has files for.

**Pre-flight** (top of the checklist): each session, **Run the pre-flight** does the loopback check, then the room
noise, then opens the sound level re-check for the first mic that needs one.

**Checklist** (beside the mics; each turns green when done, amber when done with a warning). When the interface or a
mic's input changes after this session's checks, the Audio interface step says what changed ("was Scarlett Solo USB,
now ...") and the checks to do again turn amber (room noise, loopback, the sound level after an interface change);
**Keep my checks** accepts the change when it does not matter. **Audio device...** opens LiveAlign's own settings,
which list every input and output on its own (not in stereo pairs) and show the input and output devices.

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

**Calibration files** (in the mic locker): **Add calibration files...** takes one or more files at once (0, 30, 45, 60,
90, 180 degrees). Each one's angle is read from its name (miniDSP's `_90deg`, Sonarworks' `_0degree`, "90 deg", "-90",
"on-axis", "vertical" ...; a number inside a serial number does not count). The locker lists only the files added, each
with an angle picker set to the guess; a name that gives no angle is put at the first free usual angle and flagged
in amber to check. **Remove** takes a file out.

**Mic locker** (the second tab of Mics): the mics you own, saved on this computer (not in a
venue): model, serial number, optional name, calibration files, last sound level calibration and notes. **+ Add mic**
adds one; **Save a mic from this venue...** copies one of the venue's mics. A venue mic picked from the locker stays
**linked**: a new calibration file or sound level calibration updates the locker too, and opening a venue picks up the
mic's latest calibration from the locker. The locker is the file `MicLocker.xml` in LiveAlign's user folder.

### Speakers and Tuning steps 1, 3 and 6

**Speakers are individual cabinets or outputs, in groups.** **Add speaker** offers a *left / right pair* (Main L and
Main R, one group), a sub, a front fill, a delay speaker, a centre or another speaker; a new one joins the group of its
kind when there is one (the centre joins the mains, a second sub the subs). Each has a name, **model**, role, channel,
output, **count** (cabinets on that output) and **group**. A **group** (Mains, Subs, Fills, Delays) shares one set of
filters, level, polarity and EQ, led by its first speaker; each member keeps its own delay (from a time alignment) and
level trim. Every member is still measured on its own: its capture checks its polarity, level and response against the
lead. **Own settings (no group)** takes a speaker out (two delay rings at different distances, say). A new venue starts
with Main L and Main R as the mains' group, and a sub. Speakers are edited only on the Speakers page; the tuning steps show the
selected speaker read-only with **Edit on the Speakers page**.

Set up once per venue, for any speakers and any speaker management system. The **Speakers** page builds the system;
**Speaker response**, **Crossover** and **Time alignment** are the first three Tuning steps, each its own page in the
navigation. Each turns green when done and amber while it is still needed.

**How the speakers are measured:** always close up, the mic about 12 in (30 cm) from each speaker's cone, on its axis,
so the room hardly shows (the room is Room acoustics' and Room EQ's job). **Measure the speakers** goes through every
speaker in the guided pop-up, then each further cabinet on a shared output. Close-up measurements do not carry the
speakers' true arrival times, so the time alignment step is then needed for the crossover pair. A capture that arrives
much later than the other close-up ones (more than 3 ms) is flagged "was the mic close?" and its polarity is not trusted.

**Is the signal reliable?** Coherence says how much of what the mic hears is the test signal rather than noise or
late reflections. LiveAlign uses it everywhere in this step:

- **Signal quality** (Measure step): a live bar of how much of the speaker's own range (within 20 dB of its loudest) is
  reliable at the mic while it is measured (good from 80 %, fair from 60 %, below that raise the level or reduce the
  noise). It says "Checking the signal..." until the speaker's timing is found and averaging has started, then is
  smoothed, so it no longer starts poor and jumps to good.
- **Mute the others:** each speaker is measured on its own. When others play from the same output (a left / right pair
  both on **All outputs**, the usual way when the console or processor does the routing), LiveAlign names the
  speakers to mute on the console or processor and asks you to confirm before it plays; in the guided pop-up the
  instruction is shown in amber above Measure. Checking two speakers together asks for every other speaker to be
  muted. Giving each speaker its own interface output avoids the question.
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
- **Measure** (app): pick the mic (Measurement card). **Close up**, each speaker has its own: the Mic box sets it for
  the selected speaker, and the next speakers start with the last one picked, so a different mic can sit at each
  speaker. **From one spot**, one mic measures every speaker. Measure each speaker on its own with your processor's
  crossover filters bypassed. LiveAlign plays pink noise from that speaker's output, finds its arrival, and averages
  its response. From one spot, the first measurement sets the timing and **locks** it, so every later speaker keeps
  its true arrival relative to the first; keep the mic where it is. **Re-time** after moving
  the mic, then measure every speaker again.
- **Polarity:** judged from every speaker's own capture, and shown in the list (**+** normal, **-** inverted in red,
  **?** check, an empty ring when not checked). On its own, LiveAlign reads the phase through the **woofer's own
  range** only (up to about 1.2 kHz, so a two-way box's own crossover, whose phase turn is not an inversion, is never
  read across), less what its roll-offs do to the phase and a small timing error; near 0 degrees is normal. **Inverted
  needs two tests to agree**: the phase and the sign of the impulse peak. The phase alone pointing to inverted is
  **check** (amber), since the room, a late arrival or the box's own crossover can turn it. The firmest test is a
  comparison: a speaker set **Same as** another, and each further cabinet on a shared output, is compared with that one
  and given as how the two relate: "Main R matches Main L", or "Main R is OPPOSITE to Main L" (red: one of the two is
  wired the other way round). An inversion turns the Speaker response step amber and the navigation's next step says
  to fix it and measure again. Measure with the processor's polarity for the output at normal.
- **Matching speakers:** for each speaker set "Same as" another, the level and response difference between the two
  close-up captures ("Main R against Main L: level +0.3 dB, response within 1.1 dB"). More than 1 dB apart: check its amp
  channel and gain.
- **Signal quality** when nothing plays shows the last capture of the selected speaker; **As found** on the graph shows
  each speaker's first capture (dashed) with the settings it had then.
- **Import response...** uses a manufacturer's or another analyser's response instead (FRD, CSV or TXT: frequency,
  dB, optional phase). Without phase, LiveAlign uses minimum phase; enter how much later it arrives than the measured
  speakers with **Arrival**.
- **Graph:** each speaker's magnitude and phase in its own colour, with or without the suggested processing, and the
  **top + sub sum** (white) when there is a crossover pair. Left and right mains are separate channels in separate
  places, so they are not summed.
- **Crossover:** only between a top and a sub. With no sub in the system the step says **Not needed** and is done (and
  offers to remove crossover filters left from an earlier pair of mains). **Automatic** shows the range, the filters
  your processor has and **Find best crossover**; **By hand** shows the frequency, filter, delay, level and polarity,
  starting from the automatic result. The high and low speaker of the pair come from the roles: the first main / top above, the first sub
  below, wherever they are in the list (pick others, or swap them with the arrows between the two boxes). If a sub
  is set above a top, a warning offers **Swap**, and Find best crossover waits until the pair is the right way up;
  deliberate pairs of the same kind (sub to sub, top to top) are left alone. Pick the range to search (sub to top
  40 - 250 Hz, woofer to mid, mid to high). Switch on only the filter types (Butterworth, Linkwitz-Riley, Bessel) and slopes (6 to
  48 dB/octave) your processor offers. **Find best crossover** tries each of them across the overlap where both
  speakers are within 6 dB of their passband, with every delay (+/-15 ms) and both polarities, and level-matches the
  low speaker to the high one. **Each side is its group:** the members' responses are averaged (power average, the
  lead's timing), so Main L and Main R together choose the crossover and one cabinet's quirk (a wall bump on one side)
  does not. The level match reads the low side where it plays fully (40 - 80 Hz for a sub), and says so when the two
  were measured more than 15 dB apart (an amp not yet up). It scores how flat the sum is (against what the two speakers give on their own, so a
  room notch both share does not count) and how much in phase they meet. **By hand** shows and edits the result, with a
  live readout of the sum.
- **Time alignment** (step 3) is a guided wizard. **Signal**: pink noise (the default), a sweep, or **music from the
  console** when noise is not possible: route the music to only the speaker each step names (mute the other on the
  console), with the console's feed on an input as the reference; it averages longer, since music is uneven. Either
  way LiveAlign compares the mic with the reference to get the impulse response, so no click or impulse is played. For the **sub to the main**: put the mic where both are heard
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
- **Settings for your processor** are on the **Processor sheet** (below); each step's page shows how many outputs are
  entered, with a button to the sheet.

### 4 Level balance

Each speaker on its own, with its processing in place, measured at the listening points (tick them on the left: the
centre / FOH first, then a seat in each fill's or delay's area), in the guided pop-up. The table shows each speaker's
level through its own range at each point. LiveAlign then checks:

- **Left against right** at the centre: within 1 dB, otherwise a trim down on the louder side.
- **A fill or a delay against the mains** at the seat it covers (where it is loudest against them): about level, 0 to
  -3 dB, otherwise a trim to about -1.5 dB.
- **The sub against the tops** at the centre, against the Room EQ target's low end: within 1.5 dB, otherwise a sub trim.
- **Cabinets on one output** (from their close-up captures): within 1.5 dB of the first, otherwise a note to check its
  amp, wiring and driver.

**Put the trims on the sheet** adds them to each speaker's level on the processor sheet (a trim is kept for a speaker
set "Same as" another too); **Measure again to check** measures each speaker at the centre once they are entered, and
the table shows "was" beside the new value. It comes before Time alignment, so the alignment works on level-matched
speakers.

### 5 Aim and coverage

After Level balance and before the speakers are checked together. Aim comes before EQ: a speaker pointed at the wrong
seats cannot be fixed with EQ or level. From Level balance's readings (each speaker alone at each point), each speaker
is judged at the points it covers (where it is within 6 dB of the loudest top) at 1 - 4 kHz, against its own loudest
point. The spread should stay within the coverage spec (plus or minus 3 dB by default: Venue, Details, or Settings).

A speaker that falls off more gets a hint from the plan, where the speakers and points are and which way each faces:
"Main L is 7 dB down at Back Left: tilt it up, so it reaches further back", "tilt it down", "turn it toward ...", or
"that seat needs a fill" when it is well off to the side. **Re-aimed: measure it again** measures that speaker at its
points in the guided pop-up (Level balance takes the new readings too), and the table and the report show before and
after. **Accept the coverage as it is** when a hint cannot be followed.

### 2 Gain and headroom

Right after every speaker is verified, before anything is tuned: the gain structure comes first (and the quick
re-check catches a change later). With the processing, amps and limiters as for the show. **Hearing protection on, nobody near the
speakers.** **Run the whole system** plays pink noise in 3 dB steps (bigger steps while far below), reading the level at
the FOH mic, up to the show level plus the headroom wanted (Venue, Details), never past the generator's ceiling. A clean
system rises 3 dB for every 3 dB of generator: a step that rises less is compression or limiting, and the coherence
dropping is distortion. The result: the headroom above the show level, too little, or how far short the system falls.
**Then each speaker alone** runs each output at the same drive the whole system needed (what it gets in the show): the
one that compresses first is the weak link. The **amp gain** and **limiter threshold** of each output are noted here for
the processor sheet and the report.

### 9 Listening check

After Room EQ, before the room is called tuned. Play music you know well at the show level and sit at each point:
**Good** or **A problem**, a note (or pick words: harsh, boomy, muddy ...), and **Compared** once the EQ has been
heard in and bypassed. A problem at a point offers **Measure here again** (Room EQ, in the guided pop-up). **Done**
marks the room as tuned. The notes go in the tuning report.

### Processor sheet

The switch at the top says how your processor is laid out, kept with the venue:

- **One output per speaker:** every output gets its full settings (most small DSPs and consoles).
- **Group stage, then outputs:** a matrix or group stage feeding several outputs. Each group's line (filters, level,
  polarity, EQ) is entered once on the group, and each output under it gets its own delay and level trim. A speaker in
  no group keeps its full line.

Every setting to enter, in plain words for any processor or console: high-pass and low-pass
("Butterworth 12 dB/oct at 91 Hz"), delay (ms, with feet and metres), polarity, gain (the crossover's level plus the
level-balance trim) and the room EQ bands. Click a row to tick it as entered; a change to its settings unticks it.
Where a setting differs from how it was found this session, "was ..." shows the old value. **Copy as text**, **Export
CSV...** and **Export PDF...**. The Speakers step is ready when every output is ticked. Each output's **amp gain** and
**limiter** (from Gain and headroom) are listed with it, for the record.

**History** lists every change to an output's EQ, delay, trim and polarity, newest first: when, the step it was made
on and why (Room EQ entered, Level balance trim, Time alignment, edited on the sheet, undone ...). Select one and **Roll
back to before this** to put every output's processing back as it was then (the sheet changes, and `Cmd/Ctrl+Z` takes
the roll back back). The history is in the sheet's PDF and the tuning report.

The speakers, their responses and the settings are saved in the venue file.

## Monitoring

### Layout

The generator is not shown here: monitoring listens to the programme (the transfer function works on music and
speech too). The **mic picker** follows the mic whose point is FOH until you pick another one, and the Sound level
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

### 8 Room EQ

Measures the whole system at the listening positions and tunes it toward a target, as a loop: **measure as found,
suggest EQ, enter it in your processor, measure a check, refine, and mark it as left**. LiveAlign never connects to
your processor: you type the bands in, and press **I've entered this EQ** so LiveAlign knows what is in it.

- **Listening points** (left): every point of the venue plan, ticked to be measured, with what has been measured there
  (as found, check). Points are made on Venue, Plan and points (**Points on the plan**).
- **The target follows the system:** from the speaker responses measured earlier (with their processing), LiveAlign
  works out where the system plays (its -6 dB points) and its roll-off beyond. The target's shape applies inside that
  range and follows the system's own roll-off outside it, so a system with no sub is not asked for bass it cannot make
  ("No sub: below 95 Hz the target follows the tops' own low end"). The EQ stays inside the range (greyed out beyond),
  and the RMS from the target counts only there. When Room acoustics says EQ can only partly help, or to treat the room
  first, the card says so. Bands in the processor narrower than room EQ should be (from an earlier version) are flagged.
- **Rounds:** tick the positions to measure. With as many mics as positions they are all measured at once; with fewer
  (two mics for six positions, say) LiveAlign measures in rounds and says which mic to move where. A mic left at a
  position already measured sits the round out.
- **1 Measure:** **As found** first, then **Check 1, 2 ...** after each EQ you enter. Pink noise through the whole
  system; each mic's response is stored at its position.
- **2 Target and EQ:** the target, where the EQ goes (**Whole system**, the default; **Mains only**; or one speaker), how
  many bands your processor has free and how much boost to allow. **Suggest EQ** fits parametric bands to the average
  of the positions (each levelled to its own 500 Hz - 4 kHz, smoothed to 1/3 octave): gentle bands only (cuts Q 0.7 to 4,
  boosts Q up to 2, small and only for wide dips), within the speakers' range. With one position measured it says the
  EQ is provisional. The card lists the bands **to enter** and those **in your processor now**. After a check, **Refine
  the EQ** suggests the changes on top of what is entered (a band close to an existing one changes it instead of
  taking another).
- **Allow for the room acoustics** (under the target, on by default; the default is in Settings, Tuning defaults, and a
  venue can keep its own): once Room acoustics is measured, Suggest EQ
  - uses the median of the seats below the **Schroeder frequency** (from the room's size; 200 Hz assumed without it)
    and cuts only **room modes** that most seats share, each with a width that matches how long it rings there;
  - never boosts below it: a deep dip is marked "null: move, don't boost";
  - in octaves where the reverb is long, corrects the **direct sound** (a frequency-dependent window of each
    measurement's impulse response) rather than the reverb;
  - eases the target's **low end** down where the lows ring longer than the mids;
  - leaves alone the bands where the **noise** is too close (low coherence, or a short decay range in the sweeps);
  - makes **no boosts above 2 kHz** when speech clarity is poor;
  - keeps **cuts shallow** (6 dB) when the decision tree says to treat the room.
  Each shows on the graph (the Schroeder line, the modes, the nulls, the noisy and direct-sound bands) and as a line
  in the card. Off, Room EQ fits the average as measured, as before.
- **3 Check:** as found and the latest check against the target (RMS and worst), and the level spread across the
  positions. Within the "close enough" of Settings (1.5 dB RMS by default) it says so; **Mark the room as tuned (as left)**.

The graph shows each position (thin), the as-found and check averages, the target with a +/-3 dB band, and the
latest measurement with the suggested EQ (dashed). The report shows as found beside as left.

### Guided measuring

Every measurement goes through a pop-up that walks through the points one at a time (Speaker response, Level balance,
Time alignment, Room acoustics, Room EQ and the quick re-check):

- **The venue plan** with the point to measure now pulsing, the mic on it (its number and colour) and how it is aimed:
  a dot for pointing straight up, an arrow toward the speaker for aimed. Points already measured have a tick; the
  rest are outlined. A position that is not on the plan yet is placed for you (on an area of the same name, such as
  FOH, or spread across the seating); drag the pulsing point to where the mic really is and the plan keeps it.
- **Where each mic goes** in words underneath ("Mic 1 to Row 8, seated ear height, pointing up (90 degree file)"), with
  one line on how to set up. Anything to do first is the **First:** line in amber: mute the other speakers, enter a delay
  and unmute both, or the wiring changed since the checks. Pressing Measure confirms it.
- **Aim:** a picker with the angles the mic has calibration files for, when it has more than one.
- **Measure:** a three-second count-in ("stand clear of the mic"), then the progress with the time left. **Stop**
  cancels.
- **Next point** moves on (or **Measure again**; **Try again** if nothing reliable was measured). Room EQ also has
  **Skip this point**. The dots along the bottom show how far through you are. Keys: `Space` measure (again),
  `Enter` next point, `Esc` stop (then close).

Room EQ measures as many positions at once as there are mics, so with three mics and five positions it is two
rounds; a mic left at a position already measured sits the round out. Speaker response has one point per speaker (a
speaker set "Same as" another too), with the mic about 12 in from the cone and the speaker that plays ringed on the
plan, then one for each further cabinet on a shared output (these can be skipped); a point whose polarity is inverted
or unclear shows a red **!** on the plan. A **Mic** picker chooses the mic for that speaker. Time alignment has three
points at the spot where the two speakers meet: each alone, then both together (after the delay is entered).

### 10 Feedback test

With a microphone open on stage and the system running, slowly raise the gain. LiveAlign watches the spectrum and
flags a frequency that stands 12 dB or more above its neighbours (within a third of an octave) for several frames in
a row, the start of a ring. Pull the gain back, put in the suggested cut (a narrow bell, Q 10) and go again for the
next ring; tick each one once its cut is in. The rings, with the stage mic they were found on, go in the tuning report.

### 7 Room acoustics

Before Room EQ, because it decides whether EQ can help. **Measure with a sweep** plays a 4-second exponential sweep
twice through the whole system at each ticked listening point, in the guided pop-up. Dividing what the mic heard by
what was sent gives a long, clean impulse response (3.5 seconds), long enough for the reverberation of any room (a
church's 2 seconds included). The step turns green only from real sweeps; an estimate from the Room EQ pink noise
(whose impulse responses cover only about 0.7 s, so a long reverb reads short) shows as "Estimate only".

**What it means** is a decision tree, each node with your numbers: 1 measured? 2 usable (enough decay above the
noise, or sweep louder)? 3 reverberation against the room's use and size (the lows ringing longer than the mids is
bass build-up: traps, sub placement or a cardioid sub array, since EQ won't shorten decay; long mids and highs:
absorption on the side and back walls), with the room modes; 4 speech (STI) and late energy (an EDT much shorter than
the RT points to an echo or a coupled space); 5 the verdict: **Room OK for EQ**, **EQ will help only partly**, or
**Treat before EQ**. Room EQ shows the verdict when it is one of the last two.

- **RT60** (from T20 or T30, T15 for a short or noisy response) and **EDT** per octave band, 63 Hz - 8 kHz (63 Hz
  marked when its decay is too short to trust), and the RT per **1/3 octave** (100 Hz - 5 kHz), with a band that rings
  well above its neighbours flagged.
- **Bass ratio** (warmth: 125 + 250 Hz over 500 Hz + 1 kHz) and **treble ratio** (brilliance: 2 + 4 kHz over the same).
- **C50**, **C80**, **D50** and **centre time Ts** (clarity and definition).
- **STI** per position (IEC 60268-16 indirect method from the impulse response, male weights), and the STI **with the
  room noise**, auditory masking and the hearing threshold at the speech level in Settings (70 dBA by default), with
  **%ALcons**.
- **Reflections:** the **energy-time curve** with the first reflection (initial time delay gap), **echoes** (Dietsch and
  Kraak's echo criterion for speech and music, with how much further the echo travelled) and **flutter echo** (a
  regular repeat between parallel surfaces, with their spacing). EQ can't fix either: the decision tree says so.
- **Room size** (Length, width, ceiling height, or a volume): the **Schroeder frequency** and each speaker's **critical
  distance** (directivity typical for its role, or set per speaker by clicking the line).
- **Spread** of each measure across the points (average, standard deviation, range), and **coverage**: the level
  across the Room EQ points at 2 and 4 kHz, against the common +/-3 dB.
- **Room modes**: peaks that the positions share below about 300 Hz.
- **NC** and **NR** ratings of the background noise.

These are estimates from a live-sound measurement through the PA, not a certified acoustic survey (which uses an
omnidirectional source).

## Monitoring mic

The mic used during the event, usually just one at FOH, set up on its own: its input, calibration files (or pick it from
the Mic locker, or take one of the tuning mics), sound level calibration and where it is on the Venue plan. Live
monitoring and the VST3 use it; the tuning mics are kept for the Tuning steps.

## Live monitoring additions

- **Feedback watch:** the monitoring mic is watched for frequencies starting to ring in the programme (a narrow peak
  well above its neighbours that holds for more than a second). Each is listed with the time and the cut that would
  tame it, and marked in red on the RTA and the waterfall. Sustained notes can look the same, so judge by ear; **Clear**
  empties the list and the marks.
- **Delay tracking:** **Track** (Delay compensation) finds the delay on the programme every 5 seconds, using the
  console's feed as the reference, and follows it so the transfer function stays lined up. It shows how far the
  arrival has drifted since tracking started (temperature, a moved mic). During a service every speaker plays the
  same programme, so it tracks the system as a whole; lining up one speaker with another needs them played
  separately (Time alignment, with music from the console before the service).
- **Event panel:** the sound level logged every 5 seconds through the event, the rolling LAeq and a limit you set.
- **Against the tuning:** the programme's transfer function at the monitor mic, octave by octave, against what Room EQ
  measured at that point when the room was tuned (or the average of the points). A difference of 2 dB or more that
  holds suggests a small change, half of it and never more than 3 dB ("4 kHz is 2.5 dB up on the tuning: try -1.2 dB
  there"): an audience soaks up the low end, dry air or a full room takes the top off.
- **Target:** the target curve is anchored where you put it instead of following the measurement; **Re-level**
  lines it up with the current measurement again.

## Reports

Two reports, each its own page under **Reports**: pick the chapters (remembered on this computer), who prepared it,
and export the PDF. The cover lists the chapters in bold, numbered; every page is numbered "page x of y"; the venue
plan has its own landscape page.

- **Tuning report:** in the order of the steps: summary with the settings **as found** (when the session started)
  beside **as left**, the venue plan (tuning mics), speakers (polarity, crossover, time alignment, the responses as
  found and as left), level balance (as found and after the trims), room acoustics with its verdict, room EQ,
  feedback test, the processor sheet, mics and measurements.
- **Live monitoring report:** sound level of the event against the limit, the monitoring mic and its calibration, the
  venue plan (monitoring mic) and the traces captured during the event.

## Tuning report (details)

**Tuning report (PDF)...** (on Home and on the Venue plan page) writes a PDF of the open session: a cover with the
venue, session, date, who prepared it (Settings > Reports) and the analysis settings; the venue plan; the speakers
with model, channel, output and count; the settings for the processor (high-pass, low-pass, delay, polarity, gain),
the crossover, the parametric EQ and the time alignments; the speakers' responses with the processing and the top + sub sum; the
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

Settings (bottom of the navigation pane) is a full page: the categories on the left, one shown at a time, each with
**Reset to defaults**. Everything keeps the defaults earlier versions used until you change it.

- **Audio:** audio device (app only), reference, minimum coherence, and **listening** (below). Mics are set up on the Mics page.
- **Measurement:** transfer function (FFT size, averaging, smoothing, magnitude range) and RTA (resolution, speed,
  reference overlay, peak hold).
- **Display:** the tour; number of graphs, stacked or side by side, theme; impulse response (linear or ETC, time
  range); waterfall (history, colour range).
- **Units and environment:** temperature in Celsius or Fahrenheit, distance in feet or metres (the temperature and
  humidity of the day are on Venue, Details).
- **Tuning defaults:** aim and coverage (the spec), gain and headroom (the show level at FOH, the headroom wanted),
  Room EQ (target for new venues, close enough, narrowest cut and boost, bands and boost for new
  venues, deepest cut, allow for the room acoustics), level balance (left and right within, the fill's range, the sub
  within, the largest trim), the close-up distance, the speech level for the STI, and the quick re-check's
  tolerances. Each has a **This venue** column: pick a value to keep one for the open venue only (saved with it and
  listed in the report), or "Your default". **Clear this venue's own values** puts them all back.
- **Generator and safety:** the level at start, a ceiling it never goes above, a fade-in (it always stops quickly), and
  the acoustics sweep length.
- **Monitoring:** for new sessions, the event level limit, its weighting (LAeq or LCeq) and window, and the log step;
  the feedback watch's sensitivity; how often delay tracking follows the delay.
- **Reports and files:** prepared by, a logo for the reports' covers, paper size (Letter or A4), the folder Open, Save
  and the exports start in, and how often the recovery copy is saved.
- **Room sensor (MQTT):** a temperature and humidity sensor through an MQTT broker: see below.
- **Updates:** check now, and whether to check when LiveAlign starts.

In the VST3 the audio and tuning categories are hidden (the host owns the audio; the plug-in only monitors).

### Listening (headphones)

Hear the **reference** (what the console sends the PA) or **a mic** on headphones, to check what LiveAlign is
measuring. In the **Audio interface** window (Settings > Audio > Audio device), under the interface, or in Settings >
Audio > **Listening**: pick what to hear and where it plays:

- **A spare output of the measurement interface** (its headphone or a free line output). The list says which outputs
  feed a speaker: never pick one of those. The test signals never play on the listening output, even on All outputs.
- **Another device on this computer** (its headphone jack, a USB headset, a second interface), so the measurement
  interface keeps its inputs and its outputs for the speakers and the test signals. The two run on their own clocks:
  LiveAlign buffers about 40 ms and follows the drift between them, so there are no clicks.

The **level** (and **Mute**) is on the main screen at the bottom of the menu, above Settings, on every page. App only.

### Room sensor (MQTT)

A temperature and humidity sensor on your network, read through an MQTT broker: Home Assistant's **Mosquitto broker**
add-on, Zigbee2MQTT's broker, or any other. The sensor never changes the tuning by itself:

- **Venue, Details:** **Capture** takes the sensor's reading as this session's conditions, for tuning (the speed of
  sound for delays and distances, the air absorption at the back of the room). Undo takes it back.
- **Monitoring:** a **Room conditions (sensor)** panel shows the reading live, against the conditions the system was
  tuned in (else the session's), and what the difference does to the sound: how far the delays are now off (the speed
  of sound; amber when it is worth the quick re-check) and how much more or less the air takes off 8 and 16 kHz at
  the back of the room (ISO 9613-1).

Setting it up (Settings > **Room sensor (MQTT)**):

1. Tick **Connect to a sensor**.
2. **Broker:** its name or address (`homeassistant.local`, `192.168.1.10`) and port (1883). Plain MQTT on the local
   network; TLS (8883) is not supported.
3. **Login:** if the broker needs one. For the Home Assistant add-on, a Home Assistant user (or a login set in the
   add-on's options). Kept on this computer, never in venue files.
4. **Temperature topic** and **Humidity topic**, with the **sensor's unit** (Celsius or Fahrenheit). The payload can be
   a plain number (`21.4`) or JSON read by a key (`{"temperature": 21.4, "humidity": 45}`, key `temperature`; `a.b` for
   nested keys). Both can be the same topic. `+` and `#` wildcards work.
5. **Status** shows the connection and the latest reading. A reading older than ten minutes is stale and is not
   captured.

Where the topics come from:

- **Zigbee2MQTT:** the sensor's own topic, `zigbee2mqtt/<friendly name>`, with the keys `temperature` and `humidity`.
- **Home Assistant sensors** (any integration): turn on [MQTT Statestream](https://www.home-assistant.io/integrations/mqtt_statestream/)
  in `configuration.yaml` (`base_topic: homeassistant`, `include: entities: [...]`), then use
  `homeassistant/sensor/<entity>/state` (a plain number, in the sensor's unit).
- **ESPHome, Tasmota, Shelly:** the state topic the device publishes (Tasmota and Shelly send JSON: use the key, e.g.
  `AM2301.Temperature`).

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

`Cmd/Ctrl+S` save the venue, `Cmd/Ctrl+Z` undo, `Cmd/Ctrl+Shift+Z` redo, `F1` how to do this step, `G` generator,
`F` full screen, `Esc` leave full screen. On the Monitoring page:
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
Releases page, once the Linux checks pass: the engine tests, the tuning logic tests, the screenshot run with its pass /
fail checks, and [pluginval](https://github.com/Tracktion/pluginval) on the VST3 (strictness 5). JUCE and the compiled
objects are cached between runs. Local build (CMake 3.22 or newer; JUCE 9.0.3 is downloaded automatically):

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
reverb time, NC / NR), the feedback detector (a ring found, music and noise left alone) and polarity (tops and subs
wired either way, a sub timed several ms late, Main R against Main L, random phase giving "check"). And the sweep: the
impulse response deconvolved from two sweeps through a 1.8 s room gives its RT60 back. CI runs them with address and
undefined-behaviour sanitizers before building.

`-DLA_BUILD_TESTS=ON` also builds **AppTests** (with JUCE's core): level balance, the processor sheet, the quick
re-check's record, preferences and venue overrides, the Room EQ allowance, aim and coverage (with the hints), gain and
headroom (compression, distortion, the verdicts), seat-to-seat variance, the change history and roll back, the air
absorption and delay drift, and the venue list.

`-DLA_BUILD_PREVIEW=ON` builds **LiveAlignPreview**, which feeds a simulated PA and room (with a limiter in its amps)
through the processor and walks the whole flow: every page and the reports, with pass / fail checks along the way (it
exits non-zero when one fails). It runs on its own clock, driven by the audio it feeds, with a fixed random seed, so a
run does not depend on the computer's speed. `LiveAlignPreview <folder> --stop-after=level` stops after that screenshot
(a quick run of the early steps); text that does not fit its button, label or box is listed (`--strict-overflow` fails
on it).

## Standards and methods

Where a number follows a published standard, LiveAlign says so (the glossary, the page, the report cover), so the
result is comparable with other tools: room acoustics (RT from T20 / T30, EDT, C50, C80, Ts) per **ISO 3382-1**, from
swept-sine impulse responses per **ISO 18233**; STI per **IEC 60268-16** (with %ALcons from it); A- and C-weighting,
Leq and the fast / slow meter per **IEC 61672-1**; NC per **ANSI S12.2** and NR per **ISO 1996**; air absorption per
**ISO 9613-1**; coverage uniformity in the spirit of **AVIXA A102.01**; the acoustic calibrator per **IEC 60942**.
The transfer function is a dual-channel FFT measurement as in Smaart or SIM.

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
