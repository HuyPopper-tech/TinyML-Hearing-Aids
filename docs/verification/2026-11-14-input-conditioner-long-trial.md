# Fixed +21 dB long-trial software verification

This receipt identifies the pre-cleanup +21 dB image. Its hashes remain
historical. The current image is recorded in [pipeline cleanup](2026-11-14-pipeline-cleanup.md).

Date: 2026-11-14. Approved design:
`docs/superpowers/specs/2026-11-14-input-conditioner-long-trial-design.md`.
The earlier `2026-11-14-input-conditioner.md` records the historical +18 dB
version and is not the identity of this delivery.

## Changes and ownership

Extended the existing conditioner; software preamp is +21 dB, detector has
instant hop-peak rise / 20 ms decay, compressor has 3/120 ms attack/release,
and independent -1 dBFS limiter has immediate attack / 20 ms release.
Threshold -12 dBFS, 3:1 ratio and 6 dB soft knee are unchanged.
No additional queued hop or lookahead exists. DMIC +12 dB, FIFO24/DMA32,
16 kHz, hop16 and DAC -9 dB remain selected.

State stays in DTCM under the foreground inference service. Cold initialization
clears diagnostics; stream reset preserves lifetime 64-bit counters, peaks,
minimum gain and last complete RMS, while clearing dynamics and partial RMS.
RMS uses per-hop squared sums and two square roots per 100 ms, reducing float
accumulation error without per-sample logarithms. Meaningful compression means
at least 0.1 dB reduction, fixing the old near-unity residue counter behavior.
Raw pipeline counters and adapter PCM16/PCM24 replay APIs are unchanged.

No vendor, model artifact, clock, DMA, ISR or idle/power-policy edits belong
to this change. Existing worktree changes from earlier work are preserved.

## Numerical and regression evidence

Tests were extended first: RED log records missing new state/reset API. After
implementation, the independent double oracle passed **462,448 samples per
variant**, maximum absolute error **5.83539168e-6**, below **3e-5**.
Every output is finite and bounded by `10^(-1/20)+2e-7`.
Strict scalar, native MVE lane model and fast-math with finite checks produced
identical output hash **1f2726ad33bc1aea**. Native lane tests are not physical
MVE execution or an Arm-versus-host bit-exactness claim.

The cohesive harness covers static knee/ratio points, +21 dB quiet gain,
3 ms attack and 120 ms release equations, explicit 20 ms detector decay,
sample continuity bounds with limiter inactive, independent compressor/limiter
recovery, silence/low bits/rails/all-lane bipolar impulses, steps, sweeps,
repeated peaks, long tails and seeded random streams. NULL/count/range/alias
and invalid-state failures preserve output/state. RMS tests cover complete and
partial windows/reset; counters cross 2^32, preserve across stream reset and
clear on cold reset. Compression tests bracket 0.1 dB and reject residue counts.

Actual adapter tests verify identical frontend/FIR samples, preserved raw
replay routing, callback fault latching/reset and no callbacks on bad input.
Final complete suite: **147 tests, 47.947 s, OK, no skips**. Existing exhaustive
quantizer, feedback, FIR, DMA, deadline and service regression assertions remain.
The native GCC runtime launcher supplies local DLL/path handling; assertions
were not disabled. `git diff --check` passed.

## Compiler and linked image evidence

Arm Compiler for Embedded **6.24**, armclang **[5f371800]**, Keil MDK Nuvoton
Edition. Release and Debug builds succeeded. Both caches use DMIC +12 dB.
Release retains typed conditioner symbols and MVE normalization by `2^23`.
All **10** existing final-Release MVE kernel ranges passed disassembly checks;
conditioner has 23 vector instructions in its linked Thumb range. The Debug
image is a verification build, not a second packaged trial version.

| Resource | Release | Debug |
|---|---:|---:|
| BIN bytes | 623,040 | 647,936 |
| CPU DeepFIR DTCM section | 7,512 B | 7,528 B |
| Conditioner state | 72 B | 72 B |
| Tensor arena capacity in SRAM | 65,536 B | 65,536 B |
| Model image/metadata in SRAM | 516,852 B | 516,852 B |
| Linker maximum stack estimate | 5,696 B + unknown | 6,680 B + unknown |
| Reserved stack | 8,192 B | 8,192 B |

Relative to the archived +18 dB baseline, DTCM increases 40 B Release / 48 B
Debug (state grew by 48 B; linked placement/alignment accounts for the difference).
The linker reports unknown cycles/function pointers: these stack estimates
are not runtime high-water marks or proof of stack margin.
Offline GDB resolves all typed fields: Release state at **0x20001fe0**, Debug
at **0x20001ff0**, both 16-byte aligned. Use symbols from the matching AXF,
not these addresses in a different build. Existing load-section/ELF-segment
warnings occur during GDB lookup; this lookup does not validate board loading.

Release BIN SHA256:
`db14d955e49bbd9b04dbfba3759ec6e74ab60d211cf73b63f4cf953a7309f699`.
Debug BIN SHA256 (verification only):
`ed05513348cc2131e8f9105d32ef26b3bf89c5dc4718756ddf18ac5beb300810`.
The delivery manifest records matching AXF, map, callgraph, config and source
hashes. Binary checksums identify compiled bytes, not physical acceptance.

## Reproduce and boundaries

```powershell
rtk proxy python -X utf8 diagnostics/run_host_tests_pcm24.py discover -s tests -v
rtk proxy cmd /c build.bat release build
rtk proxy cmd /c build.bat debug build
rtk proxy python -X utf8 tools/verify_mve_disassembly.py build/M55M1_GestureAI.axf --objdump C:/ST/STM32CubeCLT_1.20.0/GNU-tools-for-STM32/bin/arm-none-eabi-objdump.exe
rtk proxy python -X utf8 tools/report_model_sram.py build/M55M1_GestureAI.axf.map
rtk git diff --check
```

Raw logs and archived baseline: `diagnostics/input_long_trial_20261114/`.
Single Release package: `out/input-long-trial-21db-20261114.zip`.
Procedure: `docs/INPUT_LONG_TRIAL.md`. The archive includes the GCC launcher
used locally; it relies on the existing local GCC runtime setup for rerunning
host tests. No external runtime is needed to use the BIN/AXF on the board.

No flashing was performed. This image still needs uninterrupted 30-minute
Release testing, measured capture-to-publication <1000 us, stack watermark,
current and acoustic assessment. The prior 824 us snapshot belongs to an older
image and does not establish this version's deadline margin. The user explained
the prior single restart as debugger Pause/Continue, not spontaneous instability.
No distant-speech, quality, SNR, full-NPU replay or power improvement is claimed.
