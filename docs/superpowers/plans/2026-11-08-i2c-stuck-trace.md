# I2C stuck trace Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Native execution; no subagents, per user instructions.

**Goal:** Preserve enough Debug evidence to distinguish GPIO ownership, observed SCL/SDA levels and recovery progress behind board_status=5.

**Architecture:** Board owns a fixed trace; transport exposes a borrowed read-only pointer valid until the next init; service formats it only after recovery. Shared portable diagnostic types live in common/. Release excludes the trace and its UART formatting.

**Tech Stack:** C11, existing Nuvoton register definitions, Python unittest/native GCC harness, Armclang/CMake scripts.

**Spec:** ../specs/2026-11-08-i2c-stuck-trace-design.md (approved in chat).

## Global Constraints

- Preserve PA4/PA5, pulse/delay/recovery policy and all clocks/DMA assignments.
- No UART between recovery edges; no allocation; no libs/model/package edits.
- No flashing or board manipulation. No claim of physical root cause without device evidence.

## Review Focus

- SCL low must remain visible even when SDA is high (Task 1).
- Capacity includes all nine low/high phases and STOP phases (Task 1).
- Earlier clock failures must not expose a stale previous trace (Task 1).
- A board-init failure must propagate its trace before the early return (Task 2).
- Release must contain neither detailed trace storage nor UART marker strings (Task 3).

### Task 1: Record actual recovery observations

Files: common/i2c_recovery_trace.h; board/m55m1_audio.c/.h; tests/test_i2c_recovery_trace.py.

Interfaces: board getter returns const i2c_recovery_trace_t * or NULL; static board-owned storage, foreground-only, valid until next board init. Event capacity 22 (release, 18 clock phases, three STOP phases).

- [x] Add native tests extracting actual recovery/helpers and modeling line transitions; assert idle, release at pulse 3/9, persistent SDA/SCL, STOP order, capacity, register retention and unchanged delay count.
- [x] Run and preserve expected initial failure.
- [x] Implement bounded Debug snapshots: raw reset flags, GPIO mux/mode/latch/input path/pulls, I2C control/status/divider and timebase source/load; timestamp phases without reading side-effecting SysTick CTRL.
- [x] Run native tests; compare Release recovery behavior with Debug and check all init callers.

### Task 2: Deliver diagnostics on failed initialization

Files: drivers/audio_transport/audio_transport.c/.h; services/audio_pipeline/audio_pipeline.c; tests/test_deepfir_audio_runtime_debug.py; tests/test_i2c_recovery_trace.py.

Interfaces: Debug audio_transport_init_report_t gains const i2c_recovery_trace_t *i2c_trace; borrowed board lifetime, NULL when recovery not attempted.

- [x] Extend native pipeline tests to fail board init with a populated trace and require summary/phase/final-line logs before failure; assert no audio running during trace formatting.
- [x] Implement getter propagation before board-status early return; service logs init snapshots and every captured event after recovery only.
- [x] Verify NULL trace, failure and successful paths, no per-hop logging changes.

### Task 3: Verify and hand off

Files: docs/driver/tlv320dac3100.md; docs/verification/2026-11-08-i2c-stuck-trace/; agent_docs/project_progress.md; agent_docs/latest_session_work.md.

- [x] Run all repository unittests and git diff --check, retaining command/exit logs.
- [x] Configure/build Debug then Release sequentially; inspect maps/binary strings and record image hashes.
- [x] Self-review layers, getter lifetime, sticky reset interpretation, no side-effecting diagnostic reads, excluded Release storage and unmodified recovery policy.
- [x] Document decode/reproduction procedure, custom wiring versus historical NuMaker wiring, and device execution NOT_RUN.
- [x] Selectively commit/push authorized changes, excluding unrelated files and training packages.

## Execution ledger

- 2026-11-08: Design approved by user. Debug configure exit 0 before source edits.
- Ruling: expose a borrowed pointer instead of copying the trace into stack-heavy init reports; board owns storage and service consumes it synchronously before another init. No additional queue/framework.
- Ruling: stay in the user's current checkout and execute alone; preserve unrelated untracked files and existing build workflow. No fresh reviewer agent due explicit no-subagent instruction.
- Task 1 complete: native C recovery passed all six bus cases;22-event capacity, first uncommanded-low SCL, retained registers and unchanged delay count checked.
- Task 2 complete: board failure carries its snapshot and UART emits it before transport FAIL; normal NULL snapshot remains accepted. Final full suite123/123 PASS.
- Task 3 verification complete: Debug/Release configure/build exit0. Debug trace284B; Release BIN/AXF hashes identical to previous revision and trace excluded. Formatter18.1.8 PASS. Curated evidence and user reproduction guide written.
- Ruling: corrected a test fixture from2200 to10 cycles because its existing mock converter uses one cycle/us; expected10us remained unchanged. Raw red/green logs retained.
- Physical reproduction/permanent fix remain NOT_RUN. No flash, no device operations, no new recovery retries or reset-control assumptions.
