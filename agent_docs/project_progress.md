# Project Progress

## Continuous product finalization — 2026-11-09

Current result supersedes the earlier startup-fault snapshots below. User-supplied
Probe reached60000 hops: max total863us, capture-to-progress885us, publication884us,
zero deadline/model/submit/DMA/overrun/underrun faults, COMPLETE. Nominal60s from
sample count; STOP elapsed60233us is modular counter time, not run duration.
Flashed BIN identity was not supplied. Supplied-run timing subgate PASS; physical
slot/acoustic latency, power and final-image long soak remain unverified.
Snapshot/receipt: docs/verification/2026-11-08-board-probe-60000/.

The same pipeline now has only the fixed custom16k/hop16 audio implementation.
Removed legacy32k/scatter/warmup branches, untagged publication, write-only flag,
unused historical telemetry service/test, duplicate SRAM alias and dormant RTOS
build/link block. Kept current Debug logs, Release UART-off/WFI, finite Release
Probe, replay/capture tools, full recovery, strict1ms guard, two silences/b+2.
No model/DSP math, gain, pins, clocks, libs or source-package changes.

Verified134/134 tests, no skips. Actual C ordinary Debug/Release foreground
harnesses each pass60005 hops; driver register model passes60000 handoffs and
fault/rejection cases. These are host models, not hardware throughput/parity.
Arm6.24 Debug/Release/Probe builds and9 scoped Release/Probe MVE gates PASS.
All47 source artifact hashes and destination hashes match, array byte-exact;
one compiled image per BIN and one interpreter/core/device implementation.

Final BIN Debug643552B SHA1b7861e65c5c520d92a30ef642a5681dfdffabdc07a109bad7dd6d3f18565254;
Release618528B SHAc124cf7b37b38c1f9c0bf4fd0ace1c75cd400c4409f7bc0127ae28d13e61a044;
Probe625536B SHAe2c6cf28e912fdf070fee5ff57e3b624da78b2d7d6f5ce906a402775b33895a4.
RW_RAM remains78712/77168/78216B; model516852B, arena65536B inside RW_RAM,
DMA320B, stack8192B and heap2048B separately reserved. Static stack6656+Unknown
is not watermark. Earlier supplied Debug allocation6976/65536B is not a new
measurement of these images. Ordinary outputs/opt-ins OFF, editor Debug.

Handoff/commands/maps/hash/acceptance: docs/verification/2026-11-09-continuous-product/.
Local binaries/maps: diagnostics/submillisecond_runtime_20261108/fifo_build_001119/.
Next: final-image Probe retest with BIN hash, ordinary Release30-minute soak,
Debug boot/recovery, stack/physical audio/latency and power measurements when
authorized. Compiled parity BLOCKED without running backend; raw replay NOT_RUN.
Agent did not flash/reset/connect; deployment_approved=false. Light route,
no agents/explorer. Earlier entries remain historical snapshots.

## FIFO-aware product DMA handoff — 2026-11-08

New user trace: TX TD at4us and1021us, FIFO16/RXseq0 at both; software underrun
stops at1026us. TXUDIF bit16 clear. Driver incorrectly treats FIFO-transfer TD
as serial completion and requires audio before the first capture. No inference
timing in this log;38076us is boot self-test duration, not hop runtime. User BIN
identity not supplied. Archived exact trace with this limitation.

Product fix: CPU-prefill silence0 before TX, terminal BASIC DMA silence1, then
explicitly arm published audio2 for capture1; keep two silences/b+2/1ms guard.
No automatic silence prefetch after startup. Real I2S1 underflow IRQ uses the
existing driver callback, latches/counts/stops; health/publish also check flags.
DMA abort, pending/busy/wrong tags and copy-time underflow remain fail-stop.
Full reset required, no context retry. Legacy large diagnostic path retained.
Pins, clocks, RX DMA, model/package/libs/DSP unchanged; no device operation.

