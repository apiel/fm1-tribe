#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# TRIBE, native: the host simulator in a window with sound (SFML 2.x), on the desktop.
#   desktop/build.sh  ->  build/desktop/tribe      run it: build/desktop/tribe [--reset] [--data DIR]
# Same sources and flags as host/build_host.sh (-ffp-contract=off, like the device). Needs libsfml-dev.
set -e
cd "$(dirname "$0")/.."
ROOT=$(pwd)
CC="${CC:-cc}"
mkdir -p build/gen build/desktop
[ -f build/gen/felucca_font.h ] || python3 tools/gen_font.py build/gen/felucca_font.h >/dev/null
[ -f build/gen/x0x_drum_samples.h ] || python3 tools/gen_drum_samples.py build/gen/x0x_drum_samples.h >/dev/null
U="firmware/src/dsp/drum909.c firmware/src/dsp/drum808.c firmware/src/dsp/acid.c firmware/src/dsp/lead.c firmware/src/dsp/fm4.c firmware/src/dsp/noise.c firmware/src/dsp/tribe_syn.c firmware/src/dsp/cycle.c firmware/src/dsp/svf.c firmware/src/dsp/master.c
   firmware/src/tribe/engine.c firmware/src/tribe/mfx.c firmware/src/tribe/scatter.c firmware/src/tribe/engines.c firmware/src/dsp/kicks.c firmware/src/tribe/project.c firmware/src/tribe/store.c"
FLAGS="-O2 -ffp-contract=off -std=gnu99 -Wall -Wextra -Wno-unused-function -Wno-unused-parameter -DX0X_HOST -Ifirmware/src -Ifirmware/src/dsp -Ibuild/gen"
# shellcheck disable=SC2086
$CC $FLAGS -c desktop/tribe_core.c -o build/desktop/tribe_core.o
OBJS="build/desktop/tribe_core.o"
for f in $U; do
    o="build/desktop/$(basename "$f" .c).o"
    # shellcheck disable=SC2086
    $CC $FLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
# shellcheck disable=SC2046
c++ -O2 -std=c++17 -Wall -Wextra -DTRIBE_FONT="\"$ROOT/assets/fonts/BarlowSemiCondensed-SemiBold.ttf\"" \
    desktop/tribe_desktop.cpp $OBJS \
    $(pkg-config --cflags --libs sfml-graphics sfml-audio sfml-window sfml-system) -lm -o build/desktop/tribe
echo "desktop: build/desktop/tribe"
