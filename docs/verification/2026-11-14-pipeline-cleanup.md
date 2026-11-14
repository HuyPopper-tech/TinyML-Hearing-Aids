# M55/U55 pipeline cleanup and numerical verification

Work started 2026-11-14; final verification continued 2026-11-15.
Branch: `codex/m55-u55-pipeline-cleanup`, based on `a1cda2b`.
Implementation plan: [pipeline cleanup](../superpowers/plans/2026-11-14-pipeline-cleanup.md).
This is host/build acceptance. No board was flashed or measured in this pass.

## Current path and preserved behavior

```text
DMIC +12 dB, 16 kHz, FIFO24 -> DMA32 non-cacheable slots
 -> foreground signed PCM24 -> normalization /8388608
 -> fixed software +21 dB -> peak detector -> compressor -> independent limiter
 -> one immutable normalized hop shared by frontend and waveform FIR
 -> causal Hamming256/complex FFT256 -> compressed features/context
 -> INT16 TFLM/Ethos-U55 controller -> recurrent feedback and FIR reconstruction
 -> old/new FIR128 and crossfade16 -> PCM16 -> I2S1 DMA -> DAC startup -9 dB
```

The compressor remains -12 dBFS, 3:1, 6 dB knee, attack/release 3/120 ms.
Detector attack is instantaneous per hop, decay 20 ms; limiter is -1 dBFS with
immediate attack/20 ms release. These are gain time constants, not added sample
buffers. The pre-model limiter does not establish an acoustic output ceiling.
Compiled PCM16 replay and the raw adapter PCM16/PCM24 APIs remain unconditioned;
switching formats requires a stream reset. Product conditioning intentionally changes
levels as previously approved; this cleanup adds no further gain/math change.

Keep 220 MHz core/PL0, requested 73.728 MHz audio APLL1, 16-sample hops,
clock gating, shallow WFI, NPU WFE/SEV, cache directions, DMA/IRQ ownership,
finite checks, deadline enforcement, fault latch and synchronized restart.
The power/timebase/NPU board and runtime implementation bytes match the archived
working-tree baseline. No `libs/` or `models/` file differs from the base commit.

## Optimization decisions and primary references

