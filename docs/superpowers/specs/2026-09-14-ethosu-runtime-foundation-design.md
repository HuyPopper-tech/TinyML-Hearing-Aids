# Ethos-U55 runtime foundation design

Date: 2026-09-14
Status: implemented foundation; model integration remains gated
Target: NuMaker-GestureAI-M55M1 with one Ethos-U55

## Decision

Keep the production firmware model-free and PCM16-passthrough by default. Add a
model-neutral Ethos-U55 runtime behind `M55M1_ENABLE_ETHOSU_RUNTIME=OFF` and
connect TFLM only through a project-owned bridge when both runtime and TFLM are
explicitly enabled.

```text
app -> services/model adapter -> drivers/ethosu_runtime
                                -> board/m55m1_ethosu_board
                                   -> libs/StdDriver/src/npu
```

The BSP core remains the only implementation of the Ethos-U command parser,
device register access, and core IRQ handling. The project owns board binding,
lifecycle state, cache hooks, static synchronization, and the public seam.

## Boundaries

- `board/m55m1_ethosu_board.*` owns FMC0/NPU0 clocks, `SYS_NPURST`, NPU IRQ
  priority, and the existing cycle timebase. It never includes
  `ethosu_driver.h` or defines `NPU_IRQHandler`.
- `drivers/ethosu_runtime/*` owns one static `struct ethosu_driver`, the
  single-flight state machine, validation, cache maintenance, hooks, and
  `NPU_IRQHandler`.
- `drivers/ethosu_runtime/ethosu_tflm_bridge.cpp` owns the TFLM custom-op
  registration. It computes tensor byte sizes with
  `TfLiteEvalTensorByteLength()` and calls only `ethosu_runtime_invoke()`.
- `libs/StdDriver/src/npu/ethosu_driver.c` and
  `ethosu_device_u55_u65.c` are included only when the runtime option is on.
  `libs/` is not edited.
- The runtime contains no model, weights, Vela artifact, CPU fallback, or
  empty-command-stream execution path.

## Lifecycle contract

`UNINITIALIZED -> READY -> RUNNING -> READY` is the normal path. A core or
hardware error moves the runtime to `FAULT`; `soft_reset()` or a new lifecycle
is required to recover. Validation errors leave the state unchanged.

Only one invocation is in flight. `start()` submits without blocking, `poll()`
returns `BUSY` while the core is running, `wait()` is foreground-only, and
`invoke()` is `start()` followed by `wait()`. `deinit()` rejects a running job.
The caller keeps the payload and base arrays alive through `wait()`; the base
address array is writable because the BSP may install fast memory at its
reserved index.

The runtime rejects null/empty payloads, payload sizes that are not a multiple
of four or exceed `INT_MAX`, base-array mismatches, zero bases, and bases that
are not 16-byte aligned. Payload pointers must be 4-byte aligned. No public
API exposes the BSP driver/device structures or raw cache/power calls.

## Cache and IRQ contract

The runtime supplies static mutex/semaphore storage, so it does not call
`malloc()` or `free()`. The BSP invokes project cache hooks before and after an
inference. Ranges are expanded to the configured cache line; with
`NVT_DCACHE_ON=0` the hooks provide barriers only. There is no immutable-range
shortcut in this version.

`NPU_IRQHandler()` only calls `ethosu_irq_handler(&g_ethosu_driver)` followed by
`__DSB()` and `__ISB()`. It does not log, allocate, parse a model, or run
inference.

## CMake and package policy

`M55M1_ENABLE_ETHOSU_RUNTIME=OFF` and `M55M1_ENABLE_TFLM=OFF` are the defaults.
TFLM requires the runtime. The default source glob removes every
`libs/StdDriver/src/npu/*.c`; enabling the runtime adds only the BSP driver and
U55/U65 device files. The project bridge is added only when TFLM is enabled.

`M55M1_BUILD_ETHOSU_SMOKE=ON` creates a separate image with board/runtime/core
and debug UART only. It has no audio pipeline or model package and never submits
an empty command stream. A future package must contain
`integration_manifest.json` and `m55m1_model_package.cmake`; package paths are
restricted to the package/services roots and package sources may not provide a
second Ethos-U core or `NPU_IRQHandler`.

## Acceptance evidence

Host contract tests compile the runtime with temporary mock core/board hooks and
cover validation, single-flight, polling, terminal error, reset, and teardown.
Armclang Debug/Release runtime-enabled configure/build and the TFLM bridge
compile are separate gates. Board smoke and model numerical parity remain
external gates; no model is deployment-approved by this foundation.