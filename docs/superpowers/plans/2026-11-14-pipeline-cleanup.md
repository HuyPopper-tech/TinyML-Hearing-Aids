# M55/U55 Pipeline Cleanup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Optimize and clean the existing product pipeline without changing its arithmetic contract, then commit and push reviewable changes.
**Architecture:** Existing app -> services -> drivers -> board -> libs; CPU state in DTCM and NPU arena/model in SRAM. Inline Light-route execution.
**Tech Stack:** C/C++, Arm Compiler 6.24, CMSIS-DSP/MVE, TFLM/Ethos-U55, Python unittest.
**Spec:** `docs/superpowers/specs/2026-11-14-pipeline-cleanup-design.md`.

## Global Constraints

16 kHz/hop16, DMIC +12 dB, software +21 dB; preserve guarded double quantization,
model bytes, power/clock/IRQ/DMA policy, failure atomicity and SWD recovery.
No vendor edits, generated build commits, force push or unrelated local files.

## Review Focus

- Ring wrap/reset must preserve startup zeros and exact chronological FFT input.
- Release diagnostic removal must preserve deadline enforcement and fault latching.
- Debug/Probe must retain stage fields, failed-hop timing and stopped-only reporting.
- PCM24 rail/low-bit/sign behavior and raw replay must remain exact.
- Dirty pre-existing changes must be committed by responsibility, not mixed with local PDFs.

## Tasks

- [x] Capture fresh baseline and archive sources/images. Commit existing PCM24
  capture/adapter, DSP kernels, conditioner and pipeline integration separately,
  with matching tests and detailed messages; preserve dependency order.
- [x] Add failing ring tests in `tests/test_frontend_ring.py` and
  `tests/frontend_ring_harness.c`. Extend `deepfir_feature_state_t` in
  `services/deepfir/deepfir_dsp.h` with `history_position`; reset/validate it and
  replace history shift in `deepfir_features.c`. Exact FFT-frame oracle,
  4096+ hops, ring wraps/reset and invalid-input gates; run focused tests.
- [x] Test silent Release callbacks/process/get-last-timing versus diagnostic
  modes; condition existing service timing bookkeeping on telemetry/probe.
  Keep pipeline safety clocks/counters and stopped-only reporting. Build/test
  all profiles; commit profile cleanup separately.
- [x] Remove `common/audio_pcm.h`, put its three operations into existing
  owners and adapt tests to compile the actual owning code. Preserve endpoint,
  low-bit and all-PCM16 export assertions; run focused/full suites and commit.
- [x] Audit product architecture/style/comments; update stale PCM16 docs and
  explicit profile contracts. Full tests, three Arm builds, linked MVE/map/stack
  and model hash checks. Repackage final Release and write the evidence report.
- [x] Commit reviewed documentation, push `codex/m55-u55-pipeline-cleanup`
  without force and verify remote/local commit equality.

## Verification receipt

Final linked-image suite: 151 tests, 47.460 s, OK. Release, Debug and Release
Probe Arm Compiler 6.24 builds pass; Release/Probe pass all ten linked MVE ranges.
Model manifest hashes and guarded-double behavior are retained. One Release
trial package has verified member checksums. Detailed evidence and pending board
checks are in `docs/verification/2026-11-14-pipeline-cleanup.md`.

Remote delivery was verified with `git ls-remote`: the implementation/evidence
head `492ed41608d5d9a4f1976a916741bece4a2a8450` matched local HEAD before this
documentation-only closeout. The branch tracks origin; no merge or force push
was performed. Unrelated local documents remain untracked and preserved.
