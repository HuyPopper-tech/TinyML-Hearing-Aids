# M55M1 Hearing-Aid Firmware: Codex Instructions

## Scope and Ownership

- These instructions apply to project-owned code and configuration in `app/`,
  `board/`, `drivers/`, `services/`, `common/`, `models/`, `cmake/`, `tools/`,
  `tests/`, `build.bat`, `build.sh`, and root CMake/configuration files.
- `libs/` is vendored Nuvoton BSP, CMSIS, CMSIS-DSP, CMSIS-NN, and RTX code.
  Do not edit `libs/` unless the user explicitly requests it.
- Do not commit generated build directories, `compile_commands.json`, caches,
  IDE files, or user-owned untracked files. Preserve unrelated changes in a
  dirty worktree.
- Do not create project-owned generic HAL or platform wrappers that merely
  rename Nuvoton StdDriver calls. `libs/StdDriver` and `NuMicro.h` already
  provide the vendor driver/PAL layer.

## Architecture Contract

The target dependency direction is:

```text
app -> services -> drivers -> board -> libs
```

`common/` is optional and dependency-free. It may contain portable types,
fixed-capacity queues, framing helpers, and small utilities; it must not depend
on another project-owned layer or vendor code. `models/` contains generated or
read-only TinyML artifacts. Only the inference service accesses model artifacts.

### Layer Rules

- `app/` owns boot composition and the bare-metal event loop. It may call the
  single board bootstrap API and public service APIs. It must not include
  `NuMicro.h`, touch registers, or use driver implementation APIs directly.
- `services/` owns product behavior: audio pipeline, inference adapter, BLE
  protocol, settings, persistence policy, and telemetry. It may depend on
  `common/` and public driver headers. It must not depend on board pin/clock
  details or call vendor APIs directly.
- `drivers/` owns component-specific behavior such as E104-BT5010A, audio
  codec/DAC, and DMIC/I2S DMA orchestration. It may depend on `board/`,
  `common/`, and Nuvoton StdDriver/CMSIS APIs. It must not depend on services
  or app code.
- `board/` owns this PCB's pin map, clocks, DMA channels, IRQ priorities, and
  peripheral binding. It may include `NuMicro.h` and must not depend on
  drivers, services, or app code.
- `libs/` remains the vendor foundation. Do not change its style, APIs, or
  build contents without explicit user approval.

Create a small dependency interface only when it enables a real substitution or
test seam, such as mock NVM for settings tests or mock transport for protocol
tests. Do not add a generic `hal_uart_*()` wrapper around `UART_*()`.

## Realtime Audio and Settings

- The audio data path is: `DMIC DMA -> static buffer queue -> DSP -> inference
  adapter -> post-process -> I2S DMA -> DAC`.
- The configuration path is: `phone -> E104 UART bridge -> framed BLE protocol
  -> validated pending settings -> audio-frame apply -> explicit commit ->
  deferred NVM write`.
- ISRs must not block, allocate memory, log, parse BLE messages, run DSP/TinyML
  inference, write flash, or acquire a mutex. They only acknowledge hardware,
  move buffer descriptors, and signal events.
- Audio has priority over BLE, telemetry, logging, and persistence. Background
  work may be delayed, dropped, or retried; audio must never wait for it.
- Settings follow this exact lifecycle: `pending -> active at an audio frame
  boundary -> explicit commit -> deferred NVM write`. Validate all values before
  they become pending. Only active settings are readable by the audio path.
- Use static or fixed-capacity buffers in realtime paths. Do not dynamically
  allocate memory after audio starts.

## BLE, Debug, and Hardware Boundaries

- BLE configuration only; do not add OTA, image staging, bootloader, or rollback
  behavior without a separately approved design.
- E104-BT5010A is a UART-to-BLE bridge, not a BLE stack or OTA bootloader on the
  M55M1. Use a dedicated UART TX, RX, RTS, and CTS; do not share it with the
  SWD pins or the debug UART.
- Route and document E104 control GPIOs: WKP, LINK, MOD, and DISC, plus RST when
  a spare GPIO is available. Use `MOD` for module configuration/maintenance, not
  for the product configuration protocol. Keep module AT/log text out of the
  production transparent data stream.
- Keep the ICE recovery/programming header accessible: SWDIO, SWCLK, nRESET,
  GND, and VREF/3V3. Do not repurpose PF0/PF1 or alter SWD recovery behavior
  unless the user explicitly requests a tested recovery procedure.
- Firmware uses a bare-metal event loop. CMSIS-RTOS2 is not used currently;
  adding it requires an approved scheduling, priority, stack, and latency plan.

## C Style for Project-Owned Code

- Start each new `.c` and `.h` file with a purpose, author, and date header.
  Document non-trivial public functions and complex realtime/concurrency logic.
- Header files use `#pragma once`. Public C headers wrap declarations in
  `extern "C"` guards for C++ compatibility.
- Order includes as: C standard library headers, project-owned headers, then
  vendor headers. Include only what is needed; prefer forward declarations
  where they avoid needless coupling.
- Functions and variables use `snake_case`; typedef structs and enums use
  `snake_case_t`; macros/constants use module-prefixed `UPPER_CASE` names.
- Use four spaces for indentation. Opening braces stay on the same line. Always
  use braces for `if`, `for`, `while`, and `do`, including one-statement bodies.
- Use `//` for short comments and `/* ... */` for block comments. Use
  `// ========== section ========== //` only for meaningful logical sections.
- Keep functions focused. Prefer `static` internal linkage. Avoid mutable global
  state; when shared state is required, document its owner, execution context,
  and synchronization/frame-boundary rule.
- Validate arguments at public boundaries and return project/platform-appropriate
  status values. Do not use ESP-IDF APIs or ESP-IDF CMake such as `ESP_OK`,
  `ESP_LOGE`, or `idf_component_register`.
- Add hardware abstraction only when it is a genuine project interface. Use
  `hal_` or `platform_` prefixes only for such deliberate abstractions, not as
  aliases for existing Nuvoton StdDriver APIs.

## Build, Debug, and Validation

- Configure the active editor/build profile before editing compile-sensitive
  code: `build.bat debug config` or `build.bat release config` on Windows;
  `./build.sh debug config` or `./build.sh release config` on POSIX systems.
  The scripts update the root compilation database for clangd. Restart clangd
  after switching profiles.
- Use `build.bat debug build` or `./build.sh debug build` for debug builds, and
  the equivalent `release` commands for release builds. Report an Arm Compiler
  license failure as an external blocker; do not claim a binary was produced.
- For project changes, run `python -m unittest discover -s tests -v` and
  `git diff --check`. Run the smallest relevant build/configure command when
  toolchain access is available.
- Before flashing or changing pin mux/clock/DMA code, state the board pins and
  recovery path. Never assume a probe can assert reset when its hardware lacks
  an nRESET connection.
