# Fixed-gain input conditioner for long trials

Status: approved for implementation by the user's 2026-11-14 request.

## Intent and decisions

Package one reproducible firmware configuration for extended custom-PCB audio
trials. The user selected fixed-gain approach A and explicitly requested one
version with **+21 dB software input gain**. Distant-speaker testing is not yet
available; no claim of distant-speech quality is an acceptance prerequisite
for the software package.

The design keeps DMIC **+12 dB**, FIFO24/DMA32, mono **16 kHz**, hop16, and
the existing DAC startup gain **-9 dB**. Quiet signals below compression receive
approximately +33 dB combined digital gain. This is not acoustic calibration
or an SNR improvement. The previous raw-input rail hits cannot be repaired by
subsequent compression. The user identified the one reported transport restart
as caused by Cortex-Debug Pause/Continue, not an unexplained continuous-run fault.

## Approaches considered

- **Selected: fixed gain with improved compressor and limiter.** Deterministic,
  small state, bounded processing, and straightforward repeatable trials.
- **Deferred: adaptive AGC.** Needs speech/noise estimation and additional
  validation of pumping and input-level changes. Not needed for this package.
- **Deferred: multiband WDRC/output gain changes.** Requires separate acoustic
  fitting and quality evaluation; it does not address clipping before capture.

The previously proposed +18/+21 dB comparison package is superseded by the
user's explicit single-version request. Archive the existing image as rollback
evidence, but deliver only the new +21 dB Release configuration.

## Signal processing

```text
DMIC +12 dB -> PCM24 / 2^23 -> fixed +21 dB float gain
 -> smoothed peak detector -> soft-knee compressor -> independent peak limiter
 -> same conditioned float hop for frontend and waveform FIR
 -> existing PCM16 rendering -> existing DAC configuration
```

Detector calculation is a sidechain; the waveform itself has no extra buffer
or queued hop. Preserve the existing shared frontend/FIR input and raw
PCM16/PCM24 adapter/replay APIs.

The following time constants are engineering starting values proposed here,
not values already acoustically validated on this board:

| Parameter | Proposed value |
|---|---:|
| Software input gain | +21 dB |
| Compressor threshold | -12 dBFS after preamp |
| Compression ratio | 3:1 |
| Soft knee width | 6 dB |
| Hop-peak detector attack | Immediate rise |
| Hop-peak detector decay | 20 ms |
| Compressor gain attack | 3 ms |
| Compressor gain release | 120 ms |
| Limiter ceiling | -1 dBFS before the model |
| Limiter gain attack | Immediate reduction as required |
| Limiter gain release | 20 ms |

For each captured hop, calculate peak P after the fixed preamp. Retain detector
envelope E with immediate rise and exponential decay:

`E = max(P, E_previous * exp(-ln(9)*16/(16000*0.020)))`.

Compute the existing soft-knee compression curve from `20*log10(E)`. Keep the
quiet branch free of logarithm/exponential calls. Smooth its target linear
gain per sample using `alpha = exp(-ln(9)/(16000*T))`, with the attack or
release time selected according to gain direction. Coefficients are computed
once or precomputed, not calculated for each sample. The time convention is
the 10-90% exponential step convention; detector decay leaves 1/9 after 20 ms
without a sustaining peak.

The limiter has its own linear gain state. It releases toward unity with its
20 ms coefficient, then immediately reduces gain if the current compressor
output would exceed the ceiling. Apply a final roundoff guard. A transient
therefore cannot leave the compressor's slower recovery state stuck at the
limiter's reduction. Some distortion from immediate peak limiting remains
possible; no lookahead delay is introduced.

Do not silently change threshold, ratio or knee while implementing. The
envelope and independent limiter are the substantive smoothing changes.

## Ownership, configuration and diagnostics

Extend the existing `services/deepfir/deepfir_input_conditioner.c/.h` module;
keep its foreground-only owner in the inference service and its CPU state in
DTCM. No new generic common/HAL layer, dynamic allocation, ISR processing,
runtime logging, BLE control or settings persistence is needed.

There is one fixed +21 dB configuration. No runtime gain selector, alternate
product profile or second firmware variant is introduced. Current build caches
must explicitly use DMIC +12 dB so an older cache cannot silently retain +24 dB.
Keep MVE normalization and existing FIR/frontend optimizations, clocks, DMA
ownership, recovery pins and idle/power policy.

