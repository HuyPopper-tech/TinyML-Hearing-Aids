# Continuous audio optimization verification — 2026-11-08

Approved approach 1, implemented inline on the custom PCB product path. Baseline
revision `0eb6e0c0f0ed7cdd2d3d8f51ebc5fa8c667b4d19`. Light route, no subagents or
explorer. Source packages, libs, model bytes, goldens, pins and clocks unchanged.
No device connection, flash, reset or physical measurement was performed.

## Acceptance

| Gate | Status | Evidence and limit |
| --- | --- | --- |
| 1 Artifact integrity | PASS | [artifact_integrity.json](artifact_integrity.json): 47 source and 19 destination artifact hashes; compiled array byte-identical, 516848B, SHA69c1c3d2… |
| 2 Architecture/source integration | PASS | [source_hashes.json](source_hashes.json), full host regression and self-review; one existing adapter/interpreter, unchanged DMA/cache safeguards and model linkage |
| 3 Host numeric correctness | PASS | [host_tests.log](host_tests.log): 127/127, no skips; existing tolerances retained; integer quantization/feedback exact; host scalar CMSIS, no target MVE execution |
| 4 Debug/Release build/link | PASS | [build_commands.json](build_commands.json), [images.json](images.json), [memory.json](memory.json); Arm6.24, four ordinary configure/build commands exit0, probe/replay also link |
| 5 Raw controller replay rerun | NOT_RUN | No Linux TF2.20.0 environment executed this session; package receipt remains historical evidence |
| 6 Compiled controller replay | BLOCKED | [compiled_replay_receipt.json](compiled_replay_receipt.json): no authorized running backend; validator exit1 as designed, no mismatch results or runtime identity |
| 7 Board init/actual memory | NOT_RUN | Maps reserve64KiB arena/8KiB stack; no fresh AllocateTensors or stack watermark on target. Prior user UART allocation is historical only |
| 8 Full-hop deadline/schedule | NOT_RUN | Host ownership/sequence/recovery tests pass; 60s probe links, no physical timing/jitter or b+2 observation. Debug is diagnostic and need not meet1ms; guards retained |
| 9 Physical audio/latency | NOT_RUN | No current microphone/DAC continuity, clipping, quality or physical latency measurement |
| 10 Power/energy reduction | NOT_RUN | No matched current/energy A/B measurement; no claimed saving |

`deployment_approved` remains false. Source/build acceptance does not close device
gates. Initialization `board_status=5` remains a separate physical investigation.

## Numeric experiments

- Baseline123 tests pass; final127 tests pass in26.892s. Commands/exit codes are
  in [baseline_commands.json](baseline_commands.json) and
  [host_tests_receipt.json](host_tests_receipt.json). GCC13.2 executes real C and
  installed scalar CMSIS sources; Arm6.24 emits MVE target code.
- Guarded reciprocal quantization:11,570,644 comparisons, zero mismatch versus
  original division/rounding; halfway neighborhoods, extreme/non-finite values,
  multiple scales and offsets. Normalized/PCM frontend:4096 sequential hops,
  identical codes/history and finite/count/reset gates.
- Encoded feedback:15,400,960 comparisons, zero mismatch; all INT16 codes,
  shifts0..46, contract multipliers, offsets and saturation. Debug ARM disassembly
  removes `__aeabi_ldivmod` from this function; magnitude shift replaces it.
  [feedback_arm_disassembly.json](feedback_arm_disassembly.json) includes exact
  old/new decoded instruction excerpts. This is static, not a cycle measurement.
- CMSIS sin/cos and actual scalar CFFT128:65,536 phase codes at max gain,
  max_abs_tap_error2.7351718e-7 versus independent NumPy reference. Existing
  atol2e-5/rtol2e-5 retained. Finite/large-angle fallback checks run in native C.
- Long DSP comparison:canonical gain/phase for both11324/15117-hop traces, with
  deterministic synthetic PCM and independent persistent old/new FIR histories.
  [dsp_trace_comparison.json](dsp_trace_comparison.json): max tap error1.1920929e-7,
  max output error2.9802322e-7. PCM differs90/46 samples, max1 code; first mismatches
  zero-based hop215/sample0 and hop306/sample12. Total423056 samples. Float bounds
  remain taps2e-5+2e-5|ref| and FIR2e-5+2e-6|ref|. This does not establish output
  bit identity, perceptual quality, controller replay, frontend end-to-end parity,
  or MVE execution. PCM rules remain exact for identical float inputs.
- Rejected real FFT candidate:8192 synthetic hops,3,170,304 feature codes,
  642,552 changed, maximum197 codes, first hop11/code116; exceeds1-code budget.
  [rejected_real_fft.json](rejected_real_fft.json). Active complex FFT unchanged;
  neither expected values nor goldens were changed to admit this candidate.

## Build and memory

| Ordinary image | Baseline BIN | Final BIN | Final SHA256 |
| --- | ---: | ---: | --- |
| Debug | 634400B | 638112B | f0a6e7623900e34c5d8e19cc06ac1deaf386c8e0874b876cd0ebc744048f6477 |
| Release | 612800B | 615392B | 72ea67e693846caa642286c2a7e9e90ecfaf03f196b3542f9fc5867384e2acbf |

