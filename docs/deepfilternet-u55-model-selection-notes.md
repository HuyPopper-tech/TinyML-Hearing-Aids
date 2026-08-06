# DeepFilterNet U55: model-selection findings

## Scope

This note records board evidence from the diagnostic build with the deployed
post-Vela model `deepfilternet_u55_full_seed20260805`, MVE DSP mode, 16 kHz
audio, 512-sample frame, 256-sample hop, and the temporary `+12 dB` PCM input
calibration. It is an engineering observation, not a listening-quality or
medical validation claim.

## Observed behavior

The firmware is realtime-stable. Across the captured run, inference completed
without fallback, NPU error, DMIC overrun, DMA error, or deadline miss. The
worst observed enhancer time was about 7.5 ms against the 16 ms gate. The
single reported DAC underrun remained at one from startup and did not grow.

The intermittent loss of distant speech correlates with model output, not an
audio transport failure. The `+12 dB` calibration correctly produced a model
input RMS close to four times the raw input RMS. However, low-level windows
were often strongly attenuated to near silence:

| Example window | Raw RMS | Model-input RMS | Output RMS | Observation |
| --- | ---: | ---: | ---: | --- |
| f=200 | 17 | 71 | 0 | Speech/noise candidate suppressed. |
| f=300 | 11 | 47 | 0 | Speech/noise candidate suppressed. |
| f=7600 | 43 | 173 | 1 | Speech/noise candidate suppressed. |
| f=700 | 85 | 343 | 91 | Audible output. |
| f=1000 | 94 | 378 | 195 | Audible output. |
| f=7700 | 136 | 546 | 379 | Audible output. |

The gain tensor also changes from nearly closed values to high values between
these windows. This explains the audible on/off behavior when a distant voice
is near the model's learned speech/noise decision boundary.

## Remaining model issues

1. The model can apply near-zero spectral gains to low-level distant speech.
   A pure enhancement mask has no residual path, so a mistaken suppression
   becomes an audible dropout.
2. Input level is still a material condition. `+12 dB` improves model-input
   level but cannot create SNR absent at the microphone. Increasing it further
   is not a safe default because diagnostic clipping was observed in transient
   windows.
3. The current training/export package is engineering-ready smoke quality, not
   release-final quality. It has not completed the frozen 3,750-sample quality
   gate or board listening validation.
4. Current MVE telemetry does not populate the legacy `[DSP]` gain/spectrum
   aggregates (`valid=0`); use raw/model/output PCM and `[DFN]` tensor ranges
   for this package until that instrumentation is completed.
5. Capture-to-DAC transport latency is stable at about 46.9 ms. It does not
   explain intermittent suppression, but it is too high to ignore for a
   hearing-aid-oriented product evaluation.

## Direction for the next model

Select or train a candidate that is robust at the measured low-level input
range and cannot fully remove plausible speech without an explicit product
policy. Evaluate candidates using the same M55M1 capture chain, not only
offline normalized audio.

- Include distant speech and speech-plus-noise examples spanning the measured
  raw RMS range of roughly 10 to 100 PCM counts.
- Include microphone/board gain variation during training or calibration.
- Evaluate speech-preservation failures separately from noise-reduction score.
- Prefer a model that supplies a reliable speech-presence/confidence output, or
  pair a mask model with a bounded residual path.
- Compare at least: suppression of steady noise, speech retention at several
  distances, transient robustness, INT8 saturation, NPU timing, and listening
  tests on the board.
- Keep the current `+12 dB` input calibration only as a diagnostic point. A
  release value requires measured clipping and listening evidence.

## Candidate acceptance gates

Before replacing the current model, require all of the following:

1. No speech dropouts in a defined distant-speech board corpus at the selected
   microphone position and gain.
2. No model-input clipping during that corpus at the selected calibration.
3. Scalar golden vectors and MVE parity within one Q15 LSB.
4. End-to-end DSP time no greater than 16 ms, with no NPU, DMIC, DAC, or DMA
   errors during a ten-minute soak.
5. Flash at most 1.8 MiB, SRAM at most 1.5 MiB, and at least 2 KiB measured
   stack headroom.
