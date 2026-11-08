# Project Overview

The active product is the custom M55M1 PCB, running mono 16 kHz PCM16 in 16-sample hops: DMIC DMA -> R2 frontend -> one TFLM/Ethos-U55 controller -> gain/phase FIR reconstruction -> FIR/crossfade -> I2S1 DMA -> TLV320DAC3100.

`models/deepfir/` imports the immutable package `deepfir_r2_scratch_ptq_20261107_151431_366715` with compiled SHA-256 `69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27`. The controller has three INT16 inputs and four INT16 outputs. Encoded h/c state stays in graph order and both feedback lanes requantize using the contract.

Dependency direction remains `app -> services -> drivers -> board -> libs`. ISR work is bounded acknowledgement/descriptor handoff/signalling. DSP runs in the foreground with fixed buffers and one owner per history. `libs/` is unchanged.

The sole Debug/Release profiles use custom pins and preserve PF0/PF1 SWD. Build directories are `build-debug/` and `build/`. No NuMaker target remains. Obsolete standalone diagnostic applications and their exclusive modules were removed; app/main.c is the only application entry point.

Host numeric/regression checks and both product builds pass. Compiled replay is in scope: its opt-in firmware and runner are implemented and build, but execution is BLOCKED without an authorized running backend. The package's Linux raw replay receipt is historical evidence; it was not rerun in this session. Actual arena use, board initialization, full-hop deadline, audio and physical latency remain unmeasured. `deployment_approved=false`; no board was flashed.

See `docs/r2_custom_integration.md` and its timestamped acceptance evidence.