134/134 tests PASS, no skips, including actual C driver with60000 modeled
handoffs (not board hops). Old TD2-before-RX test reproduced FAIL before fix.
Arm6.24 Debug/Release/Probe builds and optimized nine-range MVE gates PASS.
RW_RAM Debug78712/Release77168/Probe78216B unchanged; arena65536/DMA320/stack8192.
Debug static stack6656B+Unknown, not watermark. Root outputs ordinary, opt-ins OFF,
editor Debug. Images/maps diagnostics/submillisecond_runtime_20261108/fifo_build_233653/.
Final BIN Debug643744 SHA a8888468ca85ca832581f69ed24aa6b11046a352eedb2783fb2f5f7e132c806e;
Release618688 SHA 6be144e428ba916b9852f51e4b574deb4f61bdce630be4fa71753c73b7a126d4;
Probe625696 SHA b6ead4bfeb58b033d6f7eff15a3bca35e7ed9bed30d0ee124c7f916fa4a165d6.

Evidence/reproduction: docs/verification/2026-11-08-fifo-dma-handoff/.
No claim of physical startup fix, continuous timing or b+2 alignment yet. Next:
same Release Probe profile F5/Continue, save BIN hash and every startup/summary/
result line. Expect no immediate4us TD; first DMA task transfers silence1 near
1ms. Require60000 fault-free real hops,<1000us,target <=900us; RX/serial phase
still needs measurement. Compiled parity BLOCKED, raw replay not rerun; new board
allocation/stack/audio/latency/power NOT_RUN. Light route, no agents/explorer.

## Submillisecond runtime implementation — 2026-11-08

User approved implementation after6967118. Added exact MVE Q31 h/c feedback,
guarded batched feature quantization and vector finite/roll/taper reconstruction.
Generic fallbacks, encoded state, model/DSP contract and1ms/b+2 guards retained.
Startup exhaustive feedback self-test uses scratch, never live state. Debug/Probe
record at most12 raw FIFO/TD/IRQ events and format after stop; ordinary Release
excludes trace. No pin/clock/DMA configuration, start-order, queue or libs changes.

Verified132/132 native tests, no skips, plus6/6 focused checks. Arm6.24 builds
Debug/Release/Probe;9 scoped optimized MVE ranges pass for Release/Probe. Debug
links; its unoptimized out-of-line FFT helpers make the optimized wrapper-range
gate inapplicable. Native lane model tests production vector branches, not Arm
hardware. DSP-only canonical gain/phase replay with synthetic PCM covers26441
hops; max output error2.98e-7,136 PCM differences <=1 against historical libm.
This is not compiled-controller parity; existing float tolerance unchanged.

BIN Debug642752B SHA c97208b2714337387395c177337dd1cad9cefec2ae07aa0e3634ed61550d0e03;
Release617696B SHA ad63d63fc2749c69c7ce80b799b8768483ebd5e05eb33a443c39c37903a2d5ab;
Probe624736B SHA 1df3d97b31b6452c360d5512a892bcf8c684732fa02616e75bda41bbb53bbb80.
RW_RAM Debug78712/Release77168/Probe78216B; Release unchanged. Arena65536,
DMA320, stack8192B unchanged. Debug static stack6656B+Unknown is not a watermark.
New-image allocation/stack/target MVE/timing/audio/latency/power NOT_RUN;
compiled parity BLOCKED without backend; raw Linux replay not rerun.

Evidence/reproduction: docs/verification/2026-11-08-submillisecond-runtime/.
Raw attempts/images/maps: diagnostics/submillisecond_runtime_20261108/225414/.
Builds caught LPDSCT field and microlib nextafterf dependency defects; corrected
and all profiles rebuilt. Ordinary outputs restored, opt-ins OFF, editor Debug.
Light route, no agents/explorer; no package/golden/approval change or device flash.

Next user run: existing Release Probe OpenOCD Flash+Debug profile, F5/Continue
without later halts. Keep all AUDIO_STARTUP and both complete result lines with
BIN hash. Require60000 fault-free hops,<1ms,target <=900us. Old seq0 underrun
remains unresolved; establish FIFO/TD phase before changing transport. No new
speedup or continuous schedule acceptance claimed. Earlier entries are snapshots.

