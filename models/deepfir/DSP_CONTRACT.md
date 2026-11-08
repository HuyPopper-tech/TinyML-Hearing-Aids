# R2 DSP and Streaming Contract — Revision 1

This is the normative integration specification. Read it together with
`dsp_contract.json`, `deepfir_r2_contract.h` and `firmware_manifest.json`.
Timing values below are implementation requirements, not board measurements.

## 1. PCM interface and responsibilities

- Signal path: MP34DT05 PDM → board decimation → PCM → R2 adapter → I2S → TLV320DAC3100.
- Signed PCM16, little-endian byte stream, mono, 16,000 samples/s. Each adapter
  call receives and produces `int16_t[16]` in increasing sample order.
- Convert input with `x = pcm / 32768`. Do not use 32767, utterance RMS
  normalization, unvalidated AGC or additional gain.
- The board owns PDM decimation, clocks, DMA, cache coherency, I2S slot/word
  packing and DAC routing. Wider I2S words or stereo slots must preserve the
  values and ordering of the mono PCM samples at the adapter boundary.
- The adapter owns analysis/audio history, features, h/c feedback, invocation,
  FIR reconstruction, crossfade and PCM conversion. It does not control DMA.
- Use one context and one executor per stream. Input and output buffers must
  be separate. Keep input immutable during the call; publish output only after success.

## 2. Analysis framing and features

For H=16, block b is `x[16*b : 16*b+16]`. Invoke only after it is complete.
Its analysis frame is `x[16*b-240 : 16*b+16]`; negative indices are zero at startup.
Advance by exactly 16 samples. Do not wait for 256 real samples, read block b+1,
or add a separate 16 ms analysis buffer.

Apply the symmetric Hamming window:

```text
w[n] = 0.54 - 0.46*cos(2*pi*n/255), n=0..255
```

Use the unnormalized forward FFT256, negative exponent, bins 0..128 including
Nyquist. The reference CMSIS wrapper calls
`arm_cfft_f32(..., ifftFlag=0, bitReverseFlag=1)` on 512 interleaved floats.
Changing FFT scaling or ordering requires fresh parity checks.

For R=real(X), I=imag(X), q=R*R+I*I:

```text
F[k,0] = sqrt(q)^0.3
F[k,1] = R*(q+1e-16)^(-0.35)
F[k,2] = I*(q+1e-16)^(-0.35)
```

Input context shape is `[1,2,129,3]`, ordered `[previous,current]`; channels
are magnitude, real, imaginary. Previous features start at zero and are
updated after successful invocation. Do not duplicate the current frame.

## 3. Tensor binding, quantization and state

- Semantic inputs: `feature_context,h,c`. Outputs: `gain,phase,h_next,c_next`.
  Bind by the generated actual ports; do not assume historical indices or name order.
- INT16 values represent `(code-zero_point)*scale`. Quantization uses nearest
  rounding with ties away from zero, then saturation to [-32768,32767].
- h and c each have 200 channels. Reset real zero to the input zero-point.
- Direct state copy is allowed only when input/output scale and zero-point
  match. Otherwise use the generated multiplier/shift Q31 feedback with int64
  intermediates, symmetric ties-away rounding and INT16 saturation.
- Candidate C uses ABI `deepfir_ablation_r2_ac_v1_gain_phase_split_range_hc_v2`.
  Both h and c carry the same permuted channel order; only c is radius-scaled.
  The exact mapping and variable group sizes are in
  `firmware_manifest.json:state_coordinates`. The graph already implements
  the transformation: do not decode, permute or scale it again in firmware.
  For analysis only: `h_physical[p[i]]=h_encoded[i]` and
  `c_physical[p[i]]=radius[p[i]]*c_encoded[i]`. The radii array uses original
  channel order. Old c-only state buffers and goldens are incompatible.
- INT64 tensors in the raw model are constant biases, checked to fit the U55
  signed 40-bit bias encoding; they are not INT64 activation arithmetic.
  INT8 is used for weights and INT16 for activations and external state.
  Q31 feedback uses CPU INT64 intermediates outside the accelerated graph.
  One NPU subgraph and zero CPU operators describe the controller, not DSP.
- Commit the current hop's state before processing the next hop. Never invoke
  concurrent hops on one context or reset states at ordinary DMA boundaries.

## 4. FIR reconstruction and synthesis

Dequantize 65 gain bins and phase in radians using this model's qparams.
Form `B[k]=gain[k]*exp(j*phase[k])`, k=0..64. Force the imaginary parts of
bins 0 and 64 to zero while retaining their real parts. Complete the spectrum
by Hermitian symmetry. Use IDFT128 with positive exponent and factor 1/128,
circular roll +64, then apply:

```text
taper[k] = 0.5 + 0.5*(1-abs(k-64)/64), k=0..127
b_current[k] = roll(IDFT128(B),64)[k] * taper[k]
```

This is a tap taper, distinct from the analysis Hamming window. Do not apply
either window again to output PCM. Do not add minimum-phase conversion,
DC normalization, tap clipping or an additional gain stage.

