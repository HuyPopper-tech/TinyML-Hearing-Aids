# Latest Session Work

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
