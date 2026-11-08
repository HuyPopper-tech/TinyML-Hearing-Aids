# Latest Session Work

## Continuous custom audio delivery — 2026-11-09

User's latest supplied Probe completed60000 hops with total max863us, capture-to-
progress max885us, publish max884us and all fault/deadline counters0. First TX TD
1066us follows RX TD1048us/capture take1051us, unlike the old seq0 prefetch fault.
Associated candidate99c617b; flashed hash NOT_SUPPLIED. This is user-reported
timing acceptance for that run, not final-image identity, physical latency or
compiled-controller parity. STOP60233us is modular SysTick elapsed, not wall time.
Preserved exact log/hash/parser in docs/verification/2026-11-08-board-probe-60000/.

Completed shared ordinary Debug/Release delivery and cleanup. One custom16k/
hop16 pipeline/terminal FIFO-aware driver; ordinary service has no60000-hop
stop. Kept Release Probe's finite stopped timing report and current Debug
init/fault/periodic UART logs; ordinary Release UART stays OFF. Removed unused
32k/scatter/warmup branches, untagged submit, write-only flag, historical
uncalled telemetry source/header/test, obsolete SRAM alias and RTOS build block.
DSP/model, gain, pins/clocks, libs, packages, replay/capture tools, historical
evidence and unrelated user files preserved. No device operation.

134/134 tests PASS/no skips, including60005 ordinary Debug/Release foreground
hops each,60000 modeled driver handoffs, current invalid-rate/tag and fault cases.
The removed private telemetry/scatter tests exercised deleted code; current
numeric/recovery assertions remain. Arm6.24 Debug/Release/Probe build/link PASS;
Release/Probe9-range MVE checks PASS.47 source artifact/destination hashes and
C-array/compiled bytes match, one model/core/device/service linked.

Final BIN hashes: Debug643552B1b7861e65c5c520d92a30ef642a5681dfdffabdc07a109bad7dd6d3f18565254;
Release618528Bc124cf7b37b38c1f9c0bf4fd0ace1c75cd400c4409f7bc0127ae28d13e61a044;
Probe625536Be2c6cf28e912fdf070fee5ff57e3b624da78b2d7d6f5ce906a402775b33895a4.
RW_RAM Debug78712/Release77168/Probe78216B unchanged; arena65536 within RW_RAM,
model SRAM516852, DMA320, stack8192, heap2048B separately. Static stack6656+
Unknown is not watermark; earlier user Debug arena6976/65536 not final-image
measurement. Root images ordinary/diagnostics OFF, editor Debug. Binaries/maps
in diagnostics/submillisecond_runtime_20261108/fifo_build_001119/; curated
commands/logs/nine acceptance gates in docs/verification/2026-11-09-continuous-product/.

Next: retest final Probe with BIN hash, ordinary Release30-minute soak and Debug
boot/recovery. Debug has no measured1ms guarantee; strict deadline guard retained.
Compiled parity BLOCKED without authorized running backend; raw replay NOT_RUN;
new-image board/stack/audio/physical latency/power NOT_RUN. No flash by agent,
deployment_approved=false. Light route, no agents/explorer. Older entries below
are historical and do not override this status.

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

Analysis-only follow-up to6bf6b37. Latest user log fails before any capture/hop
(frame0,seq0,DAC underrun1); no timing benefit of guarded FP32 is established.
Historical subms logs are valid limited evidence, with different model hashes
and one underrun, not acceptance of the current transport. R2's extra CPU work
is both state requantizations and gain/phase reconstruction. Old1088us capture-
to-deadline baseline needs89us reduction to <1ms,188us for target900us.

Compiled/executed native C exhaustive formula proof: h/c65536 codes each,
131072 exact matches, zero max code error. Positive-magnitude Q31 multiplication
can express both feedback shifts while preserving ties-away; MVE implementation
and speed remain NOT_RUN. DMA TD marks FIFO transfer, not serial completion;
TX threshold8/16-level FIFO motivates startup phase tracing, not a proven fix.
Illustrative FIFO model must not be reported as hardware timing.

Detailed priority/budget/acceptance: docs/pipeline_submillisecond_analysis.md.
Evidence: docs/verification/2026-11-08-pipeline-budget/; raw native proof/Thumb
disassembly: diagnostics/pipeline_budget_20261108/221230/. Next: capture first
TX/RX/TD/FIFO epochs without live ISR logging, then exact feedback MVE, frontend
split and reconstruction profiling. No runtime/config/package/golden/libs/DMA
changes, no new firmware build, no device operation. Compiled parity and board
deadline/audio/power remain open. Fresh128/128 host tests pass, no skips; source
and contract hashes match. Light route, no subagents/explorer.

