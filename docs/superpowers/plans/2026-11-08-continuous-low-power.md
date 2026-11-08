# Continuous Low Power Implementation Plan

> Execution: superpowers:executing-plans, inline by the main agent. User approved
> immediate execution and prohibited agents and commits.

**Goal:** Reduce avoidable CPU/peripheral activity while every audio/model hop runs.

**Architecture:** Service-controlled wait; driver checks transport ownership/health
atomically; board performs shallow WFI and named peripheral gating. Board timebase
continues through CPU Idle and remains the sole timing owner.

**Tech Stack:** C/C++, Nuvoton StdDriver/CMSIS, native gcc harness, Arm Compiler6.24.

**Spec:** `docs/superpowers/specs/2026-11-08-continuous-low-power-design.md`.

## Global constraints

220 MHz; mono16kHz/hop16; custom PCB; two existing Debug/Release directories;
continuous model; no libs/model/package changes, agents, commits, or flash.

## Review focus

- Capture IRQ between empty check and WFI must not lose a hop.
- DMA fault without a completed capture must keep foreground recovery runnable.
- NPU WFE/CPU WFI must not stop the deadline timebase.
- Codec startup/recovery delays must not reprogram that timebase.
- Release UART removal must preserve Debug diagnostics and SWD recovery.

## Task 1 — hardware policy and timebase

Files: board/m55m1_power.[ch], board/m55m1_timebase.[ch],
board/m55m1_audio.c, CMakeLists.txt, tests/test_continuous_low_power.py.

- [x] Configure Debug before compile-sensitive edits; preserve baseline images.
- [x] Add native tests for external SysTick/core-cycle wrap and power clock policy;
      run red, implement ACLK/2 counter, bounded delay, shallow idle and UART policy.
- [x] Gate only explicitly unused peripherals from product board bootstrap.
- [x] Run native tests green, inspect source policy and diagnostic independence.

## Task 2 — continuous foreground wait and codec

Files: app/main.c, services/audio_pipeline/audio_pipeline.[ch],
drivers/audio_transport/audio_transport.[ch], drivers/tlv320dac3100/tlv320dac3100.c,
tests/test_continuous_low_power.py, affected existing test fixtures.

- [x] Add native race/fault/no-work tests and codec script/error tests; run red.
- [x] Implement `audio_pipeline_wait_for_work(void)` ->
      `audio_transport_wait_for_work(void)`; masked recheck then board shallow WFI.
- [x] Use board delay for codec waits; enforce unused DAC blocks off after reset.
- [x] Run green; keep numerical/schedule assertions unchanged.

## Task 3 — verification and handoff

Files: diagnostics/continuous_low_power_20261108_122920/, docs/continuous_low_power.md,
agent_docs/project_progress.md, agent_docs/latest_session_work.md, driver docs.

- [x] Full unittest suite, Debug then Release script builds, model/link/map audit,
      git diff --check; save commands, exit codes, hashes, logs.
- [x] Self-review changed boundaries, publish verified acceptance and matched A/B
      measurement/recovery instructions; mark all board power/timing NOT_RUN.
