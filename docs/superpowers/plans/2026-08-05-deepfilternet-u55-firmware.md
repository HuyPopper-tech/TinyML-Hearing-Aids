# DeepFilterNet U55 Firmware Integration

**Date:** 2026-08-05  
**Route:** Heavy  
**Status:** Approved and in progress

## Goal

Replace the ERB-GRU firmware path with one DeepFilterNet binary:
`deepfilternet_u55_full_seed20260805`. There is no runtime model switch.

```text
DMIC DMA 256 PCM samples
-> 512-sample periodic sqrt-Hann STFT
-> 257 FFT bins
-> 32 ERB log-magnitude + 144 complex bins
-> DeepFilterNet INT8 on Ethos-U55
-> 257 gains + complex DF 5x144 + alpha
-> IFFT/WOLA
-> DAC DMA 256 PCM samples
```

The root `deepfilternet/` package is immutable. Firmware-owned generated and
runtime assets live under `models/deepfilternet/`.

## Delivery order

1. **DFN-01 — Artifact ingest and audit.** Verify the selected post-Vela model
   size/hash and complete FlatBuffer contract; create the minimal firmware
   package and a deterministic generator with `--check`.
2. **DFN-02 — Inference adapter.** Add model-specific Ethos-U/TFLM integration,
   validate all tensors, reset by tensor zero point, safely copy/requantize
   recurrent state, and publish outputs only after a valid invocation.
   Before relying on the stock `AddEthosU()` registration, prove that the Vela
   command stream aliases logical I/O through the shared arena and uses no more
   than the U55's eight hardware BASEP slots. The model has previously invoked
   on the target board. If the stock path cannot provide that proof, keep
   `libs/` unchanged and add only the minimum model-specific project-owned
   registration/invoke code needed for the current pipeline.
3. **DFN-03 — Scalar oracle.** Implement exact 512/256 STFT, ERB and complex
   features, gain/DF/alpha post-processing, WOLA, preroll, discontinuity and
   failure behavior. Verify five deterministic scenarios.
4. **DFN-04 — Production replacement.** Change the 16 kHz transport contract to
   256 samples, replace the pipeline call, remove ERB-GRU from the build graph,
   retain source attribution across the one-hop WOLA delay, and keep existing
   IRQ/pin/clock/U55 boundaries.
5. **DFN-05 — Diagnostics.** Emit bounded 100-frame UART snapshots containing
   timing, memory, quantization ranges/saturation, signal statistics and all
   transport/inference error counters. Separate enhancer `e2e_us` from
   capture-to-DAC `transport_us`.
6. **DFN-06 — MVE production DSP.** Optimize only after scalar golden passes.
   PARITY runs both paths and accepts at most one Q15 PCM LSB. Production links
   only MVE; fixed-point FFT is the fallback only if measured MVEF timing misses
   16 ms.
7. **DFN-07 — Verification and documentation.** Run generator, unit, diff,
   debug/release configure/build gates; then perform the explicit board golden,
   parity, live diagnostics, memory/map and 10-minute soak sequence.

## Public build contract

- `M55M1_ENABLE_DEEPFILTERNET=ON`
- `M55M1_DEEPFILTERNET_DIAGNOSTICS=OFF`
- `M55M1_DEEPFILTERNET_DSP_MODE=MVE|SCALAR|PARITY` (default `MVE`)
- `M55M1_DEEPFILTERNET_GOLDEN_SCENARIO`
- `M55M1_DEEPFILTERNET_FAULT_INJECTION`
- `M55M1_DEEPFILTERNET_INPUT_GAIN_DB=0`

Release production rejects fault, golden and parity configurations.

## Runtime ownership

- `deepfilternet_inference` owns all recurrent tensors and convolution caches.
- `deepfilternet_audio_enhancer` owns FFT/WOLA state and DF spectrum history.
- First hop loads 256 samples and emits silence; the second hop produces the
  first 512-sample frame. Reset/discontinuity restores that preroll behavior.
- Inference failure uses a unity spectrum through the same IFFT/WOLA, resets
  neural/DF history, and does not reset WOLA continuity.
- Realtime code performs no heap allocation after initialization.

## Verification gates

```powershell
python tools/generate_deepfilternet_assets.py --check
python -m unittest discover -s tests -v
git diff --check
build.bat debug config
build.bat release config
build.bat debug build
build.bat release build
```

Host tests cover artifact identity/mapping/unique model TU, quantized resets and
DF0 requantization, scalar math and WOLA, five PCM scenarios, failures and
deadlines, the 256-sample board/codec/DMA contract, telemetry aggregation/drop
behavior, and architecture direction.

Before flashing, state the recovery path: PF0/SWDIO, PF1/SWCLK, nRESET, GND and
VREF/3V3. Board acceptance requires measured `e2e_us max <= 16000`, no frame
gap/error, one model copy, Flash <= 1.8 MiB, SRAM <= 1.5 MiB, at least 2 KiB
runtime stack headroom, finalized arena capacity, and a clean 10-minute soak.
Only measured facts belong in `board_gate.json`.

Audio quality is evaluated by the user through live listening. The package
remains `engineering_ready_smoke_passed`; the incomplete 3,750-sample frozen
quality run prevents claiming release-final status.
