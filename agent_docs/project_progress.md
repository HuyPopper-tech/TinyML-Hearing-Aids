# Project Progress

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