Keep `g_input_conditioner` available to the debugger with documented fields:

- Current compressor gain, limiter gain and their product (`gain` excludes
  the fixed preamp).
- Processed samples, meaningful compression samples, peak-guard action count,
  raw normalized input peak, conditioned pre-model peak and minimum combined gain.
- Latest completed 100 ms RMS window before software gain and after conditioning.
  Accumulate squared normalized samples and update RMS at window boundaries;
  do not calculate dB/logarithms per sample for telemetry.
- Use 64-bit sample/event accumulators for extended trials. Count meaningful
  compression only at combined reduction >=0.1 dB, so float release residue
  near unity no longer marks the whole run as compressed.

Cold initialization clears all diagnostics. Automatic stream reset clears
detector/gain history and the unfinished RMS window, while preserving cumulative
diagnostics. Document current-stream versus cumulative fields explicitly and
test both lifecycles. Keep raw `audio_pipeline.c::g_stats` behavior unchanged.
These counters describe input conditioning; they do not establish final DAC
or acoustic output levels. SWD reads are taken while halted, with matching AXF.

## Verification and acceptance

Write failing tests before product edits. Use one cohesive streaming reference
harness and existing integration tests rather than fragmented fixtures.

Software gates:

1. Independent double reference for the detector, soft knee, separate gain
   envelopes and limiter. Define the expected equations before implementing.
   Target maximum absolute output error <=3e-5 across long seeded streams;
   ceiling <=10^(-1/20)+2e-7 and finite output for every tested legal sample.
2. Cover silence, low PCM24 bits, both rails, all-lane impulses, amplitude
   sweeps, level steps, repeated peaks and long quiet/release tails. Verify
   compressor and limiter recover independently, and slow detector changes
   do not depend on isolated process-call boundaries beyond the fixed hop.
3. Verify static knee/ratio points, attack/release recurrences, monotonic
   recovery where mathematically expected and the +21 dB quiet-signal gain.
   Check sample continuity bounds for smoothed gains; immediate limiter
   reductions are intentional exceptions.
4. NULL, size, range, finite-state and alias rejection leave output/state
   unchanged. Adapter failures still latch and require a full stream reset.
5. Scalar and native MVE lane branches agree under the stated numeric
   contract. Test with production-like fast-math plus finite checks; preserve
   the final linked Release MVE normalization and other existing kernel gates.
6. Exercise RMS windows, partial-window reset, meaningful-compression threshold,
   32-bit-boundary crossings using seeded counters, cold clear and cumulative
   statistics across stream restarts. Quiet float residue must not increment
   meaningful-compression samples.
7. Verify the service passes the identical conditioned hop to frontend and FIR
   and preserves raw replay behavior. Run the complete unittest suite,
   `git diff --check`, Debug/Release builds, map/DTCM/stack inspection and
   offline GDB symbol lookup with the final AXF.

Do not weaken assertions or change algorithms to claim acoustic quality from
host tests. If numeric/compiled behavior invalidates a proposed parameter or
tolerance, record the evidence and resolve it before claiming acceptance.

Physical gates remain separate: uninterrupted Release operation, full-hop
capture-to-publication <1000 us, no unexpected sequence/deadline/model/transport
faults, plus current, stack watermark and acoustic checks with identified
firmware. A future board trial should run at least 30 minutes from reset without
breakpoints and Pause only once at the end. Pausing and resuming can create a
transport fault; do not classify that as spontaneous instability.

## Single-version delivery

Create one self-contained archive under a project-owned artifacts location:
Release BIN and matching AXF, SHA256 manifest, exact gain/compressor parameters,
build/toolchain identity, map/stack/resource summary, numeric test report and
SWD/long-run procedure. Keep generated build output out of Git. Debug is a
verification build; it is not a second delivered trial configuration.

The procedure must name custom PCB capture pins PA1/PA2 and retain the existing
SWD recovery path PF0/PF1, nRESET, GND and VREF. No flashing is part of this
design. Distinguish host verification, user-supplied prior snapshots and future
physical acceptance. Preserve unrelated dirty/untracked work and do not edit
deployment-only handoff/progress documents in Light route.

Open acceptance limits: acoustic smoothing, distant-speech benefit, actual
NPU replay, continuous-run timing margin and power of this new image are not
established by design approval or software tests.
