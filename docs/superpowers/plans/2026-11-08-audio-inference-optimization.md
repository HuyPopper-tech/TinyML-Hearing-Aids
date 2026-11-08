# Continuous R2 Audio Optimization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reduce avoidable continuous-hop work while retaining R2 numerical,
streaming and fault behavior on the custom PCB.

**Architecture:** Keep one service interpreter/adapter and the existing state
owners. Normalize PCM once, remove redundant work, measure via existing timing
and bounded raw breadcrumbs; retain DMA/cache safeguards unless evidence supports
a change. Execute inline, no subagents, in the user's current workspace.

**Tech Stack:** C/C++, Arm Compiler 6.24, Cortex-M55 MVE, installed CMSIS-DSP,
TFLM/Ethos-U55, DMIC LPPDMA and I2S1 PDMA.

**Spec:** `docs/superpowers/specs/2026-11-08-audio-inference-optimization-design.md`

## Global Constraints

- Preserve compiled SHA69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27.
- Mono16kHz, hop16; three inputs/four outputs INT16; encoded h/c200 per lane.
- Exact binding/feedback/controller replay; preserve existing floating tolerances.
- No changes to libs, source packages, model bytes, goldens, pins or clocks.
- No flash/device operations; ordinary Release UART off; two existing build dirs.
- Debug optimizations must preserve fault diagnosis; no relaxed deadline guards.
- Preserve unrelated untracked files; no automatic cleanup or destructive Git.

## Review Focus

- Half-code rounding and saturation after removing software division: exhaustive
  state-code tests and adversarial feature quantizer tests in Task2.
- Normalized PCM lifetime and startup causality: compare PCM/float feature APIs
  with identical history, invalid input and reset in Task2.
- Partial output after FFT/Invoke/non-finite failure: existing actual C harness
  plus injected faults retained in Tasks2/3.
- First-hop diagnostics delaying the deadline: native trace test must show no
  formatting/enqueue until explicit stopped-stream reporting in Task3.
- Host scalar success hiding target MVE/ownership failure: linked range checks,
  target self-tests and explicit unexecuted board gates in Task4.

### Task1: Baseline and reproducible evidence

**Files:** fresh ignored `diagnostics/audio_optimization_<timestamp>/`; existing
build scripts/configuration; this plan and approved spec.

**Interfaces:** consume existing unittest/build commands and local host compiler;
produce timestamped logs, command receipts, baseline images/maps and ledger.

- [x] Capture revision/status, tools, source hashes and existing image hashes.
- [x] Configure Debug before compile-sensitive edits; run baseline suite and
  Debug/Release builds sequentially. Preserve logs and binaries/maps separately.
- [x] Record backend/hardware boundary; no OpenOCD connection or flash.

### Task2: Reduce portable/frontend/reconstruction work

**Files:** `services/deepfir/deepfir_dsp.h`, `deepfir_features.c`,
`deepfir_model_adapter.h/.c`, `deepfir_reconstruction.c`, `deepfir_dsp.c`;
callers in inference service and `tests/r2_numeric_harness.c`;
`tests/test_r2_optimization.py`, existing numerical tests and
`tools/verify_mve_disassembly.py`.

**Interfaces:** expose `deepfir_feature_float(const float *, uint32_t, int16_t *,
void *)`; retain PCM16 frontend entry for callers, change adapter feature
callback to normalized floats. `deepfir_state_feedback` ABI is unchanged.

- [x] Add native regression cases for normalized/PCM frontend equality and
  invalid inputs, quantization halfway boundaries, and all feedback shifts;
  demonstrate missing normalized interface and expensive feedback division.
- [x] Replace power-of-two integer feedback division with exact right shift.
- [x] Normalize PCM once in adapter and share with frontend/FIR; initialize
  complex real/imag values explicitly so no redundant full memset is required.
- [x] Evaluate guarded reciprocal feature quantization against original double
  division; reject on any integer difference. Preserve finite/saturation rules.