## First-hop deadline investigation — 2026-11-08

User confirmed F5→Continue, no later Pause/step. Previous UART Release first hop
FAIL fault3:1079us inference,1088us capture-to-deadline,seq1/slot2. Debug init,
binding,6976B actual allocation and MVE self-tests report OK, then underrun seq0.
This is user-reported old-image evidence without BIN hash, not a candidate run.
Neither OpenOCD nor DMA is proven the cause of Debug's early fault.

Implemented a guarded FP32 quantizer fast path, preserving existing double
fallback at ambiguous half codes/extremes. Source quantization contract unchanged.
Fixed probe failing-hop aggregates, snapshotting before reset and updating after
DMA stop. Added last frontend split/NPU/bridge/other fields. Integer comparisons:
11,570,644 existing +786,432 accepted float +4,718,592 guard-edge, zero mismatches.
128/128 host tests, all three Arm6.24 builds and scoped MVE disassembly pass.

Debug BIN638560B SHAcd290d053bd68558f86c43a3b554cb8e1a132dd6d7c254292d9489b1b16491fb;
Release BIN615936B SHA8fb1a0dcde5f1bd10b0a51fc019b0f9914d7c320c1981c1cf47f5af353f1e60f;
Probe BIN621696B SHA7b58ec0a68353db5fdd66028567c853e808c7d99f368ad12722c577148a376da.
RW_RAM remains77920/77168B ordinary Debug/Release; arena65536B/DMA320B/stack8192B.
Images/maps archived at diagnostics/first_hop_deadline_20261108/213838/; curated
commands/acceptance: docs/verification/2026-11-08-first-hop-deadline/. Root outputs
ordinary, opt-ins OFF; editor Debug. No device connection/flash/board changes.

Next: same Release Probe profile F5→Continue, keep both complete lines, compare
pre-FFT/FFT/post-FFT before further optimization. Candidate timing/steady-state,
physical audio/latency/power and stack watermark NOT_RUN. Compiled parity BLOCKED
without authorized backend execution; raw replay not rerun. Guarded FP32 is host-
verified, not a measured speed/energy saving. Intermittent board_status5 and Debug
seq0 underrun remain open. No model/package/golden/libs/pin/clock/DMA/cache changes,
no deployment approval, no subagents/explorer. Preserve unrelated untracked files.

## Release Probe OpenOCD profile — 2026-11-08

VS Code now has M55M1: Release Probe: Flash + Debug (OpenOCD Nuvoton), plus
M55M1: Flash Release Probe (APROM, verify) for standalone timing. Both use the
same build/ AXF/BIN and ordered configure Release → probe ON → build-only tasks.
Ordinary Release still forces probe OFF. No new preset/build directory or board
changes. Verified128/128 tests and the actual build task commands, then restored
ordinary Release image615392B and Debug editor. No F5 UI/target flash/device run.
Evidence: docs/verification/2026-11-08-release-probe-profile/; usage and CH549
reset limits: docs/M55M1_OPENOCD_DEBUG.md. Device acceptance remains open.

## Continuous audio/inference optimization — 2026-11-08

Implemented the approved approach1 with one existing pipeline/interpreter and
state owner: shared normalized PCM, direct current feature-context writes,
guarded reciprocal quantizer, exact magnitude-shift feedback, redundant buffer
work removal and installed CMSIS sin/cos with radians/degrees conversion/fallback.
The evaluated real FFT failed the feature-code tolerance and was rejected.
DMA/cache/pin/clock code and model/package/goldens/libs remain unchanged.

Debug now records the latest hop as16 bounded raw events; fault recovery stops
DMA before formatting/draining the trace. This supersedes live first-hop and
first-capture messages below; init logs and periodic Debug telemetry remain.
PCM stage timing was added. Opt-in Release probe observes60000 hops, stops on
the first fault and reports COMPLETE/FAIL plus sequence/slot/stage information;
ordinary builds retain synchronized reset/restart and silent Release UART.

Verified127/127 host tests;11,570,644 quantizer and15,400,960 feedback comparisons
have zero mismatches. Actual scalar CMSIS phase sweep max tap error2.7351718e-7.
Long DSP comparison uses both canonical gain/phase traces with synthetic PCM:
26441 hops, max output float error2.9802322e-7,90/46 PCM changes of at most1 code.
It does not invoke the controller or execute target MVE. No tolerance was loosened.

Arm6.24 Debug/Release and probe/replay variants build/link, final linked MVE check
passes. Release RW_RAM77936→77168B; Debug78456→77920B; reserved arena65536B,
DMA320B, stack8192B unchanged. Debug linker main estimate6560B is not high-water.
Final Debug BIN638112B SHA
f0a6e7623900e34c5d8e19cc06ac1deaf386c8e0874b876cd0ebc744048f6477;
Release BIN615392B SHA
72ea67e693846caa642286c2a7e9e90ecfaf03f196b3542f9fc5867384e2acbf.
Root outputs restored to ordinary builds, opt-ins OFF; clangd database uses Debug.

