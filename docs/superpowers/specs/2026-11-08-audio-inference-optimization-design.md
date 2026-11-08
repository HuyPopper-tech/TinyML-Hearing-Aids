# Continuous R2 audio and inference optimization

Date: 2026-11-08 (Asia/Bangkok).
Status: approach 1 and this design approved in chat; implementation requested.
Baseline revision: 0eb6e0c0f0ed7cdd2d3d8f51ebc5fa8c667b4d19.

## Intent and agreed requirements

Optimize the complete custom-PCB path from microphone capture through frontend,
inference, reconstruction, FIR/crossfade and PCM publication to the DAC. Reuse
the installed CMSIS-DSP, MVE, Ethos-U and DMA facilities. Reduce unnecessary work
and memory movement while preserving model/DSP behavior and reliable streaming.

The user selected staged optimization with numerical and streaming checks.
Shared code is optimized for both builds. Release must meet the physical
1 ms/hop budget; Debug prioritizes fault diagnosis and is not required to meet
that performance target. This does not authorize disabling Debug deadline,
overrun, underrun or stale-output protection to make slow processing appear valid.

Performance and energy gains are hypotheses until measured. Host correctness,
compiler output, device timing, physical audio and power measurements are
separate evidence categories. No automatic flashing or device operation is
authorized by this design.

## Approaches

1. **Selected: staged optimization across the whole path.** Establish comparable
   measurements, eliminate redundant work, then evaluate faster DSP kernels.
   Retain changes that satisfy numerical, ownership and failure requirements.
   This covers more opportunities but requires target validation for MVE/cache/DMA.
2. **Considered: changes preserving every output bit only.** Limit work to proven
   redundant copies, conversions and diagnostic overhead, keeping arithmetic
   kernels unchanged. Lower numerical risk, but potentially insufficient speedup.

The choice does not authorize a new algorithm, a model change or looser golden
comparison. There is no promised speedup percentage before profiling.

## Current source findings

- The feature and FIR paths already use MVE, and the FFT bridge already uses
  CMSIS complex FFTs. Enabling those facilities again is not an optimization.
- `deepfir_features.c` maintains 240 past samples, converts the new PCM16 hop,
  windows/FFTs the frame and compresses/quantizes 387 current feature values.
  Quantization currently uses double division and ties-away rounding.
- `deepfir_model_adapter.c` converts the same PCM16 hop again for FIR processing,
  stages previous/current feature context and carries the two recurrent lanes.
  The inference bridge copies three input buffers to interpreter tensors.
- Reconstruction uses separate `sinf`/`cosf` calls, complex IFFT128, roll and taper.
  The FIR path copies taps around its two convolutions. Some copies may be
  removable, subject to reset, failure and public-state semantics.
- Debug formats hop-stage text while processing its first hop. Boot-only window
  and crossfade trigonometry is already outside the steady-state hot path.
- Capture and playback handoffs briefly gate DMA requests and mask interrupts
  while checking ownership and copying/packing 16 samples. Their safety checks
  cannot be removed merely because a shorter copy looks faster.
- Ethos-U cache hooks already skip registered immutable ranges and support
  aligned range maintenance. The vendor core can request whole-cache operations
  when it receives no range sizes; actual runtime requests must be established
  before calling this a bottleneck or changing policy.

These are source observations, not stage-duration measurements. Earlier
Complex3 timings and the Vela controller estimate are not R2 full-hop evidence.

## Invariants and ownership

Keep `app -> services -> drivers -> board -> libs`. No new pipeline, interpreter,
RTOS, generic HAL, build profile or queue hop. Do not edit vendored `libs/`, model
bytes, source packages, selection locks, canonical goldens or source manifests.
Only Debug (`build-debug`) and Release (`build`) remain build destinations.

| State or buffer | Sole owner and rule |
| --- | --- |
| Capture DMA ring/descriptors | Board capture implementation; DMA produces, foreground consumes through transport; never process a DMA-writable block |
| Capture/output working buffers | Audio pipeline; one foreground hop; output is private until the entire hop succeeds |
| Analysis history/window | Feature state; 256 samples with 240 retained; reset only with stream stopped |
| Feature context and encoded h/c | Model adapter; `[previous,current]`; exactly one feedback commit per successful invoke; no coordinate transform in firmware |
| Interpreter/arena/tensor memory | Inference service; initialize once before audio; adapter may borrow only validated stable storage with explicit lifetime |
| FIR history/taps/crossfade | DSP state within the adapter; one update per input hop, old/new FIR both use the current block |
| Reconstruction scratch | Reconstruction implementation; fixed-capacity, private to the single foreground context |
| Playback DMA ring/descriptors | DAC driver; publish only complete packed blocks, preserve barriers and DMA ownership |
| Timing/fault records | Existing pipeline/service diagnostic state; bounded storage, no formatting in ISR or measured processing stages |

Keep mono PCM16 at 16 kHz, 16 samples/call, normalization by 32768, startup
zero-padding, symmetric Hamming/255, FFT convention and feature formula. Keep
the three-input/four-output INT16 ABI, qparams, port semantics, Q31 feedback
with int64/ties-away/saturation and graph-encoded h/c in model order. Compiled
SHA-256 remains `69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27`.

