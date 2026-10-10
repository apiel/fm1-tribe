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
if command -v emcc >/dev/null 2>&1 || [ -f "$HOME/emsdk/emsdk_env.sh" ]; then
    if bash -c '[ -f "$HOME/emsdk/emsdk_env.sh" ] && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1; sh web/emu/build.sh' >build/scen/web_build.log 2>&1 \
        && node tests/web_emu_test.mjs >build/scen/web.log 2>&1; then echo "  ok   web emulator"
    else echo "  FAIL web emulator (see build/scen/web_build.log, web.log)"; FAIL=1; fi
fi
exit $FAIL
