# Cortex-M55 bounded DSP optimization — 2026-11-14

Historical receipt for the bounded DSP pass before the +21 dB conditioner and
history-ring cleanup. Refer to [pipeline cleanup](2026-11-14-pipeline-cleanup.md)
for the current configuration, three builds and delivery hashes.

## Delivered candidate

Implemented the four bounded opportunities from
`2026-11-14-cortex-m55-review.md` in the existing modules:

1. `deepfir_pcm16_render_hop` converts one 16-float hop to PCM16 using MVE,
   retaining the scalar implementation for host/non-MVE builds. It stages the
   result privately and rejects NULL, overlap and non-finite input without
   changing the caller's destination. The scalar single-sample API retains its
   original non-finite-to-zero behavior. The service retains its fault latch and
   failing-lane diagnostic, and publishes only a complete successful hop.
2. The MVE FIR processes eight consecutive outputs per tap loop. Each lane still
   accumulates taps 0..127 with the same fused operations and separate
   crossfade multiplies/addition. Coefficients are shared across two vectors.
3. CPU-owned adapter, feature state and FFT context use the existing aligned
   zero-initialized DTCM section. The NPU bridge still copies inputs into the
   SRAM tensor arena before Invoke. Model, arena, DMA buffers and SWD statistics
   remain in their previous memory classes.
4. Successful hops convert/store total time once. Adapter and render failures
   retain total-time evidence; render failures now record their own elapsed
   time instead of leaving the pre-render value. Other stage fields, deadlines,
   reset policy and public timing API remain.

The memory report now explicitly lists CPU-owned DeepFIR DTCM separately from
SRAM. Debug has additional probe fields and alignment, so its count differs
from Release.

Preserved: 16 kHz mono/hop16, PCM24 input and normalization by 2^23, INT16 model,
complex FFT256/IFFT128, feature quantization, DMIC +24 dB, DAC -9 dB, core
220 MHz/PL0, audio APLL1 request 73.728 MHz, clock gating, shallow WFI and NPU
WFE/SEV. No vendor code, model artifact, clock, pin or DMA configuration changed
in this optimization pass. No global fast-math was introduced.

## Numerical contract and reproducible checks

The new tests are in `tests/r2_dsp_optimization_harness.c` and the existing
`tests/test_r2_optimization.py`. The fixed seed is `0x34cb1267`.

### PCM conversion: exact integer equality

The independent reference retains the original operations:

`scaled = input * 32768.0f`; saturate at +32767 / -32768; otherwise add +0.5 or
-0.5 according to the sign and truncate toward zero. Floating-point addition
rounding near half codes is deliberately retained; a different quantizer is
not substituted.

- Enumerate every half-code center from code -32769 through +32768, with both
  adjacent binary32 values from `nextafterf`, distributed over all 16 lanes.
- Test signed zero, smallest normal/subnormal values, both full-scale rails,
  large finite values and both signs of FLT_MAX.
- Test 65,536 blocks of deterministic random finite binary32 bit patterns.
- Compare **2,097,200 codes per implementation**, scalar and production MVE C
  branch under the native lane model: **zero mismatches** against the reference.
- Insert NaN, +Inf and -Inf at every lane: all 48 failure cases preserve the
  entire destination. Check output canaries, NULL and overlapping buffers.

The public scalar single-sample helper is checked against the same reference.
The native model implements MVE conversion/select/store and fused `fmaf` lane
semantics; it does not execute hardware MVE or prove target FP control settings.

### FIR: exact previous-MVE equality

Freeze the former group4 algorithm as a test-only oracle, including explicit
fused multiply-add, original tap order, crossfade operations and history updates.
Compare production group8 output and the complete state using `memcmp` after
every hop:

- **8,192 hops / 131,072 samples**, changing all 128 taps each hop.
- Silence, impulse and full-resolution PCM24-derived random input.
- Reset every 997 hops.
- **Zero output or state mismatches** in the native MVE lane model.

Existing scalar-versus-MVE tolerance, scalar fallback, feature quantization,
recurrent feedback, reconstruction, PCM24 low-bit and failure/restart tests
remain enabled. The new exact comparison is against the old MVE arithmetic,
not an assertion that fused and unfused scalar FIRs are bit-identical.

### Service and integration

Compile the real service `process_audio` wrapper with controlled adapter/timebase
dependencies. Require one total conversion on success and failure, correct PCM,
unchanged output on adapter failure or NaN in every lane, a latched fault, and
rejection of invalid sizes/alias before processing. The archived pre-change
wrapper fails the new success conversion-count assertion as expected; the
current wrapper passes.

Link-map tests check all three state objects fit aligned inside DTCM, the arena
stays in SRAM and the stack remains 8 KiB. The existing SWD non-cacheable
statistics placement and low-power/continuity regression gates also pass.

### Commands and observed results

All shell commands are run through `rtk` per repository instructions.