## Submillisecond pipeline analysis — 2026-11-08

Latest user Release Probe stops with frame0/capture_seq0 and DAC underrun1;
no stage timing measures the guarded FP32 image yet. Historical reports do show
submillisecond service/e2e for older models, but also underrun1 and different
artifact identities. R2 adds nonidentity feedback on both lanes and gain/phase
reconstruction. Earlier R2 single-hop1088us needs at least89us reduction to <1ms,
or188us for a900us margin target; this is not a new board measurement.

Native arithmetic proof of nonnegative-lane rounded Q31 feedback matches the
current C reference for all131072 h/c INT16 cases, max error0. Candidate MVE
instructions are not implemented/executed. Source inspection identifies FIFO
prefetch versus DMA-TD playback epoch as a startup hypothesis; simplified model
is illustrative, actual request rule/phase unconfirmed. Next: bounded raw
startup FIFO trace, then exact MVE feedback and measured frontend/reconstruction
optimization; retain one owner, b+2,1ms deadline and existing parity requirements.

Analysis: docs/pipeline_submillisecond_analysis.md; native evidence:
docs/verification/2026-11-08-pipeline-budget/. Runtime/model/build/pins/clocks/DMA
unchanged; fresh128/128 host tests pass, no skips. No flash or target timing.
Light route, no agents/explorer.

## First-hop deadline investigation — 2026-11-08

User reports F5→Continue without later halt. Previous Release Probe first hop
FAIL: inference1079us, capture-to-deadline1088us, seq1/playback_slot2. Debug init,
binding,6976/65536B arena and MVE tests report OK, then seq0 transport underrun.
User observations have no BIN hash; Debug cause and intermittent I2C remain open.

Fixed missing completed-hop statistics on process/submit/publish fault exits;
probe now reports last pre-FFT/FFT/post-FFT/NPU/bridge/other stages after stop.
Added private guarded FP32 feature quantization with unchanged double fallback.
Existing11,570,644 comparisons plus786,432 accepted FP32 and4,718,592 guard-edge
cases have zero integer mismatches.128/128 tests, Arm6.24 Debug/Release/Probe
build/link and scoped MVE checks pass. Ordinary RW_RAM77920/77168B unchanged.
New BIN Debug638560B SHAcd290d053bd68558f86c43a3b554cb8e1a132dd6d7c254292d9489b1b16491fb;
Release615936B SHA8fb1a0dcde5f1bd10b0a51fc019b0f9914d7c320c1981c1cf47f5af353f1e60f.
Probe621696B SHA7b58ec0a68353db5fdd66028567c853e808c7d99f368ad12722c577148a376da.
Ordinary root outputs/opt-ins OFF and Debug editor restored. Evidence:
docs/verification/2026-11-08-first-hop-deadline/; guide updated.

No device operation; candidate timing benefit, continuous1ms margin/b+2, physical
audio/power and stack watermark NOT_RUN. Compiled parity remains BLOCKED without
authorized execution; raw Linux replay not rerun. No package/golden/libs, board,
DMA/cache/pins/clocks or approval changes. Next: user retries same Release Probe,
preserves both lines and compares frontend split; no claim of resolved deadline.
Light route, no subagents/explorer.

## Release Probe OpenOCD profile — 2026-11-08

Added Release Probe Flash + Debug and standalone APROM flash task. Actual task
chain configures canonical Release, enables probe, then build-only in build/;
ordinary Release resets probe OFF. No extra build directory/preset or hardware
changes. Verified128/128 host tests, probe build and ordinary Release restore;
probe BIN620896B SHA7a6df1b8e6655216d1bae78d6f62289e74278713b9bf8dc8081efb87d7482e61.
Editor restored to Debug. Evidence: docs/verification/2026-11-08-release-probe-profile/.
VS Code F5 UI, target flash and physical timing NOT_RUN; no device operation.

## Continuous audio/inference optimization — 2026-11-08

