# Ethos-U55 runtime foundation

This repository now has a reusable, model-neutral Ethos-U55 runtime for the
M55M1. It is deliberately opt-in; the shipped firmware remains the PCM16
passthrough baseline.

## Ownership and build switches

```text
app -> services/model adapter -> drivers/ethosu_runtime
                                -> board/m55m1_ethosu_board
                                   -> libs/StdDriver/src/npu
```

- `board/m55m1_ethosu_board.c` enables `FMC0_MODULE` and `NPU0_MODULE`, resets
  `SYS_NPURST`, prepares the existing timebase, and owns the fixed NPU IRQ
  priority.
- `drivers/ethosu_runtime/ethosu_runtime.c` owns one static BSP driver,
  lifecycle, validation, cache hooks, synchronization, timing, and the NPU
  handler.
- `drivers/ethosu_runtime/ethosu_tflm_bridge.cpp` is the only TFLM custom-op
  bridge. It creates byte-accurate base-size arrays and calls the public runtime
  API.
- `libs/` is vendored and unchanged.

The safe defaults are:

```text
M55M1_ENABLE_ETHOSU_RUNTIME=OFF
M55M1_ENABLE_TFLM=OFF
M55M1_MODEL_PACKAGE_DIR=""
M55M1_BUILD_ETHOSU_SMOKE=OFF
```

TFLM cannot be enabled without the runtime. The default CMake glob excludes all
BSP NPU sources; runtime opt-in adds only `ethosu_driver.c` and
`ethosu_device_u55_u65.c`. No model source is discovered automatically.

## Public API

Include `drivers/ethosu_runtime/ethosu_runtime.h`. The API exposes only status,
state, health, invocation, result, reset, and statistics types. It does not
expose `struct ethosu_driver`, `struct ethosu_device`, raw power calls, or cache
maintenance.

```c
ethosu_runtime_config_t config = {
    .fast_memory = NULL,
    .fast_memory_size = 0U,
    .secure_enable = true,
    .privilege_enable = true,
};
ethosu_runtime_result_t result;

if (ethosu_runtime_init(&config) == ETHOSU_RUNTIME_OK) {
    /* A future validated package supplies the payload and base arrays. */
    (void)ethosu_runtime_invoke(&invocation, &result);
}
```

The normal state sequence is `UNINITIALIZED -> READY -> RUNNING -> READY`.
`start()` is non-blocking; `poll()` returns `BUSY` until completion; `wait()` is
foreground-only; `invoke()` combines start and wait; and `deinit()` rejects a
running job. There is no cancel or fabricated timeout because the BSP has no
safe cancellation primitive.

Validation rejects null/empty payloads, non-4-byte payload sizes, sizes above
`INT_MAX`, missing base arrays, zero bases, and non-16-byte-aligned base
addresses. Payload and base arrays remain caller-owned through `wait()`.
Validation leaves the state unchanged. Core/hardware errors enter `FAULT` and
require `soft_reset()` or a new lifecycle.

## Cache and IRQ behavior

The adapter performs the BSP clean/invalidate hooks around inference. Ranges are
expanded to cache-line boundaries; `NVT_DCACHE_ON=0` becomes barrier/no-op
behavior. Static synchronization objects mean no `malloc()` or `free()` in the
runtime. `NPU_IRQHandler()` only forwards to the BSP handler and executes
`__DSB()`/`__ISB()`.

## TFLM and model-package boundary

The bridge registers `ethos-u`, obtains command payload bytes from the first
input, and builds base addresses for every remaining input and output tensor.
`TfLiteEvalTensorByteLength()` supplies actual byte counts; there is no fixed
8-buffer cap. The bridge never calls `ethosu_reserve_driver()`,
`ethosu_invoke_v3()`, or `ethosu_release_driver()`.

A future package must be an explicit directory containing
`integration_manifest.json` and `m55m1_model_package.cmake`. It may contribute
only package/services sources, includes, and definitions. It may not add a
second Ethos-U core or `NPU_IRQHandler`. A valid custom-operator payload must
come from Vela/model integration; the smoke image intentionally does not submit
an empty command stream.

## BSP provenance

The current Nuvoton BSP carries the Arm Ethos-U driver version `0.16.0`:

| File | SHA-256 |
| --- | --- |
| `libs/StdDriver/src/npu/ethosu_driver.c` | `360c54e413845567367c570691e4ac3624f62619d9b347a8383cbe2f072f6a2f` |
| `libs/StdDriver/src/npu/ethosu_device_u55_u65.c` | `931c4e002e75c80de3fc7a1c9cb17674380e7f44197db2a78abf8ce45a7428ed` |
| `libs/StdDriver/inc/npu/ethosu_driver.h` | `d95ea9cab513c3fac4c704b7833ccb51d31c5f90663607c16918d5252e11dd4d` |
| `libs/StdDriver/inc/npu/ethosu_types.h` | `5503a870cb090eafe968540bc14df9b2a553e51015bad351709bd48d9379248e` |

The source paths above are the retained BSP paths; no Arm core source was
copied into the project. The headers identify the core as Apache-2.0
(`SPDX-License-Identifier: Apache-2.0`, Arm Limited copyright). Keep the BSP
files byte-for-byte unchanged and re-check these hashes when updating it.

## Gates and limits

The host runtime contract and CMake policy tests are required before model work.
Armclang Debug/Release runtime-enabled builds and the standalone smoke image
prove compilation/linkage only. Board execution is still `BLOCKED_BOARD_ACCESS`
until a physical M55M1 board is available. There is currently no valid command
stream, model parity result, realtime/soak result, capture-to-DAC result, or
deployment approval. Every future DeepFIR/Wave-U-Net package must pass its own
manifest, FP32/INT8 parity, Vela, board cold-boot parity, realtime, soak, and
capture-to-DAC gates.