# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Bare-metal firmware for TinyML hearing aids on Nuvoton NuMaker-GestureAI-M55M1 (`M55M1R2LJAE`, Cortex-M55). Build uses CMake, Ninja, and Arm Compiler 6 (`armclang`); supported host systems are Linux and Windows. Firmware runs secure-only with TrustZone disabled.

## Commands

Run commands from repository root.

```bash
# Configure active profile; updates root compile_commands.json symlink for clangd.
./build.sh debug config
./build.sh release config

# Configure and build firmware.
./build.sh debug build
./build.sh release build

# Build an already configured profile, or delete/reconfigure/rebuild it.
./build.sh debug only
./build.sh release clean

# Run all repository checks.
python -m unittest discover -s tests -v
git diff --check

# Run one test module or one test.
python -m unittest discover -s tests -p 'test_erb_gru_architecture.py' -v
PYTHONPATH=tests python -m unittest test_erb_gru_architecture.ErbGruArchitectureTests.test_app_depends_only_on_the_audio_pipeline_service -v

# Regenerate immutable ERB-GRU deployment output; requires TensorFlow 2.20.
python tools/generate_erb_gru_assets.py \
  --package models/erb_gru/packages/w4_erb-gru_deployment_v2_seed20260730 \
  --output models/erb_gru/generated

# Check generated output without writing.
python tools/generate_erb_gru_assets.py \
  --package models/erb_gru/packages/w4_erb-gru_deployment_v2_seed20260730 \
  --output models/erb_gru/generated --check
```

Firmware build prerequisites: CMake >= 3.20, Ninja, Arm Compiler 6 tools (`armclang`, `armasm`, `armlink`, `armar`, `fromelf`), and active Arm license. Host-parity/diagnostic tests may skip when `g++` or TensorFlow 2.20 is absent. No CTest, lint target, or formatter target exists.

After profile switch, restart clangd. `.clangd` removes Armclang-only flags for editor parsing.

## Architecture

Dependency direction is strict:

```text
app -> services -> drivers -> board -> libs
```

`common/` stays portable and dependency-free. `models/` holds generated/read-only TinyML artifacts; only `services/erb_gru` consumes them.

- `app/` composes boot and bare-metal foreground loop. `main.c` initializes/starts audio pipeline, then calls `audio_pipeline_poll()`.
- `services/audio_pipeline/` owns non-reentrant foreground audio flow: dequeue DMIC block, enhance it, submit to DAC, optionally emit diagnostics/telemetry.
- `services/erb_gru/` implements 16 kHz, 10 ms (160-sample) ERB-GRU enhancement. Audio enhancer performs FFT/features/state/gain/WOLA; inference owns static TFLM/Ethos-U55 setup, model contract validation, and 10 ms deadline.
- `drivers/` exposes service-safe component APIs. `audio_transport/` combines board audio bindings with TLV320 DAC driver.
- `board/` owns board pinmux, clocks, DMA, cache-safe buffers, IRQs, I2C recovery, and Ethos-U55 bindings. Keep ISRs limited to acknowledge hardware, descriptor/buffer ownership, and event publication.
- `libs/` is vendored Nuvoton BSP, CMSIS, CMSIS-DSP/NN, RTX, and TFLM dependencies. Do not modify it unless user explicitly requests it.

Audio path: `DMIC DMA -> static buffer queue -> DSP -> inference adapter -> post-process -> I2S DMA -> DAC`.

Realtime rules: after audio starts, use static/fixed-capacity memory. ISRs must not block, allocate, log, parse BLE, run DSP/inference, write flash, or lock mutexes. Audio takes priority over BLE, telemetry, logging, and persistence.

Settings lifecycle is fixed: `pending -> active at audio-frame boundary -> explicit commit -> deferred NVM write`. Validate before pending; audio reads active settings only.

## Build Configuration

`CMakeLists.txt` builds `M55M1_GestureAI.axf`, pulls TFLM through `cmake/tensorflow_lite_micro.cmake`, and controls optional CMSIS libraries. Defaults currently enable DSP, CMSIS-NN, TFLM, and ERB-GRU; RTX and NPU self-test are off. ERB-GRU and NPU self-test cannot both own Ethos-U55 IRQ.

Use `CMakeLists.txt` as authority for current defaults; README's CMSIS-NN default is stale.

Diagnostics, golden scenarios, and fault injection are build options. Production Release rejects golden data and fault injection unless diagnostics constraints are met.

## Boundaries and Conventions

- Do not commit generated build directories, `compile_commands.json`, caches, IDE files, or unrelated user changes. Preserve dirty worktree content.
- `models/erb_gru/packages/` is deployment source package; `models/erb_gru/generated/` is generated/read-only output. Regenerate, never hand-edit generated model contracts/vectors/data.
- Public interfaces must not leak board or vendor dependencies upward. Architecture tests enforce layer rules.
- Do not add generic project HAL wrappers around Nuvoton StdDriver APIs; add interfaces only for real substitution or test seams.
- New project-owned C/C++ code: four spaces, K&R braces, `snake_case`, `_t` typedefs, module-prefixed uppercase macros, `#pragma once`, and `extern "C"` guards on public C headers. Include order: C standard library, project headers, vendor headers.
- New `.c`/`.h` files begin with purpose, author, and date header. Document non-trivial public functions plus realtime/concurrency behavior.
- Remain bare metal. Do not add CMSIS-RTOS2, OTA, bootloader/image staging, or rollback behavior without separately approved design.
- Preserve SWD recovery (`SWDIO`, `SWCLK`, `nRESET`, `GND`, `VREF/3V3`); do not repurpose PF0/PF1 or change recovery behavior without explicit tested procedure.