```text
rtk proxy python -X utf8 diagnostics/run_host_tests_pcm24.py discover -s tests -v
  144 tests, 36.141 seconds, OK; no skips
rtk proxy cmd /c build.bat release config
rtk proxy cmd /c build.bat release build
  PASS, Arm Compiler 6.24
rtk proxy cmd /c build.bat debug config
rtk proxy cmd /c build.bat debug build
  PASS, Arm Compiler 6.24
rtk proxy python -X utf8 tools/verify_mve_disassembly.py build/M55M1_GestureAI.axf --objdump C:/ST/STM32CubeCLT_1.20.0/GNU-tools-for-STM32/bin/arm-none-eabi-objdump.exe
  PASS, all 10 required linked kernel ranges
rtk proxy git diff --check
  PASS
```

The native GCC installation needs colocated helper/runtime DLLs on this host.
The existing ignored `diagnostics/run_host_tests_pcm24.py` only adjusts subprocess
loader paths and temporary locations; unittest assertions are unchanged. Its
entry point runs standard unittest discovery. Full final output is retained at
`diagnostics/m55_dsp_optimization_20261114/host-tests.log`.

RED checks rejected the absent PCM block API, the old two-FMA tap loop, old SRAM
state placement and the missing DTCM report. The report regression initially
assumed Release's 7,432 B for Debug too; map evidence showed Debug-only probe
fields/alignment (7,448 B), so the test now compares the report with that image's
actual linked section. The failed intermediate report run is retained separately.

## Linked-image evidence

Release explicit Thumb disassembly:

- Production PCM24 service: **0 scalar PCM calls; 1 block-render call** per hop.
- Both FIR tap loops: **4 vector FMAs / 2 scalar coefficient loads** per
  iteration, low-overhead `LE`, no inner-loop stack spills/reloads. Thus the tap
  coefficient loads fall from 1,024 to 512 per hop; FMA count and history-load
  count are unchanged. Extra callee-saved vector registers are saved/restored
  outside the loops; this is not a measured net speedup.
- FIR local stack frame is 680 B versus the previous 624 B. The linker callgraph
  reports maximum stack 5,696 B Release / 6,680 B Debug **plus unknown indirect
  calls/cycles**; these are not measured stack high-water marks.
- PCM block kernel contains vector finite masks, rounding conversion and
  halfword stores. Existing frontend/feedback/reconstruction vector checks pass.

Release placement:

| Object | Address | Bytes |
|---|---:|---:|
| adapter | 0x200002d0 | 5,280 |
| FFT context | 0x20001770 | 80 |
| feature state | 0x200017c0 | 2,072 |
| DTCM section total | 0x200002d0 | 7,432 |
| tensor arena, SRAM | 0x201c01c0 | 65,536 |
| SWD `g_stats`, non-cacheable SRAM | 0x20230180 | 88 |
| reserved stack | 0x2001e000 | 8,192 |

The Release scatter-loading table has an entry for destination 0x200002d0,
length 0x1d08, calling `__scatterload_zeroinit`. Debug places its 7,448 B DSP
section in DTCM too; an existing debug UART DTCM buffer is separate. Both linkers
accept the DTCM/stack bound assertion. No FFT tables or additional executable
code were moved to TCM in this candidate.

The Release-oriented MVE verifier was also tried on Debug: it reports no vector
registers inside the `arm_cfft_f32` wrapper itself. Debug delegates work to
out-of-line helpers. This is not treated as a passing Release-style check, and
the verifier was not weakened. Debug builds and host gates pass; only Release
has the complete 10-range verification claim.

## Images and physical acceptance

| Image | SHA-256 |
|---|---|
| Previous Release | `7cfaa93529acbbba455f0779354fed5365404c3820b0b20e93a3f8fd4252eb1f` |
| Current Release `build/M55M1_GestureAI.bin` | `6f866c8cecb251c36021c0a1b3bd16dd39c91e3b3f8188616a1c2191f791ab39` |
| Current Debug `build-debug/M55M1_GestureAI.bin` | `bddcba7a151c844fe37a8cdf1ae22c39b8272fa4e16a2b096ee9f1209dd3af09` |

The previous Release AXF/BIN and changed source baseline are preserved under
ignored `diagnostics/m55_dsp_optimization_20261114/baseline/`. No flash or board
access occurred. Light-route self-review was performed; no independent agent
review, commit or push.

Next physical procedure, with the custom PCB's existing SWD workflow:

1. Program each identified Release image separately, power-reset and use the
   same supply, headphones/load, acoustic stimulus, microphone distance and
   warm-up. Keep the debug UART disabled.
2. Let each run continuously for the same interval (at least 60,000 hops), then
   Pause once and read `p 'audio_pipeline.c'::g_stats`. Record samples, clipping,
   sequence/deadline/model/transport faults, restarts and maximum
   capture-to-publish time. Do not compare a resumed paused run with a clean run.
3. Check quiet speech and loud transients at the same distances for continuity
   and audible behavior. UART paired-WAV capture is unavailable on this PCB.
4. Compare current with the same voltage/supply point, load, firmware profile,
   averaging interval and meter resolution. A shorter instruction sequence is
   not proof of lower current or energy.

No new board timing, wake latency, power, stack high-water, acoustic recording
or real NPU end-to-end numerical parity was measured. Native scalar/lane gates
and compiled instructions establish the stated software properties only. Keep
this image a trial candidate until the matched board run passes. Further FFT
table/ITCM placement and frontend math fusion remain separate experiments.
