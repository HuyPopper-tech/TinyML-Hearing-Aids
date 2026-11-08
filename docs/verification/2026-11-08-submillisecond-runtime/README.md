# R2 submillisecond runtime implementation — 2026-11-08

Implemented after the user approved `docs/pipeline_submillisecond_analysis.md`.
Base commit: `6967118` (code baseline `6bf6b37`). Light route, inline work;
no subagents or explorer, no device operation. This report records implementation
and host/build evidence, not a measured speedup or resolved startup underrun.

## Runtime changes and ownership

- `services/deepfir/deepfir_model_adapter.c`: four-lane MVE rounded Q31 feedback
  for zero-centered shift30/31 states. Magnitude multiplication and sign restore
  preserve negative ties-away. INT16 saturation and generic nonzero-qparam/other-
  shift fallback remain. Adapter alone owns encoded h/c. Startup checks every
  65,536 code per lane against the generic INT64 reference using local scratch;
  it never commits test state. Init fails cleanly if this check fails.
- `deepfir_features.c`: four-bin feature assembly followed by four-lane guarded
  quantization. Keep the scalar division fallback around half-code boundaries,
  subnormals and exceptional inputs. Feature state owns analysis history; adapter
  owns previous/current context. Private 48-byte assembly scratch adds no
  persistent buffer. FFT256, window, feature/channel order and scaling remain.
- `deepfir_reconstruction.c`: vector finite checks, roll+64 and exactly
  representable taper. Retain installed CMSIS sin/cos, Hermitian endpoints and
  IFFT128 convention. Caller taps are copied only after the whole filter passes.
  No approximation of sqrt/trig, new FFT format, gain or PCM rule was introduced.
- Existing FIR MVE/history/crossfade, transport ownership, b+2 schedule and
  strict 1 ms guard remain. One interpreter/model, no new queue or allocation.
- `board/m55m1_audio.c`, codec driver and transport report: bounded raw startup
  observations for Debug/Release Probe only. Board owns 788 bytes; IRQ/foreground
  writers preserve PRIMASK, append at most 12 events and stop recording before
  transport shutdown. CPU-only data needs no DMA cache maintenance. Service
  formats `[AUDIO_STARTUP]` only after stop. Ordinary Release contains neither
  the trace buffer nor its UART format string. No DMA threshold, start order,
  clock, pin, IRQ priority or descriptor configuration was changed.

Feedback startup enumeration increases boot self-test work, not per-hop work.
`mve_test_us` includes it; its new target value is unmeasured. Debug logs
`mve.feedback BEGIN/OK/FAIL`. Tests explicitly exercise vector tails and output
canaries, fallback offsets, finite-check failures and unchanged failure outputs.

## Verified results and limits

Full native regression: **132/132 PASS**, no skips on this installed toolchain.
Final focused optimization run: 6/6 PASS. See `host/` and `attempts.json`.

- Integer feedback: 15,400,960 generic comparisons plus 393,600 commit/carry
  comparisons, zero mismatch. Vector C branches run under an explicitly labelled
  native lane model, including exhaustive h/c codes, count0..4 tails and startup
  self-test adapter immutability. This is not execution of Arm instructions.
- Feature quantization: existing 11,570,644 comparisons, 786,432 accepted FP32
  cases, 4,718,592 guard-edge cases; new vector-branch lane model adds 7,077,966
  comparisons. Zero code mismatches. Independent scalar reference retained.
- Actual Arm6.24 builds/link pass: Debug, ordinary Release and Release Probe.
  Release and Probe scoped disassembly gates cover nine kernel ranges, including
  Q31 `VQRDMULH` and vector float conversion. Static counts: feedback11,
  quantizer26, reconstruction18 vector instructions. Counts are not timings.
  Debug builds the same kernels, but the optimized FFT-wrapper range gate is not
  applicable to its out-of-line unoptimized helpers; no Debug nine-range claim.
- DSP replay: both canonical gain/phase traces, **26,441 hops**, synthetic PCM,
  actual native scalar CMSIS FFT/sin/cos plus vector reconstruction lane model.
  Maximum tap error1.1920929e-7, output error2.9802322e-7 against the saved pre-
  optimization libm reference. Existing tolerances unchanged: taps
  `2e-5 + 2e-5*abs(reference)`, output `2e-5 + 2e-6*abs(reference)`.
  There are136 PCM differences, each1 code (90/46 by trace), the same observed
  differences as the previous CMSIS conversion. No bit-exact PCM claim.
  This does not invoke raw/compiled controller, validate frontend PCM end-to-end,
  or establish target MVE numerical parity. See `dsp_receipt.json` for hashes,
  first mismatches and per-trace errors; reference source is evidence only.

Build attempts caught two implementation defects: LPPDMA descriptor field is
`LPDSCT`, and microlib lacks `nextafterf`. Both were corrected and all profiles
rebuilt. The Debug optimized-range procedure failure is recorded separately;
the strict Release/Probe checks were not weakened. Earlier red tests, native
compile failures, style failure and all subsequent results remain in the raw
timestamped ledger and `attempts.json`.

## Images and memory

