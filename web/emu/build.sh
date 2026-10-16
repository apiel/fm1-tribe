#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# TRIBE in the browser: the host simulator compiled to WebAssembly (Emscripten), plus its page.
#   web/emu/build.sh  ->  build/emu/{index.html, worklet.js, tribe.wasm}
# Same sources and flags as host/build_host.sh (-ffp-contract=off, like the device).
# Needs emcc: source ~/emsdk/emsdk_env.sh
set -e
cd "$(dirname "$0")/../.."
mkdir -p build/gen build/emu
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
[ -f build/gen/x0x_drum_samples.h ] || python3 tools/gen_drum_samples.py build/gen/x0x_drum_samples.h >/dev/null
U="firmware/src/dsp/drum909.c firmware/src/dsp/drum808.c firmware/src/dsp/acid.c firmware/src/dsp/lead.c firmware/src/dsp/master.c
   firmware/src/tribe/engine.c firmware/src/tribe/mfx.c firmware/src/tribe/scatter.c firmware/src/tribe/engines.c firmware/src/tribe/kicks.c firmware/src/tribe/project.c firmware/src/tribe/store.c"
# shellcheck disable=SC2086
emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function -Wno-unused-parameter -Wno-unused-variable \
    -DX0X_HOST -DTRIBE_WEB -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen \
    --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
    -o build/emu/tribe.wasm web/emu/tribe_web.c $U
cp web/emu/index.html web/emu/worklet.js build/emu/
echo "emu: build/emu ($(wc -c < build/emu/tribe.wasm) B wasm)"
