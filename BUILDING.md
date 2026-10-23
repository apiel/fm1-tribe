# Building TRIBE

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `x0x.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `tribe.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow: `pip3 install Pillow`
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.1-beta` makes a release build; the package is `build/tribe-0.1-beta.fwsc`.

On macOS with podman instead of Docker, put a `docker` script that runs `exec podman "$@"` first
on your PATH. `X0X_JOBS` (default 4) limits parallel compiles: a podman machine drops
connections when many containers start at once.

Build option: `X0X_CDC=1` adds Felucca's USB serial function (off by default: one plain
MIDI interface).

The build also generates `build/gen/`: the font, and the 909's samples and tables
(`tools/gen_drum_samples.py`). It fails if a soft-double routine is linked (a `double` crept in)
and checks the image, RAM and pool sizes.

## Tests

```
tests/run_tests.sh
```

Runs the whole app in the host simulator (`tests/scenarios/*.tribe`: buttons, keys, knobs, with the
screenshots and audio in `build/scen/`), the flash log under power cuts, the master effects, the sends,
the scatter effects and the sequencer clock against reference renders, and, when Emscripten is
installed, the browser build in node.

`host/build_host.sh` builds the simulator alone; `build/host/tribe_host SCRIPT OUTDIR` runs one
script (the command list is at the top of `host/tribe_host.c`).

The browser emulator needs Emscripten: `source ~/emsdk/emsdk_env.sh && ./run_emu.sh` builds it and
serves it on <http://localhost:8000>.

## The native emulator

`desktop/build.sh` builds TRIBE as a program with a window and sound (needs SFML 2.x and pkg-config; on Linux,
`libsfml-dev` and `pkg-config`). It runs the same sources as the browser build:

```
desktop/build.sh                       # build/desktop/tribe
build/desktop/tribe                    # starts at once; saves to ~/.local/share/tribe
build/desktop/tribe --reset            # the factory reset: forget the saved clips and settings
build/desktop/tribe --shot panel.png   # no window: two seconds of the device, drawn to a PNG
```

The controls are the browser page's: click or drag the knobs (or use the wheel), `Shift`-click or
right-click a button to latch it, `Q`–`I` and `A`–`K` are the white keys, `1`–`0` and `-` the black
keys, `Space` PLAY, `Enter` REC, `Shift` SEL, `Esc` HOME, `Z` and `X` OCT− and OCT+, the arrow keys
turn SELECT. `--data DIR` puts the saved data somewhere else.

## Install

TRIBE's web installer is <https://apiel.github.io/fm1-tribe/install/>. To install your own build, use
the command line, or a local copy of the site (below).

From the command line (needs `pip3 install mido python-rtmidi`):

```
tools/flash.sh --build            # build, and check that the FM-1 answers: writes nothing
tools/flash.sh --yes              # install build/tribe.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
./build.sh --release 0.1-beta                     # the site wants a release build (identity FM-1_9XXYYZZ)
sh web/emu/build.sh                               # the emulator (Emscripten)
python3 tools/make_pages.py build/tribe-0.1-beta.fwsc 0.1-beta build/pages
cd build/pages && python3 -m http.server 8000
# open http://localhost:8000/install/
```

Installing firmware is at your own risk. If the FM-1 no longer starts but reaches the chip's
update mode (4C4A:8057 on USB), `tools/fm1_rescue.sh` puts stock V15 back from a Mac (docs/MANUAL.md,
Troubleshooting); otherwise recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## The manual and the site

The manual is `docs/MANUAL.md`, what's new is `docs/NEW.md`. Their screenshots come from the simulator:

```
tests/run_tests.sh                  # tests/scenarios/manual.tribe takes the shots into build/scen/
python3 tools/gen_manual_figures.py # -> docs/img (the panel and key drawings, the screenshots)
```

## Publishing a release

1. `./build.sh --release X.Y-beta` (the identity, FM-1_9XXYYZZ, is what the installers check).
2. `tools/publish_pages.sh X.Y-beta`: builds the site (landing page, emulator, web installer, manual,
   firmware) with `tools/make_pages.py` and pushes it to the `gh-pages` branch, which GitHub
   Pages serves at <https://apiel.github.io/fm1-tribe/>. In the repository's Settings > Pages, the
   source is "Deploy from a branch": `gh-pages`, folder `/ (root)`.
3. `gh release create vX.Y-beta build/tribe-X.Y-beta.fwsc --prerelease` for the command-line
   download.