Approved approach1 implemented inline, no agents: adapter normalizes PCM once and
frontend writes current context directly; guarded reciprocal quantization and
nonnegative magnitude-shift feedback preserve tested integer results. Removed
redundant spectrum initialization/MVE tap copy; reconstruction uses installed
CMSIS sin/cos with explicit radians/degrees and safe libm fallback. Existing
complex FFT retained after real-FFT candidate failed the feature-code budget.
Debug raw16-event latest-hop trace reports only with DMA stopped; PCM timing and
60,000-hop fail-fast Release probe extend existing instrumentation. Ordinary
recovery still full-resets/restarts; normal Release UART remains off.

Verified127/127 host tests, including11,570,644 exact quantizer and15,400,960 exact
feedback comparisons; both Arm6.24 builds, probe/replay links and final scoped MVE
checks pass. Final Release RW_RAM77168B (-768), Debug77920B (-536); arena reserved
65536B, uncached DMA320B and stack8192B unchanged. Final Debug BIN638112B
SHAf0a6e7623900e34c5d8e19cc06ac1deaf386c8e0874b876cd0ebc744048f6477;
Release BIN615392B SHA72ea67e693846caa642286c2a7e9e90ecfaf03f196b3542f9fc5867384e2acbf.
Release BIN grows2592B; no claimed cycle/power saving from static code changes.

Canonical gain/phase DSP replay with synthetic PCM covers26441 hops: max float
output error2.9802322e-7,136 PCM samples differ by at most1 code. This is neither
controller compiled parity nor physical quality evidence. Compiled execution
BLOCKED without an authorized backend; raw Linux receipt not rerun. No flash,
board init/actual arena/stack, full-hop deadline, acoustic latency or power run.
Model/package/golden/libs/pins/clocks unchanged; deployment_approved remains false.
Both ordinary build caches restored with opt-ins OFF; editor uses Debug.

Guide: docs/audio_inference_optimization.md. Acceptance/commands/hashes/memory:
docs/verification/2026-11-08-audio-optimization/. Next: authorized compiled replay,
actual target memory/MVE checks, matched60s Release timing and physical audio/power.
Intermittent board_status=5 root-cause evidence remains open independently.

## I2C_STUCK recovery trace — 2026-11-08

Implemented the approved firmware-only trace for intermittent board_status=5 on
custom PA4 SDA/PA5 SCL. Board retains raw reset/GPIO/I2C/timebase observations and
up to22 recovery samples; transport exposes a read-only borrowed pointer even on
init failure; service prints only after recovery, before audio starts. Each init
invalidates the prior trace. Nine-pulse/5us/STOP policy and pin/clock/DMA assignments
remain unchanged. Historical NuMaker wiring is explicitly separated in driver docs.

Verified123/123 host tests, native six-case C recovery and failed-init UART tests,
clang-format18.1.8 and Debug/Release configure/build/link exit0 with Arm6.24.
Debug BIN634400B SHA3704fe5ab26ffed171174837ab2833c7464cad5d09a344545141a2205ad24b82;
trace284B, RW_RAM78456B, formatter stack frame1688B (static map, not high-water).
Release BIN612800B SHAe2f535d2e0b2b7a407941fd5a3ecb4c6575f41f6e5cecb1a3276ce3f3922a34f
and AXF remain identical to the prior verified revision; trace/log additions absent.
Evidence: docs/verification/2026-11-08-i2c-stuck-trace/.

No flash/device operation. Physical cause and permanent-fix acceptance remain
NOT_RUN pending user Debug traces from repeated OpenOCD and power-cycle workflows.
Probe has no physical nRESET; software reset requests do not prove reset occurred.
Guide: docs/i2c_stuck_trace.md. Deadline/underrun investigation remains separate.

## Product source cleanup and style — 2026-11-08

