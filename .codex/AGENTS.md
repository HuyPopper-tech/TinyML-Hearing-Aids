# Repository guidance

The dependency direction is `app -> services -> drivers -> board -> libs`.
`libs/` is vendored. Do not edit `libs/`. `common/` is optional and dependency-free. Model artifacts are generated or read-only and are accessed only through an explicitly supplied package.

BLE configuration only; do not add OTA. The E104 bridge uses a dedicated UART TX, RX, RTS, and CTS and documents WKP, LINK, MOD, and DISC. Keep SWDIO, SWCLK, nRESET, GND, and VREF/3V3 available for recovery.

The firmware uses a bare-metal event loop. CMSIS-RTOS2 is not used currently. ISRs must not block, allocate memory, log, parse protocol messages, run DSP/inference, write flash, or acquire a mutex. Settings follow pending -> active at an audio frame boundary -> explicit commit -> deferred NVM write.

C uses `snake_case`, `snake_case_t`, and module-prefixed `UPPER_CASE` constants with four spaces. Do not use ESP-IDF APIs or ESP-IDF CMake.

Before handoff run `python -m unittest discover -s tests -v`, `git diff --check`, and the appropriate `build.bat debug build` or `./build.sh debug build` command when the Arm toolchain is available.
