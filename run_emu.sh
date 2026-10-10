#!/bin/bash
# Build the browser emulator and serve it: http://localhost:8000
cd "$(dirname "$0")"
[ -f "$HOME/emsdk/emsdk_env.sh" ] && . "$HOME/emsdk/emsdk_env.sh" >/dev/null 2>&1
sh web/emu/build.sh && cd build/emu && echo "open http://localhost:8000" && exec python3 -m http.server 8000