For each sample i=0..15 in the SAME current block b, insert the current
input sample into audio history, then calculate:

```text
old[i] = sum(k=0..127, b_previous[k]*x[16*b+i-k])
new[i] = sum(k=0..127, b_current[k]*x[16*b+i-k])
alpha[i] = 0.5 - 0.5*cos(pi*(i+0.5)/16)
y[16*b+i] = (1-alpha[i])*old[i] + alpha[i]*new[i]
```

Keep 127 preceding audio samples and the previous FIR across hops. Both
branches use the same history. After the hop, set previous FIR to current FIR.
Do not apply the current FIR to block b+1 instead, add STFT/OLA synthesis,
or introduce a 32-sample future window. Midpoint-Hann crossfade is the R2
project convention used consistently in training, replay and C.

Convert output with `round_ties_away(y*32768)` and saturate to [-32768,32767];
never wrap INT16.

## 5. Required capture/process/play schedule

Let t0 be the PCM stream boundary and H=1 ms. Sequence b increases continuously.

| Time | Capture | Process | Play |
|---|---|---|---|
| 0–1 ms | A=b0 | No complete block | Silence |
| 1–2 ms | B=b1 | A | Silence |
| 2–3 ms | C=b2 | B | A |
| 3–4 ms | D=b3 | C | B |

Capture b during `[t0+bH,t0+(b+1)H)`, process it after capture completes,
and start playback at `t0+(b+2)H`. Finish at `t0+(b+3)H`. Sample position i
must remain i in its playback slot. This is a 32-sample/2 ms scheduling shift,
excluding FIR response and additional codec/transport delay. Add no extra queue hop.

DMA must not overwrite input being processed or read output being modified.
ISRs record sequence/timestamps and wake the executor; inference must not block
capture/play interrupts. RX/TX clocks must have verified rate synchronization;
do not silently drop/repeat samples to hide drift. Larger DMA buffers require
16-sample sub-block delivery; whole-buffer callbacks add latency and no longer
satisfy this 2 ms schedule.

The strict deadline is the ENTIRE frontend, quantization, inference, feedback,
tap reconstruction, two FIRs/crossfade, PCM conversion, publish/cache/DMA
overhead and interrupt jitter under 1 ms, with measured margin. Vela estimates
only the model. Compute occupies slot b+1 and is not added again to the 2 ms
schedule when the deadline is met.

## 6. Startup, failures and reset

Before capture/playback, clear buffers and call `deepfir_r2_reset`. Zero analysis
and audio histories, previous features and recurrent states. Both FIRs start
as a delta at index 64. Play silence in the first two slots. Do not discard
additional samples to compensate for startup or retain an earlier stream's state.

Adapter/invoke/FFT failures, NaNs, missing/repeated sequences, RX overrun,
TX underrun and missed deadlines break continuity. Count/log the fault, mute,
stop publishing the partial context, flush old jobs/buffers and restart at a
synchronized hop boundary with a full reset and two silence slots. Do not retry
on partially updated state, repeat stale output or hide a late queue as 2 ms latency.
For finite files only, zero-pad the final partial hop and trim to the true length.
Metric alignment/cropping is not a real-time output operation.

## 7. Latency interpretation and measurements

```text
L_schedule = L_synthesis_block + L_hop_pipeline = 1 ms + 1 ms = 2 ms
L_estimated = 2 ms + L_FIR_group_delay + L_codec_transport_extra
```

Avoid counting hardware buffering already included in the PCM schedule twice.
The 64-sample target alignment and roll64 are not instructions to add a FIFO,
nor proof of a constant 4 ms group delay. The learned FIR is time-varying;
measure signal latency and, where useful, frequency-dependent group-delay
distributions. Do not infer 8 ms latency from 128 taps or reuse another board's measurements.

Record sequence, sample index, capture-complete, process-start/end, TX-publish,
DMA/DAC-play-start, overrun/underrun counts and worst-case runtime. Check
`playback_start(b)-capture_start(b)=2 ms` in the slot/sample domain. Time from
capture-complete to enqueue is not acoustic microphone-to-output latency.
Use tagged impulse/loopback through the real microphone/DAC path. Separate
requirements, compiler estimates, processing measurements and acoustic measurements.

## 8. Acceptance and evidence boundaries

Verify model hashes, ABI and tensor binding before loading. Verify host C
framing, features, taps, crossfade, reset and PCM16; then raw integer replay
under the recorded runtime, compiled Ethos-U replay, and physical-board behavior.
Check startup slots, ownership, sequence faults and deadline recovery. Host
checks do not establish board readiness. Keep `deployment_approved=false`
until deployment evidence is complete.

The analysis/FIR/crossfade foundation and latency decomposition are described
in [Deep FIR](https://arxiv.org/html/2409.18239v2). Complex3, gain/phase/taper,
midpoint crossfade and this firmware schedule are project-specific R2 choices.
