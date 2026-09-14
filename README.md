# TinyML Hearing Aids - model-neutral PCM16 firmware

Bare-metal firmware baseline for the NuMaker-GestureAI-M55M1 hearing-aid audio path. The active path is stateless PCM16 passthrough; no neural model is active or deployment-approved.

## Audio path

```text
DMIC DMA -> static PCM16 block queue -> enhancement_passthrough_process
  -> optional I2S DMA -> TLV320DAC3100
```

Realtime work remains static after initialization. ISRs only acknowledge DMA, move descriptors, and signal the foreground loop. The dependency direction is `app -> services -> drivers -> board -> libs`.

## Model packages

`M55M1_MODEL_PACKAGE_DIR` is empty by default and model discovery is disabled. A future package must be supplied explicitly and contain `integration_manifest.json` plus `m55m1_model_package.cmake`. The package must pass package, host-parity, Vela, board-parity, realtime, soak, and capture-to-DAC gates before enablement.

The documentation-only future slots are `models/deepfir/` and `models/wave_unet/`. They contain no binaries or generated runtime sources.

## Ethos-U55 runtime foundation

The model-neutral Ethos-U55 runtime is opt-in with
`M55M1_ENABLE_ETHOSU_RUNTIME=OFF`; TFLM additionally requires that option to be
`ON`. It owns the M55M1 board clock/reset/IRQ binding, one static BSP driver,
single-flight lifecycle, cache hooks, health statistics, and the TFLM `ethos-u`
bridge. `M55M1_BUILD_ETHOSU_SMOKE=ON` creates a separate model-free board smoke
image.

The retained BSP driver is version `0.16.0` under
`libs/StdDriver/src/npu/` and remains unchanged. See
[docs/ETHOSU_RUNTIME_FOUNDATION.md](docs/ETHOSU_RUNTIME_FOUNDATION.md) for its
SHA-256 provenance and API contract. No valid command stream, model parity,
board execution, realtime/soak, capture-to-DAC, or deployment approval is
claimed by this foundation.

## Build

Requirements: CMake 3.20+ and Ninja. Arm Compiler for Embedded is required for the device build; a missing license is an external blocker.

```powershell
build.bat debug config
build.bat release config
build.bat debug build
build.bat release build
```

The POSIX equivalents are `./build.sh debug config` and `./build.sh release config`. The default build is model-free with TFLM disabled. No board flashing is part of cleanup acceptance.

## Verification

```powershell
python -m unittest discover -s tests -v
git diff --check
cmake --preset m55m1-debug
cmake --preset m55m1-release
```

Acceptance is ordered: verified archive; passthrough vectors and pipeline tests; model-free CMake policy/configure; retained-source audit; explicit deletion checkpoint; hygiene/style/full host suite; then device build and link-map audit when the Arm license is available.

## Layout

```text
app/                         boot composition
services/audio_pipeline/     foreground capture and passthrough orchestration
services/enhancement_passthrough/ stateless PCM16 copy service
drivers/                     audio transport, codec and UART support
board/                       pins, clocks, DMA and IRQ binding
models/deepfir/              documentation-only future slot
models/wave_unet/            documentation-only future slot
tests/                       host and repository contract gates
libs/                        vendored BSP/CMSIS foundation; unchanged
```