Keep gain/phase65 in radians, endpoint real components, Hermitian completion,
positive-exponent IFFT128 scaled by 1/128, roll +64, contract taper, 127-sample
FIR history, midpoint-Hann16 crossfade and saturating ties-away PCM conversion.
Reset taps to delta at index 64. Do not introduce AGC, tap normalization,
minimum-phase conversion, STFT/OLA or delayed application of current taps.

## Proposed changes and decision gates

### 1. Establish comparable timing without perturbing the hop

Reuse existing timing structures, cycle source, Release probe and debugger
snapshots rather than create another telemetry framework. Record raw cycle
durations for frontend, input binding/copy, Invoke/NPU/cache overhead, feedback,
reconstruction, FIR/crossfade, PCM and capture-to-publish. Preserve sequence,
capture/process/publish timestamps, scheduled playback slot and fault counters.
Use existing fields where sufficient; add only missing attribution needed for
an actual optimization decision. Do not count overlapping NPU/Invoke intervals
twice. Record time spent waiting for foreground dispatch as part of the deadline.

Replace formatted first-hop stage messages with bounded stage identifiers/raw
timestamps visible to the debugger and reported after stopping or a fault.
Keep useful initialization logs. Never wait for UART space in a hop. Ordinary
Release keeps UART disabled; an opt-in measurement build uses an existing
profile and retains its own configuration/image hash before ordinary builds
are restored. Quantify probe overhead and compare like-for-like compiler flags,
clock/cache configuration and instrumentation.

### 2. Remove redundant work before changing arithmetic

Trace every caller and fixture before changing interfaces. Prefer converting
PCM16 once into an existing fixed buffer and sharing that immutable hop with
frontend and FIR. Remove redundant tap/context copies and zero-initialization
only after proving all values are initialized before use on scalar and MVE
paths, reset and failure paths. Keep required history movement and atomic output
publication. Do not replace small safe copies with a complicated queue/ring.

Evaluate direct writes into validated interpreter input storage after allocation
only if tensor lifetimes, aliasing and backend input reuse are proven safe.
Retain the current bridge if the saving does not justify the coupling. Do not
borrow output storage as state or reuse scratch across Invoke without proof.
Recurrent arithmetic and integer binding remain exact.

### 3. Evaluate faster DSP kernels within existing numerical limits

First examine generated code for quantization, reconstruction and FIR. Evaluate
constant preparation, less repeated arithmetic and vectorization before adding
tables. Any table must have a measured cost/benefit and recorded ROM/RAM cost;
do not add a large LUT or build-time generator speculatively.

Consider real FFT256/IFFT128 and shared/fast trigonometry only where the installed
CMSIS version supports the required target implementation. Validate DC/Nyquist
packing, sign, normalization, ordering and phase units independently. In this
checkout `arm_sin_cos_f32` takes degrees while the model phase is radians; it is
not a drop-in replacement for `sinf`/`cosf`. A kernel failing existing tolerances
is rejected, not accepted by editing expectations. Preserve finiteness checks;
do not enable global `-ffast-math`/finite-only assumptions.

### 4. Review handoff, cache and idle behavior

Keep DMIC on LPPDMA channel 0 and I2S1 playback on PDMA0 channel 0. Inspect actual
cache ranges, immutable registration before AllocateTensors, alignment and
barriers. Change project-owned hooks only with a demonstrated redundant action
or a safe bounded range; no blind removal of clean/invalidate operations.

For a measured critical-section bottleneck, prepare data outside the masked
region only in CPU-owned storage and publish atomically after rechecking DMA
state. Preserve overrun/pending-transfer detection, synchronization and no
sample loss. Keep continuous capture/Invoke/playback and existing event-driven
idle/wait behavior. Do not duty-cycle inference, lower clocks blindly, migrate
I2S to unsupported LPPDMA or claim lower power from fewer CPU operations alone.

The current custom pin assignment is DMIC CLK PA1/DAT PA2; I2S1 DO PA8, MCLK
PA10, BCLK PA11, LRCK PB0; codec I2C0 SDA PA4/SCL PA5. Preserve these assignments
and SWD PF0/PF1. Before any later clock/pin/DMA code edit, state the affected
assignment and recovery again. CH549 has no physical nRESET; recovery uses the
user's existing physical reset/power access, not an assumed probe reset signal.

## Failure, scheduling and diagnostic behavior

Capture block b in slot b, process in b+1 and play in b+2; first two slots are
silence. Preserve sequence/sample positions, common RX/TX rate configuration
and the existing no-extra-queue policy. Optimizing a stage does not create a
2 ms compute budget or prove physical microphone-to-output latency.

On FFT/Invoke/non-finite data, missing/duplicate sequence, overrun, underrun or
deadline failure: retain counters and failure-stage evidence; never publish a
partial hop; stop/mute, flush stale work and perform a full synchronized reset
before restarting with two silence slots. No retry on partially updated state.
Debug may fail the same protection because it is slower; its acceptance is
diagnosability, not continued invalid audio or a relaxed schedule.

