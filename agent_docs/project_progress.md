# Project Progress

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