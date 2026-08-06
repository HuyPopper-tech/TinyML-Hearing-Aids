# AGENTS.md

## Project Context


## Core Design Principles

The project must strictly follow modular design.

Each module should have:

- A clear responsibility.
- A clear interface.
- Minimal unnecessary coupling.
- A structure that makes it easy to test, debug, replace, extend, and reuse.

Nested modules are allowed when they make responsibilities clearer. Avoid placing unrelated responsibilities into the same file, class, service, or large function.

- Define proportionate acceptance and verification requirements before implementation.
- Keep related tests cohesive enough to avoid fragmented micro-tests, but never reduce meaningful coverage, weaken assertions, or hide failures merely to save tokens or execution time.


## Tool Execution and Batching

For each bounded work stage, identify independent, already-known, non-conflicting tool calls before invoking tools. When practical, execute them through one outer `functions.exec` or Code Mode `exec` call.

Use `Promise.allSettled()` when successful results remain useful even if another call fails. Inspect and attribute every returned result. Use `Promise.all()` only when any individual failure invalidates the entire batch.

Prefer batching for:

- Read-only file inspection.
- Independent symbol, text, and call-site searches.
- Repository metadata and status collection.
- Independent log or artifact inspection.
- Validation commands that do not share mutable state.

Keep operations sequential when they involve:

- A result that determines the next operation.
- Adaptive investigation where the next target is not yet known.
- Approvals or permission boundaries.
- Agent spawn, wait, resume, message, or replacement operations.
- Overlapping or order-sensitive writes.
- Git staging, commits, resets, or other Git-state mutations.
- Builds or tests sharing a build directory, generated output, database, port, fixture, device, or other mutable resource.

Do not split an otherwise batchable inspection across repeated outer tool calls. Do not create extra work, broaden scope, obscure failure attribution, or increase worker count merely to fill a batch.

Tool-call concurrency is local to one agent thread. It does not change route selection, worker ownership, scope boundaries, verification requirements, or subagent-concurrency limits. A stage requiring only one useful tool call should remain one call.

## Working State

At any given time, we will be in one of two working states:
- `deployment state`: beginning to plan a broad task or in the process of deploying a plan. A  deployment plan can span multiple sessions.
- `leaf state`: for tasks outside the plan being deployed by the `deployment state`, such as general queries, document editing, or performing operations to add, modify, or delete small files, modules, or tools.

## Project Documentation Framework

The main project documents are stored under `agent_docs/`:

- `agent_docs/project_overview.md`: goals, architecture, workflow, and major decisions.
- `agent_docs/project_core_tech.md`:A brief summary of special technologies or architectures of project.
- `agent_docs/project_structure.md`: directory layout, modules, components, and ownership boundaries.
- `agent_docs/project_progress.md`: active implementation plan and cross-session execution status.
- `agent_docs/project_diary.md`: durable architecture decisions, discarded approaches, and lessons.
- `agent_docs/latest_session_work.md`: Summarizing previous sessions along with any unfinished tasks.
- Module-specific documents, when present.

--------
`agent_docs/project_progress.md` and `agent_docs/latest_session_work.md` are two documents designed to ensure smooth and seamless deployment between multiple sessions in deployment mode. These two files can only be edited in `deployment state` or when the user explicitly requests it. The main agent is responsible for updating these two files, while subagents are not allowed to edit them.

Update documentation only with verified facts. Keep temporary reasoning, raw logs, and short-lived checkpoints out of durable project documents.

Never delete any main project document without warning the user and receiving a second explicit confirmation.

## Route Selection

There are three routes.
### Light route: 
Use for light tasks which in the `leaf state`.
Performs tasks by yourself. Do not spawn subagents in this route.

### Medium route: 
Use for deploying large tasks/plans in the `deployment state`.
Perform implementation, verification, and documentation by yourself. Do not spawn worker subagents in this route. The deployment session's persistent `explorer` companion is the only exception and is not counted as a subagent.
Read and follow `agent_docs/workflow/medium_route.md`.

### Heavy route: 
You a orchestrator, coordinates subagents to deploy large tasks/plans in the `deployment state`.
Reuse the deployment session's persistent `explorer` companion to absorb bounded supplementary context and return concise findings to the main agent. It is not counted as a worker subagent.
Read and follow `agent_docs/workflow/heavy_route.md`.

### Route selection rules and state interpolation

The route will be specified by the user, like: "use Light/medium/heavy route...". Apply that route throughout the entire session until it ends or until the user indicates to switch to the other route. If the user does not specify a route, select the light route as the default. Do not guess and choose a route yourself.