- [x] Remove only the redundant aligned-hop MVE tap copy, retaining public state
  equality and partial scalar stream semantics. Keep atomic reconstructed taps.
- [x] Evaluate real FFT/fast trig against installed library support, existing
  numeric bounds and memory cost. Retain proven kernels if target benefit cannot
  be supported; document each rejected/deferred candidate rather than force it.
- [x] Run focused real C/CMSIS tests, differential vectors, and target assembly
  checks. Expected: exact feedback, preserved feature codes for the guarded
  optimization, existing FFT/FIR tolerances and unchanged fault publication.

### Task3: Non-perturbing stage diagnostics and timing

**Files:** `services/deepfir/deepfir_inference_service.cpp/.h`,
`services/audio_pipeline/audio_pipeline.c`, existing runtime debug/probe tests.

**Interfaces:** add a foreground stopped-stream trace-report API if needed;
reuse existing timing struct and stage IDs. The pipeline calls reporting after
quiescing DMA, never during a measured hop or ISR.

- [x] First change native trace test to require bounded raw marks/no live UART
  and explicit reporting outside processing; run RED.
- [x] Replace first-hop snprintf/enqueue with raw bounded records and delayed
  formatting; retain init logs, last failure stage and first-hop visibility.
- [x] Remove live first-capture formatting; include its sequence/timestamp in
  post-stop fault reports. Update mock callers without weakening fault checks.
- [x] Attribute full-hop stages, PCM and capture-to-publish in existing probes;
  retain ordinary silent Release and failure/reset behavior.
- [x] Run Debug/probe, schedule, ownership and recovery native regressions.

### Task4: Full verification, target review and handoff

**Files:** fresh `docs/verification/2026-11-08-audio-optimization/`, relevant
audio guide, `agent_docs/project_progress.md`, `agent_docs/latest_session_work.md`.

**Interfaces:** consume Tasks2/3 binaries and reports; produce reproducible
acceptance table, measured/static distinctions and remaining device steps.

- [x] Review cache core call sites, TFLM input/output lifetime and DMA masked
  handoffs. Do not change hardware code for an unmeasured hypothetical gain.
- [x] Run `rtk python -m unittest discover -s tests -v`, formatting and
  `rtk git diff --check`; Debug/Release configure/build sequentially.
- [x] Compare images/maps, context/scratch/stack costs, arena reservation and
  linked MVE kernels; no duplicate runtime/model/interpreter.
- [x] Run replay verifier/comparator and record compiled execution BLOCKED if
  no authorized backend; never call mock output compiled parity.
- [x] Self-review entire diff and tests; fix demonstrated issues, then document
  all PASS/FAIL/NOT_RUN/BLOCKED rows and device timing/power procedure.
- [x] Prepare only this task's verified sources/tests/docs for commit/push using
  configured author; preserve ignored packages and unrelated untracked files.
  Prior user commit/push authorization persists; report actual Git outcome.

## Verified outcome

Baseline123 and final127 host tests pass; Debug/Release and both opt-in variants
build/link. Final ordinary profiles restored, clangd uses Debug. Quantization and
feedback differential checks have zero mismatches. Fast sin/cos retains existing
float limits; long synthetic-PCM DSP replay reports136 one-code PCM differences.
Real FFT rejected (max197-code error). Release RW_RAM reduces768B, BIN grows2592B.
No device execution: compiled parity BLOCKED, actual MVE/arena/stack,1ms timing,
physical audio/latency and power NOT_RUN. No deployment approval or flash.
Curated acceptance and reproduction: docs/verification/2026-11-08-audio-optimization/.

## Execution rulings

The user explicitly requested immediate implementation and already chose inline
execution without subagents. Do not ask for another plan approval or create a
second checkout/build tree. The ledger/evidence lives in the fresh diagnostics
directory; use existing compiler runtime read-only, with no global PATH changes.
All board timing, actual MVE execution, compiled replay and power claims require
device evidence; lack of it does not block portable implementation and builds.
