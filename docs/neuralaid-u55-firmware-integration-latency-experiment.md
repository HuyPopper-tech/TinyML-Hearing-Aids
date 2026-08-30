# NeuralAid U55 Firmware Integration and Latency Experiment Record

**Date:** 2026-08-24
**Target:** Nuvoton M55M1 + Arm Ethos-U55-256 + DMIC + TLV320DAC3100
**Experiment:** `neuralaid_u55_equivalent_seed20260823`
**Status:** integration/latency experiment only; not quality- or release-ready.

## 1. Purpose and scope

This record captures the NeuralAid U55 firmware integration, the measured
runtime behavior, and the architectural conclusions relevant to future
low-latency model experiments. It distinguishes verified facts from proposed
next steps. It does not claim that the current model improves speech quality.

The source export directory remains immutable:

```text
neuralaid_u55_equivalent_seed20260823/
```

Firmware-owned copies and integration code live under:

```text
models/neuralaid/
services/neuralaid/
```

## 2. Export and model facts

| Item | Value / result |
|---|---|
| Sample rate | 16 kHz mono PCM16 |
| Model input/output frame | `[1, 129, 2]` signed INT8 complex spectrum |
| Persistent state inputs | 14 |
| Persistent state outputs | 14 |
| Vela target | Ethos-U55-256 |
| Vela CPU operators | 0 |
| Vela NPU operators | 3,636 |
| Vela Size estimate | 57.92 KiB SRAM, 688.91 KiB off-chip Flash |
| Vela Performance estimate | 61.41 KiB SRAM, 688.89 KiB off-chip Flash |
| Vela predicted inference time | 5.5928 ms at 220 MHz in the exported system configuration |
| Vela predicted total cycles | 1,230,420 |
| Firmware embedded Vela model SHA-256 | `074d441e0b56c37695a45a2aaeb5e87c057fce4777c47c5d822f8cab023d49ff` |

The firmware artifact is byte-identical to
`integration_package/neuralaid_int8_vela.tflite` and to the Vela **Size**
variant. The separately generated Vela Performance variant has a different
SHA-256. Its exported summary reports the same predicted inference time, but
it has not yet been board-benchmarked in this firmware.

### Training and quality status

The supplied handoff explicitly marks this artifact as an integration/parity
artifact. Training was deadline-limited:

| Phase | Completed/target steps | Quick SI-SDRi |
|---|---:|---:|
| FP32 | 91/900 | -6.328 dB |
| QAT/deployed INT8 | 4/300 | -11.081 dB |

The model therefore may degrade noisy speech. Latency work must not be read as
an audio-quality validation.

## 3. Intended audio and state contract

The handoff contract specifies:

```text
input/output hop: 96 samples = 6 ms at 16 kHz
lookback:         96 samples
lookahead:        64 samples
FFT length:       256
pre-emphasis:     0.97, Q15 coefficient 31785
```

Each invocation consumes the current `[129, 2]` spectral frame and 14 state
tensors, then returns one enhanced frame and 14 next-state tensors. The state
contract permits raw copy only for:

```text
conv_history
dual_path_history
lstm_h_3
lstm_c_0
```

All other state pairs require saturating requantization:

```text
real = (q_out - zero_point_out) * scale_out
q_in = clamp(round(real / scale_in) + zero_point_in, -128, 127)
```

State is valid only across contiguous chunks. A confirmed discontinuity must
reset it at a frame boundary.

## 4. Firmware integration performed

### 4.1 Build and ownership

- Added `M55M1_ENABLE_NEURALAID`; it is the default model selection.
- Disabled simultaneous DeepFilterNet/NPU model ownership.
- Added `M55M1_NEURALAID_DIAGNOSTICS` for UART1 diagnostics.
- Copied firmware-owned model bytes, contracts, golden assets, manifests, and
  Vela artifact metadata into `models/neuralaid/`.
- Preserved the export source directory without editing it.

### 4.2 Runtime path

The current connected path is:

