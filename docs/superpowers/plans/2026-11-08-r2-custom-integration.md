# R2 Custom PCB Implementation Plan

> Execute inline with superpowers:executing-plans. User explicitly forbids agents and commits and authorizes routine decisions/tests without confirmation.

**Goal:** Integrate the supplied gain/phase controller and complete streaming path into the custom PCB's sole Debug/Release build.

**Architecture:** Preserve app -> services -> drivers -> board -> libs. Reuse one interpreter, frontend and FIR; add stateless reconstruction, strict binding and continuity recovery.

**Tech Stack:** C11/C++11, existing TFLM/Ethos-U55/CMSIS-DSP/MVE, Armclang 6.24, Python unittest and native host GCC.

**Spec:** `docs/superpowers/specs/2026-11-08-r2-custom-design.md` and immutable package DSP_CONTRACT.md.

## Global constraints

No flash, subagents, commits, libs edits, source-package changes, retraining/PTQ/Vela or contract changes. Preserve dirty worktree. Runtime PCM16 16000 Hz/hop16; four INT16 outputs; b+2 play with entire hop <1000 us. Only root build and build-debug profiles.

## Review focus

- Late submit crossing a DMA boundary must fail without modifying an active buffer (task 3).
- FFT/invoke/NaN faults must latch and cannot publish a partial block (task 2/3).
- h/c feedback uses output semantics 2/3, not old 1/2, with exact Q31 rounding (task 2).
- Source package and existing unrelated dirty files must remain unchanged (task 1/5).
- Replay artifacts must distinguish raw, compiled backend, mocks and actual board execution (task 4/5).

### Task 1: Baseline, artifact import and target configuration

Files: `models/deepfir/*`, root CMake/build scripts/presets, `board/m55m1_audio.c`, debug configuration, evidence under `diagnostics/r2_custom_integration_20261108_105000`.

- [x] Record status/patch, package inventory/hashes and baseline suite (initial run: temporary-directory permission errors).
- [x] Resolve test environment, record valid baseline; configure Debug before compile-sensitive edits.
- [x] Archive previous active package; import immutable compiled model/contract/provenance with destination hash receipt.
- [x] Add failing identity/four-output/custom-only configuration checks, observe failure, then update import/build/pins. Remove NuMaker routes and obsolete generated root build folders after bounded path validation and preserving map/binary identity evidence.

### Task 2: Runtime and DSP correctness

Files: `services/deepfir/deepfir_model_adapter.*`, `deepfir_model_binding.h`, `deepfir_inference_service.*`, `deepfir_cmsis_fft_adapter.*`, new `deepfir_reconstruction.*`, tests.

Interfaces: preserve `deepfir_inference_process(input,output,16)`, add quantized replay entrypoint and memory diagnostics; mapped infer semantic order gain/phase/h_next/c_next.

- [x] Native C tests first: four outputs, every INT16 feedback code, reset/carry/context, reconstruction independent IDFT and package C, frontend synthetic PCM/FFT, alias/NaN/invoke error and output canary.
- [x] Replace direct taps, exact contract qparams/feedback, add checked FFT128 inverse reconstruction and fault latch; atomically publish PCM only after complete success.
- [x] Run focused native tests and old frontend/FIR regressions; adapt affected callers/tests to the new ABI without weakening coverage.

### Task 3: Schedule, ownership and recovery

Files: `drivers/audio_transport/*`, `drivers/tlv320dac3100/*`, `board/m55m1_audio.*`, `services/audio_pipeline/*`, associated tests.

- [x] Failing native tests for first two silence slots, capture and TX sequence, late/duplicate/gap/BUSY, DMA ownership and partial publish; full reset after fault.
- [x] Use shared audio clock/custom pins, hop16 DMA, bounded safe descriptor publish, sequence/deadline gates and foreground stop/flush/restart. Expose counters and opt-in timestamp records.
- [x] Verify independent diagnostics source still works without model linkage; only product Debug/Release presets remain.

### Task 4: Replay and build evidence

Files: `tools/replay_deepfir_r2.py`, compiled runner/service seam, tests and timestamped evidence.

- [x] Validate both trace schemas/hashes/counts; reuse package Q31 verifier/helper.
- [x] Provide compiled backend protocol/runner with firmware feedback and full-trace comparator, runtime identity, per-lane mismatch counts/max/first mismatch. Run actual backend if available; otherwise explicit BLOCKED receipt and reproducible instructions.
- [x] Run full suite, Debug configure/build, Release configure/build sequentially; capture commands/exit codes/versions, binary hashes/map totals, static context/DMA/stack sizes. Actual arena allocation and timing remain target gates if not executed.

### Task 5: Handoff and cleanup

Files: README/model/audio docs; `agent_docs/project_overview.md`, `project_structure.md`, `project_progress.md`, `latest_session_work.md`; integration manifest/evidence acceptance.

- [x] Inspect current diff and linkage/dependencies; verify all source hashes unchanged and no unrelated loss.
- [x] Nine-row PASS/FAIL/NOT_RUN/BLOCKED table with receipts and exact follow-up commands, quality limits and custom pins/recovery. Keep deployment_approved=false.
- [x] Final full suite/diff check after fixes; no flash. List skipped hardware measurements and risks.

## Verified outcome and bounded exceptions

Completed the implementable tasks inline. Full native/regression suite132 PASS;
both custom product builds and opt-in replay Debug build PASS. Actual source/CMSIS
vectors cover the contract; controller mocks/register models remain component
tests. Compiled execution is BLOCKED, raw Linux replay was not rerun, and board
allocation/timing/audio gates remain open as recorded in acceptance.json.
No flash or deployment approval. Final self-review used owning source/callers and
link maps; user forbids reviewer subagents. It found/fixed stale diagnostic capture
cache restoration in both scripts/presets, with a red/green regression.
