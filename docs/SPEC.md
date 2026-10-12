# TRIBE – design spec (draft v0)

10-track, engine-per-track groovebox for the M-VAVE FM-1. Fork of X0X (itself a fork of Felucca):
reuses its hardware layer, USB, loader, storage, and web emulator approach.

## Engines (initial)
- ZicModKick: KickWave, PotKick (ported from zicBox/zicModKick)
- X0X 909 voices (kick, snare, toms, rim, clap, hats, cymbals), 808 voices
- X0X 303 (Open303) – heavy, limit number of simultaneous instances
- More later: engine interface must be pluggable (see below)

Engine interface (to define): `init`, `note_on(note, vel, params)`, `render(buf, n)` (mono),
param table (name, range, default, group = FX/ENV/LFO/EDIT, page), `is_synth` (step edit layout).

## Tracks
- 10 tracks, each mono, each assigned any engine (chosen in GLO). Engine is per clip.
- 16 clips per track per project.

## Controls
| Control | Behaviour |
|---|---|
| Keyboard row 1, keys 1-10 | Select track |
| Row 1, key 11 (last) | Mute: hold + track key toggles mute |
| Keyboard row 2 | Sequencer steps; Oct-/Oct+ change step page (as X0X) |
| FX / ENV / LFO / EDIT | Show that param group of current engine. Unlit if engine has none; 50% if available; 100% if shown. Press again = next page when >4 params; view shows page indicator |
| ARP | Whole keyboard plays selected track. Hold = arp settings |
| SEQ | Back to sequencer mode (track select) |
| Step toggled on | 4 encoders: drum = note/vel/prob; synth = note/vel/len/slide. Long-press an already-on step to edit |
| REC | Whole keyboard available for recording |
| SAVE | Save current project |
| GLO | Global: load project, engine per track, master-knob FX assignment, more later |
| HOME | Row1 = track, row2 = clip select. Press again = scatter FX page (row2 triggers scatter FX) |
| SEL | TBD (ideas below) |
| SELECT / PRESETS / ALGORITHM knobs | Master FX (DJ filter à la MC101, +1 more); assignable in GLO |

## SEL ideas
1. Shift/modifier (copy/paste/clear step, clip, track; hold SEL + step = copy)
2. Track-scope toggle: parameter edits apply to track vs. current step (p-lock)
3. Fill/roll: hold SEL + step = note repeat/ratchet
4. Mixer page: encoders = level/pan-less (mono)/send per track

## Hardware facts (confirmed)
- Keyboard: 16 white keys (row 2) + 11 black keys (row 1). Row 1: keys 1-10 = tracks, key 11 = mute.
  Row 2: 16 keys = 16 steps per page = 16 clips (fits exactly).
- MASTER pot is read in software (SARADC ch4, `fm1_adc_read(FM1_ADC_MASTER)`, see X0X
  `main_fm1.c`), so it is assignable: 4 master knobs = MASTER, SELECT, PRESETS, ALGORITHM.
  Master volume moves to GLO, and can optionally be assigned back to any knob.
- Encoders: KNOB1-4 (step edit + param pages). OCT-/OCT+ buttons. Dedicated PLAY/STOP.

## Decisions
- Tempo lives in GLO. Master limiter on the output. MIDI deferred.

## Decisions (round 2)
- Clip stores everything for its track (pattern, engine, params). 16 clips/track/project.
- Clip launch: 1st press = queued, switches at end of pattern; 2nd press (while queued/same clip) = immediate.
- Track length: 16, 32 or 64 steps (per track).
- Drum engines are pitched; step note is editable (essential for kicks).
- Master knobs (MASTER, SELECT, PRESETS, ALGORITHM): each is a *slot* assignable in GLO to
  (a) a master FX from a registry, (b) an FX bound to specific track(s) (e.g. send/insert on tracks 2+5),
  (c) a 2-parameter FX (e.g. cutoff+resonance uses two params of one knob or two knobs),
  (d) a macro (one knob -> N params with ranges), or (e) a single parameter of any track.
  FX registry is pluggable like engines; zicBox effects to port: bitcrusher, decimator, drive,
  clipping, waveshape, ring mod, reverb (tinyReverb), flanger, tremolo, bandpass, filter, compressor.