The intermittent `board_status=5` investigation stays separate. An initialization
failure blocks device timing and is not evidence of a DSP performance regression.

## Verification and acceptance

Before each arithmetic experiment, record the baseline, vectors and acceptance
limits. Existing limits are ceilings, not permission to weaken a stricter check:

- Binding, qparams, port order, Q31 feedback, reset/context carry and controller
  replay: exact integer results. For fixed canonical features, gain, phase and
  next-input states require zero mismatches across both traces (26,441 hops).
- Copy/ownership-only changes: bit-identical results to the pre-change path on
  equivalent builds, including state/history and failure publication behavior.
- Frontend: existing maximum one INT16 code from the double reference. Report
  changed-code count, max error and first difference; add half-code, near-zero,
  clipping and FFT endpoint cases. Do not silently compound a prior error budget.
- Reconstruction: existing `atol=2e-5`, `rtol=2e-5` against independent reference.
  FIR MVE/scalar: existing `abs(error) <= 2e-5 + 2e-6*abs(reference)`; retain all
  tighter existing assertions. PCM rounding, saturation and no-wrap remain exact
  for fixed float inputs. Report output PCM differences from any arithmetic
  change separately; local float tolerance is not proof of equal audio quality.
- Execute actual C/C++ numeric harnesses, including CMSIS scalar host FFT, on
  silence, impulses, DC/Nyquist, tones, random sequences, PCM extremes, state
  saturation, reset/restart and injected failures. Include long carry/history
  sequences and canary/alias checks for changed buffer interfaces. Host tests do
  not execute Cortex-M55 MVE; retain target self-tests and scoped linked-kernel
  disassembly checks, including vector padding/alignment where applicable.
- Run schedule/ownership/recovery, architecture, linkage, low-power and isolated
  diagnostic-target regressions; then the complete unittest suite and diff check.
  Build Debug and Release sequentially using existing scripts. Inspect image
  identity, actual arena use when executed, map totals, fixed buffers, scratch,
  alignment and stack estimates; do not call a static estimate measured high-water.

Keep source-package goldens immutable. Reuse the existing replay verifier and
runner. A raw receipt from the package is historical evidence; claim a rerun
only if executed in its specified environment. If no authorized compiled backend
is available, validate the runner/comparator independently and mark compiled
execution BLOCKED, never substitute a mock Invoke for device parity.

Proposed device acceptance, after separate permission and hardware access: run
the same configured Release baseline/candidate for at least 60,000 consecutive
hops (60 s), including cold-start and synchronized recovery observations. Require
zero deadline misses, overruns, underruns, sequence faults and restarts during
the continuous acceptance run. Record maximum capture-to-publish time, minimum
remaining margin to 1,000 us, per-stage timing and IRQ effects. The hard limit
is strictly below 1,000 us; a proposed engineering target is at most 900 us to
retain 100 us margin. Report an unmet margin separately rather than hide it.
Retain startup failures and do not select only successful portions of a run.

Measure physical continuity, packing, clipping and latency separately when
equipment permits. Measure current/energy with the same audio/load/supply and
measurement method before claiming a power reduction. Without the board or
instruments these rows remain NOT_RUN/BLOCKED; proceed with independent work.

## Scope, evidence and handoff

Expected source surface: `services/deepfir/`, `services/audio_pipeline/`, related
public interfaces/tests; project-owned transport/DAC/Ethos-U/board code only for
demonstrated costs; CMake source/flags only if required by a retained kernel.
`app/` keeps boot/event-loop responsibility. Read relevant callers/fixtures and
`docs/c_style_guide.md`; use Doxygen for changed public contracts and inline
comments for non-obvious rounding, lifetime and synchronization decisions.

Reuse existing tools; add no generic benchmark framework. Store new commands,
versions, exit codes, hashes, numeric comparisons, disassembly, build/map reports
and any device records under fresh timestamped evidence. Update the requested
progress/session handoff with verified outcomes only. Keep unrelated changes,
source packages and old evidence; do not commit generated build/runtime files.

The final acceptance table separately reports artifact integrity, architecture,
host numeric correctness, Debug/Release build, raw replay, compiled replay,
board initialization/memory, full-hop schedule/deadline and physical audio/latency.
Add power as a separate measurement row. Use PASS/FAIL/NOT_RUN/BLOCKED with links.
An optimization can be source-integrated and host-verified while device speed,
parity or power remains unverified; deployment approval is not changed.

## Builder self-review

No subagents are used, as requested. The self-review separates changes that can
be proven on the host from those requiring MVE/DMA/device evidence. Proposed
60 s observation length and 100 us margin are engineering choices for this
design, not previously approved user requirements. Kernel selection, scratch
placement and small interface details are implementation decisions within the
stated gates; do not ask the user to choose individual memcpy or CMSIS APIs.
If a candidate fails a gate, retain the proven path and report the rejected
candidate. Missing device evidence limits the claim, not the independent work.
