# Continuous custom product finalization

User authorized inline implementation, status updates and removal of unused code.
Light route; no subagents, no board operations. Base: 99c617b.

## Scope and ownership

- Record the supplied 60,000-hop Probe UART snapshot with an explicit image-identity limitation.
- Keep one continuous application in ordinary Debug and Release. Keep Release Probe as
  an opt-in of the same pipeline, not another audio implementation/build directory.
- `board/m55m1_audio.*`: sole RX DMA/history-of-descriptors owner; delete unreachable
  old diagnostic warmup/clock and 32-kHz selection. Keep current custom pins, clocks,
  IRQ priorities, RX LPPDMA and TX PDMA bindings exactly unchanged.
- `drivers/tlv320dac3100/*`: retain the tested terminal-task FIFO handoff as the only
  playback implementation; remove the legacy scatter ring, unused 32-kHz profile,
  untagged submit wrapper and write-only queued-audio flag.
- `drivers/audio_transport/*`: retain tagged publication and WFI; remove the untagged
  wrapper and compile-time alternate product path. No change to service/DSP ownership.
- CMake/build wrappers: remove obsolete SRAM alias, unused gain definition and optional
  RTOS source/link block; canonical Debug/Release configurations remain bare metal.
- Remove the uncalled historical `services/audio_telemetry.c/.h` and its private test:
  the active pipeline owns its own init/fault/timing reporting. Caller search found only
  the old module's test and CMake list; no production dependency.
- Callers/tests: normal transport has only tagged submit callers. Update obsolete static
  32-kHz expectations and replace the old scatter-harness case with current API rejection
  checks. Preserve actual C startup/60,000-handoff/fault coverage. Prove ordinary pipeline
  continues beyond Probe's 60,000-hop limit.
- Update current driver/integration documentation and both requested status handoffs.
  Preserve historical evidence, source packages, libs, replay/capture tools and user files.

## Acceptance / verification

- [x] Supplied log parser passes; evidence distinguishes configured clock, user report,
  candidate image association, nominal duration and modular STOP timestamp.
- [x] No legacy product-selection/32-kHz/unsequenced-publication branches in active audio.
- [x] Real C normal foreground harness exceeds 60,000 hops without a finite stop; strict
  sequence/deadline/fault/reset assertions and independent DMA ownership cases still pass.
- [x] Full unittest suite and whitespace/style checks pass; no weakened numeric tolerances.
- [x] Arm Debug, Release Probe and ordinary Release configure/build/link pass sequentially;
  Release/Probe MVE checks pass. Archive hashes/maps/memory and restore ordinary opt-ins OFF.
- [x] Self-review verifies ISR/buffer ownership, no duplicate pipeline/model/runtime and
  no source-package changes. Only task files are eligible for the authorized commit/push
  with the user's author identity.

Verified on2026-11-09:134/134 tests, Arm6.24 three variants and9 Release/Probe
MVE ranges PASS. Receipts: docs/verification/2026-11-09-continuous-product/.
Removed empty obsolete `app/npu/` and `drivers/ethosu_u55/` directories after
confirming no active references. Ninja cleandead removed stale generated outputs
without changing final BIN hashes. Two active build directories remain.

Board retest of final images, compiled-controller parity, physical latency/audio/power
remain separate. Debug has symbols/logging and no measured 1-ms guarantee; retain fault
guards rather than hide a deadline miss. New binaries do not inherit the supplied
candidate's hardware identity or claim new measured timing.
