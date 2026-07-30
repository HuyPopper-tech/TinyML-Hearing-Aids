# TinyML Hearing Aids — M55M1 ERB-GRU firmware

Bare-metal hearing-aid firmware for the **NuMaker-GestureAI-M55M1** board
(M55M1R2LJAE, Cortex-M55 at 220 MHz) with ERB-GRU inference accelerated by the
on-chip **Ethos-U55** NPU.

The production data path captures mono PCM at 16 kHz in 160-sample (10 ms)
hops, constructs a causal 320-sample analysis frame, runs the recurrent model,
and sends enhanced PCM to a TLV320DAC3100 headphone output.

> **Validation status:** host parity, package audit, unit tests, and Debug/Release
> Arm Compiler builds pass. The firmware is **not production-ready** until the
> final board A/B, runtime arena/stack measurements, five-scenario board parity,
> fault checks, and 10-minute soak pass.

## Quick start

### Requirements

- Arm Compiler for Embedded 6.24 with a valid user license
- CMake 3.20 or newer
- Ninja
- Python 3.10 or newer for host validation

The Windows build script automatically adds the standard Arm Compiler 6.24
installation directory and an available Ninja installation to `PATH` when
needed.

### Build production firmware

Windows:

```powershell
build.bat release build
build.bat debug build
```

Linux, macOS, or WSL:

```bash
./build.sh release build
./build.sh debug build
```

Release uses `build/`; Debug uses `build-debug/`. Both scripts configure before
building and update the root `compile_commands.json` for clangd.

Canonical outputs:

| Artifact | Release path | Purpose |
| --- | --- | --- |
| AXF | `build/M55M1_GestureAI.axf` | Symbols and debugger input |
| Binary | `build/M55M1_GestureAI.axf.bin` | Firmware image for flashing |
| Map | `build/M55M1_GestureAI.axf.map` | Flash/RAM and symbol inspection |
| HTML report | `build/M55M1_GestureAI.htm` | Arm linker report |

Replace `build/` with `build-debug/` for Debug artifacts.

Other script actions:

```text
[release|debug] [build|config|only|clean]
```

- `config`: configure without building.
- `only`: build an existing configured tree.
- `clean`: remove and rebuild only the selected profile.

The equivalent CMake presets are `m55m1-release` and `m55m1-debug`.

## Firmware architecture

Project-owned dependencies follow one direction:

```text
app -> services -> drivers -> board -> libs
```

- `app/` composes the board and audio-pipeline lifecycle.
- `services/audio_pipeline/` owns the foreground DMIC → enhancement → DAC flow.
- `services/erb_gru/` owns DSP, TFLM, model validation, recurrent state, and
  WOLA processing.
- `drivers/` owns audio transport, codec behavior, Ethos-U, and diagnostic UART
  transmission.
- `board/` owns M55M1 clocks, pins, DMA channels, IRQ priorities, and peripheral
  bindings.
- `libs/` is the vendored Nuvoton BSP, CMSIS, TFLM, and Ethos-U foundation.
- `models/` contains immutable deployment packages and generated, audited
  artifacts; only the inference service includes model bytes.

Realtime audio remains foreground-only and non-reentrant. ISRs acknowledge
hardware, publish buffer descriptors, and drain the bounded UART FIFO; they do
not allocate, log, run DSP/inference, or block. No dynamic allocation occurs
after audio starts.

### Audio processing

```text
DMIC DMA
  -> 160-sample capture queue
  -> sqrt-Hann window + FFT
  -> 32-band ERB/log features
  -> INT8 ERB-GRU on Ethos-U55
  -> 32-to-161 gain expansion
  -> IFFT + WOLA
  -> PCM16
  -> I2S/PDMA
  -> TLV320DAC3100
```

The first hop produces silence without FFT or inference. The second hop creates
the first complete 320-sample frame. An invocation failure uses unity gains
through the same IFFT/WOLA path, resets both recurrent states, and preserves
stream continuity.

## Deployed model

Current immutable package:

```text
models/erb_gru/packages/w4_erb-gru_deployment_v2_seed20260730/
```

| Property | Value |
| --- | --- |
| Vela model size | 59,232 bytes |
| Vela SHA-256 | `45f59d59598d74d36daebbb0d61b4d05020e164e4f78a62fcc31b4bb6b7d8fe8` |
| MACs per inference | 45,056 |
| Vela NPU operators | 30 |
| Vela CPU operators | 0 |
| Tensor arena capacity | 128 KiB bring-up allocation |

The model consumes two recurrent states and one 32-band feature tensor. It
produces two next-state tensors and 32 INT8 band gains. Firmware copies
successful next states byte-for-byte and expands the 32 gains to 161 FFT-bin
gains using the package matrix.

Armclang 6.24 miscompiled that stride-161 reduction under Release `-O3` during
board investigation, producing approximately half-scale bin gains. The current
workaround disables vectorization/interleaving only for the affected reduction
loop; the rest of the firmware remains at `-O3`. The exact loop-level diagnosis
still requires final board A/B confirmation.

See [ERB-GRU model deployment](docs/erb-gru-model-deployment.md) for the CPU
post-processing strategy and preferred replacement-model contract.

## Build options

Production defaults:

