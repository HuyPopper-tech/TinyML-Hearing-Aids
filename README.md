# TinyML Hearing Aids — DeepFilterNet U55 firmware

Bare-metal speech-enhancement firmware for NuMaker-GestureAI-M55M1
(Cortex-M55, 220 MHz) and Ethos-U55. Production build contains one model:
`deepfilternet_u55_full_seed20260805`; ERB-GRU remains only as immutable
provenance and is not linked.

Current package status is `engineering_ready_smoke_passed` with quality scope
`smoke_512_not_release_gate`. Host integration is implemented. Board timing,
memory-map, parity and soak gates are still pending real hardware evidence.

## Audio path

```text
DMIC DMA, 256 PCM16 samples at 16 kHz
  -> 512-sample periodic sqrt-Hann + FFT, 257 bins
  -> 32 ERB log-magnitude + 144 complex-bin features
  -> DeepFilterNet INT8 on Ethos-U55
  -> 257 gains + complex DF 5 x 144 + alpha
  -> IFFT + 50% WOLA
  -> DAC DMA, 256 PCM16 samples
```

First hop primes the analysis frame and emits silence. Invoke failure sends a
unity spectrum through the same IFFT/WOLA path, resets neural/DF history, and
preserves WOLA continuity. Realtime code is foreground-only, static after
initialisation, and follows `app -> services -> drivers -> board -> libs`.

## Model identity

| Property | Value |
| --- | --- |
| Post-Vela bytes | 1,197,856 |
| SHA-256 | `7ee81189598812499796635c3f88ae4c4bca1ff9be81c79ceb4d1fd6d723e475` |
| MAC/frame | 1,174,528 |
| NPU / CPU operators | 426 / 0 |
| Tensor contract | 9 inputs, 10 outputs, 21 tensors, one `ethos-u` op |
| Bring-up arena | 256 KiB; final size requires board measurement |

Firmware-owned artifacts live in `models/deepfilternet/`. The user-owned root
`deepfilternet/` export package is not modified.

## Build

Requirements: Arm Compiler for Embedded 6.24 with a valid license, CMake 3.20+
and Ninja.

```powershell
build.bat debug config
build.bat release config
build.bat debug build
build.bat release build
```

Linux/macOS equivalents use `./build.sh`. Outputs are AXF, binary and map files
under `build-debug/` or `build/`.

Key CMake options:

| Option | Default |
| --- | --- |
| `M55M1_ENABLE_DEEPFILTERNET` | `ON` |
| `M55M1_DEEPFILTERNET_DIAGNOSTICS` | `OFF` |
| `M55M1_DEEPFILTERNET_DSP_MODE` | `MVE` (`SCALAR`, `PARITY`) |
| `M55M1_DEEPFILTERNET_INPUT_GAIN_DB` | `12` diagnostic (`0` production) |
| `M55M1_DEEPFILTERNET_GOLDEN_SCENARIO` | empty |
| `M55M1_DEEPFILTERNET_FAULT_INJECTION` | `OFF` |

Golden scenarios: `silence`, `impulse`, `sine_440`, `sine_3000`, `noise`.
Fault modes: `INVOKE_FAIL`, `INVOKE_SLOW`, `DAC_BUSY`. Production Release
rejects scalar/parity, golden and fault builds. MVE/PARITY require CMSIS-DSP.

## Diagnostics

Diagnostic UART remains UART1, PA2/PA3, 115200-8N1. Every 100 processed frames
one ≤1536-byte atomic snapshot reports avg/max/p99 stage timing, transport
latency, model/arena/DSP/stack metadata, all feature/output/state ranges and
saturation, PCM peak/RMS/clip, parity delta, and cumulative audio/NPU/UART
errors. Formatting is deferred while capture is pending; a full 2048-byte TX
ring drops the whole message.

`e2e_us` measures PCM input to output-ready and gates at 16,000 us.
`transport_us` separately measures capture completion to DAC completion.

## Verification

```powershell
python tools/generate_deepfilternet_assets.py --check
python -m unittest discover -s tests -v
git diff --check
```

Host gates cover artifact identity, tensor mapping, transactional recurrent
state, scalar formulas/WOLA, five deterministic scenarios, 256-sample
transport, telemetry aggregation/atomic drop, MVE build contract and layering.

Before board acceptance:

1. Keep PF0 SWDIO, PF1 SWCLK, nRESET, GND and VREF/3V3 recovery access.
2. Run five scalar golden scenarios, then PARITY; require max PCM difference
   ≤1 LSB.
3. Capture at least ten diagnostic windows; require `e2e_us max <= 16000`,
   stack headroom ≥2 KiB and no frame/error gaps.
4. Verify map: one model, Flash ≤1.8 MiB, SRAM ≤1.5 MiB, no golden/ERB
   expansion/scalar code in production.
5. Set arena from measured use (16 KiB rounding + 16 KiB guard), then run a
   10-minute production soak with audio/NPU/DMA errors at zero.

No board result is inferred from host tests. See
[deployment](docs/deepfilternet-u55-deployment.md) and
[current handoff](docs/session-deepfilternet-u55-handoff.md).

## Layout

```text
app/                         boot composition
services/audio_pipeline/     foreground audio orchestration
services/deepfilternet/       enhancer and inference adapter
drivers/                     transport, codec, Ethos-U, UART TX
board/                       pins, clocks, DMA, IRQ, stack monitor
models/deepfilternet/         audited firmware model package
tools/                       deterministic asset generator
tests/                       host and architecture gates
libs/                        vendored BSP/CMSIS/TFLM; unchanged
```

Legacy ERB-GRU documents remain historical evidence, not current build
instructions.
