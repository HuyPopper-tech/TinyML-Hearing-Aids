# DMIC clipping statistics over SWD

The normal Debug and Release images collect input statistics without UART.
The foreground audio pipeline owns the counters in `audio_pipeline.c`'s
`g_stats`. They survive automatic stream restarts and pipeline initialization;
a board reset clears them. The linker places `g_stats` in non-cacheable SRAM
so SWD sees CPU writes without a D-cache clean. `volatile` alone is insufficient.
Only contiguous capture hops accepted by the
foreground are counted, before inference and deadline checks. Dropped DMA
blocks and rejected sequence hops are not counted.

Release retains optimization and typed debug information for the audio pipeline
in its AXF. Use the AXF matching the flashed BIN. The trial uses DMIC +12 dB,
FIFO24, DMA32, mono 16 kHz and DAC startup gain -9 dB. Each 16-sample hop is
sign-extended PCM24 in `int32_t`; normalization by 8388608 is followed by fixed
+21 dB software gain, a -12 dBFS/3:1/6 dB soft-knee compressor and independent
-1 dBFS peak limiter. This same conditioned hop feeds feature extraction and
waveform/FIR. Compressor attack/release are 3/120 ms (10-90%). The detector rises
immediately with the captured hop peak and decays over 20 ms; the limiter has
immediate attack and 20 ms release. These are gain time constants, with no added
hop buffer or sample delay. There is no automatic quiet-level gain tracking.

Run audio continuously, speak for a bounded interval, then pause once. In the
VS Code Cortex-Debug Debug Console, enter:

```text
p 'audio_pipeline.c'::g_stats
p 'audio_pipeline.c'::g_stats.input_samples
p 'audio_pipeline.c'::g_stats.input_clip_samples
p 'audio_pipeline.c'::g_stats.input_peak
p 'audio_pipeline.c'::g_stats.deadline_misses
p 'audio_pipeline.c'::g_stats.restarts
p '(anonymous namespace)::g_input_conditioner'
p 100.0 * 'audio_pipeline.c'::g_stats.input_clip_samples / 'audio_pipeline.c'::g_stats.input_samples
```

Do not prefix these commands with `-exec`: this Cortex-Debug configuration
treats a leading dash as a raw MI command. The equivalent explicit MI syntax
is `-interpreter-exec console "p 'audio_pipeline.c'::g_stats"`.

Only calculate the percentage when `input_samples > 0`. Sample count divided
by 16000 gives observed audio seconds, excluding gaps. `input_clip_samples`
counts exact PCM24 codes -8388608 and +8388607. Peak is an absolute code magnitude:
4194304 is -6.02 dBFS, 7476355 is approximately -1 dBFS, and 8388608 is 0 dBFS.
For positive peak, use `20 * log10(input_peak / 8388608.0)` offline.
A zero endpoint count does not rule out distortion or earlier clipping in the
microphone/DMIC. Frequent restarts mean coverage has gaps.

`g_input_conditioner` is foreground-owned DTCM, with typed symbols in both
profiles. Its peaks are normalized floats: `input_peak` is before software gain,
`output_peak` is after compression/limiting. `compressor_gain` and `limiter_gain`
are separate current states; `gain` is their product, excluding the fixed
+21 dB preamp. `limited_samples` counts immediate limiter interventions, not its
release tail. `compressed_samples` counts samples with combined reduction at
least 0.1 dB, including meaningful release tails. All three conditioner counters
are 64-bit and survive stream restarts, as do cumulative peaks and `min_gain`.
`input_rms` (before preamp) and `output_rms` are the latest complete 100 ms
window; zero also means no window completed yet after cold initialization.
`rms_window_samples` and square sums belong to an incomplete window and clear
on stream reset, together with detector and gain dynamics. Board reset clears
all conditioner diagnostics; raw `g_stats` retain their existing semantics.
Read this DTCM state while halted, with the matching AXF. A zero initialized
gain means cold stream initialization has not completed. Offline GDB symbol
lookup is not evidence of live hardware writes. Compression cannot recover
raw PCM rail saturation or prove speech intelligibility.

Pause stops foreground processing and disrupts the audio stream. Reset for
the next trial. The existing Attach profiles reset the board and stop at main,
so attaching after a trial discards that trial; attach first, run, then pause.

Compare separate reset-to-pause trials at 20 cm, 50 cm and 1 m with the same
speech level and environment. Run each for at least 10 seconds without stepping
or breakpoints. Save counters before resetting. Compare peak, endpoint fraction,
fault counts and listening observations. The model can suppress quiet sounds;
these input counters measure the stream before enhancement. They do not measure
the speech/noise ratio or prove that distant speech becomes intelligible.

The historical paired-UART export still serializes PCM16 by explicitly dropping
the low eight bits at export only. It is unavailable on the current custom PCB;
use SWD for this trial. Production inference never passes through that conversion.

Build, host-test and artifact evidence for the cleanup image is recorded with the
[pipeline verification](verification/2026-11-14-pipeline-cleanup.md).
Normal Release returns zero from `deepfir_inference_get_last_timing()`;
stage timing belongs to Debug/Probe. This does not disable `g_stats`.
Physical FIFO packing, audio quality and the 1 ms deadline still need a board run.
