# Project Structure

- `app/`: boot composition and isolated hardware diagnostics.
- `services/audio_pipeline/`: foreground capture, passthrough, and optional submit orchestration.
- `services/enhancement_passthrough/`: stateless validated PCM16 copy service.
- `drivers/`: audio transport, DAC, bounded UART, and opt-in Ethos-U runtime support.
- `board/`: M55M1 pins, clocks, DMA, IRQ priorities, and recovery-sensitive bindings.
- `models/deepfir/`: documentation-only future package slot.
- `models/wave_unet/`: documentation-only future package slot.
- `tests/`: host vectors, runtime contract, repository hygiene, CMake policy, transport, and architecture gates.
- `libs/`: unchanged vendored BSP and CMSIS foundation.

Model sources enter only through an explicitly supplied package containing `integration_manifest.json` and `m55m1_model_package.cmake`.
