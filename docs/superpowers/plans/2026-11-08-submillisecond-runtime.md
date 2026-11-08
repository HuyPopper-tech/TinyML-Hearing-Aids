# Submillisecond runtime optimization implementation plan

> Execution: superpowers:executing-plans, inline only, no agents by user instruction.

**Goal:** Reduce R2 CPU work without changing model codes, DSP contract or the 1 ms guard.
**Spec:** `docs/pipeline_submillisecond_analysis.md`, approved for implementation by the user.
**Architecture:** Keep adapter-owned state/history and existing frontend/FIR/transport.
**Tech:** Arm6.24, Cortex-M55 MVE, installed CMSIS-DSP, native GCC references.

## Constraints and ownership

Keep the existing worktree and its unrelated files, source packages and libs untouched.
Use only build-debug/build. No flash, pin/clock/DMA configuration changes or new queue hop.
Adapter owns encoded h/c; feature state owns analysis history; reconstruction scratch
is private until success. Transport trace is bounded and formats only after stop.
Existing authorization covers inline work, documentation and commit/push.

## Tasks

- [x] Feedback: extend `tests/test_r2_optimization.py` and native harness with
  exhaustive commit-state/carry/fallback cases; add failing Arm assembly gate for
  a four-lane rounded Q31 kernel. Implement guarded zero-point-zero shift30/31
  MVE + scalar equivalent in `deepfir_model_adapter.c`; preserve generic fallback.
  Add startup exhaustive MVE self-test in inference init, before DMA starts.
- [x] Reconstruction/frontend: retain CMSIS complex FFT and guarded quantizer;
  batch quantization/conversion and fixed roll/taper where exactness is provable.
  Add differential tests first; retain generic and exceptional-input behavior.
  Reject experiments outside existing numeric tolerances. No speculative approximate sqrt.
- [x] Startup evidence: add bounded raw TX/RX/TD/FIFO phase observations to the
  existing opt-in probe/report path, with actual C lifecycle tests. No live IRQ UART
  or changes to request thresholds/start order until board evidence establishes cause.
- [x] Verification/handoff: full132 host tests, Debug/Release/Probe build/link,
  scoped MVE disassembly, existing DSP trace replay, hashes/maps/stack/size checks.
  Restore ordinary opt-ins OFF and Debug editor. Update guide and handoffs with
  actual results, unresolved target timing/parity and exact next-run instructions.

## Acceptance and review focus

Integer carry exact for all65536 codes/lane including negative ties, INT16_MIN,
saturation and nonzero-qparam fallback. SIMD remainder must not overread 200 codes.
Public failure outputs unchanged; no partial publish/reset/retry semantics changed.
Keep floating tolerances; actual MVE numerical execution and 60000-hop ≤900us
margin require board evidence, never inferred from assembly/native simulation.
Trace must be reset per lifecycle, bounded, nonblocking and absent from silent Release.
Self-review replaces delegated review because the user prohibits subagents.

Progress/evidence ledger: `diagnostics/submillisecond_runtime_20261108/ledger.md`.

## Verified handoff

Implementation, host/build verification and self-review completed. Evidence:
`docs/verification/2026-11-08-submillisecond-runtime/README.md`.
Hardware timing/stack/MVE/audio acceptance remains NOT_RUN; compiled parity
BLOCKED. Checkboxes mean implementation/handoff work completed, not board gates.