The [Cortex-M55 Software Optimization Guide, 102692_0101_03](https://documentation-service.arm.com/static/63ac5ce1396e6135830ba9fe)
discusses floating-point/MVE execution, loads/stores and dependencies in
sections 3.9, 3.12, 3.14, 3.15 and 4.1. This supports examining redundant data
movement and coefficient reuse. It does not establish cycle or power savings
for this board. No global fast-math, approximate replacement or FP64-to-FP32
rewrite was introduced.

The existing FIR pass reuses old/new coefficients across eight output samples
while preserving each lane's tap order and fused operations. Block PCM16
rendering uses MVE and preserves rounding, saturation and failed-hop atomicity.
CPU-owned DSP state stays in DTCM; the arena/model stay in NPU-accessible SRAM.
These preceding changes are committed separately with their numerical tests.

The [Ethos-U55 TRM, 102420_0200_03](https://documentation-service.arm.com/static/6305faeffc87ea23a5e0aa18),
[driver documentation](https://arm-software.github.io/CMSIS-Ethos-U/main/driver/index.html)
and [Vela documentation](https://arm-software.github.io/CMSIS-Ethos-U/main/vela/index.html)
were checked against the current integration. The existing compiled controller
already uses one NPU subgraph without CPU operator fallback. Its immutable
memory skips unnecessary cache maintenance; input copies cross into the SRAM
arena and output maps reference interpreter buffers directly. These copies and
cache/ownership boundaries serve real hardware requirements. They were retained.
No speculative Vela recompile, NPU buffer relocation or always-awake PMU mode
was added. Vendor driver versions must be reconciled before using newer APIs.

### Causal frontend ring

The former frontend moved 240 history floats on every hop, then appended 16.
Now it appends 16 at `history_position`, advances that cursor modulo 256 and
builds the windowed FFT input in at most two contiguous segments. For frame b:

```text
frame[i] = x[16*b - 240 + i] * window[i], i = 0..255
x[n] = 0 for n < 0
```

After append, the ring cursor points to the oldest sample, so logical sample i
is `history[(history_position+i) % 256]`. Multiplication order, window, FFT,
startup zeros, feature packing and quantization remain unchanged. Segments are
multiples of 16, so the four-lane MVE loop has no new tail or overread.
This satisfies the framing in `models/deepfir/DSP_CONTRACT.md`; storage layout
is not a model tensor/package ABI. There is no future sample or extra hop delay.

This removes a 960-byte history copy per hop: 960 logical bytes read and 960
written, or 1.92 MB/s at 1000 hops/s, before considering cache/bus effects.
Cursor/segment work is added. The compiled feature state grows from 2072 to
2080 bytes due to cursor/alignment; the total Release DTCM region still fits
the former 7512 bytes. Neither byte count proves wall-clock or energy benefit.

### Quantization safeguards

Guarded double computations remain in place. A binary32 intermediate near a
half-integer can cross the ties-away rounding boundary relative to the accepted
calculation, changing an INT16 feature code. That may affect subsequent
recurrent inference even when the float error is small. Current MVE fast paths
use their existing acceptance guards and scalar fallback. A blanket float
conversion was rejected; no new numerical tolerance was substituted.
The M55 supports scalar FP64 as well as FP32; vector MVE floating point is
FP16/FP32, as specified in the [Arm M55 datasheet](https://documentation-service.arm.com/static/6205407494e7af28dd7c9cb5).

### Profile partition

| Behavior | Debug | Ordinary Release | Release Probe |
|---|---|---|---|
| Continuous product audio | Yes | Yes | Finite probe run |
| Boot/stopped diagnostic UART | Yes | Excluded | Probe report after stop |
| Service stage clocks/conversions/runtime-stat snapshots | Yes | Excluded | Yes |
| `deepfir_inference_get_last_timing()` | Actual stage snapshot | Zero struct | Actual stage snapshot |
| Pipeline deadline/health/fault gates | Retained | Retained | Retained |
| Raw clipping and conditioner SWD state | Retained | Retained | Retained |
| Before-DMA MVE checks | Retained | Retained | Retained |
| Paired UART capture / compiled replay | Off | Off | Off |

The normal Release AXF contains neither `g_last_timing` nor
`debug_uart_tx_try_enqueue`; Debug and Probe contain both. Safety timestamps
are independent of optional service timing. No deadline check was removed.
Optional UART/replay source is retained for its explicit build/test purpose;
it is not linked into these product profiles. Paired WAV capture is physically
unavailable on the custom PCB and is not a trial measurement path.

### Ownership and style

Deleted `common/audio_pcm.h` without a replacement generic wrapper:

- Board capture owns FIFO low-24-bit sign extension; unspecified upper bits are ignored.
- Adapter/conditioner own raw normalization, with the existing argument/range gates.
- Optional UART export owns PCM24-to-PCM16 floor conversion, outside inference.

Audited all 49 project-owned C/C++ source/header files across app/services/
drivers/board/common. Explicit include edges follow the architecture contract;
services do not include board pin/clock or `NuMicro.h`. Mathematical CMSIS/TFLM
interfaces remain at the inference owner. ISR descriptor/barrier logic,
initialization-only allocation and foreground failure/restart paths remain.
No new HAL, scheduling layer or module was added. Callback typedefs now use
`_t`, duplicate includes and the stale Debug-only startup-check comment were
corrected. Existing style/header/linkage tests cover the complete owned surface.
The transport's standalone fallback gain metadata was corrected from 24 to
12 dB; configured firmware already supplied 12 dB. Overrides remain supported.

## Numerical and regression acceptance

Fresh working-tree baseline: **147 tests, 61.747 s, OK**. After PCM ownership,
ring/profile checks, gain regression and style cleanup: **151 tests, 53.802 s, OK**.
Final rerun against the newly linked images: **151 tests, 47.460 s, OK**.
No assertions were weakened and no tests were removed.

- `frontend_ring_harness.c`: independent former shift-history oracle, 8192 hops,
  resets every 503 hops, silence/impulse/rails/random PCM24, invalid state/input,
  reset and failed-FFT behavior. **4,194,304 FFT input float lanes per variant**
  match bit for bit in scalar and native MVE lane-model builds. This checks the
  windowed FFT input, not target FFT execution or NPU output quality.
- `test_profile_partition.py`: compile the actual service callbacks/process/
  timing getter under all three profile combinations. Verify input copies,
  successful output maps, error/invalid input, every non-finite output lane,
  unchanged failed-hop output and fault latch. Normal Release executes zero
  optional service clock/conversion/runtime-stat calls; diagnostic modes retain them.
- `test_pcm24.py`: execute the actual board descriptor/capture function with
  FIFO upper-byte/rail/low-bit cases and busy/ownership checks. Execute the actual
  export over **all 16,777,216 PCM24 codes**, comparing with independent double
  `floor(code/256)`. Verify fallback gain 12 and explicit override 30.
- Conditioner/adapter integration checks **131,072 raw normalization values**
  across the complete PCM16 grid with low-byte offsets 0 and 255. The existing
  double conditioner oracle covers **462,448 samples per variant**, maximum
  error **5.83539168e-6**, below **3e-5**; ceiling, release, RMS, counters,
  state/input validation, reset and output atomicity remain checked.
- Existing render oracle compares **2,097,200 codes per implementation**,
  including half-code neighbors and non-finite lane failures. Group8 FIR is
  bitwise equal to the former group4 MVE arithmetic over **8192 hops**.
  Quantizer/feedback/reconstruction, transport/restart/continuity, CMSIS scalar
  and repository tests remain enabled. See the earlier receipts for detailed
  vector definitions; those receipts' binary hashes are historical.

Native MVE models execute the production C branch with lane semantics, not
physical MVE. Scalar CMSIS execution and compiled-image checks do not establish
full on-board model parity. Full controller replay and acoustic tests remain open.

## Arm builds, linked checks and identity

Arm Compiler for Embedded 6.24. All three builds completed successfully.
Release/Probe use `-O3 -DNDEBUG`; Debug uses its configured `-g` service build
and existing per-source DSP options. MVE is ON, hardware gain 12, cache/model
SRAM enabled, replay/UART capture OFF in all three. Probe alone has its finite
probe switch ON. The root compilation database was restored to ordinary Release.

| Artifact | BIN bytes | Total DTCM region | CPU DeepFIR DTCM | Linker stack estimate |
|---|---:|---:|---:|---:|
| Release | 622848 | 7512 B | 7512 B | 5696 B + Unknown |
| Debug | 648096 | 9576 B | 7528 B | 6712 B + Unknown |
| Probe | 630496 | 9576 B | 7528 B | 5600 B + Unknown |

Stack reservation remains 8192 B. All stack reports explicitly contain
`Unknown(Cycles, Untraceable Function Pointers)`; these are not worst-case
bounds or runtime watermarks. Debug's reported estimate increased by 32 B
versus the preceding receipt. Board stack acceptance remains pending.

Release BIN is 192 bytes smaller than the archived pre-cleanup 623040-byte
working-tree image. Model image plus metadata remains 516852 B; arena capacity
65536 B. Actual arena peak and CPU energy are not inferred from map sizes.
Final Release and Probe disassembly checks pass all ten required MVE ranges,
including PCM24 `vcvt.f32.s32 ... #23`; instruction presence is not timing evidence.

Offline GDB against matching Release/Debug AXFs resolves the full conditioner,
64-bit raw counters and ring cursor. `g_stats` is at non-cacheable SRAM
`0x20230180`; conditioner state remains in DTCM. These are offline symbol checks,
not observations of live board writes. GDB emits known scatter-section/ELF
segment warnings; this session did not use GDB to program or run hardware.

Release BIN SHA256:
`0ef9f7479034410e3308e991b55ccdb64d76ec315acbf1a0858ad6571c9fe0dc`

Release AXF SHA256:
`6ce8eabf0b9a41a4a488838b62300521eeab16052ece2c7917664eb15dc8f3f9`

The BIN was regenerated from that AXF with `fromelf --bin` and matched byte for
byte. The compiled model identity remains
`69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27`;
all 19 integration-manifest file hashes match. `deployment_approved=false`
remains unchanged. Build/receipt JSON contains separate Debug/Probe hashes.

## Reproduction and trial delivery

```powershell
rtk proxy python -m unittest discover -s tests -v
rtk proxy git diff --check
rtk proxy cmd /c build.bat release build
rtk proxy cmd /c build.bat debug build
rtk proxy cmake -S . -B build-release-probe -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/armclang.cmake -DCMAKE_BUILD_TYPE=Release -DM55M1_AUDIO_RELEASE_PROBE=ON -DM55M1_AUDIO_UART_PCM_CAPTURE=OFF -DM55M1_DEEPFIR_COMPILED_REPLAY=OFF -DM55M1_DMIC_GAIN_DB=12 -DM55M1_DEEPFIR_MVE=ON -DM55M1_ENABLE_DAC_I2S_OUTPUT=ON -DM55M1_ENABLE_DCACHE=ON
rtk proxy cmake --build build-release-probe
rtk proxy python tools/verify_mve_disassembly.py build/M55M1_GestureAI.axf --objdump C:/ST/STM32CubeCLT_1.20.0/GNU-tools-for-STM32/bin/arm-none-eabi-objdump.exe
rtk proxy python tools/report_model_sram.py build/M55M1_GestureAI.axf.map
rtk proxy cmd /c build.bat release config
```

On this Windows machine native compiler tests use
`diagnostics/run_host_tests_pcm24.py` to expose the installed GCC/DLL/temp paths;
it invokes standard unittest discovery without assertion changes. The package
contains that script as an environment-specific receipt; it expects the local
GCC helper DLL directory and is not a standalone portable test runner.
Run builds sequentially; the root compilation database is shared. Restart
clangd after restoring the intended profile.

One final ordinary Release is packaged in
`out/m55-u55-cleanup-21db-20261114.zip` with matching BIN/AXF/map/callgraph,
parameters, source hashes, build/test/MVE/GDB/resource receipts and member
checksums. Earlier packages are not overwritten. Generated artifacts, build
trees, local PDFs and unrelated user notes are excluded from Git.

Follow [INPUT_LONG_TRIAL.md](../INPUT_LONG_TRIAL.md) for the uninterrupted
30-minute SWD trial. Capture remains PA1/PA2 and recovery remains PF0/PF1 SWD,
nRESET/GND/VREF; no pin/clock/DMA/recovery change was made here. Preserve the
existing resetless setup when the probe has no physical nRESET wire.

Pending physical checks: FIFO alignment/scaling, final-image deadline and wake
jitter, stack watermark, matched current/energy measurements, distant-speech
intelligibility and listening comfort. A prior user-reported Pause/Continue
restart is debugger interference, not evidence of spontaneous failure in this
image. No new latency, current, acoustic quality or long-soak result is claimed.
