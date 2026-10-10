# TRIBE

Custom firmware for the M-VAVE FM-1: 10 tracks, any engine on any track, 16 clips per track,
scatter FX, assignable master knobs. Forked from X0X (see [docs/X0X-README.md](docs/X0X-README.md)),
with engines from zicBox (zicModKick).

Status: phase 1 (engines, 10-track sequencer, clips, UI, browser emulator) works on the host and in the browser;
the FM-1 build and saving are next. See [docs/SPEC.md](docs/SPEC.md).

```
./run_emu.sh           # build the browser emulator and serve it on http://localhost:8000 (needs ~/emsdk)
tests/run_tests.sh     # host scenarios + the wasm build in node
```

Licence: GPL-3.0-only (inherits X0X, Felucca, zicBox).