| CMake option | Default | Meaning |
| --- | --- | --- |
| `M55M1_ENABLE_ERB_GRU` | `ON` | Build the 16 kHz ERB-GRU application |
| `M55M1_ENABLE_TFLM` | `ON` | Build TFLM with Ethos-U support |
| `M55M1_ENABLE_NPU` | `OFF` | Keep the separate NPU self-test disabled |
| `M55M1_ERB_GRU_DIAGNOSTICS` | `OFF` | Exclude diagnostic UART and telemetry |
| `M55M1_ERB_GRU_INPUT_GAIN_DB` | `0` | Production PCM input gain |
| `M55M1_ERB_GRU_GOLDEN_SCENARIO` | empty | Exclude golden corpus data |
| `M55M1_ERB_GRU_FAULT_INJECTION` | `OFF` | Disable injected faults |
| `M55M1_STACK_SIZE` | `0x2000` | Reserve an 8 KiB stack |

Valid golden scenarios:

```text
silence | sine_1khz | clean_speech | speech_noise | near_full_scale
```

Valid fault modes:

```text
OFF | INVOKE_FAIL | INVOKE_SLOW | DAC_BUSY
```

Golden scenarios require diagnostics. A production Release build with
diagnostics disabled rejects golden data and fault injection.

### Diagnostic Release build

Keep diagnostic output in a separate build directory:

```powershell
cmake --preset m55m1-release -B build-diagnostics `
  -DM55M1_ERB_GRU_DIAGNOSTICS=ON `
  -DM55M1_ERB_GRU_INPUT_GAIN_DB=0 `
  -DM55M1_ERB_GRU_FAULT_INJECTION=OFF
cmake --build build-diagnostics
```

Diagnostics use UART1 at 115200-8N1:

- PA2: RX
- PA3: TX

One atomic snapshot is emitted every 100 processed hops when no DMIC block is
pending. It reports performance, model/memory metadata, acoustic statistics,
and DSP-stage measurements. With diagnostics `OFF`, the UART binding,
telemetry accumulator, formatter, and project log strings are not linked into
production firmware.

## Flashing and debugging

For the normal USB mass-storage workflow:

1. Build the selected profile.
2. Copy `M55M1_GestureAI.axf.bin` from its build directory to the board volume
   named `M55M1`.
3. Use the matching AXF when attaching a debugger.

Keep the ICE recovery header accessible:

| Signal | Board pin/use |
| --- | --- |
| SWDIO | PF0 |
| SWCLK | PF1 |
| nRESET | Reset recovery |
| GND | Probe ground |
| VREF/3V3 | Target voltage reference |

Do not repurpose PF0/PF1 or cross-power the target through VREF. The checked-in
VS Code Attach configuration uses the Debug AXF and does not program Flash.

### TrustZone and NSCBA

M55M1 implements Armv8.1-M TrustZone, but this firmware is built secure-only:
there is no Secure/Non-secure application split and no `-mcmse`.

If the device was previously programmed by a TrustZone BSP sample, Flash CONFIG
may contain an incompatible Non-Secure Code Base Address. Back up CONFIG before
vendor recovery. The normal USB-load and debugger-attach workflows must not
write CONFIG, LDROM, or NSCBA.

## Validation

Package reproducibility:

```powershell
python tools/generate_erb_gru_assets.py `
  --package models/erb_gru/packages/w4_erb-gru_deployment_v2_seed20260730 `
  --output models/erb_gru/generated `
  --check
```

Host tests and whitespace validation:

```powershell
python -m unittest discover -s tests -v
git diff --check
```

Current completed validation includes package/hash auditing, five-scenario host
golden parity, failure recovery, telemetry/DSP tests, architecture contracts,
and licensed Debug/Release builds producing AXF, binary, map, and HTML reports.

Still required before production acceptance:

- stable-tone board A/B for the loop-local Release workaround;
- runtime tensor-arena use and final arena sizing;
- stack high-water with at least 2 KiB headroom;
- five-scenario board parity and fault-boundary checks;
- diagnostic latency/acoustic measurements;
- 10-minute production soak with audio/DMA/NPU error counters at zero.

See the [ERB-GRU integration handoff](docs/session-erb-gru-ethosu-handoff.md)
for current evidence, counters, memory results, and remaining board work.

## Repository layout

```text
TinyML-Hearing-Aids/
├── app/                    # boot composition and diagnostic applications
├── board/                  # M55M1 clocks, pins, DMA and IRQ bindings
├── common/                 # dependency-free fixed-capacity utilities
├── drivers/                # audio transport, codec, Ethos-U and UART TX
├── services/
│   ├── audio_pipeline/     # production foreground audio orchestration
│   └── erb_gru/            # frontend, inference, WOLA and diagnostics
├── models/erb_gru/
│   ├── packages/           # immutable source deployment packages
│   └── generated/          # audited firmware contracts and model assets
├── tools/                  # model asset generator
├── tests/                  # host and architecture regression tests
├── docs/                   # deployment, handoff and hardware documentation
├── libs/                   # vendored BSP/CMSIS/TFLM/Ethos-U sources
├── CMakeLists.txt
├── CMakePresets.json
├── build.bat
└── build.sh
```

Detailed references:

- [ERB-GRU model deployment and replacement strategy](docs/erb-gru-model-deployment.md)
- [Current ERB-GRU integration handoff](docs/session-erb-gru-ethosu-handoff.md)
- [TLV320DAC3100 driver notes](docs/driver/tlv320dac3100.md)
- [MP34DT05 DMIC notes](docs/driver/mp34dt05_dmic.md)
- [NuMaker-GestureAI-M55M1 user manual](docs/en-us--UM_NuMaker-GestureAI-M55M1_EN_Rev1.00.pdf)
- [M55M1 series datasheet](docs/en-us--DS_M55M1_Series_EN_Rev1.00.pdf)
