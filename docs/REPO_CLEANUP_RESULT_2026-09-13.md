# Repository cleanup result - 2026-09-13

## Baseline

The repository now builds a model-neutral PCM16 passthrough baseline. The
active path is `DMIC DMA -> static capture queue -> enhancement passthrough ->
optional I2S/DAC DMA`. No neural model, placeholder binary, CPU fallback, or
automatic model discovery is enabled.

## Evidence

- External archive: `D:\Tài liệu ĐATN\archive\tinyml-hearing-aids-pre-model-reboot-20260913`.
- Archive marker: `ARCHIVE_VERIFIED=1`.
- Archive inventory: 30,764 snapshots verified against source SHA-256 values.
- Passthrough, repository hygiene, CMake policy, style, and full host suite:
  `58/58` tests passed.
- Debug configure/build: exit `0`; linked AXF, map, and binary produced.
- Release configure/build: exit `0`; linked AXF, map, and binary produced.
- Retained-source audit: no legacy backend references under project-owned
  `app/`, `board/`, `common/`, `drivers/`, `services/`, `cmake/`, or root build
  scripts.
- `git diff --check`: exit `0`.

Generated build directories and `compile_commands.json` were removed after
the build evidence was captured. `libs/`, `.git/`, and board recovery
configuration were not modified by the cleanup.

## Future package contract

Only an explicitly supplied `M55M1_MODEL_PACKAGE_DIR` may contribute model
sources. It must contain `integration_manifest.json` and
`m55m1_model_package.cmake`. Future DeepFIR/Wave-U-Net packages remain
documentation-only until package, host parity, Vela, board parity, realtime,
soak, and capture-to-DAC gates pass.