If the light route is specified or choosed, it means we are in the `leaf state`. 
If the medium route/heavy route is specified, it means we will proceed to the `deployment state`. 

## Context Loading

- In the Light route (`leaf state`), read only the files relevant to the current task.
- On first entering the `deployment state`, immediately initialize exactly one session-long `explorer` companion. Reuse the same thread for every later bounded context investigation, including across Medium/Heavy route changes within the session. Do not spawn a new explorer for each request; replace it only when the applicable lifecycle rules require it. The explorer is a read-only second brain for the main agent and is excluded from worker/subagent counts.
- An explorer assignment defines the investigation focus, not a hard reading boundary. The explorer may follow directly related files, symbols, call sites, documentation, dependencies, and configuration when needed, while remaining read-only and avoiding unrelated repository-wide exploration.
- Load the foundational project context in one bounded read-only batch:
  1. `agent_docs/project_overview.md`
  2. `agent_docs/project_structure.md`
  3. `agent_docs/project_progress.md`
  4. `agent_docs/latest_session_work.md`
- After the batch returns, interpret overview and structure before reconciling progress and the latest-session handoff. This interpretation order does not require separate outer tool calls.
- Use the resulting status and ownership map to inspect the smallest relevant interfaces, call sites, tests, and configuration surface.
- Read only relevant module documentation. Expand source inspection only when repository evidence requires it.
- Reconstruct active tasks, dependencies, verification state, and blockers. Resolve contradictions with targeted evidence.
- Under the Heavy route, review only critical hunks and integration boundaries after delegation unless risk, missing evidence, or conflicting results require broader inspection.
- In final agent-usage statistics for a deployment session, always include the explorer's call count and label it as a `companion`, even though it is excluded from worker/subagent counts.

## Platform-specific paths

Paths in this workflow are written using `/` as a platform-neutral separator.
When running filesystem commands, use paths appropriate for the current operating system and shell:

* On Linux and macOS, use `/`.
* On Windows, use the equivalent Windows path format and `\` where required.

Do not treat the example path separator as a literal requirement. Resolve every path using the conventions of the current environment.

# M55M1 Hearing-Aid Firmware: Codex Instructions

## Scope and Ownership

- These instructions apply to project-owned code and configuration in `app/`,
  `board/`, `drivers/`, `services/`, `common/`, `models/`, `cmake/`, `tools/`,
  `tests/`, `build.bat`, `build.sh`, and root CMake/configuration files.
- `libs/` is vendored Nuvoton BSP, CMSIS, CMSIS-DSP, CMSIS-NN, and RTX code.
  Do not edit `libs/` unless the user explicitly requests it.
- Do not commit generated build directories, `compile_commands.json`, caches,
  IDE files, or user-owned untracked files. Preserve unrelated changes in a
  dirty worktree.
- Do not create project-owned generic HAL or platform wrappers that merely
  rename Nuvoton StdDriver calls. `libs/StdDriver` and `NuMicro.h` already
  provide the vendor driver/PAL layer.

## Architecture Contract

The target dependency direction is:

```text
app -> services -> drivers -> board -> libs
```

`common/` is optional and dependency-free. It may contain portable types,
fixed-capacity queues, framing helpers, and small utilities; it must not depend
on another project-owned layer or vendor code. `models/` contains generated or
read-only TinyML artifacts. Only the inference service accesses model artifacts.

### Layer Rules

- `app/` owns boot composition and the bare-metal event loop. It may call the
  single board bootstrap API and public service APIs. It must not include
  `NuMicro.h`, touch registers, or use driver implementation APIs directly.
- `services/` owns product behavior: audio pipeline, inference adapter, BLE
  protocol, settings, persistence policy, and telemetry. It may depend on
  `common/` and public driver headers. It must not depend on board pin/clock
  details or call vendor APIs directly.
- `drivers/` owns component-specific behavior such as E104-BT5010A, audio
  codec/DAC, and DMIC/I2S DMA orchestration. It may depend on `board/`,
  `common/`, and Nuvoton StdDriver/CMSIS APIs. It must not depend on services
  or app code.
- `board/` owns this PCB's pin map, clocks, DMA channels, IRQ priorities, and
  peripheral binding. It may include `NuMicro.h` and must not depend on
  drivers, services, or app code.
- `libs/` remains the vendor foundation. Do not change its style, APIs, or
  build contents without explicit user approval.

Create a small dependency interface only when it enables a real substitution or
test seam, such as mock NVM for settings tests or mock transport for protocol
tests. Do not add a generic `hal_uart_*()` wrapper around `UART_*()`.

## Realtime Audio and Settings

- The audio data path is: `DMIC DMA -> static buffer queue -> DSP -> inference
  adapter -> post-process -> I2S DMA -> DAC`.
- The configuration path is: `phone -> E104 UART bridge -> framed BLE protocol
  -> validated pending settings -> audio-frame apply -> explicit commit ->
  deferred NVM write`.
- ISRs must not block, allocate memory, log, parse BLE messages, run DSP/TinyML
  inference, write flash, or acquire a mutex. They only acknowledge hardware,
  move buffer descriptors, and signal events.
- Audio has priority over BLE, telemetry, logging, and persistence. Background
  work may be delayed, dropped, or retried; audio must never wait for it.
- Settings follow this exact lifecycle: `pending -> active at an audio frame
  boundary -> explicit commit -> deferred NVM write`. Validate all values before
  they become pending. Only active settings are readable by the audio path.
- Use static or fixed-capacity buffers in realtime paths. Do not dynamically
  allocate memory after audio starts.

## BLE, Debug, and Hardware Boundaries

- BLE configuration only; do not add OTA, image staging, bootloader, or rollback
  behavior without a separately approved design.
- E104-BT5010A is a UART-to-BLE bridge, not a BLE stack or OTA bootloader on the
  M55M1. Use a dedicated UART TX, RX, RTS, and CTS; do not share it with the
  SWD pins or the debug UART.
- Route and document E104 control GPIOs: WKP, LINK, MOD, and DISC, plus RST when
  a spare GPIO is available. Use `MOD` for module configuration/maintenance, not
  for the product configuration protocol. Keep module AT/log text out of the
  production transparent data stream.
- Keep the ICE recovery/programming header accessible: SWDIO, SWCLK, nRESET,
  GND, and VREF/3V3. Do not repurpose PF0/PF1 or alter SWD recovery behavior
  unless the user explicitly requests a tested recovery procedure.
- Firmware uses a bare-metal event loop. CMSIS-RTOS2 is not used currently;
  adding it requires an approved scheduling, priority, stack, and latency plan.

## C Style for Project-Owned Code

- Start each new `.c` and `.h` file with a purpose, author, and date header.
  Document non-trivial public functions and complex realtime/concurrency logic.
- Header files use `#pragma once`. Public C headers wrap declarations in
  `extern "C"` guards for C++ compatibility.
