# NOISE engine and the shared SVF

## Goal

A mono engine whose sound is built around noise. An oscillator morphs through five anchors,
square, noise, saw, noise, tri, and the noise is always there. A filter with a choice of
type shapes it. The engine is a synth (`EK_SYNTH`) like LEAD and FM4, so it takes notes,
an envelope, a filter envelope, a slide and the clip's LFO.

## Sound

- **Morph** runs 0..127 across four segments. The anchors are square (0), noise (1/4), saw
  (1/2), noise (3/4) and tri (1). Between two anchors one wave fades out while the next one
  fades in, so square goes to noise, noise goes to saw, saw goes to noise, noise goes to tri.
- **Noise** is the noise floor. It is always mixed in, at this level, at every point of the
  morph. At a noise anchor the oscillator is gone and the noise is at full level, so there
  the engine is noise alone. Between the anchors the oscillator's level falls as the noise
  rises.
- The oscillator is a square, a saw or a tri, with polyBLEP on the square and the saw, as in
  LEAD. The tri is `2 * |2p - 1| - 1`, which has few harmonics and needs no correction.
- **NType** picks the colour of the noise: white, pink or brown. Pink is a three-pole
  filter on white (Paul Kellet's economy form). Brown is a leaky integrator of white.

Mixing per sample, with `g_o` the oscillator's gain (0 at a noise anchor, 1 at a square,
saw or tri anchor) and `n` the noise floor in 0..1:

    mix = g_o * osc + (n + (1 - n) * (1 - g_o)) * noise

## Filter

- **FType**: LP, HP, BP, Notch. Notch is low + high.
- **Cutoff** 40 Hz .. 12 kHz, exponential over 100 semitones, as LEAD's.
- **Reso** from Q 0.5 up to about Q 24. The damping is `k = 2 * 2^(-5.6 * reso)`.
- **KeyTrk** moves the cutoff with the note: 0 is no tracking, 127 is one semitone of cutoff
  for each semitone of note.
- **EnvMod** opens the cutoff with the envelope, up to 60 semitones.
- The filter is the shared SVF (`firmware/src/dsp/svf.c`), so other engines can use it later.

## Envelope and gain

The envelope is LEAD's: Attack, Decay, Sustain, Release. It sets the level and the filter's
EnvMod. A note over a sounding one with the slide flag only changes the pitch. A note
without it restarts the attack from the current level. The engine goes idle when the
release ends.

## Parameters

Pots are 0..127 unless a switch says otherwise. The defaults are what a fresh engine starts with.

| Pot | Range | Default | Page | What it does |
| --- | --- | --- | --- | --- |
| Morph | 0..127 | 0 | EDIT 1 | the oscillator's position in the morph (square, noise, saw, noise, tri) |
| Noise | 0..127 | 32 | EDIT 1 | the noise floor: the noise level at every point of the morph |
| NType | White, Pink, Brown | White | EDIT 1 | the colour of the noise |
| Tune | 0..127 | 64 | EDIT 1 | +-1.5 semitones |
| FType | LP, HP, BP, Notch | LP | EDIT 2 | the filter type |
| Cutoff | 0..127 | 70 | EDIT 2 | 40 Hz .. 12 kHz, exponential |
| Reso | 0..127 | 40 | EDIT 2 | Q 0.5 .. 24 |
| KeyTrk | 0..127 | 64 | EDIT 2 | cutoff follows the note |
| Attack | 0..127 | 20 | ENV 1 | 1 ms .. 2 s |
| Decay | 0..127 | 64 | ENV 1 | 5 ms .. 2.5 s |
| Sustain | 0..127 | 110 | ENV 1 | the level a held note settles at |
| Release | 0..127 | 50 | ENV 2 | 5 ms .. 3 s |
| EnvMod | 0..127 | 50 | ENV 2 | filter envelope depth, 0 .. 60 semitones |
| Drive | 0..127 | 0 | FX | gain into the filter, 1 .. 8 |

The pages come from the parameter names (`eng_group` in `engines.c`), as for the other
engines. Drive is on FX, the envelope names are on ENV, and the rest are on EDIT.

## The shared SVF (`firmware/src/dsp/svf.{h,c}`)

Andrew Simper's zero-delay state-variable filter. It takes a cutoff in Hz and a damping
`k = 1/Q`, and gives low-pass, band-pass (scaled by `k`, so its peak is near unity) and
high-pass outputs from each sample. The coefficients are set at the control rate, every 8
samples, as LEAD does. The cutoff is clamped to 20 Hz .. 0.45 x the sample rate.

## Tests (`tests/host/`)

- `svf_test.c`: DC gain of LP is 1 and of HP is 0; the band-pass peaks near 1 at its
  centre; the notch nulls at its centre.
- `noise_test.c`: finite and bounded at the corners of the controls; the square anchor has
  the pitch asked; the noise anchor is not pitched and is loud; the noise floor is present
  on a wave anchor; HP has more energy than LP on noise; the noise colours differ in their
  zero-crossing rate; a slide keeps the envelope; the release ends in silence; two
  instances do not touch each other.

## Wiring

- `firmware/src/tribe/tribe.h`: `F_NOISE` appended after `F_FM4` (projects store engines by index).
- `firmware/src/tribe/engines.c`: the table entry `NOISE` (cost 4), the parameter tables,
  the instance pool (`NNOISE` 2), and the switch cases.
- `tools/build.py`, `host/build_host.sh`, `web/emu/build.sh`, `tests/run_tests.sh`: the new
  sources.

## Out of scope

- A per-engine sample-rate or bit-rate reduction.
- MIDI, which TRIBE does not have yet.
- A separate noise level on the wave anchors. The noise floor covers that case for now.
