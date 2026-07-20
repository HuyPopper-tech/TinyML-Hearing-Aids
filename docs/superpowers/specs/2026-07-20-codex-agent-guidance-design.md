# Codex Agent Guidance Design

## Goal

Add a repository-local `.codex/AGENTS.md` that gives future Codex sessions
clear, project-specific instructions for safely changing the M55M1 hearing-aid
firmware and preserving its target software architecture.

## Scope

The guidance applies to project-owned code and configuration:

- `app/`, future `board/`, `drivers/`, `services/`, `common/`, and `models/`
  directories, plus `cmake/`, `tools/`, `tests/`, `build.bat`, `build.sh`, and
  project CMake/configuration files.
- It excludes `libs/`, which contains vendored Nuvoton, CMSIS, CMSIS-NN, and
  RTX code. Agents must not edit it unless the user explicitly requests it.

## Document Structure

The agent guide will be concise and written in English so tool-using agents can
apply it consistently. It will contain these sections:

1. Project boundaries and safety rules for vendor code, dirty worktrees, and
   generated build outputs.
2. A strict layer contract: `app -> services -> drivers -> board -> libs`, with
   an optional dependency-free `common/` layer and generated/read-only `models/`
   artifacts. The Nuvoton StdDriver/PAL remains the vendor HAL; no redundant
   project-wide wrapper layer is introduced.
3. Realtime audio and configuration data flows. Audio is prioritized over BLE,
   telemetry, and persistence; settings move from pending to active only at an
   audio-frame boundary and persist only after explicit commit.
4. M55M1 build, debug, BLE, and pin-mux constraints. E104-BT5010A is a
   dedicated UART configuration bridge with flow control and control GPIOs;
   OTA is explicitly out of scope. ICE remains the programming/recovery path.
5. C coding rules adapted from the supplied guide: file headers, `#pragma once`,
   include ordering, `snake_case`/`snake_case_t` naming, indentation, braces,
   comments, error handling, internal linkage, and global state.
6. Project-specific adaptations: use platform-appropriate error/status and log
   APIs instead of ESP-IDF-only names; do not duplicate Nuvoton StdDriver.
7. Required validation and handoff checks, including the static Python test
   suite and a clear report when the Arm Compiler license prevents a full build.

## Non-goals

- Do not reform the existing vendor code style.
- Do not impose ESP-IDF CMake or logging conventions on this ArmClang/CMake
  firmware project.
- Do not change firmware behavior, board pin assignments, or debug configuration
  as part of creating the guidance.
- Do not add CMSIS-RTOS2, OTA, image staging, a bootloader, or rollback behavior
  as part of creating the guidance.

## Acceptance Criteria

- `.codex/AGENTS.md` is located at the repository root and is self-contained.
- It preserves all applicable rules from the supplied C style guide, with
  explicit M55M1 alternatives for ESP-IDF-specific rules.
- It identifies the owned directories and the `libs/` exclusion unambiguously.
- It tells future sessions how to configure Debug/Release and validate changes.
- It defines allowed and forbidden dependencies for every project-owned layer.
- It preserves the agreed audio, BLE configuration, settings persistence, and
  ICE recovery constraints.