Evidence: docs/verification/2026-11-08-audio-optimization/; raw images/maps/logs:
diagnostics/audio_optimization_20261108_180148/. Reproduction, device pins/recovery
and timing/power procedure: docs/audio_inference_optimization.md.

No flash/device operation. Compiled runner reports BLOCKED without authorized
backend; Linux raw replay not rerun. Target initialization/actual memory/MVE,
full-hop<1ms margin, physical continuity/latency and energy savings remain NOT_RUN.
Do not claim board_status=5 fixed or deployment approval. Next: separately
authorize exact replay/probe image and CH549-without-nRESET recovery, run compiled
26441-hop parity, then matched60s timing and physical audio/current measurements.
No subagents/explorer were used; unrelated untracked files/packages are preserved.

## I2C_STUCK recovery trace — 2026-11-08

Approved design implemented: fixed284B Debug board snapshot of reset flags, GPIO
configuration, I2C0 state/timebase and up to22 phase samples. Init failure preserves
the trace through transport; service logs after recovery while audio is stopped.
No changes to PA4/PA5, nine recovery pulses, nominal5us delay, STOP or DMA/clocks.
Trace documents sampled logic and sticky flags rather than claiming waveforms/reset.

Verified123/123 tests (including actual C recovery and UART failure logging),
formatting and both Arm6.24 builds. Debug image634400B SHA
3704fe5ab26ffed171174837ab2833c7464cad5d09a344545141a2205ad24b82.
Release BIN/AXF are identical to the previous revision and exclude trace storage/logs.
Evidence: docs/verification/2026-11-08-i2c-stuck-trace/; reproduction/decoder:
docs/i2c_stuck_trace.md. Debug RW_RAM78456B; formatter's static stack frame1688B,
with8192B configured stack; actual high-water has not been measured.

No flash or device manipulation. Next: user captures complete I2C groups from the
failing OpenOCD workflow and comparable power cycles, then correct the demonstrated
cause and repeat both workflows. Physical root cause/permanent fix NOT_RUN; runtime
deadline/underrun remains open. No deployment approval change. No subagents used.

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

Completed the model-neutral cleanup baseline on 2026-09-13.

Verified: external archive SHA-256 gate; explicit legacy target deletion; stateless PCM16 passthrough vectors; model-free CMake package policy; retained-source audit; repository hygiene/style gates; full host suite (`58/58`); Debug/Release configure; Debug/Release build/link (`826/826`, exit `0` each); and `git diff --check`. Generated build output was removed after evidence capture. No board flashing is requested for cleanup.

## Runtime foundation implementation - 2026-09-14

Implemented on `codex/ethosu-runtime-foundation` without staging existing dirty
files. The archive preflight was verified before edits. The new board binding
owns M55M1 clock/reset/IRQ setup; the runtime owns static core state, validation,
cache hooks, IRQ forwarding, health/timing stats, and lifecycle. CMake excludes
all BSP NPU sources by default and adds only the two required core files when
runtime opt-in is enabled. TFLM is additionally opt-in and uses the project
bridge. The standalone smoke image has no audio/model source.

Verified: host runtime mock contract and full host suite (`66/66`); runtime/CMake static contracts; model package duplicate-core/IRQ guard; model-free Debug/Release configure and build/link; runtime-enabled Debug/Release configure and build/link; Armclang C++11 bridge compilation; model-free link-map audit with no Ethos-U runtime/core objects. No board was flashed or executed; no model package or valid command stream was introduced. `git diff --check` passed.

## 2026-11-08 — Debug init and first-hop stage diagnostics

- Added foreground Debug checkpoints and exact expected/actual tensor metadata in the inference service; audio pipeline logs board progress, transport report, init/start failures and the first capture. Runtime first-hop markers and fault reports never block on UART; repeated fault categories are suppressed.
- Native C/C++ checks and full host regression passed 121/121, no skips. Debug/Release configure/build/link passed with Arm Compiler 6.24. Debug BIN 631680 B, Release BIN 612800 B; all six new trace marker strings are absent from Release. Evidence: `docs/verification/2026-11-08-debug-stage-logging/`.
- User supplied the earlier UART allocation line (arena_used=6976/65536, expected compiled hash); this is user-reported allocation evidence only. The subsequent board failure remains unidentified until the new Debug log is captured. No flash, board timing/power/audio measurement or compiled parity run was performed by the agent.
