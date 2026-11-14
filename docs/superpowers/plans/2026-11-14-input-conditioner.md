# PCM24 input conditioning implementation

Historical +18 dB conditioner plan; implemented and superseded by the approved +21 dB long-trial configuration. See [current verification](../../verification/2026-11-14-pipeline-cleanup.md).

The user approved implementation in the existing dirty workspace (Light route).
Keep 16 kHz, FIFO24/DMA32, existing MVE DSP, clocks and idle/power policy.

## Contract

Use DMIC +12 dB and a fixed software +18 dB preamp. Normalize PCM24 without
discarding its low bits, then compress once before the frontend/FIR split.
Threshold -12 dBFS, ratio 3:1, soft knee 6 dB, attack 1 ms, release 80 ms.
The detector uses the peak of the already captured 16-sample hop; no extra hop
buffer or delay. Smooth the linear target gain per sample with
alpha = exp(-ln(9)/(16000*time_seconds)). An immediate gain guard enforces
-1 dBFS even while the compressor attack is settling; retain its reduction
in the release state. This guard can distort transients; it cannot undo DMIC
saturation. Fixed gain also amplifies noise and changes the model input distribution.

The service owns a small conditioner state, reset together with model/frontend.
Legacy raw adapter PCM16/PCM24 APIs and compiled model replay remain unchanged.
The production PCM24 service forwards the same conditioned float hop to both
feature extraction and FIR through a validated float adapter entry point.
No allocation, logging or ISR processing is added.

## Execution and acceptance

1. Write failing host tests for streaming numeric agreement with an independent
   double reference, bounds, quiet gain, soft knee, attack/release, reset,
   PCM24 low bits, both rails, invalid arguments and atomic failure.
2. Add the conditioner and float adapter boundary; integrate service reset,
   hardware default and build/MVE verification. Verify service routing with the
   actual adapter and controlled inference/FFT callbacks.
3. Run the full host suite, Release and Debug builds, final linked Release MVE
   verification, map/stack/resource inspection and diff checks. Preserve evidence
   in diagnostics/input_conditioner_20261114 and publish a verification report.

Numeric tolerance: absolute error <= 3e-5 over long streaming double-reference
comparisons; scalar/MVE lane-model normalization must match. Output bound
<= 10^(-1/20) + 2e-7, finite for every legal PCM24 sample. Invalid input must
leave conditioner state/output unchanged and must not invoke adapter callbacks.
Physical deadline, acoustic quality and power remain unmeasured until board tests.

## Ledger

- Baseline images/sources archived with SHA256 before edits; Release configured.
- Ruling: execute in this workspace without commits or agents, per user route
  and existing working changes. Self-review at the end cannot replace board evidence.
- Task 1 complete: new numeric/API tests observed RED, then GREEN. Independent
  double reference max absolute error 5.07930639e-6 across 462448 samples per
  branch; strict scalar, lane model and fast-math lane model hashes agree.
- Task 2 complete: shared conditioned float service path and stream reset,
  validated adapter entry, +12 dB current build caches and +18 dB preamp;
  actual production MVE normalization range replaces the now-unused raw API.
- Task 3 complete: final 147-test suite passes (44.370 s), Release/Debug builds
  exit 0, Release 10-kernel MVE check passes, typed GDB symbols verified offline,
  map/stack inspection and self-review complete. Full evidence and physical
  acceptance limits: docs/verification/2026-11-14-input-conditioner.md.