```text
DMIC LPPDMA
  -> two capture slots / latest-ready flag
  -> audio_pipeline foreground loop
  -> NeuralAid pre-emphasis + RFFT + INT8 quantization
  -> TFLM Ethos-U custom operator
  -> state transfer/requantization + iRFFT
  -> I2S/PDMA -> TLV320DAC3100
```

The audio transport, board DMIC profile, codec profile, and application buffers
were changed to 96 samples at 16 kHz.

### 4.3 TFLM/Ethos-U bridge

`services/neuralaid/neuralaid_inference.cpp` currently:

1. verifies the firmware model SHA-256 and TFLite FlatBuffer;
2. checks one subgraph and exactly 15 inputs plus 15 outputs;
3. registers only `AddEthosU()` in the MicroMutableOpResolver;
4. rejects a FlatBuffer that contains a non-`ethos-u` operator;
5. allocates a 128 KiB static TFLM arena;
6. copies model state inputs, invokes the custom operator, and retrieves output
   state using the saved logical mapping.

The runtime logs `cpuop=0 ethosop=1`. Together with matching command,
completion, and NPU IRQ counts, this is evidence that the deployed model does
not silently execute a TFLM CPU fallback.

### 4.4 DSP caveat: parity has not passed

The current DSP service is a bring-up implementation, not a completed parity
implementation. In particular, source inspection shows it currently uses a
160-sample lookahead and returns samples from `iRFFT[160..255]`; the handoff
requires a 96-sample lookback, a 64-sample lookahead, a synthesis window, and
overlap-add. The current service does not yet implement the required window/OLA
path.

Consequences:

- output audio must not be used for quality conclusions;
- changing the transport block directly from 96 to 256 would overrun the
  current 256-sample iRFFT output selection and violate the trained streaming
  contract;
- full host-to-board stage parity remains required before any listening claim.

## 5. Capture-buffer behavior and the deadline failure

At 16 kHz, the 96-sample input hop creates a new capture block every 6 ms:

```text
96 / 16,000 = 0.006 seconds
```

The DMIC driver has two DMA slots, but foreground availability is represented
by a single `block_ready` flag and the newest completed slot. If a new DMA block
finishes while `block_ready` is still set, the ISR increments `overrun_blocks`
and overwrites the foreground-visible completion with the latest block. This
is a deliberately bounded latest-block design, not a deep FIFO.

The current model path needs more than 6 ms. Sustained processing therefore
has lower throughput than DMIC capture:

```text
capture rate: 1 block / 6 ms
model path:   approximately 1 real model frame / 10.65 ms after optimization
```

The UART trace consequently reports deterministic discontinuities such as:

```text
ERR capture_gap expected=4 got=5
ERR capture_gap expected=8 got=9
```

On each discontinuity, the pipeline resets NeuralAid stream state. The next
model result therefore follows a reset rather than the required contiguous
state sequence. This repeated reset is the direct firmware cause of clicks,
rè, and invalid streaming behavior. It is not an invoke failure; the traces
show `bypass=0`.

Increasing a queue can absorb temporary jitter, but cannot solve a permanent
throughput deficit. A larger queue would eventually fill and add latency.

## 6. Board measurements

### 6.1 Before state-requantization optimization

Release trace before the lookup optimization:

```text
total approximately 14.14 to 14.35 ms
pre-emphasis ~4 us
RFFT ~22 to 23 us
quantization ~24 us
combined inference wrapper ~14.05 ms
iRFFT/output ~29 to 30 us
```

NPU PMU cumulative cycles measured about 1.728 million cycles per completed
Ethos-U command. If the PMU counter is interpreted at 220 MHz, that is about
7.85 ms of accelerator command execution. The remaining observed wrapper time
included state copies, float requantization, TFLM/Ethos-U dispatch/cache work,
and timing granularity. This was an estimate; the stages were not yet split.

### 6.2 State lookup optimization

The per-frame float requantization for the ten non-raw state mappings was
replaced with an INT8 lookup table:

