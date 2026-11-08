# Project C and C++ Coding Style Guide

Applies to project-owned sources in `app/`, `services/`, `drivers/`, `board/`
and `common/`. Follow `AGENTS.md` and the Architecture Contract when a generic
example conflicts. Exclude `libs/`, generated model artifacts, source packages,
archives and build output. Use `.clang-format` with clang-format 18.1.8.

## Files and includes

- Start each source/header with Doxygen `@file`, `@brief`, `@author Bui Minh Huy`
  and its creation `@date`; preserve third-party attribution.
- Use `#pragma once`. Public C headers wrap declarations in `extern "C"` guards.
- Include standard headers, project headers, then vendor headers. Keep conditional
  includes with their feature guards; include only what is required.

## Names and layout

- Use `snake_case` for project functions/variables and `snake_case_t` for typedefs.
  Vendor APIs, interrupt vector names and external C++ interfaces keep their ABI.
- Use module-prefixed `UPPER_CASE` macros and constants.
- Indent with four spaces, never tabs. Put opening braces on the same line.
- Always use braces for `if`, `else`, `for`, `while` and `do`, including one statement.
- Expand short function bodies and error branches onto separate lines. Prefer a
  100-column line limit, without rewriting strings or numerical expressions.

## Interfaces and comments

- Declare public functions in headers; keep implementation helpers static or in
  a C++ anonymous namespace. Keep responsibilities focused and avoid forwarding
  wrappers that merely rename another API.
- Document non-trivial public functions with @brief, @param and @return when
  applicable. Specify units, valid sizes, buffer ownership, call context and reset
  requirements. Use @pre/@note for lifecycle and concurrency constraints.
- Use // for short inline explanations of the reason for ordering, quantization,
  barriers, cache maintenance or DMA ownership. Do not narrate obvious assignments.
- Shared buffers/state document producer, consumer, synchronization and lifetime.
  Only use // ========== section ========== // for meaningful logical sections.

## Errors and realtime behavior

- Validate arguments at public boundaries. Use existing project return types;
  document error codes. Never import ESP-IDF identifiers into this Nuvoton project.
- Follow app -> services -> drivers -> board -> libs. Do not add generic HAL
  wrappers around Nuvoton StdDriver calls; add an interface only for a real seam.
- ISR work is acknowledge, descriptor handoff and signal. No DSP, inference,
  allocation, blocking logging, flash write or mutex acquisition in an ISR.
- Use fixed-capacity realtime storage and no allocation after audio starts.
- Preserve arithmetic evaluation order, tensor ABI, sample rate, hop, state and
  fault recovery during style-only changes. Test numerical behavior separately.

## Build and verification

List CMake sources/include paths explicitly; use the existing CMake build scripts.
Configure the active Debug/Release profile before compile-sensitive edits. Run
native numerical/regression tests, both builds when the compiler is available,
and git diff --check. Formatting and documentation do not prove board behavior.
