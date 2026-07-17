# M55M1 build and debug workflow

The project exposes two isolated build profiles. `release` uses `build/` and
`debug` uses `build-debug/`; each produces an AXF and BIN without deleting the
other profile. Both `build.bat` and `build.sh` accept the same positional
interface: `[release|debug] [build|config|only|clean]`. Existing one-word
commands remain valid: no arguments builds Release, and `clean`, `config`, and
`only` operate on Release.

The Debug profile uses the existing `m55m1-debug` CMake preset and embeds
DWARF. Firmware is loaded through the board USB Mass Storage bootloader, then
the checked-in Cortex-Debug Attach configuration connects through WCH-Link.
The attach workflow only reads/halt/resets through SWD and does not program
Flash. It performs a SYSRESETREQ, sets a temporary `main` breakpoint, and
continues so source debugging begins at `main` without an NRST wire.

The README describes this TinyML hearing-aid firmware, its microphone test,
the build profiles, USB load procedure, safe WCH-Link attach procedure, and
the separately gated Flash launch configuration.
