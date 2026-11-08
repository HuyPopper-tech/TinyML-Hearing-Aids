# Model packages

The active custom-PCB product uses the R2 gain/phase controller from `models/deepfir/`. Generated model data and immutable metadata live here; executable streaming, DSP and TFLM integration live in `services/deepfir/`. The model is integrated into the continuous audio path but is not deployment-approved.

Scripts and presets select the project-local package with Ethos-U, TFLM, CMSIS-DSP/MVE, D-cache, model SRAM and DAC/I2S enabled. Normal Release is continuous and silent.

A package must provide:

- `integration_manifest.json` with immutable artifact identity and validation status.
- `m55m1_model_package.cmake` defining the package sources, include paths, and compile definitions.

The source package remains alongside the active copy for provenance. Compiled replay is in scope: runner and opt-in firmware build are ready, execution is BLOCKED without a running authorized backend. Board allocation, MVE readiness, audio quality, latency, deadlines and soak remain separate gates. See [current integration record](../docs/r2_custom_integration.md).