- at initialization, all 256 possible signed INT8 output values are evaluated
  with the existing float/rounding contract for each non-raw state mapping;
- during every frame, the mapping is an array lookup;
- the lookup preserves the same source tensor scales, zero points, rounding,
  and saturation rule as the original implementation;
- the table is small static RAM and does not modify model bytes, weights,
  graph, inputs, or outputs.

Release trace after this optimization:

```text
CPU in=169 invoke=9756 out=629  (microseconds)
```

Representative interpretation:

| Stage | Measured time |
|---|---:|
| Copy state inputs and TF frame | ~0.169 ms |
| TFLM/Ethos-U Invoke-only | ~9.75 ms |
| Enhanced output copy plus state output transfer/LUT | ~0.629 ms |
| DSP outer stages | ~0.10 ms |
| Total enhanced frame | ~10.65 ms |

This reduced the measured model wrapper from about 14.25 ms to about 10.65 ms,
or roughly 3.6 ms. It did **not** reduce model NPU work: PMU cycles remained
near 1.727 million cycles per command.

The main new bottleneck is `Invoke-only ~9.75 ms`, still above the 6 ms hop
deadline.

### 6.3 Interpreting inconsistent telemetry rows

Rows such as the following are not valid enhanced-frame timings:

```text
LAT ... total=8 ... npu=10556 ...
```

They occur after a stream reset when the audio enhancer returns early with a
preroll result. `total=8` measures only the current short pre-emphasis path,
while several other global diagnostic fields retain the last enhanced-frame
values. Future telemetry must include `result=enhanced|preroll|bypass` and
avoid mixing timing fields from different result types.

Similarly, the current `e2e` field is capture-completion to foreground
output-ready/submit time. It is not DAC-completion latency. Accurate physical
end-to-end latency requires `audio_transport_submit_tagged()` and
`audio_transport_take_completion()`.

## 7. Why 220 MHz does not solve the current design

The board audio setup configures and checks a 220 MHz core/APLL0 target. The
firmware NPU binding enables and resets `NPU0_MODULE`; it does not program a
separate NPU divider/mux in project code. The existing traces therefore do not
prove that the U55 is running below 220 MHz.

The critical measured result is independent of that ambiguity:

```text
Invoke-only ~9.75 ms > 6.00 ms deadline
```

Even the optimistic Vela estimate of 5.59 ms leaves very little margin for the
required state transfer, DSP, DMA scheduling, and telemetry. The current board
measurement is materially slower than the Vela estimate. Potential causes that
must be measured rather than assumed are:

- model/command-stream placement and actual Flash bandwidth;
- cache maintenance and TFLM arena traffic;
- Vela system/memory configuration mismatch with board reality;
- Ethos-U/TFLM dispatch overhead;
- PMU event-counter configuration, currently reporting `active=0 idle=0
  mac=0` even though command cycle/IRQ evidence is valid.

## 8. No-retraining optimization options

### Safe, contract-preserving work

1. Keep the state lookup optimization and verify every state/output against
   golden vectors.
2. Separate timings for cache clean, input copy, Invoke-only, raw output copy,
   LUT output mapping, and cache invalidate.
3. Add result type to UART summaries and aggregate DMA gaps rather than print a
   line for each event.
4. Configure/read PMU events after the relevant Ethos-U power transition and
   report counter-enable/status fields.
5. Inspect linker map and memory addresses for model command stream, weights,
   arena, and DSP buffers.
6. Benchmark the Vela Performance artifact using its own SHA and the same
   golden vectors.

None of these should change model quality when exact parity passes. Cache work
must be changed only with a demonstrated coherence contract; removing cache
maintenance blindly can create stale or random tensors.

### Limits without changing the model

Eliminating CPU wrapper cost alone is insufficient because the measured
Invoke-only stage already exceeds the 6 ms audio deadline. Deeper buffering
does not alter sustained input/output rates. Reducing model invocation cadence
or filling missing output with zeros/bypass invalidates the recurrent state
contract and produces time-alignment artifacts.

## 9. Architecture choices for future experiments

