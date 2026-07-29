# DMIC Input-Gain Diagnostic Design

## Goal

Determine whether low hearing-aid output originates at the DMIC capture level
or in the ERB-GRU frontend/model path, without changing the existing timing
telemetry or final DAC gain.

## Design

The foreground audio loop will apply a fixed +12 dB digital test gain (4x) to
each 160-sample DMIC PCM block immediately before
`erb_gru_audio_enhancer_process`. The original capture block is retained only
for diagnostics; the model receives the gain-adjusted block and the existing
enhanced output continues to feed DAC DMA.

The saturating gain operation uses signed 32-bit intermediate arithmetic and
clamps each output sample to the int16 range. It has no dynamic allocation and
runs only in foreground context.

## Observability

The existing 100-frame `[PERF]` line remains unchanged for latency fields. It
will gain a compact capture diagnostic suffix:

```text
in_pk=<raw peak> in_rms=<raw RMS> gain_pk=<post-gain peak>
gain_rms=<post-gain RMS> gain_clip=<saturated samples> out_pk=<model peak>
```

`gain_clip` is reset each 100-frame window. Any non-zero value means the +12
dB experiment is not a faithful input-scale test for that window.

## Interpretation

- Raw peak/RMS stay low while post-gain values rise without clipping, and
  model output becomes audible: DMIC level/input scale is the limiting factor.
- Raw and post-gain levels are healthy but model output remains roughly 24 dB
  lower: investigate frontend normalization, predicted gain expansion, and
  WOLA synthesis scaling.
- Gain clipping is non-zero: repeat with a lower test gain before attributing
  behavior to the model.

## Constraints

- Preserve the current 16 kHz, 160-sample hop and all SysTick timing logic.
- Do not log from DMA/NPU ISR context.
- Do not change model artifacts, model quantization, or DAC output gain.
- The test gain is a temporary diagnostic setting, clearly printed at boot.

## Verification

Source-contract tests cover the fixed +12 dB constant, saturating multiply,
RMS/peak/clip fields, foreground placement before inference, and retention of
the existing `[PERF]` timing fields. Build and board verification require an
active Arm Compiler license.
