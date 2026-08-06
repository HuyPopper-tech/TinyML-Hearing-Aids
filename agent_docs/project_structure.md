# Project Structure

- `app/`: boot composition and isolated board diagnostics.
- `services/audio_pipeline/`: foreground capture/enhance/submit orchestration.
- `services/deepfilternet/`: 512/256 DSP enhancer and model inference adapter.
- `drivers/`: audio transport, DAC, Ethos-U and bounded UART TX.
- `board/`: M55M1 pins, clocks, DMA, IRQ priority and stack monitor.
- `models/deepfilternet/`: audited package, generated contracts and board gate.
- `tools/generate_deepfilternet_assets.py`: deterministic artifact generator.
- `tests/`: host math, contract, telemetry, transport and architecture gates.
- `libs/`: unchanged vendored BSP/CMSIS/TFLM foundation.

Legacy `services/erb_gru/` and `models/erb_gru/` are provenance only and are
absent from the production build graph.