### Option A: preserve 96-sample hop

Design the new model for a board-measured P99 total below 6 ms; a practical
target is NPU below 4-4.5 ms to leave scheduling margin. Candidate changes:

- fewer recurrent layers;
- hidden width below the current 32 channels;
- fewer frequency steps/bins;
- smaller GRU/temporal-convolution design replacing primitive recurrent
  expansion;
- export and benchmark every candidate with the exact M55M1 U55 memory model.

This is the low-latency hearing-aid direction, but requires retraining, QAT,
Vela conversion, host parity, and board parity.

### Option B: use a longer valid model hop

A model expressly trained/exported for 256 samples has a 16 ms transport
deadline. A current 10.65 ms model path could then avoid continuous capture
loss, at the cost of increased algorithmic/output latency. This requires a
matching DSP contract and golden vectors; changing only the firmware DMA block
is not valid for the present 96-sample model.

### Option C: separate board parity from live listening

Keep the 96-sample contract for deterministic host/board golden playback and
run the model without live-DMIC deadlines. Use a clean PCM bypass transport
for live microphone/DAC hardware validation. This isolates DSP/model parity
from real-time scheduling while new candidate architectures are evaluated.

## 10. Required validation before the next listening claim

1. Verify all 15 inputs/outputs, tensor indices, shapes, types, scales, and
   zero points against the export contract.
2. Run at least 100 contiguous golden chunks for silence, impulse, sine 440,
   sine 3000, and noise without state reset.
3. Compare pre-emphasis, RFFT packing, input INT8, output INT8, all 14 states,
   iRFFT/OLA, and PCM with explicit tolerances and first mismatch reporting.
4. Repeat after an explicit reset and with alternating stream contexts.
5. Report P50/P95/P99 for all stages, DMA overruns, invoke failures, bypasses,
   saturation, arena/static memory, stack, and linker-map Flash.
6. Require no unexplained DMA gap or click before attempting a quality
evaluation.
7. Require a converged training/QAT run with positive quality metrics before
describing the model as a hearing-aid enhancement candidate.

## 11. Current decision record

The state lookup optimization is retained because it preserves the existing
quantization contract by construction and substantially lowers CPU wrapper
cost. The current 96-sample live NeuralAid route remains unsuitable for
continuous listening because Invoke-only latency exceeds the hop deadline and
causes recurring DMA discontinuities/state resets. Further model architecture
experiments should use this record's measured board latency, not parameter
count or Vela CPU-op partition alone, as their primary design constraint.

## 12. Contract-exact DSP revision (2026-08-24)

The notebook is now the DSP oracle. The firmware context is exactly
`[LB|LC|LF]=[96|96|64]`: first frame waits for 160 captured samples, later
frames advance by 96 samples. The former `160` lookahead was wrong; 160 is the
synthesis-frame length after the 96-sample lookback is removed.

The revised firmware performs the CMSIS packed-RFFT adapter with zero
imaginary DC/Nyquist bins, quantizes using the model input tensor
`(0.0135895619, -15)`, and dequantizes the enhanced output using its distinct
output tensor `(0.0123825278, -37)`. It applies the exported
`[1/3 x64 | 1 x32 | 1/3 x64]` window with persistent 64-sample value and
weight OLA tails. Invoke writes to a candidate state; only a successful DSP
completion commits it.

`tools/generate_neuralaid_golden.py` creates 100-chunk raw-TFLite traces for
silence, impulse, 440 Hz, 3000 Hz and seeded noise under
`models/neuralaid/generated/golden/`. The artifacts record context, packed
RFFT, INT8 tensors, state feedback, iRFFT, synthesis and PCM output. UART now
reports logical frame/required samples, capture-to-ready and tagged
capture-to-DAC latency, all stage timings, saturation, NPU/CPU timing,
deadline misses and DMA health.

This revision makes contract mismatch observable; it does not change the
measured model cost. A strict live run remains expected to report deadline
misses and capture gaps while compute exceeds the 6 ms hop.
