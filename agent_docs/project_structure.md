# Project Structure

- `app/main.c`: product boot composition and bare-metal event loop; opt-in replay runs without audio.
- `services/audio_pipeline/`: hop continuity, deadline/transport fault gates, PCM working set and synchronized restart.
- `services/deepfir/deepfir_features.c`: sole analysis history, Hamming/FFT256 and three-channel frontend.
- `services/deepfir/deepfir_model_adapter.c`: previous/current context, encoded h/c feedback, previous taps and dispatch.
- `services/deepfir/deepfir_reconstruction.c`: stateless gain65/phase65 to FIR128, checked FFT128 inverse/roll/taper.
- `services/deepfir/deepfir_dsp.c`: sole FIR history, midpoint-Hann crossfade, MVE kernel and PCM conversion.
- `services/deepfir/deepfir_tensor_binding.c`: strict portable tensor metadata validation.
- `services/deepfir/deepfir_inference_service.cpp`: one interpreter/resolver/arena, lifecycle, memory snapshot and optional replay mailbox.
- `drivers/`: sequence-tagged hop16 audio transport, sole TLV320DAC3100 terminal-task
  FIFO handoff, bounded UART and single-instance Ethos-U/cache/IRQ support. Legacy
  32-kHz/scatter and untagged submit paths are removed.
- Runtime telemetry is owned by the active pipeline/inference modules; the unused
  historical `services/audio_telemetry.c/.h` was removed.
- `board/`: custom PCB pins, common audio clock source, DMA/IRQ binding and recovery-sensitive configuration.
- `models/deepfir/`: immutable compiled model/contract/provenance plus separate integration manifest; no DSP implementation.
- `common/`: dependency-free portable utilities.
- `tools/replay_deepfir_r2.py`: package schema/hash checks and full compiled trace runner/comparator through an already running OpenOCD backend.
- `tests/fixtures/r2/`: hash-checked immutable replay helper; full training/golden packages remain local and Git-ignored.
- `tests/`: native C/CMSIS scalar numeric vectors, mocked transport/backend contracts, architecture/linkage and repository gates.
- `diagnostics/`: Git-ignored local logs, receipts and old model/build/source archives.
- `docs/verification/`: curated source/test/build receipts for the checked-in firmware.
- `build-debug/`, `build/`: the only active generated Debug/Release directories.
- `libs/`: unchanged vendored BSP/CMSIS/TFLM foundation.

Only inference accesses model artifacts. Root source packages/old archives are provenance, not additional link inputs. Ownership is detailed in `docs/r2_custom_integration.md`.
