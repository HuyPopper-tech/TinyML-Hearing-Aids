# Codex Agent Guidance Design

## Goal

Add a repository-local `.codex/AGENTS.md` that gives future Codex sessions
clear, project-specific instructions for safely changing the M55M1 firmware.

## Scope

The guidance applies to project-owned code and configuration:

- `app/`, `cmake/`, `tools/`, `tests/`, `build.bat`, `build.sh`, and project
  CMake/configuration files.
- It excludes `libs/`, which contains vendored Nuvoton, CMSIS, CMSIS-NN, and
  RTX code. Agents must not edit it unless the user explicitly requests it.

## Document Structure

The agent guide will be concise and written in English so tool-using agents can
apply it consistently. It will contain these sections:

1. Project boundaries and safety rules for vendor code, dirty worktrees, and
   generated build outputs.
2. M55M1 build, debug, and pin-mux constraints, including the standard Debug
   and Release commands and the PF0/PF1 SWD recovery risk.
3. C coding rules adapted from the supplied guide: file headers, `#pragma once`,
   include ordering, naming, declarations, indentation, braces, comments,
   error handling, internal linkage, and global state.
4. Project-specific adaptations: use platform-appropriate error/status and log
   APIs instead of ESP-IDF-only names; use `hal_` and `platform_` prefixes only
   when adding those abstraction layers.
5. Required validation and handoff checks, including the static Python test
   suite and a clear report when the Arm Compiler license prevents a full build.

## Non-goals

- Do not reform the existing vendor code style.
- Do not impose ESP-IDF CMake or logging conventions on this ArmClang/CMake
  firmware project.
- Do not change firmware behavior, board pin assignments, or debug configuration
  as part of creating the guidance.

## Acceptance Criteria

- `.codex/AGENTS.md` is located at the repository root and is self-contained.
- It preserves all applicable rules from the supplied C style guide, with
  explicit M55M1 alternatives for ESP-IDF-specific rules.
- It identifies the owned directories and the `libs/` exclusion unambiguously.
- It tells future sessions how to configure Debug/Release and validate changes.
