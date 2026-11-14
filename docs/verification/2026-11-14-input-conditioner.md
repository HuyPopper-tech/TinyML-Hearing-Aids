# PCM24 input conditioning trial — 2026-11-14

Historical +18 dB conditioner receipt. The current trial uses +21 dB and the
independent limiter; see [pipeline cleanup](2026-11-14-pipeline-cleanup.md).

## Implemented signal path

```text
DMIC +12 dB -> FIFO24 / DMA32 -> signed PCM24 / 8388608
 -> fixed +18 dB float gain -> compressor + immediate peak guard
 -> same float hop for frontend and waveform FIR
 -> existing PCM16 render -> existing DAC -9 dB startup gain
```

The sample rate remains 16 kHz, hop 16 samples (1 ms). The conditioner uses
the peak of the already captured hop, without another queued hop or lookahead
buffer. Quiet signals below the knee receive approximately +30 dB combined
DMIC/software gain. This is a digital gain setting, not calibrated acoustic
sensitivity or proof of distant-speech intelligibility. Relative to the prior
+24 dB raw capture, lowering DMIC gain by 12 dB creates digital DMIC output
headroom while software gain adds 18 dB after capture. It cannot undo saturation
inside the microphone or DMIC.

`services/deepfir/deepfir_input_conditioner.c/.h` owns these stateful dynamics;
the inference service owns its DTCM state and resets it with the stream.
The adapter's new validated float entry feeds the identical immutable float
hop to the frontend and FIR. Raw adapter PCM16/PCM24 entry points and the
PCM16 compiled replay path keep their original arithmetic. Product PCM24
conditioning changes the model input distribution and requires acoustic testing.

The new processing is foreground-only with fixed local scratch. No allocation,
ISR work, logging, clock, IRQ, DMA layout, idle/WFI policy or model artifact
change was introduced. Existing MVE DSP and CPU DTCM placement are retained.
Maintaining that policy does not establish unchanged measured power.

## Numeric definition

The preamp is `A = 10^(18/20)`. For one hop, detect
`L = 20*log10(max(abs(input / 8388608))*A)` in dBFS. Silence and levels below
the knee avoid logarithm/exponential calls.

For threshold `T=-12 dBFS`, ratio `R=3`, knee width `W=6 dB`, target gain in dB:

```text
L <= T-W/2:  G = 0
inside knee: G = (1/R-1) * (L-T+W/2)^2 / (2*W)
L >= T+W/2:  G = (1/R-1) * (L-T)
target = 10^(G/20)
```

The gain state follows `g = target + alpha*(g-target)` per sample. Use
`alpha=exp(-ln(9)/(16000*time_seconds))`, attack 1 ms and release 80 ms;
the time convention leaves 1/9 of a gain step after the stated duration.
An immediate guard lowers and retains `g` when `abs(A*x)*g` exceeds
`10^(-1/20)=0.891250938`. A final roundoff guard bounds every emitted float.
This ceiling applies **before the model/FIR**, not to the acoustic output or
the final PCM16 renderer. Immediate transient limiting can introduce distortion.

