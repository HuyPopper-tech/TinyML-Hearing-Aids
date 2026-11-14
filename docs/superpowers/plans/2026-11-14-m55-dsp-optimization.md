# Cortex-M55 bounded DSP optimization implementation plan

Historical bounded DSP plan; implemented and verified. Current image and profile evidence supersede its earlier build identities. See [current verification](../../verification/2026-11-14-pipeline-cleanup.md).

> **For agentic workers:** Use superpowers:executing-plans; Light route, main agent only. Work in the existing dirty tree; preserve unrelated work and do not commit.

**Goal:** Reduce CPU work in the approved PCM24 audio path without changing its numerical or power contract.

**Architecture:** Extend the existing DSP module with a fixed-hop PCM renderer. Reuse FIR coefficients for eight outputs while retaining each lane's tap order. Put only CPU-owned persistent DSP state in DTCM; retain all NPU/DMA allocations. Consolidate duplicate successful-hop timing work.

**Tech stack:** Arm Compiler 6, CMSIS-DSP/MVE, C/C++, Python unittest.

**Spec:** `docs/verification/2026-11-14-cortex-m55-review.md`, implementation authorized in the following user turn.

## Global constraints

- Preserve 16 kHz/hop16, PCM24 input, PCM16 output, DMIC +24 dB, DAC -9 dB, core 220 MHz/PL0, APLL1 request 73.728 MHz, WFI/WFE/SEV and clock gating.
- No model, FFT, feature, quantizer or compiler arithmetic changes; no vendor edits.
- Preserve ties-away PCM rounding, saturation, finite checks, alias rejection, full reset on fault and complete-output publication.
- Compare exact bytes with the previous MVE FIR algorithm; also retain existing scalar tolerance gates.
- Link-map/instruction evidence is not physical timing, power or acoustic evidence.

## Review focus

- Half-code neighbors and saturation endpoints must produce identical PCM16 codes.
- NaN/Inf in any lane must leave the public output unchanged.
- Changing taps, reset, long streams and non-hop scalar fallback must retain state/history semantics.
- NPU arena/model, DMA and SWD statistics must stay outside CPU-only DTCM.
- Failed-hop timing, fault latch and frame deadline checks must survive bookkeeping changes.

## Task 1: Freeze baseline and add numerical gates

Files: existing `tests/test_r2_optimization.py`, new `tests/r2_dsp_optimization_harness.c`, test-only `tests/mve_model/arm_mve.h`.

- [x] Preserve pre-change source, image/map and hashes under ignored diagnostics.
- [x] Add a native scalar/MVE lane harness: exhaustive half-code neighbors over PCM16 range, random float bit patterns, all-lane nonfinite failures, pointer/overlap validation, output canaries; require zero PCM mismatches.
- [x] Compare production FIR to the old group4 algorithm using explicit fused multiply-add over changing-tap/reset streams; require identical outputs and state, preserve old scalar tests.
- [x] Run RED before implementing the new renderer. Native lane tests are not MVE hardware execution.

## Task 2: Optimize PCM rendering and FIR

Files: `services/deepfir/deepfir_dsp.c/.h`, `deepfir_inference_service.cpp`, existing disassembly verifier and regression tests as needed.

Interface: `int deepfir_pcm16_render_hop(const float *input, int16_t *output)` returns 0, -1 invalid/overlap, -2 nonfinite; 16 samples only; unchanged output on failure.

- [x] Validate the full block before stores; render using MVE four-lane conversion and scalar fallback, retaining original rounding operations.
- [x] Replace scalar service render loop with the block API; preserve the diagnostic failing index.
- [x] Try FIR group8; inspect target assembly for coefficient reuse and inner-loop spills. Reject that variant if compiler output defeats the intended reduction.
- [x] Run exact numerical gates and existing regression suite.

## Task 3: Memory placement and bookkeeping

Files: `deepfir_inference_service.cpp`, existing scatter file only if necessary; existing runtime tests.

- [x] Align adapter/frontend/FFT context in the existing zero-init DTCM section. Keep model, arena, DMA and SWD counters unchanged. Verify startup zeroing, region bound and 8 KiB stack reservation in both maps.
- [x] Keep failed adapter timing while removing the redundant successful-hop conversion; retain other public timing fields and deadlines.
- [x] Consider FFT tables/ITCM only as a separate variant if link evidence supports a bounded change; do not move unverified dependencies indiscriminately.
- [x] Run all host tests, Release/Debug builds, linked MVE checks and `git diff --check`.

## Task 4: Evidence and board handoff

- [x] Record exact commands/counts/mismatches, image hashes, map placement and code-generation findings in an English verification report.
- [x] Self-review the connected changes under Light route; do not spawn agents or commit.
- [x] Supply flashable build paths and a matched board test procedure: no-fault continuous run, stage/end-to-end timing and matched power measurement. Mark all unrun physical checks explicitly.

## Completion evidence

Implemented and verified in the existing working tree; no commit. See
docs/verification/2026-11-14-m55-dsp-optimization.md for exact numeric gates,
144-test result, compiler/map evidence, hashes and unrun physical checks.
FFT table/ITCM variants were considered and deferred to matched board profiling;
only CPU state moved in this candidate. Light route required self-review.