Removed four obsolete diagnostic applications, their standalone CMake roots and
exclusive services/board/driver/common modules, the unused passthrough/stream
wrappers and old register-experiment scripts. app/main.c is the sole entry point.
Product capture-start coverage remains; tests only for removed features were
retired. Project C/C++ uses the checked-in clang-format style and Doxygen headers,
author Bui Minh Huy, plus explicit buffer/lifecycle/numeric rationale. Vendor and
model artifacts remain unchanged. Token comparison preserved executable logic
apart from documented constant renames and duplicate include removal.

Verified120/120 host tests and Debug/Release configure/build/link with Arm6.24.
Training packages are retained locally and ignored by Git. Imported model hashes
are tested without those packages; synthetic replay protocol/comparator tests use
a small hash-checked reference fixture. The full source/golden audit still runs
when the local package is available and is explicitly skipped when it is absent.
Git-index checkout without training folders:119 PASS and1 explicit source-audit
SKIP out of120. Formatting48 files passed.
Curated evidence: docs/verification/2026-11-08-source-cleanup/.

No flash. Runtime compiled parity, power, timing and physical audio remain open;
build/style evidence grants no deployment approval. Local raw evidence and removed
source archives remain under diagnostics/source_cleanup_20261108_143748/.


## Continuous low-power update — 2026-11-08

Implemented the approved approach1 on custom PCB: continuous220MHz/16kHz/hop16
model path; shallow CPU WFI after atomic capture/transport-health recheck; normal
Release UART clocks/IRQs and console link off; Debug UART2 retained. Board gates
only named unused peripherals. External ACLK/2 SysTick preserves wall time through
WFI/WFE in core-cycle units (2-cycle resolution, 25-bit wrap); board/codec delays
share that counter and preserve protected-register ownership. Explicit DAC unused
blocks remain off, with PRB_P7/headphone/gain unchanged. DMIC already uses LPPDMA0;
I2S1 is absent from LPPDMA request map, so playback retains PDMA0.

Verified136/136 tests, both Debug/Release builds exit0 (Arm6.24), Release DSB/WFI/ISB
Thumb code, no linked Release console, unchanged model identity/arena/DMA/RW sizes.
Debug BIN627360B SHA7cc8260d35e3f227a65a84985939e29b39e058fe8ce8843f363fd14d36839fe6;
Release BIN612736B SHAd405e68bb2a8b37e255bbf4cd9f9bdd8f0ecb5691aa69612ae73337e129b8bca.
Evidence: diagnostics/continuous_low_power_20261108_122920/verified_builds/,
power_link_audit.json, 20261108_124949_regression.log and
20261108_124907_build_receipt.json. GCC DLL failures were isolated with unchanged
host compiler copies and host-only -B/-static; no toolchain/libs edits.

No flash or board manipulation. Current/power saving, wake jitter, full-hop margin,
actual allocation/stack and physical audio remain NOT_RUN; compiled replay remains
BLOCKED. No deployment approval. Baseline images are preserved for matched Release
A/B after separate flash authorization. See docs/continuous_low_power.md.

## Current integration — 2026-11-08

Custom PCB is now the sole product target. Integrated the supplied R2 gain/phase
controller into model import, strict three-input/four-output binding, exact
encoded h/c feedback, FFT frontend and FIR reconstruction/filter/crossfade/PCM.
One interpreter and one owner per history; no source-package or libs edits.
Foreground faults stop/flush/full-reset/restart with two silence slots. Tagged
DMA submission checks sequence/slot/ownership; timestamps and counters are available.

Verified:47 source artifact hashes, destination hashes and byte-identical compiled
array;132/132 host tests (native C, actual CMSIS scalar FFT and regression gates);
Debug/Release configure/build/link exit0 with Arm Compiler6.24; single compiled
model/runtime/core link; static map audit; git diff check recorded in acceptance.
Debug/Release production scripts and presets restore replay/capture/probe OFF.
Removed15 redundant build directories and4 wrappers after preserving inventory,
binary/map/probe evidence. Only root build-debug and build remain.

Compiled replay runner/comparator and mailbox firmware build are ready. Execution
is BLOCKED without an authorized running backend; no compiled mismatch count is
available. The source Linux raw receipt is historical PASS, not a session rerun.
No WSL Linux distro is installed. Board boot, actual64KiB arena allocation/stack,
DMA physical alignment, full-hop<1ms margin, soak and physical audio/latency are
unverified. No board was flashed; deployment_approved remains false.