- Order includes as: C standard library headers, project-owned headers, then
  vendor headers. Include only what is needed; prefer forward declarations
  where they avoid needless coupling.
- Functions and variables use `snake_case`; typedef structs and enums use
  `snake_case_t`; macros/constants use module-prefixed `UPPER_CASE` names.
- Use four spaces for indentation. Opening braces stay on the same line. Always
  use braces for `if`, `for`, `while`, and `do`, including one-statement bodies.
- Use `//` for short comments and `/* ... */` for block comments. Use
  `// ========== section ========== //` only for meaningful logical sections.
- Keep functions focused. Prefer `static` internal linkage. Avoid mutable global
  state; when shared state is required, document its owner, execution context,
  and synchronization/frame-boundary rule.
- Validate arguments at public boundaries and return project/platform-appropriate
  status values. Do not use ESP-IDF APIs or ESP-IDF CMake such as `ESP_OK`,
  `ESP_LOGE`, or `idf_component_register`.
- Add hardware abstraction only when it is a genuine project interface. Use
  `hal_` or `platform_` prefixes only for such deliberate abstractions, not as
  aliases for existing Nuvoton StdDriver APIs.

## Build, Debug, and Validation

- Configure the active editor/build profile before editing compile-sensitive
  code: `build.bat debug config` or `build.bat release config` on Windows;
  `./build.sh debug config` or `./build.sh release config` on POSIX systems.
  The scripts update the root compilation database for clangd. Restart clangd
  after switching profiles.
- Use `build.bat debug build` or `./build.sh debug build` for debug builds, and
  the equivalent `release` commands for release builds. Report an Arm Compiler
  license failure as an external blocker; do not claim a binary was produced.
- For project changes, run `python -m unittest discover -s tests -v` and
  `git diff --check`. Run the smallest relevant build/configure command when
  toolchain access is available.
- Before flashing or changing pin mux/clock/DMA code, state the board pins and
  recovery path. Never assume a probe can assert reset when its hardware lacks
  an nRESET connection.
