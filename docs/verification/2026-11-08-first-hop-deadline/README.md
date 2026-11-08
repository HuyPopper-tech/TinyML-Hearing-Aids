# First-hop deadline investigation — 2026-11-08

User reports **F5 → Continue without a subsequent halt**. The reported Release
first hop takes1079us inside inference and1088us from capture to the deadline
check. Fault3 is the intended process-deadline guard; the1ms requirement failed.
The firmware image hash was not supplied, so these observations are not attested
to a particular BIN. [user_reported_run.json](user_reported_run.json) preserves
the supplied values separately from locally executed verification below.

Debug reports successful board/codec/clocks, tensor binding, arena allocation
6976/65536B and MVE boot tests, followed by transport underrun before a hop was
accepted (`seq=0`). This is a separate observation from Release fault3. There
is no basis to blame a later debugger halt, or to declare a DMA hardware defect.
One successful `BUS_FREE` recovery does not close intermittent `board_status=5`.

## Scope, ownership and acceptance

Work follows the existing Light route without agents. Retain one pipeline,
adapter/interpreter and their existing history/state owners. No interface,
model/package/golden, pin/clock/DMA/cache, queue or deadline changes. No device
connection, flash, reset, or production deployment approval was performed.

The implementation and acceptance scope is:

1. Reproduce the missing failed-hop summary in the actual C pipeline harness;
   retain the failing test evidence, then require stage maxima, means and late
   count to include a completed hop exactly once on process/submit/publish exits.
2. Extend the existing private feature quantizer with a guarded FP32 path.
   Preserve integer results against independent original division/ties-away
   rounding, including halfway and saturation/exception cases; do not change
   expected values or floating DSP tolerances.
3. Build Debug, Release and the actual VS Code Release Probe task chain. Keep
   only the two existing build directories; snapshot images/maps as evidence.
4. Restore ordinary Release with opt-ins OFF and the Debug compilation database;
   leave board timing/permanent-fix acceptance open for a new physical run.

Changed production files are `services/deepfir/deepfir_features.c` and
`services/audio_pipeline/audio_pipeline.c`. Their corresponding numerical and
probe tests were extended; headers, callers, fixtures and CMake need no ABI
change. Frontend history remains owned by the inference feature state; adapter
owns context/h/c/FIR history/taps; pipeline owns capture/playback working buffers;
drivers retain DMA ownership. No new persistent runtime buffer or allocation.

## Cause and changes

The process-deadline exit precedes `record_progress`, so the first failed hop
never updated aggregate timings. The failure handler now snapshots its stage
timing before reset, stops DMA, then includes completed process/submit/publish
failures in the summary. It does not publish rejected output or retry a partly
updated context. E2E maxima on those failures measure capture-to-fault snapshot,
before stop/reset/logging; the fault `detail` retains the original deadline
measurement. An output peak of zero on this exit is not proof of a zero model
output. Release Probe additionally reports last pre-FFT, FFT, post-FFT, NPU,
bridge and other timing. UART formatting/drain still occurs after stop.

The frontend accounts for317us in the supplied first hop. This does not identify
how much is FFT, compression or quantization. The old quantizer uses FP64
instructions in the actual Arm image; it is **not** shown to be software floating
point. A conservative FP32 reciprocal/multiply/add path can now avoid those
instructions when its rounded code is unambiguous; the existing double path
remains the fallback. [feature_disassembly.json](feature_disassembly.json) and
the two scoped Thumb logs show both paths. This is static evidence, not proof
of a speed improvement. Feature function code grows1856→2440B.

For normal finite reciprocal and input, with scaled magnitude≤65536 and validated
INT16 zero point, binary32 reciprocal/product/add errors are conservatively
bounded below0.012 code. A1/32-code guard around each half integer is wider than
that bound, so accepted values preserve nearest/ties-away integer rounding.
Subnormal inputs, non-normal/invalid reciprocals, extreme or non-finite products
and ambiguous half codes use the unchanged double implementation. Arbitrary
positive double scales remain supported. This bound assumes the ordinary
round-to-nearest floating environment used by the firmware; floating mode was
not changed. No finite-only/fast-math assumptions were introduced.

## Executed verification

- RED: [quantizer harness](20261108_213116_test_r2_optimization.py.log) fails before
  the fast-path helper exists. More importantly, the
  [probe regression](20261108_213250_test_deepfir_audio_release_probe.py.log)
  reproduces a deadline fault reporting all maxima/late count as zero.
- GREEN: focused tests after implementation and full
  [128-test regression](tests.log), no skips,29.438s. Includes real native C,
  independent quantizer/feedback references and scalar CMSIS DSP.
- Quantization:11,570,644 adversarial/random cases,786,432 accepted FP32 cases,
  and4,718,592 values at/adjacent to float/double guard edges: zero mismatches.
  Half-code fallback, invalid reciprocal and unchanged output canaries pass.
  Existing4096-hop PCM/shared-float frontend and15,400,960 feedback cases pass.
