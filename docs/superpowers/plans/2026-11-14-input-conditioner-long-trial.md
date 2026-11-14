# Input conditioner long-trial implementation

The fixed +21 dB trial is implemented and numerically verified. Current image hashes are in the pipeline-cleanup receipt. See [current verification](../../verification/2026-11-14-pipeline-cleanup.md).

Approved by the user's 2026-11-14 request to implement the accepted design.
Execute inline on the Light route; preserve unrelated work and do not flash.

Design: `docs/superpowers/specs/2026-11-14-input-conditioner-long-trial-design.md`.

## Acceptance

One +21 dB software-gain profile, DMIC +12 dB, FIFO24/DMA32, 16 kHz,
hop 16. No added buffering, architecture or power-policy changes. Separate
compressor and limiter dynamics, cumulative 64-bit statistics, complete 100 ms
RMS windows. Independent double oracle error <= 3e-5; ceiling tolerance 2e-7.
Raw replay APIs and identical frontend/FIR input remain intact.

## Tasks

- [x] Extend `tests/input_conditioner_harness.c` and
  `tests/test_input_conditioner.py` for the accepted numerical and reset contract;
  observe failure before implementation.
- [x] Extend the existing conditioner C/H and service cold/stream reset callers.
  Verify scalar, native MVE lane model and fast-math contracts; run all tests.
- [x] Build Release and Debug sequentially. Verify Release MVE ranges, typed
  debugger symbol, map/stack resources, and diff hygiene. Write a new verification
  report and SWD long-run procedure; package one matching Release BIN/AXF with
  hashes and configuration identity.

## Evidence boundaries

Host arithmetic, compilation and offline symbols are distinct from board timing,
continuous-run reliability, current and acoustic quality. Those board checks
remain explicitly pending. Baseline bytes are retained under ignored
`diagnostics/input_long_trial_20261114/baseline/`.