The compressor curve and time convention follow the published
[MathWorks compressor algorithm](https://www.mathworks.com/help/audio/ref/compressor-system-object.html).
Finite attack alone does not guarantee peak containment, hence the additional
immediate guard rather than relying on smoothed gain for the ceiling.

## Verification performed

The final full suite passed **147 tests in 44.370 s**, with no skips/failures.
The existing isolated native GCC launcher was used because the installed GCC
runtime needs its local DLL/path workaround; assertions and tests were not
disabled. Exact command:

```powershell
rtk proxy python -X utf8 diagnostics/run_host_tests_pcm24.py discover -s tests -v
rtk proxy cmd /c build.bat release build
rtk proxy cmd /c build.bat debug build
rtk proxy python -X utf8 tools/verify_mve_disassembly.py build/M55M1_GestureAI.axf --objdump C:/ST/STM32CubeCLT_1.20.0/GNU-tools-for-STM32/bin/arm-none-eabi-objdump.exe
rtk git diff --check
```

New tests were run RED before implementation (missing conditioner/API), then
GREEN. The MVE product-path requirement was also observed failing before its
verifier update. The first full-suite run found missing `@file` tags; these
were fixed before the final clean run.

`tests/input_conditioner_harness.c` checks:

- **462,448 streaming samples per branch**, compared with an independent
  double reference. Maximum absolute error **5.07930639e-6**, below the
  predefined 3e-5 limit. Every sample is finite and within the ceiling tolerance.
- Strict scalar, native MVE lane model, and MVE lane model with
  `-ffast-math -fno-finite-math-only` produce the same output hash
  `31fbd2b8b1f71fba`. This models the vector C branch, not physical MVE execution.
- Silence, quiet PCM24 low bits, positive/negative rails, sign symmetry,
  gain steps, long release tails, amplitude sweeps and seeded random streams.
- Independent static-curve points, including knee center -12.5 dBFS output,
  and explicit 1 ms attack/80 ms release step equations.
- Single rail impulses at all 16 lanes and both polarities, immediately bounded.
- NULL/count/range/overlap/invalid-state rejection and unchanged state/output
  on conditioner argument failures; float adapter invalid inputs invoke no callbacks.
- Actual adapter with controlled inference/IFFT callbacks: frontend observes
  exactly the conditioned hop and FIR retains exactly its reversed input history.
  Feature failures latch until reset. These callbacks do not execute the model.
- Extracted actual service routing/reset functions: PCM24 is conditioned once,
  failures stop before the adapter, stream reset clears its state, PCM16 replay
  continues through its existing entry point.

Existing full-suite numeric gates also pass: FIR group4/group8 bit parity,
PCM16 rounding/finite failure atomicity, feature quantization, recurrent carry,
DMA scheduling/fault lifecycle, architecture, style and map placement.

Both **Arm Compiler 6.24 Release and Debug builds exit 0**. Both current CMake
caches explicitly contain `M55M1_DMIC_GAIN_DB:STRING=12`; a previous CMake cache
can retain its old gain despite a changed default. To reproduce in an older
tree, set `cmake -S . -B build -DM55M1_DMIC_GAIN_DB=12` (and similarly
`build-debug`) before using the ordinary build scripts.

Final linked Release verification passes all **10 MVE ranges**, including
PCM24 conversion by `2^23` inside the conditioner (17 vector instructions).
The verifier now checks the actual production normalization path rather than
an unused raw-adapter entry that the linker may discard. Debug's CMSIS FFT
wrapper remains out-of-line; no full Debug range-verifier pass is claimed.

## Resource and image evidence

| Item | Release | Debug |
|---|---:|---:|
| Conditioner state | 24 B | 24 B |
| CPU DeepFIR DTCM section | 7,472 B | 7,480 B |
| DTCM increase from previous trial | 40 B | 32 B |
| Tensor arena capacity, SRAM | 65,536 B | 65,536 B |
| Linker maximum stack estimate | 5,696 B + unknown | 6,680 B + unknown |
| Reserved stack | 8,192 B | 8,192 B |
| BIN size | 621,888 B | 647,392 B |

The Release BIN grows 1,792 B versus the archived previous trial. The stack
report still has cycles/untraceable indirect calls; it is not a stack watermark
or a proof that all runtime paths fit. No physical stack measurement was made.

| Image | SHA-256 |
|---|---|
| Previous Release | `6f866c8cecb251c36021c0a1b3bd16dd39c91e3b3f8188616a1c2191f791ab39` |
| Final Release `build/M55M1_GestureAI.bin` | `846f06d87c88feb54ab2d2746bdfc6e853670504bbedd05ced08a2b149361f60` |
| Final Debug `build-debug/M55M1_GestureAI.bin` | `fa83843897096fddb252326da77ed63923a07628593e9645593055e325213cb5` |

Ignored `diagnostics/input_conditioner_20261114/` contains baseline sources and
images, final AXF/BIN/map/stack/cache/compile databases, `receipt.json`, complete
host/build logs, linked MVE output and GDB symbol-lookup logs. The conditioner
has typed GDB symbols in both images. Offline lookup verified
`p &'(anonymous namespace)::g_input_conditioner'`: Release 0x20001f90,
Debug 0x20001ff0. These addresses belong only to the identified images.
Offline symbols establish observability, not live hardware measurements.

Self-review compared the changed integration boundaries with the archived
baseline and checked state ownership/reset, alias/range rejection, compression
equations, shared frontend/FIR input and preservation of replay/power policy.
Light route: no independent agent review, commit, push or device flash.

## Board trial still required

Use the matching Release AXF/BIN with the existing custom PCB SWD workflow.
Run continuously from reset for at least 60 seconds without breakpoints,
then Pause once. Read:

```text
p 'audio_pipeline.c'::g_stats
p '(anonymous namespace)::g_input_conditioner'
```

Inspect raw rail count/peak and all sequence/deadline/model/transport/restart
faults. Conditioner gain excludes the +18 dB preamp; its output peak is
pre-model. Its counters reset at stream reset, while raw statistics survive
automatic restarts. Compare the same voice/stimulus at 20 cm, 50 cm and 1 m
with the same supply, headset/load and environment. Reset between paused trials.
Use SWD; paired UART WAV capture is unavailable on this custom PCB.

No new full-hop deadline, acoustic latency, actual NPU numerical replay,
speech quality, live clipping reduction, current/power or stack watermark has
been measured. The extra log/exp and sample smoothing consume processing time;
the 1 ms real-time deadline must be checked with this exact image. Input
compression and software gain may amplify noise or change enhancement quality.
This remains a trial image pending those physical checks.
