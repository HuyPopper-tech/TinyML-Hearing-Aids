# Project Progress

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
