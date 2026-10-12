#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build (optionally) and install TRIBE on a connected M-VAVE FM-1 over USB-MIDI.
#
#   tools/flash.sh [--build] [--yes]
#
# Without --yes it only checks that the FM-1 answers and says what it WOULD install: nothing is written. With --yes it installs
# build/tribe.fwsc. --build runs tools/build.py first (needs the JieLi toolchain and the AC79 SDK, see BUILDING.md).
# Only pass --yes when the user has said to flash THIS build.
set -e
cd "$(dirname "$0")/.."
BUILD=0
YES=0
for a in "$@"; do
    case "$a" in
        --build) BUILD=1 ;;
        --yes) YES=1 ;;
        *) echo "usage: tools/flash.sh [--build] [--yes]" >&2; exit 2 ;;
    esac
done

# the MIDI libraries live in a venv that survives reboots (not in /tmp)
VENV="${FM1_VENV:-$HOME/.cache/fm1-tribe/venv}"
if [ ! -x "$VENV/bin/python" ]; then
    echo "setting up the MIDI tools in $VENV (once) ..."
    python3 -m venv "$VENV"
    "$VENV/bin/pip" install -q mido python-rtmidi
fi
PY="$VENV/bin/python"

if [ "$BUILD" = 1 ]; then
    export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
    export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
    [ -d "$JIELI_TOOLCHAIN" ] || { echo "no toolchain at $JIELI_TOOLCHAIN: run tools/get_toolchain.sh" >&2; exit 3; }
    [ -d "$AC79_SDK" ] || { echo "no AC79 SDK at $AC79_SDK (BUILDING.md has the git clone line)" >&2; exit 3; }
    python3 tools/build.py
fi

PKG=build/tribe.fwsc
[ -f "$PKG" ] || { echo "$PKG does not exist: run with --build" >&2; exit 3; }
echo "package: $PKG ($(stat -c %y "$PKG" | cut -d. -f1))"
echo "device:"
"$PY" tools/fm1_install.py --info || { echo "the FM-1 does not answer: see the skill's troubleshooting" >&2; exit 4; }
if [ "$YES" != 1 ]; then
    echo "(dry run: nothing written. Run again with --yes to install.)"
    exit 0
fi
"$PY" tools/fm1_install.py "$PKG" --yes 2>&1 | tr '\r' '\n' | grep -vE '^(writing|checking)'
sleep 4
echo "after the restart:"
"$PY" tools/fm1_install.py --info
