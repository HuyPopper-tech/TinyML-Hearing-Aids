# NeuralAid U55 Equivalent — Board Firmware Test Handoff

**Run:** `neuralaid_u55_equivalent_seed20260823`
**Track:** `u55_equivalent`
**Updated:** 2026-08-23
**Purpose:** hand off the concrete work needed to run a controlled NeuralAid firmware bring-up on the M55M1 + Ethos-U55 board.

## 1. Current status and decision

The exported U55 model is structurally usable for a firmware bring-up, but it is **not a release candidate** and cannot yet be used for an end-to-end listening/quality test.

### Verified host/export facts

| Check | Result |
|---|---|
| Raw INT8 conversion | PASS |
| Vela U55-256 conversion | PASS |
| CPU operators | 0 (all 3,636 operators assigned to NPU) |
| Vela Performance estimate | 61.41 KiB SRAM; 688.89 KiB off-chip Flash |
| Vela Size estimate | 57.92 KiB SRAM; 688.91 KiB off-chip Flash |
| Streaming DSP identity | PASS; max error `2.9802322387695312e-08` |
| Release gates | FAIL: functional parity, SRAM, Flash, runtime deadline |

### Quality/training status

The `p100_8h_final` run stopped by deadline before convergence:

| Phase | Completed/target steps | SI-SDRi on quick test (128/3,750) |
|---|---:|---:|
| FP32 | 91/900 | -6.328 dB |
| QAT / deployed INT8 | 4/300 | -11.081 dB |

The current output degrades the noisy input on average. Treat this model only as an **integration and numeric-parity artifact**. Do not use it to make audio-quality, hearing-aid, latency, or deployment-ready claims.

## 2. Existing artifacts

All paths below are relative to this directory.

```text
integration_package/
  neuralaid_int8_raw.tflite       # host numeric oracle
  neuralaid_int8_vela.tflite      # model to embed/run on U55
  model_data.cc / model_data.h     # embedded Vela model bytes
  neuralaid_contract.h             # fixed DSP/model dimensions
  tensor_contract.json             # TFLite tensor index/name/shape/quant params
  state_quant_contract.json        # input->output state mapping and requant policy
  vela_report_final_*.txt          # Vela resource/operator reports
  release_gates.json               # current release status
  window_coeffs.c / .h             # window coefficients
  golden/                          # host test vectors and stage traces
```

Model SHA-256 from `integration_manifest.json`:

```text
eede0f7fae56e7bf11e75d7008e0c619e6884ffd2dc93857fd4e4aa0a1478375
```

## 3. Critical blocker in the current C package

`integration_package/neuralaid_pipeline.c` is a scaffold. Its `process_one_chunk()` contains a TODO for RFFT, TFLM/Ethos-U invocation, state transfer, iRFFT, and OLA; it currently enqueues 96 zero samples.

Therefore, **the supplied pipeline must not be connected to the speaker path as an enhanced-audio implementation.** It can only serve as a dimension/API skeleton.

## 4. Required implementation on the board

### 4.1 Audio/DSP contract

- Input/output: mono 16 kHz signed PCM16.
- Chunk: 96 samples (6 ms).
- Lookback: 96 samples; lookahead: 64 samples.
- FFT: 256; complex tensor: `[129, 2]` (real, imaginary).
- Pre-emphasis: `0.97` (`NEURALAID_PREEMPHASIS_Q15=31785`).
- Reset stream state on boot, source switch, or confirmed stream discontinuity.

Implement and unit-test these stages:

1. PCM16 normalization according to one documented float/Q15 convention.
2. Pre-emphasis and 256-sample ring/context construction.
3. CMSIS-DSP RFFT adapter: CMSIS packed spectrum <-> TensorFlow `[129,2]` layout.
4. Quantization of `tf_frame` using the exact input scale/zero-point.
5. Model invocation and dequantization of enhanced TF output.
6. iRFFT, synthesis window and overlap-add; release exactly 96 valid output samples per chunk.

### 4.2 TFLM/Ethos-U inference bridge

Implement in a `.cc` bridge, with a C API consumed by the DSP/audio layer:

1. Embed only `neuralaid_int8_vela.tflite` (`model_data.cc`).
2. Build the precise TFLM op resolver and register the BSP Ethos-U delegate/custom operation.
3. Create `MicroInterpreter`, allocate the real tensor arena, and record allocation result/bytes.
4. Resolve every I/O tensor by the saved **index and contract mapping**, not by an assumed output order or `Identity_*` names.
5. Write `tf_frame` plus 14 state inputs; invoke; retrieve enhanced TF plus 14 next-state outputs.
6. Report invoke errors and use an explicit audio bypass policy on failure.

### 4.3 Persistent-state handling

The model has 14 persistent states:

```text
conv_history, dual_path_history,
lstm_h_0..lstm_h_5,
lstm_c_0..lstm_c_5
```

Use `state_quant_contract.json` exactly.

- Raw copy permitted: `conv_history`, `dual_path_history`, `lstm_h_3`, `lstm_c_0`.
- All remaining LSTM state pairs must use saturating requantization:

```text
real  = (q_out - zp_out) * scale_out
q_in  = clamp(round(real / scale_in) + zp_in, -128, 127)
```

- Retain state across adjacent chunks; do not reset every invocation.
- Make the stream state per audio stream, not global across concurrent streams.

## 5. Test sequence and acceptance criteria

### Stage A — firmware bring-up (allowed now)

1. Verify embedded-model SHA-256 against the value above.
2. Confirm model version, Ethos-U registration and `AllocateTensors()` succeed.
3. Log each I/O tensor index, shape, dtype, scale and zero-point; compare with `tensor_contract.json`.
4. Invoke one raw-TFLite golden frame with zero state and compare int8 enhanced-TF output and every state output.

**Pass:** model loads, all contract fields agree, invocation succeeds, and no tensor mapping is ambiguous.

### Stage B — DSP and model streaming parity

For each golden scenario (`silence`, `impulse`, `sine_440`, `sine_3000`, `noise`):

1. Run at least 100 contiguous chunks without resetting state.
2. Compare stage traces: pre-emphasis, packed/unpacked RFFT, TF input int8, enhanced TF int8, each state output, synthesis/OLA, final PCM.
3. Record maximum absolute error, RMSE, saturation count and first failing chunk/tensor.
4. Repeat after a deliberate reset and with two alternating streams.

**Pass:** output/state values are within an explicit, stage-specific tolerance against raw-TFLite golden; no unexplained saturation, state drift, click, or missing chunk.

### Stage C — board resource/deadline measurement

Measure separately for each 6 ms chunk:

| Block | Required measurement |
|---|---|
| Ring/context + pre-emphasis | cycles/us |
| RFFT and tensor packing | cycles/us |
| Quantization + state transfer | cycles/us |
| Ethos-U/TFLM invoke | cycles/us |
| Dequantization + iRFFT + OLA | cycles/us |
| Total microphone-to-output pipeline | cycles/us, P50/P95/P99 |

Also record tensor-arena bytes, DSP buffers, state storage, stack high-water mark, `.text/.rodata/model` Flash bytes, DMA overruns, invoke failures, and output clipping count from the linker map and runtime.

**Target:** total P99 less than 6 ms with scheduler margin. Vela estimates are not board measurements.

### Stage D — quality test (blocked)

Run listening tests or report SI-SDRi/PESQ/STOI only after:

1. FP32 has a meaningful positive quality result versus noisy input.
2. QAT has been trained from that FP32 checkpoint for a meaningful schedule.
3. FP32/raw-TFLite/board streaming parity passes.
4. Resource and deadline gates pass.

## 6. Required firmware test log

For each board build, save:

```text
git commit / BSP version / TFLM version / Ethos-U driver version
model SHA-256 / Vela config SHA-256 / compiler flags
tensor arena + stack + Flash linker-map measurements
per-stage cycles and P50/P95/P99 total latency
golden comparison report, saturation and reset results
DMA/SD/audio overruns, invoke failures and bypass activations
```

## 7. Exit criteria for this handoff

Firmware integration is complete only when stages A-C pass. The model becomes a candidate for board audio quality testing only after Stage D prerequisites pass. Current status: **Stage A implementation pending; Stage B-C-D blocked.**
