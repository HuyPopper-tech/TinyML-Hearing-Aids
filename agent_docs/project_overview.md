# Project Overview

The active product is the custom M55M1 PCB, running mono 16 kHz in 16-sample hops: DMIC FIFO24/DMA32 -> signed PCM24 -> fixed input conditioner -> R2 frontend -> one TFLM/Ethos-U55 controller -> gain/phase FIR reconstruction -> FIR/crossfade -> PCM16 I2S1 DMA -> TLV320DAC3100. The trial uses DMIC +12 dB, software +21 dB, a soft-knee compressor with 3/120 ms attack/release and an independent -1 dBFS input limiter. The same conditioned float hop feeds analysis and waveform filtering; raw PCM16 replay bypasses the conditioner.

`models/deepfir/` imports the immutable package `deepfir_r2_scratch_ptq_20261107_151431_366715` with compiled SHA-256 `69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27`. The controller has three INT16 inputs and four INT16 outputs. Encoded h/c state stays in graph order and both feedback lanes requantize using the contract.

Dependency direction remains `app -> services -> drivers -> board -> libs`. ISR work is bounded acknowledgement/descriptor handoff/signalling. DSP runs in the foreground with fixed buffers and one owner per history. `libs/` is unchanged.

Debug, ordinary Release and opt-in Release Probe use custom pins and preserve PF0/PF1 SWD. Build directories are `build-debug/`, `build/` and `build-release-probe/`. No NuMaker target remains. Obsolete standalone diagnostic applications and their exclusive modules were removed; app/main.c is the only application entry point. Ordinary Release excludes service stage timing and UART reporting while retaining deadline/fault enforcement and halted SWD statistics. Debug and Probe retain stage timing; only Probe ends after a finite run.

Host numeric/regression checks and both product builds pass. Compiled replay is in scope: its opt-in firmware and runner are implemented and build, but execution is BLOCKED without an authorized running backend. The package's Linux raw replay receipt is historical evidence; it was not rerun in this session. User-supplied Debug boot logs report arena use6976/65536B. A supplied Release Probe
completed60,000 hops with max total863us/capture-to-progress885us and zero reported
faults; its flashed BIN hash was not supplied. Final cleanup image board retest,
physical sample/slot alignment, acoustic latency, stack watermark and power remain
unmeasured. Ordinary Debug/Release are continuous; Probe alone is finite. `deployment_approved=false`; no board was flashed by the agent.

The frontend stores history in a ring while presenting the same chronological 256-sample Hamming/FFT frame required by the package. Guarded double quantization and model artifacts remain unchanged. See `docs/r2_custom_integration.md`, `docs/INPUT_LONG_TRIAL.md` and the current `docs/verification/2026-11-14-pipeline-cleanup.md` for verification boundaries.
