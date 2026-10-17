#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# TRIBE tests: the host simulator runs every scenario (tests/scenarios/*.tribe: buttons, keys, knobs,
# screenshots and audio into build/scen/), then, if emcc is there, the browser build in node.
#   tests/run_tests.sh
set -u
cd "$(dirname "$0")/.."
FAIL=0
sh host/build_host.sh >/dev/null || exit 1
mkdir -p build/scen
for s in tests/scenarios/*.tribe; do
    case "$s" in *save1.tribe|*save2.tribe) continue;; esac
    if build/host/tribe_host "$s" build/scen >build/scen/$(basename "$s").log 2>&1; then echo "  ok   $(basename "$s")"
    else echo "  FAIL $(basename "$s")"; tail -5 build/scen/$(basename "$s").log | sed 's/^/       /'; FAIL=1; fi
done
SRCS_ALL="firmware/src/tribe/engine.c firmware/src/tribe/mfx.c firmware/src/tribe/store.c firmware/src/tribe/scatter.c firmware/src/tribe/engines.c firmware/src/tribe/kicks.c firmware/src/tribe/project.c firmware/src/dsp/drum909.c firmware/src/dsp/drum808.c firmware/src/dsp/acid.c firmware/src/dsp/lead.c firmware/src/dsp/fm4.c firmware/src/dsp/noise.c firmware/src/dsp/svf.c firmware/src/dsp/master.c"
SRCS="firmware/src/tribe/engine.c firmware/src/tribe/mfx.c firmware/src/tribe/scatter.c firmware/src/tribe/engines.c firmware/src/tribe/kicks.c firmware/src/tribe/project.c firmware/src/dsp/drum909.c firmware/src/dsp/drum808.c firmware/src/dsp/acid.c firmware/src/dsp/lead.c firmware/src/dsp/fm4.c firmware/src/dsp/noise.c firmware/src/dsp/svf.c firmware/src/dsp/master.c"
rm -f build/scen/flash.bin
if TRIBE_FLASH=build/scen/flash.bin build/host/tribe_host tests/scenarios/save1.tribe build/scen >build/scen/save1.log 2>&1 \
    && TRIBE_FLASH=build/scen/flash.bin build/host/tribe_host tests/scenarios/save2.tribe build/scen >build/scen/save2.log 2>&1; then echo "  ok   save and load (two runs)"
else echo "  FAIL save and load (see build/scen/save1.log, save2.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -DX0X_HOST -Ifirmware/src/dsp -o build/host/acid_test tests/host/acid_test.c firmware/src/dsp/acid.c firmware/src/dsp/lead.c firmware/src/dsp/fm4.c -lm 2>build/scen/acid_build.log \
    && build/host/acid_test >build/scen/acid.log 2>&1; then echo "  ok   ACID engine (pitch, decay, filter, slide)"
else echo "  FAIL ACID engine (see build/scen/acid.log, acid_build.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -DX0X_HOST -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -o build/host/keys_test tests/host/keys_test.c $SRCS_ALL -lm 2>build/scen/keys_build.log \
    && build/host/keys_test >build/scen/keys.log 2>&1; then echo "  ok   keys on a synth (the last one held sounds, a release goes back to the one before)"
else echo "  FAIL keys on a synth (see build/scen/keys.log, keys_build.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -DX0X_HOST -Ifirmware/src/dsp -o build/host/lead_test tests/host/lead_test.c firmware/src/dsp/lead.c firmware/src/dsp/fm4.c -lm 2>build/scen/lead_build.log \
    && build/host/lead_test >build/scen/lead.log 2>&1; then echo "  ok   LEAD engine (pitch, unison, legato, envelope)"
else echo "  FAIL LEAD engine (see build/scen/lead.log, lead_build.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -DX0X_HOST -Ifirmware/src/dsp -o build/host/fm4_test tests/host/fm4_test.c firmware/src/dsp/fm4.c -lm 2>build/scen/fm4_build.log \
    && build/host/fm4_test >build/scen/fm4.log 2>&1; then echo "  ok   FM4 engine (operators at their ratios, routings bounded, release, legato)"
else echo "  FAIL FM4 engine (see build/scen/fm4.log, fm4_build.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -DX0X_HOST -Ifirmware/src/dsp -o build/host/svf_test tests/host/svf_test.c firmware/src/dsp/svf.c -lm 2>build/scen/svf_build.log \
    && build/host/svf_test >build/scen/svf.log 2>&1; then echo "  ok   SVF filter (DC, band-pass centre, notch, corners)"
else echo "  FAIL SVF filter (see build/scen/svf.log, svf_build.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -DX0X_HOST -Ifirmware/src/dsp -o build/host/noise_test tests/host/noise_test.c firmware/src/dsp/noise.c firmware/src/dsp/svf.c -lm 2>build/scen/noise_build.log \
    && build/host/noise_test >build/scen/noise.log 2>&1; then echo "  ok   NOISE engine (morph law, pitch, noise anchors, filter types, colours, envelope)"
else echo "  FAIL NOISE engine (see build/scen/noise.log, noise_build.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -DX0X_HOST -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -o build/host/store_test tests/host/store_test.c $SRCS_ALL -lm 2>build/scen/store_build.log \
    && build/host/store_test >build/scen/store.log 2>&1; then echo "  ok   clip store under power cuts"
else echo "  FAIL clip store under power cuts (see build/scen/store.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -DX0X_HOST -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -o build/host/scatter_test tests/host/scatter_test.c $SRCS -lm 2>build/scen/scatter_build.log \
    && build/host/scatter_test base build/scen/scatter_base.f32; then
    ok=1
    for s in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do build/host/scatter_test $s build/scen/scatter_base.f32 >>build/scen/scatter.log 2>&1 || ok=0; done
    [ $ok = 1 ] && echo "  ok   scatter (16 slots)" || { echo "  FAIL scatter (see build/scen/scatter.log)"; FAIL=1; }
else echo "  FAIL scatter build"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -DX0X_HOST -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -o build/host/mfx_test tests/host/mfx_test.c $SRCS_ALL -lm 2>build/scen/mfx_build.log \
    && build/host/mfx_test base build/scen/mfx_base.f32; then
    ok=1; : >build/scen/mfx.log
    B=build/scen/mfx_base.f32
    # dj CUTOFF RESO expect: the DJ filter (LP, HP, neutral) on the mix
    for c in "10 40 diff" "120 40 diff" "64 40 same" "62 40 same" "10 100 diff" "120 0 diff"; do
        build/host/mfx_test dj $c $B >>build/scen/mfx.log 2>&1 || { ok=0; echo "  case failed: dj $c" >>build/scen/mfx.log; }
    done
    # live transposition of the 303 track: 0 changes nothing, +12 and -5 change the notes
    for c in "0 same" "12 diff" "-5 diff"; do
        build/host/mfx_test trans $c $B >>build/scen/mfx.log 2>&1 || { ok=0; echo "  case failed: trans $c" >>build/scen/mfx.log; }
    done
    # the master compressor (always on, set in GLO): its pots change the sound; the defaults are the base
    for c in "0 95 same" "0 20 diff" "4 100 diff" "5 0 diff"; do
        build/host/mfx_test comp $c $B >>build/scen/mfx.log 2>&1 || { ok=0; echo "  case failed: comp $c" >>build/scen/mfx.log; }
    done
    # FX 1 / FX 2 (SLOT TYPE AMT PAR SEND): every type, on the 303 track's send. Send 0 or amount 0 leaves the audio alone, a send of 100 changes it.
    # types: 0 reverb 1 delay 2 drive 3 dist 4 fuzz 5 wave 6 crush 7 decim 8 ring
    for t in 0 1 2 3 4 5 6 7 8; do
        for sl in 0 1; do
            for c in "$sl $t 100 80 0 same" "$sl $t 0 80 100 same" "$sl $t 100 80 100 diff" "$sl $t 100 20 20 diff" "$sl $t 127 127 127 diff"; do
                build/host/mfx_test send $c $B >>build/scen/mfx.log 2>&1 || { ok=0; echo "  case failed: send $c" >>build/scen/mfx.log; }
            done
        done
    done
    # the LFO (shape rate depth target sync retrig): depth 0 changes nothing; every shape, free and synced, moves the cutoff / the envelope amount
    for c in "0 60 0 0 1 1 same" "0 60 100 0 1 1 diff" "1 60 100 0 1 1 diff" "2 60 100 0 1 1 diff" "3 60 100 0 1 1 diff" "4 60 100 0 1 1 diff" \
             "0 90 127 0 0 0 diff" "0 60 100 2 1 1 diff" "0 60 100 0 1 0 diff"; do
        build/host/mfx_test lfo $c $B >>build/scen/mfx.log 2>&1 || { ok=0; echo "  case failed: lfo $c" >>build/scen/mfx.log; }
    done
    # the volume: MASTER as the pot (4096 here) or as a number: 127 = the pot's full, 64 = quieter
    build/host/mfx_test vol 1 127 same $B >>build/scen/mfx.log 2>&1 || ok=0
    build/host/mfx_test vol 1 64 quieter $B >>build/scen/mfx.log 2>&1 || ok=0
    build/host/mfx_test vol 0 20 same $B >>build/scen/mfx.log 2>&1 || ok=0
    [ $ok = 1 ] && echo "  ok   master effects and volume" || { echo "  FAIL master effects and volume (see build/scen/mfx.log)"; FAIL=1; }
else echo "  FAIL master effects build (see build/scen/mfx_build.log)"; FAIL=1; fi
if cc -O2 -ffp-contract=off -std=gnu99 -DX0X_HOST -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -o build/host/seq_test tests/host/seq_test.c $SRCS_ALL -lm 2>build/scen/seq_build.log; then
    ok=1; : >build/scen/seq.log
    for c in "0 0 0" "1 0 0" "2 0 0" "3 0 0" "4 0 0" "5 0 0" "0 1 0" "0 2 0" "0 3 0" "0 0 127" "1 0 127" "3 2 64"; do build/host/seq_test $c >>build/scen/seq.log 2>&1 || ok=0; done
    [ $ok = 1 ] && echo "  ok   sequencer clock (rate, direction, swing)" || { echo "  FAIL sequencer clock (see build/scen/seq.log)"; FAIL=1; }
else echo "  FAIL sequencer clock build"; FAIL=1; fi
if command -v emcc >/dev/null 2>&1 || [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    if bash -c '[ -f "$HOME/emsdk/emsdk_env.sh" ] && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1; sh web/emu/build.sh' >build/scen/web_build.log 2>&1 \
        && node tests/web_emu_test.mjs >build/scen/web.log 2>&1; then echo "  ok   web emulator"
    else echo "  FAIL web emulator (see build/scen/web_build.log, web.log)"; FAIL=1; fi
fi
exit $FAIL
