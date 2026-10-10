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

## Status (phase 1 done: host + browser)
Done: engine registry (`firmware/src/tribe/engines.c`: 909 and 808 voices, 303, KickWave, PotKick), per-track
render hooks in the 909/808 DSP (`vout`), 10-track sequencer with 16/32/64 steps, 16 clips per track with
queued (end of pattern) and immediate launch, mute, master filter/compressor/limiter (X0X master.c),
the UI (SEQ, FX/ENV/LFO/EDIT pages, CLIPS, GLO basics, ARP/REC keys), host simulator with scripts,
browser emulator (`run_emu.sh`), tests (`tests/run_tests.sh`).
Not yet: SAVE/LOAD (flash layout for ~55 KB of clips), scatter FX, ARP engine + settings, REC quantise,
master-knob slot assignment and FX registry, SEL, GLO pages, MIDI, LFO params (no engine has any yet),
(device build now done: `python3 tools/build.py` with JIELI_TOOLCHAIN=~/.jieli/toolchain AC79_SDK=~/fw-AC79_AIoT_SDK -> build/tribe.fwsc; never flashed or run yet).
Known approximations: kit-voice pitch (909/808) shifts the TUNE pot 1.5 units per semitone; the 303 takes
slide as on/off; kick level trimmed 0.25 (zicBox's drive leaves +-3). Nothing is verified on hardware or by ear.