Static model+metadata516852B; Debug/Release RW_RAM78168/77936B, uncached DMA320B,
stack8192B. These linked values do not prove runtime arena use or deadline.
Next: separately authorize the exact replay image/target and recovery wiring,
run26441 canonical hops to zero mismatch, inspect actual allocation/stack, then
measure product stage/total timing, b+2 slots and physical audio on custom PCB.

Handoff: docs/r2_custom_integration.md.
Evidence: diagnostics/r2_custom_integration_20261108_105000/acceptance.json,
final_regression.log, profile_restore_build_receipt.json and final_audit/.
Route Light; no workers/explorer, no commit/reset/clean, no retraining/PTQ/Vela.

## Earlier session records

## Model reboot baseline - 2026-09-13

- Status: model-free PCM16 passthrough baseline implemented; no active or deployment-approved neural model.
- Archive gate: external snapshot, inventory, binary Git patch, and SHA-256 verification completed with `ARCHIVE_VERIFIED=1`.
- Runtime change: model-specific execution and legacy model services/packages were removed; DMA, static capture buffers, queueing, and optional DAC transport remain.
- Build policy: `M55M1_MODEL_PACKAGE_DIR` is empty by default and TFLM is disabled. A future package is opt-in only through `integration_manifest.json` and `m55m1_model_package.cmake`.
- Verification: passthrough vectors and pipeline, model-free CMake policy/configure, retained-source audit, hygiene/style/full host suite (`58/58`), and Debug/Release build/link all passed. Final `git diff --check` passed.
- Build evidence: Debug and Release each completed `826/826`, exit `0`, and produced AXF/map/bin artifacts before those generated directories were removed.
- Board flashing is not a cleanup acceptance gate. Do not infer deployment from host tests, placement, or link success.

## Ethos-U55 runtime foundation - 2026-09-14

- Status: model-neutral runtime foundation implemented on branch `codex/ethosu-runtime-foundation`; PCM16 passthrough remains the default.
- Board/runtime: added `board/m55m1_ethosu_board.*`, static single-flight `drivers/ethosu_runtime/*`, opt-in CMake switches, TFLM bridge, and model-free smoke target. No `libs/` files were edited.
- BSP provenance: retained Nuvoton/Arm Ethos-U driver version 0.16.0; source/header SHA-256 values are recorded in `docs/ETHOSU_RUNTIME_FOUNDATION.md`.
- Verification: temporary-mock host runtime contract and full host suite (`66/66`) pass; model-free Debug/Release configure and build/link pass; runtime-enabled Debug/Release configure and build/link pass; TFLM bridge compiles with Armclang C++11; model-free link-map audit finds no Ethos-U runtime/core objects; `git diff --check` passes.
- Remaining boundary: no valid command stream, model numerical parity, physical board smoke, realtime/soak, capture-to-DAC, or deployment approval. Board execution remains `BLOCKED_BOARD_ACCESS` until hardware is available.

## 2026-11-08 — Debug init and first-hop stage diagnostics

- Added foreground Debug checkpoints and exact expected/actual tensor metadata in the inference service; audio pipeline logs board progress, transport report, init/start failures and the first capture. Runtime first-hop markers and fault reports never block on UART; repeated fault categories are suppressed.
- Native C/C++ checks and full host regression passed 121/121, no skips. Debug/Release configure/build/link passed with Arm Compiler 6.24. Debug BIN 631680 B, Release BIN 612800 B; all six new trace marker strings are absent from Release. Evidence: `docs/verification/2026-11-08-debug-stage-logging/`.
- User supplied the earlier UART allocation line (arena_used=6976/65536, expected compiled hash); this is user-reported allocation evidence only. The subsequent board failure remains unidentified until the new Debug log is captured. No flash, board timing/power/audio measurement or compiled parity run was performed by the agent.
