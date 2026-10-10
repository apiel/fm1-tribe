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
    if build/host/tribe_host "$s" build/scen >build/scen/$(basename "$s").log 2>&1; then echo "  ok   $(basename "$s")"
    else echo "  FAIL $(basename "$s")"; tail -5 build/scen/$(basename "$s").log | sed 's/^/       /'; FAIL=1; fi
done
SRCS="firmware/src/tribe/engine.c firmware/src/tribe/scatter.c firmware/src/tribe/engines.c firmware/src/tribe/kicks.c firmware/src/tribe/project.c firmware/src/dsp/drum909.c firmware/src/dsp/drum808.c firmware/src/dsp/bass303.c firmware/src/dsp/master.c"
if cc -O2 -ffp-contract=off -std=gnu99 -DX0X_HOST -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -o build/host/scatter_test tests/host/scatter_test.c $SRCS -lm 2>build/scen/scatter_build.log \
    && build/host/scatter_test base build/scen/scatter_base.f32; then
    ok=1
    for s in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15; do build/host/scatter_test $s build/scen/scatter_base.f32 >>build/scen/scatter.log 2>&1 || ok=0; done
    [ $ok = 1 ] && echo "  ok   scatter (16 slots)" || { echo "  FAIL scatter (see build/scen/scatter.log)"; FAIL=1; }
else echo "  FAIL scatter build"; FAIL=1; fi
if command -v emcc >/dev/null 2>&1 || [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    if bash -c '[ -f "$HOME/emsdk/emsdk_env.sh" ] && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1; sh web/emu/build.sh' >build/scen/web_build.log 2>&1 \
        && node tests/web_emu_test.mjs >build/scen/web.log 2>&1; then echo "  ok   web emulator"
    else echo "  FAIL web emulator (see build/scen/web_build.log, web.log)"; FAIL=1; fi
fi
exit $FAIL
