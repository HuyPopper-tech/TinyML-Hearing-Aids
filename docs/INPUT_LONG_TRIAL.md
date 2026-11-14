# Fixed +21 dB input trial — 2026-11-14

For new firmware builds, gains can be changed through
[CMake audio gain configuration](AUDIO_GAIN_CONFIGURATION.md). This procedure
and the previously delivered package describe the default 12/21/-9 dB settings.

This package contains one Release configuration, not an acoustic calibration.
Use `M55M1_GestureAI.bin` and its matching `M55M1_GestureAI.axf` together.
Verify their SHA256 values against `SHA256SUMS.txt` before programming.
The package has not been flashed or measured on the board by this session.
The cleanup package is `out/m55-u55-cleanup-21db-20261114.zip`; its build and
numeric evidence is recorded in [pipeline verification](verification/2026-11-14-pipeline-cleanup.md).
Earlier long-trial receipts describe earlier image hashes.

## Configuration

```text
MP34DT05 -> DMIC +12 dB -> FIFO24 / DMA32 -> signed PCM24 / 2^23
 -> fixed software preamp +21 dB
 -> hop-peak envelope: instant rise / 20 ms decay
 -> soft-knee compressor: -12 dBFS, 3:1, 6 dB knee, 3/120 ms
 -> independent limiter: -1 dBFS, immediate attack / 20 ms release
 -> identical float samples to feature extraction and waveform FIR
 -> existing PCM16 output -> DAC startup -9 dB
```

Mono 16 kHz; hop 16 samples, 1 ms. Time constants use the exponential
10-90% convention (1/9 residual after the named duration). There is no added
sample buffer or lookahead delay. Compressor recovery can temporarily reduce
quiet sound after a loud sound. Fixed gain increases noise along with speech;
it cannot restore raw saturation or establish distant-speech intelligibility.
The pre-model ceiling does not guarantee an acoustic or final-output ceiling.
Clocks remain 220 MHz core / 73.728 MHz audio APLL; existing MVE, DTCM,
DMA ownership and WFI idle policy remain in use. Power is unmeasured here.

## Program and connect

Use the existing custom-PCB programming/debug procedure. Capture pins remain
PA1 (DMIC clock) and PA2 (DMIC data). Recovery access remains PF0/PF1
(SWDIO/SWCLK), nRESET, GND and VREF/3V3. A probe without an nRESET wire
cannot assert physical reset; retain the established resetless recovery setup.
No pin, clock or recovery wiring change is needed for this image.

Select the matching packaged AXF in the debugger instead of a later rebuilt
file. Connect before starting the trial: existing Attach profiles reset and
stop at main, so attaching after a run loses that run's statistics.

## Uninterrupted 30-minute trial

1. Start from reset, run Release without stepping, breakpoints or Pause/Continue.
   Record package hash, supply voltage, headphone/load, distance, speech level
   and environment. Begin listening at a comfortable existing output setting.
2. Include quiet intervals, ordinary speech and bounded loud transients. When
   available, compare 20 cm, 50 cm and 1 m with consistent source level. Save
   listening observations separately from numeric evidence.
3. After at least 30 minutes, pause once and enter these Cortex-Debug commands:

   ```text
   p 'audio_pipeline.c'::g_stats
   p '(anonymous namespace)::g_input_conditioner'
   p 100.0 * 'audio_pipeline.c'::g_stats.input_clip_samples / 'audio_pipeline.c'::g_stats.input_samples
   p 100.0 * '(anonymous namespace)::g_input_conditioner'.compressed_samples / '(anonymous namespace)::g_input_conditioner'.processed_samples
   ```

   Compute ratios only with nonzero denominators. Do not prefix commands with
   `-exec` in this console. Save the entire structs and the firmware hash.
4. Require `max_capture_to_publish_us < 1000`, no spontaneous sequence,
   deadline, model or transport faults, and no unexpected restarts/failures.
   A fault after Pause/Continue is a debugger-interrupted trial, not evidence
   of spontaneous failure. Reset before a fresh uninterrupted trial. The user
   explicitly attributed the earlier single restart to Pause/Continue.
5. Measure stack watermark and current separately with this exact image.
   For power comparisons keep supply, firmware profile, headphone/load and
   continuous playback the same; record meter resolution. Linker estimates
   and unchanged idle policy do not establish runtime stack/power acceptance.

## Interpret the snapshot

`g_stats.input_peak` is a raw PCM24 magnitude: 8388608 means an endpoint was
observed; `input_clip_samples` counts codes -8388608 or +8388607. Those
events can precede software compression. They do not identify speech versus
startup/transient clipping. Zero endpoint events do not exclude earlier
microphone distortion. Raw `input_samples`, `input_clip_samples` and
`capture_sample_position` are 64-bit. Sequence tags and fault counters remain
32-bit; read the entire snapshot while halted.

`g_input_conditioner` lives in foreground-owned DTCM; read it while halted:

- `compressor_gain`, `limiter_gain`, `gain`: current linear gains, with `gain`
  their product, excluding the fixed +21 dB preamp.
- `detector_peak`: current post-preamp envelope; it can exceed 1.
- `processed_samples`: accepted conditioner samples. Divide by 16000 for
  processed audio seconds, excluding gaps.
- `compressed_samples`: samples whose combined reduction is at least 0.1 dB.
  Inaudible float release residue does not count.
- `limited_samples`: immediate peak-limiter interventions; release tails
  are not additional interventions.
- `input_peak`, `output_peak`, `min_gain`: cumulative raw normalized peak,
  conditioned pre-model peak and minimum combined gain.
- `input_rms`, `output_rms`: latest complete 100 ms RMS window, respectively
  before software gain and after conditioning. They are not run averages.
  For a positive normalized value, calculate dBFS offline as `20*log10(value)`;
  zero RMS is -infinity dBFS and can also mean no complete window yet.
- `rms_window_samples` and square sums: incomplete current-stream window.

The three conditioner counters are 64-bit. Stream restart preserves counters,
cumulative peaks, minimum gain and last complete RMS, but resets detector,
gains and the incomplete RMS window. Cold initialization clears all fields
and initializes gains/minimum to unity. Raw pipeline stats keep their existing
lifecycle. Conditioner output peak is before model/FIR and PCM16 rendering.

Custom-PCB paired UART WAV capture is unavailable. These SWD snapshots do not
measure intelligibility, SNR, physical FIFO packing or acoustic output level.
Keep host/build verification separate from these pending physical checks.
