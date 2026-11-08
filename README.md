# TinyML Hearing Aids — custom M55M1 PCB

Continuous-model low-power update: CPU Idle between work, Release UART gated off,
and audited unused peripherals/codec blocks. See [power verification and board A/B
procedure](docs/continuous_low_power.md). Current saving and board timing remain
unmeasured; no board was flashed.

Source cleanup, Doxygen/style checks and current build evidence: [verification receipt](docs/verification/2026-11-08-source-cleanup/README.md). Training packages and raw local diagnostics are Git-ignored; the firmware model and small verification fixture are checked in.

Bare-metal DeepFIR firmware for the custom PCB. The active model is the supplied R2 gain/phase controller, ABI `deepfir_ablation_r2_ac_v1_gain_phase_split_range_hc_v2`. Source integration and host/build checks do not grant deployment approval.

The product path is mono PCM16 at 16 kHz, one 16-sample hop:

```text
DMIC DMA -> frontend -> one TFLM/Ethos-U controller -> gain/phase reconstruction
  -> FIR/crossfade -> PCM16 -> I2S1 DMA -> TLV320DAC3100
```

`app -> services -> drivers -> board -> libs` remains the dependency direction. Only the inference service accesses model artifacts. DSP/history live in services; DMA lives in drivers/board. Runtime buffers have fixed capacity.

## Build and verify

Use CMake/Ninja and Arm Compiler for Embedded:

```powershell
rtk proxy cmd /c build.bat debug config
rtk proxy cmd /c build.bat debug build
rtk proxy cmd /c build.bat release config
rtk proxy cmd /c build.bat release build
rtk python -m unittest discover -s tests -v
rtk git diff --check
```

POSIX equivalents use `./build.sh`. Only `m55m1-debug` and `m55m1-release` presets remain, writing `build-debug/` and `build/`. Both target the custom PCB; NuMaker build/pin branches and obsolete build wrappers/directories were removed. Obsolete standalone diagnostic applications and their exclusive modules were removed; app/main.c is the sole entry point. Normal scripts disable compiled replay and diagnostic capture/probe modes.

Product pins: DMIC PA1 clock / PA2 data; I2S1 PA8 data / PA10 MCLK / PA11 BCLK / PB0 LRCK; I2C0 PA4 SDA / PA5 SCL; debug UART2 PC5 TX / PC4 RX. PF0/PF1 retain SWD. Before programming, read the [OpenOCD recovery guide](docs/M55M1_OPENOCD_DEBUG.md); the CH549 probe has no physical nRESET output. Programming needs separate authorization.

See [R2 integration and acceptance](docs/r2_custom_integration.md) for ownership, numeric tolerances, exact model identity, memory, replay commands and remaining board gates. The source package stays immutable; `models/deepfir/` contains runtime artifacts/provenance, while `diagnostics/r2_custom_integration_20261108_105000/` contains logs, archived prior artifacts and acceptance receipts.