- Probe harness:60,000-hop completion, ordinary-build probe OFF, and fail-fast
  transport/sequence/invoke/process-deadline/submit/publish-deadline cases.
  Completed failed stages recorded once, no early UART, no restart/publication
  after stop. These are mock-transport tests, not compiled controller parity.
- Arm Compiler6.24: Debug, Release Probe, ordinary Release build/link exit0.
  [commands.json](commands.json) contains actual commands/exits; adjacent logs
  contain configure/build output. [mve.log](mve.log) confirms all six scoped
  linked vector kernels remain. Native compiler is GCC13.2 via the existing
  isolated Windows runtime; global toolchain/PATH was not modified.

## Images and memory

| Image | BIN bytes | SHA256 |
| --- | ---: | --- |
| Debug | 638560 | cd290d053bd68558f86c43a3b554cb8e1a132dd6d7c254292d9489b1b16491fb |
| Release | 615936 | 8fb1a0dcde5f1bd10b0a51fc019b0f9914d7c320c1981c1cf47f5af353f1e60f |
| Release Probe | 621696 | 7b58ec0a68353db5fdd66028567c853e808c7d99f368ad12722c577148a376da |

[images.json](images.json) records BIN/AXF/MAP hashes; [memory.json](memory.json)
records map regions/symbols. Ordinary Debug/Release RW_RAM stays77920/77168B;
probe77428B. Arena65536B, feature state2072B, DMA320B and stack8192B unchanged.
Model region0x20100000 remains516852B including alignment/metadata for the
516848-byte compiled artifact. Ordinary total RW90528/87728B remains unchanged.
Relative to the previous image, BIN grows448B Debug and544B Release; probe800B.
Map sizes are not actual stack high-water or full runtime memory verification.

Raw snapshots: `diagnostics/first_hop_deadline_20261108/213838/{debug,release,
release_probe}/`. Root outputs restored to ordinary builds; opt-ins OFF;
[restore.json](restore.json) confirms clangd uses build-debug. F5 Release Probe
will rebuild/re-enable the probe using the existing task. Source hashes in
[source_hashes.json](source_hashes.json) tie tested files to this evidence.
Console copies normalize line endings/trailing whitespace for Git checks;
[log_normalization.json](log_normalization.json) records raw/curated hashes and
the location of retained raw bytes. No test result or diagnostic value is edited.

## Acceptance status

| Gate | Status | Evidence / remaining boundary |
| --- | --- | --- |
| 1 Artifact integrity | PASS | Existing source-manifest/model-array regression passes; model/source packages and golden files untouched |
| 2 Architecture/source integration | PASS | Existing service boundaries and state owners retained; full regression |
| 3 Host numeric correctness | PASS | Exact integer comparisons and native pipeline fault tests above |
| 4 Debug/Release build/link | PASS | Commands, images and maps; Release Probe also built |
| 5 Raw controller replay rerun | NOT_RUN | No new Linux TF2.20 replay; previous package receipt is historical |
| 6 Compiled controller replay | BLOCKED | Existing runner prepared; no authorized compiled-backend execution in this session |
| 7 Board init/actual memory | NOT_RUN | User reports old image init/binding/6976B allocation/MVE PASS; candidate/stack high-water not run |
| 8 Full-hop deadline/schedule | NOT_RUN | User's previous first hop FAIL1079/1088us; candidate and continuous margin/b+2 measurement pending |
| 9 Physical audio/latency | NOT_RUN | No microphone-to-output continuity or physical latency measurement |
| 10 Power/energy | NOT_RUN | No matched physical current/energy comparison |

## Reproduce and next target run

Use the commands in [commands.json](commands.json) sequentially. Full host tests
on this Windows installation prepend the existing isolated compiler runtime:
`diagnostics/i2c_stuck_trace_20261108_162019/host_compiler_runtime/` to the child
process PATH. See the raw local `verify.py` for exact environment; no flash task
was invoked. [final_checks.json](final_checks.json) records passing
clang-format18.1.8, `rtk git diff --check`, unchanged tested source/image hashes,
restored flags/editor and local documentation links.

For a comparable user run, choose **M55M1: Release Probe: Flash + Debug (OpenOCD
Nuvoton)**, F5 then Continue, with no subsequent halt. Preserve both complete
`AUDIO_RELEASE_PROBE` and `AUDIO_RELEASE_RESULT` lines, including a first failure.
Compare the newly exposed frontend split before deciding the next optimization.
Require capture-to-publish<1000us for all60000 hops with no fault, sequence gap,
restart, underrun or overrun; a proposed900us maximum provides margin but has
not been measured. Do not relax the deadline or add a queue to pass.

A standalone comparison uses **M55M1: Flash Release Probe (APROM, verify)** then
whole-board power cycle after successful verify, avoiding debugger attachment.
CH549 has no nRESET wire; OpenOCD reset requests do not prove hardware reset.
Unchanged custom pins/recovery are documented in
[the optimization guide](../../audio_inference_optimization.md). No agent-run
flash or board change has been performed or authorized by this report.
