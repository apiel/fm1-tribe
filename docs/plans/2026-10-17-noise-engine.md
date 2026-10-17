# NOISE engine and the shared SVF

## Goal

A mono engine built on noise. The noise goes through a filter with a choice of type. The
note moves the filter, so with a resonant band-pass the noise rings at the note. It is a synth
(`EK_SYNTH`) like LEAD and FM4, so it takes notes, an envelope, a slide and the clip's LFO.

Earlier versions had an oscillator that morphed through square, noise, saw, noise and tri.
It was removed: the engine is noise only, which is simpler and is what the engine is for.

## Sound

- **NType** picks the colour of the noise: white, pink or brown. Pink is a three-pole filter on
  white (Paul Kellet's economy form). Brown is a leaky integrator of white. Pink and brown are
  scaled to the rms of white.
- **Drive** is the gain into the filter, through a soft limit.
- **Output** is scaled so that a default note is about as loud as LEAD's (rms 0.17).

## Filter

- **FType**: LP, HP, BP, Notch.
- **Cutoff** 40 Hz .. 12 kHz, exponential over 100 semitones, as LEAD's.
- **Reso**: the damping `k = 2 * 2^(-5.6 * reso)`, so Q 0.5 .. 24 for LP, HP and BP. The notch
  stops at Q 4 (`k = 2 * 2^(-3 * reso)`): a narrower notch is too thin to hear on noise.
- **BP is constant-energy**: its gain is `sqrt(k)`, not the unit-peak `k`. White noise through a
  unit-peak band-pass loses energy as Q rises, so the resonant band went almost silent. With
  `sqrt(k)` the band keeps its loudness and its peak grows with Q: it rings.
- **KeyTrk** moves the cutoff with the note: 0 is no tracking, 127 is one semitone of cutoff
  for each semitone of note. The note is the only pitch input, so this is the note's job.
- **EnvMod** opens the cutoff with the envelope, up to 60 semitones.
- The filter is the shared SVF (`firmware/src/dsp/svf.{h,c}`), so other engines can use it later.

## Envelope

LEAD's envelope: Attack, Decay, Sustain, Release. It sets the level and the filter's EnvMod.
A note over a sounding one with the slide flag keeps the envelope (the note changes the
filter). A note without it restarts the attack from the current level. The engine goes idle
when the release ends.

## Parameters

| Pot | Range | Default | Page | What it does |
| --- | --- | --- | --- | --- |
| NType | White, Pink, Brown, Dust | White | EDIT 1 | the colour of the noise; Dust is sparse clicks |
| FType | LP, HP, BP, Notch | LP | EDIT 1 | the filter type |
| Cutoff | 0..127 | 70 | EDIT 1 | 40 Hz .. 12 kHz, exponential |
| Reso | 0..127 | 40 | EDIT 1 | Q 0.5 .. 24; the notch: Q 0.5 .. 4 |
| KeyTrk | 0..127 | 64 | EDIT 2 | the cutoff follows the note |
| Mod | Off, Sine, Rand, Drift, Gate | Off | EDIT 2 | a built-in modulation, in floats (not stepped by the pots) |
| ModAmt | 0..127 | 0 | EDIT 2 | its depth: up to 2 octaves on the cutoff, or the depth of the gate |
| ModRat | 0..127 | 40 | EDIT 2 | its rate, 0.1 .. 51 Hz, exponential |
| Attack | 0..127 | 20 | ENV 1 | 1 ms .. 2 s |
| Decay | 0..127 | 64 | ENV 1 | 5 ms .. 2.5 s |
| Sustain | 0..127 | 110 | ENV 1 | the level a held note settles at |
| Release | 0..127 | 50 | ENV 2 | 5 ms .. 3 s |
| EnvMod | 0..127 | 50 | ENV 2 | filter envelope depth, 0 .. 60 semitones |
| Drive | 0..127 | 0 | FX | gain into the filter, 1 .. 8 |

## The LFO (all synth engines)

The rate was a set of ten divisions in sync and 0.05–25 Hz free, updated once per audio block
(about 172 times a second). Now:

- **Rate** is one continuous pot. Synced: 1/64 to 8 bars (`LFO_STEPS`). Free: 0.02 Hz to 100 Hz
  (`LFO_HZ`), in two parts: 0.02 to 1 Hz in the first 40 positions, 1 to 100 Hz in the other 87.
  The synced labels are the octaves of the range.
- **Glide**: a change of rate glides with a 30 ms time constant. A change of sync snaps.
- **Update**: every 32 samples (1378 times a second), so a fast LFO is not stepped.
- **Default**: 85, which is 1 bar in sync.
- The encoder moves the rate one step a click; the other LFO controls keep 2.

Projects saved before this change keep their rate pot values, which now mean a different speed.

## Built-in modulation (noise engine)

The noise engine has its own modulation, in floats, so it is smooth and not stepped by the pots.
It is not a second LFO. Each mode is a choice of Mod:

- **Sine**: the cutoff follows a sine at ModRat, up to 2 octaves each way at full ModAmt.
- **Rand**: the cutoff takes a new random value each cycle (a stepped sample and hold).
- **Drift**: the same, but glides to each new value (about 35 ms).
- **Gate**: the level pulses at ModRat, softly; ModAmt is how far it drops.

## Tests (`tests/host/`)

- `svf_test.c`: LP passes DC and HP blocks it; the band-pass peaks at `1/sqrt(k)` at its centre;
  the notch nulls at its centre; the filter stays finite at the corners.
- `noise_test.c`: finite at the corners; the three colours differ in crossings and level; HP
  passes more noise than LP at 40 Hz; KeyTrk brightens with the note; a resonant band-pass keeps
  its loudness; a resonant band-pass rings at twice the frequency for an octave up; the notch
  moves with its cutoff and its width follows the resonance; the default level is LEAD's; a
  slide keeps the envelope; the release ends in silence; two instances do not touch.

## Wiring

- `firmware/src/tribe/tribe.h`: `F_NOISE` appended after `F_FM4` (projects store engines by index).
- `firmware/src/tribe/engines.c`: the table entry `NOISE` (cost 4), the parameter tables, the
  instance pool (`NNOISE` 2), and the switch cases.
- `tools/build.py`, `host/build_host.sh`, `web/emu/build.sh`, `tests/run_tests.sh`: the new sources.

## Effects (the FX page)

The chain is: noise, then the drive, the filter, the crusher, the sample-rate reduction, the comb, the
envelope.

- **Drive** (1 .. 16) with **DrvTyp**: Soft (tanh), Hard (clip), Fold (a sine fold, which adds harmonics as
  the drive rises). Before the filter.
- **Crush**: the bit depth, from off down to 3 bits (9 levels).
- **CrshRt**: the sample-rate reduction: off, or up to 48 samples held.
- **Comb** and **CombFb**: a delay line with feedback whose length is one period of the note, so the
  noise rings at the note. The gain is normalised so a high feedback does not get louder.

The delay line is 1024 samples per instance (4 KB), so RAM is now 86.7 KB of 98.3 KB.

## Out of scope, for later

- A formant sweep, and modulation of the comb's pitch or feedback.
