# Cortex-M55 audio-path optimization review — 2026-11-14

Historical review of the earlier +24 dB capture configuration. Its proposals,
image identities and measurements are time-specific. Current implementation and
profile evidence are in [pipeline cleanup](2026-11-14-pipeline-cleanup.md).

## Scope and evidence

Read-only review of the current working-tree product, including the approved
PCM24 trial. No firmware source, compiler option, clock, model, pin or power
configuration was changed for this review. Light route; main agent only.

Reviewed the repository instructions, current integration/optimization/power
notes, app loop, capture/transport/pipeline, frontend, adapter, controller bridge,
reconstruction/FIR, PCM rendering, playback ownership, power and NPU runtime.
The generated Release compile commands, linker map and explicit Thumb/MVE
disassembly were inspected. The checked Release BIN has SHA-256
`7cfaa93529acbbba455f0779354fed5365404c3820b0b20e93a3f8fd4252eb1f`.
Objects inspected for the relevant app/power/transport/frontend/adapter sources
are newer than their source files. This is artifact/static evidence, not a new
board timing or current measurement.

Local inspection artifacts: `diagnostics/cortex_m55_review_20261114/`
(`release_thumb.txt`, `symbol_blocks.json`; ignored).

Primary references:

- [Arm Cortex-M55 Software Optimization Guide, r1p1, issue03,
  102692_0101_03](https://documentation-service.arm.com/static/63ac5ce1396e6135830ba9fe),
  especially sections3.9, 3.12, 3.14–3.15 and4.1.
- [Arm Cortex-M55 Technical Reference Manual, r1p1,
  101051_0101_01](https://documentation-service.arm.com/static/619529c383e60c5c768e34ff),
  section2.6, including LOB and EPU power-state considerations.
- [Arm CMSIS-DSP build guidance](https://github.com/ARM-software/CMSIS-DSP#building-for-speed).

The optimization guide describes instruction overlap/hazards and DTCM banking;
it does not supply measured latency for this complete board. CMSIS guidance
recommends aggressive optimization and fast data memory, but application
rounding, finite-value checks and the existing DSP contract constrain adoption.

## Current flow and retained strengths

`app/main.c` polls foreground audio and then requests guarded shallow Idle.
DMIC FIFO24 feeds two non-cacheable 16-word DMA slots. Foreground gates DMA,
checks ownership, decodes signed PCM24, records statistics and normalizes by
2^23. The same normalized samples feed both the causal FFT256 frontend and
waveform FIR. Quantized features plus recurrent h/c enter the single
TFLM/Ethos-U55 controller; gain/phase become a complex IFFT128 filter, followed
by FIR/crossfade, PCM16 rendering and tagged I2S/DAC submission.

Verified existing strengths:

- Release uses Arm Compiler, `-O3`, Cortex-M55 and hard-float. MVE is enabled.
- `SystemInit` already enables CCR.LOB, followed by barriers. The linked FIR
  inner loops already contain low-overhead `LE` instructions.
- The existing disassembly verifier passed all nine linked kernel ranges.
  PCM24 normalization uses four `vcvt.f32.s32 ... #23` operations; feedback uses
  Q31 vectors, FIR uses vector FMA, and frontend uses vector log/exp.
- The driver already avoids repeated cache maintenance of immutable model data.
  Inference outputs already reference interpreter tensors directly. Do not
  propose removing output copies that are absent.
- Stack/scratch is already in DTCM: the reserved stack is8KiB at0x2001e000.
- Capture and playback ownership guards/barriers, complete-output publication,
  finite checks, full synchronized restart and the 1ms fault threshold remain.

## Ranked opportunities

### 1. Batch PCM16 output rendering in the existing DSP module

`services/deepfir/deepfir_inference_service.cpp:650` loops over16 output samples.
Release unrolls that loop but still emits16 calls to
`deepfir_pcm16_from_float` in the successful hop path. The scalar helper in
`services/deepfir/deepfir_dsp.c:209` repeats finite/saturation/rounding work.

Candidate: one bounded block-render operation owned by the existing DSP module,
with a scalar reference and MVE implementation, keeping a private destination
until the whole block succeeds. Preserve multiplication by32768, ties-away
rounding, both saturation thresholds, invalid-input behavior and failure
atomicity. Do not substitute a CMSIS conversion without checking its rounding
configuration against these rules. No new generic PCM module is necessary.

Confidence: high that calls/checks exist; their actual share of the829us supplied
observation is unknown. This is a small, testable first optimization.

### 2. Reuse FIR coefficients across more output lanes

`services/deepfir/deepfir_dsp.c:110` processes four output groups, each with128
tap iterations and two vector accumulators. The disassembly repeats the
load-coefficient / vector-load / FMA / load-coefficient / FMA / LE sequence for
each group. Coefficients are therefore loaded again for every four samples.

Candidate: evaluate two output groups per tap loop, reusing each pair of
coefficients across eight samples. That can halve coefficient-load operations
without reducing the required FMA count or changing tap order per output lane.
Four accumulators and extra history vectors increase register pressure; reject
the variant if spills or worse instruction overlap erase the benefit. Preserve
the existing crossfade arithmetic and history semantics. Do not use a different
parallel reduction across taps merely to obtain shorter code.

Confidence: high for repeated loads, conditional for speed/energy benefit.
Use a bounded kernel comparison before considering handwritten assembly.

### 3. Place selected CPU DSP state/tables in DTCM; test ITCM separately

The Release map currently places frontend state2072B at0x201d0200 and adapter
state5280B at0x201d0b18, both in SRAM. DTCM's static-data region is empty after
the712B vector table; stack reservation is separate. ITCM contains only320B
from vendor `kpi.o`, not the DSP hot functions. Several active FFT tables remain
in flash, including ordinary and rearranged MVE twiddles.

Candidate: first test CPU-owned frontend/FIR/adapter state in aligned DTCM with
explicit linker ownership, then selected active tables and hot functions as
separate variants. CPU input maps are copied into NPU-visible interpreter
tensors before Invoke; that ownership distinction must be preserved. Keep the
model image, arena, DMA buffers and SWD statistics in their appropriate existing
regions. Check actual MPU/security/accessibility, linker capacity, stack margin
and startup copying before board use. Never move every buffer to TCM blindly.

Confidence: high for current placement; the current cache may already serve
much of the working set, so the benefit requires a matched board comparison.

### 4. Reduce Release bookkeeping after preserving diagnostic semantics

`feature_callback`, `infer_callback` and `process_audio` retain timing collection
even with runtime telemetry disabled. `process_audio` converts/stores total time
once after the adapter and again after successful PCM rendering. Inference also
snapshots runtime statistics every hop. These are separate from required
pipeline deadline checks and the user's SWD clipping counters.

Candidate: avoid the first successful-hop total-time conversion, retaining it
on failure, and review whether detailed stage accounting can be consolidated.
Preserve the public last-timing API, failed-hop timing, counter visibility and
deadline behavior. Treat this as lower potential than FIR/memory optimization;
do not remove observability without a defined replacement.

### 5. Frontend fusion is a later, higher-risk investigation

`deepfir_feature_float` shifts240 floats of history, computes power arrays,
performs six CMSIS log/scale/exp calls over129 bins, then packs/quantizes387
values. These are potential memory-pass/packing targets. However, approximate
math, FMA contraction, silence handling, tail behavior and quantizer half-code
guards interact with feature codes and recurrent inference.

Do not replace the guarded double fallback indiscriminately. Its per-hop
reciprocal division is visible in disassembly, but one division alone is not
evidence that it dominates runtime. Existing real-FFT256 evaluation was rejected
for exceeding the feature-code tolerance. The current complex FFT/IFFT remains
the approved reference until a separately accepted numerical comparison.

## Power and integration constraints for any next change

Keep core220MHz/PL0, audio APLL1 request73.728MHz,16kHz/hop16, PDM2.048MHz/OSR128,
gain24 and DAC-9. Preserve peripheral gating, Release UART-off policy, shallow
WFI, NPU semaphore WFE/SEV, external timebase and uninterrupted audio clocks.
Do not hold the EPU awake, busy-wait for NPU completion, change power level,
raise clocks or weaken fault guards in an optimization comparison.

The app/service/driver/board ownership remains usable. `common/audio_pcm.h` is
allowed but optional; moving its inline arithmetic into existing owners is code
organization, not a demonstrated performance improvement. The foundational
overview still describes PCM16 capture and older power notes contain a historic
DAC ceiling; use current source and the PCM24 trial notes for present settings.

The user's snapshot covers235568 accepted samples (~14.723s), max recorded
capture-to-publish829us and zero reported faults before Pause. It is neither
per-stage profiling nor an acoustic-latency or long-soak guarantee. Timestamping
at the capture ISR also excludes hardware-completion-to-ISR wake latency.

## Proposed acceptance and next decision

First preference: a bounded code-only pass on block PCM output and FIR load
reuse, one variant at a time; consider memory placement separately. An
alternative is a memory-placement-first comparison to preserve arithmetic while
testing whether memory is the dominant constraint. Broader frontend math changes
should follow measured stage evidence and an explicit numerical acceptance.

For either approach, retain existing numeric/failure tests, add the necessary
edge vectors, inspect final linked instructions/map, then compare matched
Release stage timing, continuity, wake jitter and current/energy per successful
hop. Faster software is not proof of lower board power. No projected percentage
speedup or current saving is claimed.

Checks run for this review: current Release MVE disassembly verifier (PASS), map
and compile-command inspection, source/object freshness spot checks and source
review. No rebuild, full regression rerun, flash, board measurement or firmware
optimization was performed during this review.
