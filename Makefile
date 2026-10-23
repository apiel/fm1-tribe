# TRIBE: `make emu` builds the native emulator (desktop/build.sh) and starts it.
# Other arguments go to the emulator: make emu ARGS="--reset".

.PHONY: emu

emu:
	desktop/build.sh
	build/desktop/tribe $(ARGS)