| Profile | BIN bytes | RW_RAM bytes | BIN SHA-256 |
|---|---:|---:|---|
| Debug | 642752 | 78712 | `c97208b2714337387395c177337dd1cad9cefec2ae07aa0e3634ed61550d0e03` |
| Release | 617696 | 77168 | `ad63d63fc2749c69c7ce80b799b8768483ebd5e05eb33a443c39c37903a2d5ab` |
| Release Probe | 624736 | 78216 | `1df3d97b31b6452c360d5512a892bcf8c684732fa02616e75bda41bbb53bbb80` |

Ordinary Release RW_RAM unchanged; Debug increases792 bytes including alignment.
Model SRAM region0x20100000 is516852 bytes (compiled model516848 plus alignment),
arena capacity65536, uncached DMA320, stack8192, heap reserve2048. Adapter5288,
feature state2072 and pipeline working set64 bytes. These objects are already
inside RW_RAM, not extra allocations. Linker Release total RW including other
regions is87728 bytes; RW_RAM alone is not the total memory footprint.
Model SRAM is a separate immutable region and must also be counted.

Debug static callgraph reports6656 bytes + Unknown(cycles/untraceable function
pointers); this is not a stack watermark or proof of adequate headroom. Release
callgraph lacks useful debug metadata. New-image actual AllocateTensors usage,
runtime stack margin, cache coherence and board initialization are NOT_RUN.
Earlier user-reported arena6976 bytes is not an attestation of these BIN hashes.
See `memory.json`, `build_evidence/*/images.json`; full BIN/AXF/maps/callgraphs remain in
`diagnostics/submillisecond_runtime_20261108/225414/`, excluded from Git.

Source package, model bytes, contract, qparams, goldens and `deployment_approved`
were not changed. Compiled identity remains
`69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27`.

## Acceptance

| Item | Status | Evidence / remaining boundary |
|---|---|---|
| Artifact integrity | PASS | Full package/hash and compiled-array regression gates; source hashes in final checks |
| Architecture/source integration | PASS | Existing owners/dependency gates, target linkage, self-review; no libs edits |
| Host numeric correctness | PASS | 132 tests and26,441-hop DSP comparison, limits above |
| Debug/Release build/link | PASS | Also Probe; commands/logs/images/map summary in `build_evidence/` |
| Raw controller replay | NOT_RUN | Existing Linux raw receipt retained; no new compatible TF2.20 run |
| Compiled controller replay | BLOCKED | No backend/device execution in this run; mock/native DSP is not parity |
| Board initialization/memory | NOT_RUN | Linker sizes available; new target allocation/stack checks required |
| Full-hop deadline/schedule | NOT_RUN | No new board run; prior1079/1088us and seq0 underrun remain open |
| Physical audio/latency/power | NOT_RUN | No physical measurements or flash |

## Reproduce and run the board next

Native prerequisites: Python/NumPy, executable GCC/G++ runtime; Armclang for two
optional target assembly tests. GCC-only machines explicitly skip those two
checks. This Windows run used the existing isolated compiler wrapper on child
PATH, Arm Compiler6.24 and GNU objdump from STM32CubeCLT1.20.0.

```powershell
rtk python -m unittest discover -s tests -v
rtk git diff --check
rtk proxy python -X utf8 docs/verification/2026-11-08-submillisecond-runtime/compare_dsp_traces.py
rtk proxy cmd /c build.bat debug config
rtk proxy cmd /c build.bat debug build
rtk proxy cmd /c build.bat release build
```

The comparator writes a new timestamped ignored diagnostics directory; the
packaged traces stay in the repository source package. It compares DSP against
the included historical C reference, not a fabricated controller. All commands,
exit codes and full MVE invocation/objdump path are recorded in `build_evidence/commands.json`.
Root outputs are restored to ordinary Debug/Release, probe/replay/capture OFF,
and root compile database points to Debug. Restart clangd after switching.

For the next user-authorized device run select **M55M1: Release Probe: Flash +
Debug (OpenOCD Nuvoton)**. Its existing prelaunch tasks build Probe in `build/`;
the ordinary Release BIN currently there is intentionally silent. F5, Continue,
no further halts/steps/breakpoints during the run. Preserve every
`[AUDIO_STARTUP]` line and the complete `[AUDIO_RELEASE_PROBE]` and
`[AUDIO_RELEASE_RESULT]` lines, with the newly flashed BIN hash.

Inspect TX_ARM/TX_ENABLED/DMIC_ENABLED, first four TX_TD, first two RX_TD,
first CAPTURE_TAKE and STOP. Times are relative to the first recorded event,
not a physical playback epoch. TX TD means data reached FIFO, not serial output;
RX TD is sampled after acknowledgement so `rxtd=0` can be expected. Register
reads are observations, not a logic-analyzer waveform. Probe instrumentation
has bounded but unmeasured overhead on the first transfers.

Required acceptance: 60,000 consecutive hops, fault/deadline/overrun/underrun0,
all capture-to-publish and total hops <1000us, target maximum <=900us for margin,
correct b+2 slots and sequence continuity. If seq0 underrun persists, use the
new trace to establish FIFO/TD phase before changing scheduling. If timing still
fails, profile the reported frontend/Invoke/carry/taps/FIR/PCM split; do not
relax deadline, controller codes, DSP tolerance or playback rules.

Custom pins remain DMIC PA1/PA2, I2S1 PA8/PA10/PA11/PB0, I2C PA4/PA5,
UART PC4/PC5 and SWD PF0/PF1. CH549 lacks physical nRESET; power-cycle recovery
remains available. No agent flash/reset/pin/clock/DMA reconfiguration occurred.
