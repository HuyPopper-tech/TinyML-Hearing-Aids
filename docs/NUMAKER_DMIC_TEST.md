# Removed NuMaker build flow

On 2026-11-08 the product moved to the custom PCB. NuMaker pin branches,
presets, wrappers and generated build directories were removed. No NuMaker
build command remains supported. Previous build binaries/maps and wrapper
evidence are archived under
`diagnostics/r2_custom_integration_20261108_105000/obsolete_build_evidence.zip`.

Use [custom product integration](r2_custom_integration.md), with Debug in
`build-debug/` and Release in `build/`. The standalone custom diagnostics and their exclusive modules have also been
removed. app/main.c is the sole product entry point.
