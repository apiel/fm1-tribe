#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the TRIBE host simulator (the whole app on this machine) into build/host/tribe_host.
# Same sources as the firmware, -ffp-contract=off like the device (no fused multiply-add).
set -e
cd "$(dirname "$0")/.."
CC="${CC:-cc}"
mkdir -p build/gen build/host
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
[ -f build/gen/x0x_drum_samples.h ] || python3 tools/gen_drum_samples.py build/gen/x0x_drum_samples.h >/dev/null
U="firmware/src/dsp/drum909.c firmware/src/dsp/drum808.c firmware/src/dsp/acid.c firmware/src/dsp/lead.c firmware/src/dsp/fm4.c firmware/src/dsp/master.c
   firmware/src/tribe/engine.c firmware/src/tribe/mfx.c firmware/src/tribe/scatter.c firmware/src/tribe/engines.c firmware/src/tribe/kicks.c firmware/src/tribe/project.c firmware/src/tribe/store.c"
# shellcheck disable=SC2086
$CC -O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter \
    -DX0X_HOST -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen -o ${TRIBE_HOST_OUT:-build/host/tribe_host} host/tribe_host.c $U -lm
echo "host: build/host/tribe_host"