- Scatter FX (HOME pressed twice, row 2 keys trigger): from zicBox `audio/Scatter.h`, 7 modes:
  comb LFO, gater, decimate+distortion, reverb+delay, bitcrush/crunch, hard-clip drive, acid
  resonant sweep + ring mod; plus Kick-Repeat stutter (zicPixelDrift). 16 keys = 16 slots,
  momentary or latched. Port notes: Scatter.h uses `double` (11 places) and big buffers
  (192000 samples / 131072 FX buffer) -> must be float/fixed + shrunk for FM-1 RAM.
- Emulator: like X0X, compile the same C sources to WebAssembly with Emscripten (`web/emu/`),
  plus a host build (`host/`) for scripted tests. Needs `emcc` (not installed yet) and a host cc.

## Decisions (round 3)
- Second row of keys while PLAYING in the FX/ENV/LFO/EDIT (and GLO) pages: scatter FX, not notes (stopped:
  audition notes). The SCATTER page (HOME twice) is the same 16 keys. Keys 1-8 act on the selected track, 9-16
  on the whole mix. Momentary (held), one effect per scope; track loop effects share 3 rings, the mix has one.
- The plain keyboard is the KEYS mode of ARP: ARP page knobs = MODE (KEYS/UP/DOWN/UPDN/RAND), RATE (1/16, 1/8, 1/4),
  GATE, OCT. The arpeggiator runs with PLAY (stopped: keys play directly). REC keeps its own keyboard.
- Scatter slots: T: 1/64 1/32 1/16 (loop), REV, GATE, CRSH, DIST, RING; mix: 1/32 1/16 1/8 (loop), REV, GATE, CRSH, LPF, HPF.

## Decisions (round 4): saving
- No project file, no autosave. The clips are the data: `firmware/src/tribe/store.c` keeps a log of 512-byte records
  (one per saved clip, one for the settings) in 48 flash sectors from 0x97000; newest valid record of a clip wins,
  a save writes only changed clips (header last, then read back), garbage collection keeps an erased spare sector.
- SAVE button: press and release = save every changed clip + settings; hold SAVE and tap a clip key on the CLIPS page = save
  that clip only. The SAVE light glows while something is unsaved. GLO lists the number of unsaved clips.
- Later, maybe: a shared pool of clips that any track can load (the 16 slots per track become quick-load spots);
  more than 16 clips per track by reading clips from flash when queued. Conflict question (the same clip on two tracks)
  is left open on purpose.