| Linked item | Baseline Debug / Release | Final Debug / Release |
| --- | ---: | ---: |
| RW_RAM region | 78456 / 77936B | 77920 / 77168B |
| Adapter object | 6056 / 6048B | 5288 / 5280B |
| Feature state | 2072 / 2072B | unchanged |
| Arena reserved | 65536 / 65536B | unchanged |
| Uncached DMA | 320 / 320B | unchanged |
| Total RO | 634296 / 612744B | 638004 / 615356B |
| Total RW incl. stack/heap/other regions | 91064 / 88496B | 90528 / 87728B |

Model region0x20100000 remains516852B (516848-byte artifact plus alignment/metadata);
RW_RAM starts0x201c0000, uncached DMA0x20230000, reserved stack8192B, heap2048B.
Debug trace records192B. Debug linker main stack estimate6568→6560B; estimate is
not a runtime high-water and does not fully bound indirect calls/interrupt nesting.
Release map lacks that stack estimate. No claim of fresh actual arena use.
Release BIN grows2592B; report this ROM tradeoff alongside RAM reduction.

[mve.log](mve.log) verifies vector instructions in six scoped linked Thumb ranges:
frontend110, FIR48 (including vfma), FFT177, vlog48, vexp49 and q15 conversion6.
Static disassembly is not actual M55 execution or a timing result.

[variant_commands.json](variant_commands.json) records Release probe/replay
configure/build, ordinary Release restore and Debug editor restore, all exit0.
Normal root outputs match final hashes above. Both caches have replay, capture
and probe OFF; root compile_commands points at build-debug. Only build-debug and
build are build directories; timestamped diagnostic image snapshots are evidence.

After the final comment review, both ordinary profiles were rebuilt again:
[20261108_184423_build_receipt.json](20261108_184423_build_receipt.json), all exit0.
All six BIN/AXF/MAP hashes remain identical to182911.
[post_review_restore.json](post_review_restore.json) verifies current files, both
caches/editor and exactly one linked model/core/runtime symbol each.

## Reproduction and raw evidence

Raw run root: `diagnostics/audio_optimization_20261108_180148/` (Git ignored).
Baseline images/maps: `20261108_180223_baseline/`. Final ordinary images/maps:
`20261108_182911_build/`. Probe and replay snapshots:
`20261108_183101_release_probe/`, `20261108_183101_compiled_replay/`.
All image/map hashes are in [images.json](images.json); binaries/full maps/compiler
files are intentionally not committed. Logs here are text copies normalized for
line endings/trailing whitespace; the original logs remain in the raw run root.

Recipe copies here retain the exact scripts used, including historical diagnostic
paths. **Do not execute them in this documentation directory.** For another run:

1. Create a fresh `diagnostics/audio_optimization_<timestamp>/` directory. Copy
   run_local_verification.py to verify.py, run_local_variants.py to verify_variants.py,
   compare_dsp_traces.py and evaluate_real_fft.py into it. Their root calculation
   expects that two-level diagnostic location. Run from the repository root.
2. The current machine reuses the read-only GCC wrapper/runtime from
   `diagnostics/i2c_stuck_trace_20261108_162019/host_compiler_runtime/`, prepended only
   to each subprocess PATH; wrappers use -B and -static to avoid Windows DLL
   failures. On another machine set this path to a working GCC installation or
   use the ordinary commands in the [guide](../../audio_inference_optimization.md).
   No toolchain or global PATH changes are required. NumPy2.1.3/Python3.12.10 used.
3. `rtk proxy python -X utf8 diagnostics/<new>/verify.py tests` runs the full suite;
   `... verify.py build` configures/builds Debug then Release serially and archives
   images/maps. `... verify_variants.py` builds opt-ins serially and restores the
   ordinary Release image/Debug editor in finally. No device operations occur.
4. For DSP comparison only, create `<new>/baseline_sources/` and extract the
   baseline reconstruction C from revision0eb6e0c0f0ed7cdd2d3d8f51ebc5fa8c667b4d19
   into deepfir_reconstruction.c there (byte-safe subprocess stdout to file).
   `... compare_dsp_traces.py` creates its own fresh dsp_trace_replay subdirectory,
   compiles the two implementations and consumes local package gain/phase.
   `... evaluate_real_fft.py` regenerates the independent synthetic candidate.
   Use a new run root to preserve previous results. Neither invokes a controller.
5. `rtk proxy python -X utf8 tools/replay_deepfir_r2.py --output diagnostics/<new>/replay`
   validates package hash/schema, then exits1/BLOCKED without a supplied backend.
   See existing replay guide/runner for executing an authorized target backend.
6. `rtk proxy python -X utf8 tools/verify_mve_disassembly.py build/M55M1_GestureAI.axf
   --objdump <arm-none-eabi-objdump.exe>` and `rtk git diff --check` reproduce static
   gates. [final_checks.json](final_checks.json) has commands and exit codes.

Package paths recorded under Kaggle are historical provenance only. All artifacts
used here resolve under the repository package named in artifact_integrity.json;
no external research folder or training rerun is used. Raw replay environment was
not recreated. Baseline/current C hashes are listed in source_hashes.json.

Next authorized-device procedure, pins/recovery and interpretation of probe fields
are in [audio_inference_optimization.md](../../audio_inference_optimization.md).
Do not infer measured power, deadline, quality or deployment approval from this
host/build evidence.