## Status (phase 1 done: host + browser)
Done: engine registry (`firmware/src/tribe/engines.c`: 909 and 808 voices, 303, KickWave, PotKick), per-track
render hooks in the 909/808 DSP (`vout`), 10-track sequencer with 16/32/64 steps, 16 clips per track with
queued (end of pattern) and immediate launch, mute, master filter/compressor/limiter (X0X master.c),
the UI (SEQ, FX/ENV/LFO/EDIT pages, CLIPS, GLO basics, ARP/REC keys), host simulator with scripts,
browser emulator (`run_emu.sh`), tests (`tests/run_tests.sh`).
Not yet: REC quantise,
master-knob slot assignment and FX registry, SEL, GLO pages, MIDI, LFO params (no engine has any yet),
(device build now done: `python3 tools/build.py` with JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/fw-AC79_AIoT_SDK -> build/tribe.fwsc; never flashed or run yet).
Known approximations: kit-voice pitch (909/808) shifts the TUNE pot 1.5 units per semitone; the 303 takes
slide as on/off; kick level trimmed 0.25 (zicBox's drive leaves +-3). Nothing is verified on hardware or by ear.

## Decisions (round 5)
- Track LEVEL lives in two places: the first knob of the EDIT page (it heads the track's EDIT list, even for a track with no engine)
  and KNOB 1 on the CLIPS page (HOME). It belongs to the track, not the clip, and is saved with the settings.
- A mixer page on SEL is still free.

## Decisions (round 6): the master knobs
- Four slots: MASTER (a pot, absolute), SELECT, PRESETS, ALGORITHM (encoders, relative, 2 steps a detent). Each is
  one of: nothing, VOLUME, one parameter of an effect, or a MACRO. Defaults: MASTER volume, SELECT DJ filter,
  PRESETS compressor, ALGORITHM drive.
- Effects (`tribe/mfx.c`, plus the master section's COMP): FILTER (DJ: LP left of the middle, HP right, off in the
  middle; RESO), DRIVE (AMT, MIX), CRUSH (AMT, MIX), COMP (AMT). Each parameter can sit on any knob, so cutoff
  and resonance can be two knobs. An effect runs on the whole mix, or on the tracks of its mask (black keys on the
  KNOBS page; the MUTE key = all); a neutral effect costs nothing.
- A MACRO moves up to four track parameters (a track's LEVEL or any engine pot) from one knob; SEL inverts the target.
  It writes the pot like turning it, so it changes the clip (and the SAVE light).
- MASTER as anything but volume acts only after it has been moved (no jump at power-on or when assigned). The volume
  then comes from GLO > VOLUME, or from another knob assigned to VOLUME.
- Where: GLO, KNOBS row, SEL opens the page. K1 slot, K2 what it does, K3/K4 macro target and pot.
- Not done: macro ranges (always the full 0..127, or inverted); more effects (delay, reverb, tilt, ...);
  an effect's tracks on COMP; the settings record is bigger, so settings saved by an earlier build are ignored once.
- (round 6b) DELAY (MIX, FDBK: a dotted eighth, darker repeats, 16-bit ring 32 KiB) and REVERB (MIX, SIZE: four damped
  combs + two allpasses, 24 KiB) join the list; they always act on the whole mix, after FILTER, DRIVE, CRUSH. The
  mix-scatter ring is separate; track loop-scatter rings went from 3 to 2 to pay for the memory.

## Decisions (round 7): REC and LFO
- REC quantises to the nearest 1/16 step: the first half of a step rounds back, the second half forward (that
  note then sounds when its step comes, not twice). A synth note's length is the time the key was held (1..32 quarter
  steps). New steps get default prob/len/slide. Only while playing; stopped, REC just plays.
- One LFO per clip (saved with it), on one pot of its engine: SHAPE (sine tri saw square random), RATE (synced
  1/64..8 bars, or free 0.05..25 Hz), DEPTH (0 = off; +-half the pot's range at full), TARGET (the pot), SYNC, RETRIG
  (restart on each note; random takes a new level). LFO button, two pages. Updated once a block (172 Hz): slow
  modulation, not audio rate. The pot goes back to the clip's value when the depth is 0 or the target moves.
- Clips saved before the LFO (356 bytes) still load, with the LFO off.

## Decisions (round 8): SEL, sends, LFO, scatter
- SEL = the COMMON page, the pots every engine has: ENGINE (of the clip on the track), VOLUME (the track's level),
  SEND 1 (to FX1 = the delay), SEND 2 (to FX2 = the reverb). The page also shows all ten tracks' volume and sends as bars.
  (In GLO > KNOBS, SEL still opens the page / inverts a macro target.)
- Sends: per track, in the settings, post-fader and post-mute, default 127 (open): the effect's own MIX on the knobs page
  is its return level. Delay and reverb now return into the mix before the mix scatter and the FILTER / DRIVE / CRUSH
  (so a DJ filter takes the echo and the room with it); they have no track masks (the sends are that).
- The LFO is for synth engines only (the 303 now). A drum hit is too short for it; drums have no LFO page and the LFO
  button stays dark. (Retrig + random on a drum would give per-hit variation: left out until asked for.)
- Kick P: the click is gone (disabled in the DSP, the two pots hidden but kept so saved clips keep their numbers).
- Scatter, track row: 1/32 1/16 1/8 REV GATE CRSH DIST RING (1/64 removed, 1/8 added); mix row unchanged.

## Decisions (round 9): the sequencer's config (SEQ held)
- Hold SEQ (about 0.4 s): a page with the clip's LENGTH (16/32/64), RATE (1/16, 1/8, 1/32, 1/16T, 1/8T, 1/32T),
  DIRECTION (FWD, REV, PING-pong, RAND) and SWING (0..100 %); the four knobs edit them, letting go goes back to the steps.
  All four are per clip and saved with it (zeros = the old behaviour, so older clips load unchanged).
- The clock is now a 1/96-note tick; each track steps on its own schedule (rate, swing), arpeggiators and LFOs stay on 1/16.
  Swing delays the second step of each pair by up to a third of a step (whole ticks: coarse at fast rates). A pattern
  ends after `length` steps in any direction (that is where a queued clip takes over). Synth gate lengths are a share of
  the clip's own step. REC rounds to the track's own step (and its next step in its direction).
- In SEQ the knobs are only the step's (NOTE VEL PROB / NOTE VEL LEN SLIDE); with no step picked they are greyed and inert.

## Decisions (round 10): battery, no scatter page, always-on compressor
- Battery: an icon (three bars) at the left end of the knob strip's rule, on every page that has the strip; amber at one
  bar, red and blinking when empty, "USB" beside it when powered over USB; GLO has a BATTERY row (the last).
- HOME has one page, the clips. The scatter keys live in the parameter pages (FX / ENV / LFO / EDIT, and GLO) while playing;
  the separate SCATTER page is gone.
- The master compressor is always on and has no knob: 2:1 from -12 dB, 8 ms attack, 110 ms release, +2 dB makeup, then the
  limiter. PRESETS is now the delay's MIX and ALGORITHM the reverb's MIX by default; a saved assignment to the old COMP
  knob is replaced by the delay's.

## Decisions (round 11): ARP > TRSP
- A sixth ARP mode, TRSP: a key is a transposition in semitones from middle C (key note - 60, the OCT buttons move the keyboard by
  12), kept by the track until the next key; middle C = 0. The track's sequence plays transposed (every engine; kicks and 909/808
  voices change pitch). Not saved with the clip. Leaving TRSP puts the track back to 0. While it is on the SEQ page shows
  "TRSP +n". Stopped, a key also plays the note so you can hear it. RATE / GATE / OCT are greyed (they are for the arpeggio modes).

## Decisions (round 12): master FX slot, compressor menu, splash
- Defaults: MASTER = volume, SELECT = the master FX (type chosen in GLO > MASTER FX: DRIVE, CRUSH, RING, DECIM, VINYL; the knob is
  its AMT, the second parameter is assignable on the KNOBS page as "MASTER FX 2nd"), PRESETS = DJ filter, ALGORITHM = its resonance. (superseded by round 14)
- New effects: RING (carrier 40 Hz..2 kHz), DECIM (sample-rate reducer), VINYL (band-limit, hiss, pops, WEAR); they can have track masks.
- The master compressor is always on and set in GLO (COMP THRESH / RATIO / ATTACK / RELEASE / MAKEUP / MIX; defaults -12 dB, 2:1,
  ~2.6 ms... see project.c), saved with the settings. GLO is a scrolling list of 14 rows.
- Start-up screen: TRIBE in 3x pixel letters (a track colour each), ten squares that light up, "10 TRACKS ANY ENGINE", credit line
  "BASED ON X0X AND FELUCCA". About 1.3 s on the device; the browser build shows it too. Crash / safe-mode texts say TRIBE.

## Decisions (round 13): GLO and the mixer page cleaned up
- GLO rows: TEMPO, MASTER FX (the type), the four knobs (MASTER / SELECT / PRESETS / ALGO KNOB: K2 changes what each does), six COMP rows,
  CLIPS, CPU, BATTERY. No ENGINE (it is on the SEL page) and no VOLUME row (the volume is the MASTER knob; if MASTER is given another job, a
  knob set to VOLUME, else full). The old "KNOBS: SEL OPEN" row is gone: SEL on a knob row opens its details only when it has some (a
  macro's targets, the tracks an effect acts on), else it says so.
- The bottom strip: GLO shows Scroll (n/15) and Edit (the row's value), plus "SEL MORE" when the row has details; REC shows no knobs.
- Only the master FX the GLO menu chose runs of DRIVE / CRUSH / RING / DECIM / VINYL; the others are skipped whatever their amounts. They are no
  longer on the knob assignment list separately (the master FX 1st / 2nd parameter are).
- SEL glows at 50% (full on its page). Track sends start at 0. The SEL page shows a live level meter per track (after volume and mute) with the
  volume as a white tick and thin bars for the two sends.

## Decisions (round 14): FX 1 / FX 2, GLO rows for every master job
- FX 1 and FX 2 are two master effects every track sends to (SEL page, post-fader, post-mute). Each is one of REVERB, DELAY, DRIVE, DISTORT, FUZZ,
  WAVSHAPE (a wavefolder), CRUSH, DECIM, RING (chosen in GLO; defaults FX 1 = DELAY, FX 2 = REVERB). AMOUNT is the return level, PARAM the effect's
  own control (SIZE, FDBK, DRIVE, FUZZ, FOLD, BITS, RATE, FREQ; its label in GLO follows the type). VINYL is gone, and so is the "only the master FX
  the menu chose runs" rule: the old per-effect DRIVE / CRUSH / RING / DECIM on the mix no longer exist, they are send effects now.
- The reverb and the delay have one buffer each: the type step skips the one the other slot has, and if a saved project ever has both the second is off.
- GLO rows: TEMPO, VOLUME, DJ FILTER, DJ RESO, DJ TRACKS (black keys; MUTE = whole mix), FX1 TYPE / AMOUNT / PARAM, FX2 TYPE / AMOUNT / PARAM, the four
  knob rows, six COMP rows, CLIPS, CPU, BATTERY (24). The VOLUME row only counts while MASTER is not the volume pot (it then says MASTER KNOB).
- A knob (MASTER pot, SELECT / PRESETS / ALGO encoders) does one of: OFF, VOLUME, DJ FILTER, DJ RESO, FX1 AMOUNT, FX2 AMOUNT, BPM, MACRO. Every one
  edits the same byte as its GLO row. BPM: an encoder moves a whole BPM a detent, the pot spans 30..300. Defaults: MASTER volume, SELECT DJ filter,
  PRESETS FX1 amount, ALGORITHM FX2 amount (DJ reso is in GLO only until a knob takes it).
- The settings record changed again: settings saved by an earlier build are ignored once (clips are kept).

## Decisions (round 15): SAVE, SEL
- SEL no longer toggles back to SEQ: it opens the SEL page and stays there (SEQ leaves it).
- SAVE opens a pick: SAVE glows at half, every other button is dark, the track keys whose clip on screen changed blink and so does GLO if the
  settings changed. A track key saves that clip, GLO saves the settings, SAVE again saves everything changed (any clip, not just the ones on
  screen), any other button or key leaves. The pick stays on screen (a modal) until a track or GLO is picked, then closes. With nothing to save, SAVE just says NOTHING TO SAVE and opens nothing.
- Only what the MASTER knob is set to do is not saved (its volume, DJ cutoff, FX amount, ... : not in the settings' CRC either); at start it is read from the pot. Every other setting, the volume included when MASTER is something else, is saved with GLO.
- Loading a clip (HOME) that was changed and not saved puts it back as last saved. A clip that was never saved has nothing to go back to and keeps its edits.

## Decisions (round 16): simpler knobs
- Removed: the DJ filter's track mask (DJ TRACKS row), the MACRO assignment, OFF, and the KNOBS page that existed for them. The DJ filter always acts on the
  whole mix; a knob does one of VOLUME, DJ FILTER, DJ RESO, FX1 AMOUNT, FX2 AMOUNT, BPM. GLO has 23 rows. The SEL page is view 6 now.
- The settings record shrank (no masks or macros): settings saved by an earlier build are ignored once.
