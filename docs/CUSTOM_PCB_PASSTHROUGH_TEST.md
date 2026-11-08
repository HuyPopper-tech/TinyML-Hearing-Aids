# Custom PCB microphone-to-DAC passthrough test

> 2026-11-08: Historical diagnostic/evidence guide. Its old build directories,
> wrappers and presets were removed. The custom PCB is now the product target
> in `build-debug/` and `build/`; use [current integration instructions](r2_custom_integration.md).
> Commands and numeric observations below describe the earlier session, not the current R2 acceptance.
> The diagnostic applications, exclusive modules and register-experiment scripts have now been removed.
> This document preserves historical observations; commands and source paths below are retired.


This standalone image forwards captured PCM16 unchanged from the custom-board
microphone to both TLV320DAC3100 headphone channels. It reuses the existing DMIC,
LPPDMA, codec and PDMA implementations. No model or inference service is linked.
The product entry point and build selection are unchanged. Custom I2S/I2C pin
selection is compiled only for the custom board; the preprocessed product audio
implementation was compared byte-for-byte before and after the change.

## Build and flash

```bat
tools\build_custom_passthrough.bat debug
```

Outputs:

- `build-custom-passthrough-debug/M55M1_CustomPassthrough.bin`
- `build-custom-passthrough-debug/M55M1_CustomPassthrough.axf`

The BIN loads at APROM `0x00100000`. In Cortex-Debug select
**M55M1: Custom Passthrough Flash + Debug (OpenOCD Nuvoton)**, press F5, then
Continue from `main`. This builds and programs the matching BIN with the existing
CH549/OpenOCD configuration. The separate Attach profile only halts/attaches;
it does not program an image. No board was flashed as part of host verification.
The existing DMIC and I2S sweep binaries remain separate.

## Connections and audio format

| Signal | Custom pin | Configuration |
| --- | --- | --- |
| Microphone CLK/DATA | PA1/PA2 | DMIC0 channel 2, rising latch, HPF enabled |
| I2S DIN to codec | PA8 | I2S1 DO, PCM16 duplicated to L/R |
| MCLK | PA10 | 4.096 MHz |
| BCLK | PA11 | 512 kHz |
| LRCK | PB0 | 16 kHz |
| Codec SDA/SCL | PA4/PA5 | I2C0, 400 kHz, 7-bit address 0x18 |
| UART TX/RX | PC5/PC4 | UART2, 115200 8N1, PDMA1 channel 0 TX |
| SWD | PF0/PF1 | Preserved; nRESET wiring is unchanged |

The microphone uses the last tested NuMaker recipe: HXT 24 MHz, APLL0/core
220 MHz, APLL1 requested at 475136006 Hz, PDM 2.048 MHz, OSR 128, gain +30 dB,
and the normal startup sequence without the custom reset/warm-up override.
Capture uses LPPDMA0 channel 0. Playback uses PDMA0 channel 0 with static
ping-pong buffers. Each hop is 16 mono samples, or 1 ms at 16 kHz. The common
audio PLL supplies both rates. Clock values are firmware calculations, not
external frequency measurements.

The codec gain is limited to -12 dB as in the existing board binding. PCM is
not additionally scaled, filtered in software, or processed by TinyML. For this
diagnostic only, D-cache is disabled so the Watch snapshot is directly visible
over SWD. This is not a product performance or acoustic-latency measurement.

Use headphones, initially off your ears. Live microphone-to-speaker playback
can produce acoustic feedback; keep a speaker away from the microphone.

## Runtime checks

UART reports boot stages, then a line identifying `gain_db=30 startup=main`.
Once per second `[PASS]` reports sample count, cumulative captured/forwarded/DAC
blocks, input peak/mean magnitude, clipping, overruns, underruns and DMA errors.
Approximately 16000 samples and 1000 blocks per second are expected. Startup
may contain a transient; check that loss/error counters do not keep increasing
while running freely. Halting at breakpoints disrupts live DMA refill and can
add overruns/underruns; reset before an uninterrupted listening test.

Watch `g_custom_passthrough_status`:

- `report.board_status`, `report.codec_status`, `start_status`: 0 on success.
- `report.core_hz`: 220000000; `report.mclk_hz`: 4096000.
- `running`: true after capture and playback start successfully.
- `captured_blocks`, `forwarded_blocks`, `playback_completed_blocks`: increasing.
- `input_peak`, `input_mean_abs`: updated once per second.
- `health`: capture/playback DMA errors and overrun/underrun counters.
- `i2c_failures`, `last_i2c_*`: codec-control failure evidence.

The foreground loop retains a pending PCM block while playback is BUSY and
does not overwrite it with a newer capture. Persistent backpressure may still
cause the existing capture driver's overrun counter to rise. A submit error
stops capture/playback immediately in the foreground; DMA errors are detected
at the next one-second health snapshot and stop the transport. UART continues
draining after a failure. ISRs only acknowledge DMA and publish buffer state.

Host checks cover custom/product pin selection, exact PCM values including
INT16_MIN/MAX, BUSY retries without consuming another capture, init/start/submit
failure paths, counter wrap, DMA-fault shutdown, and flash/debug image matching.
Compilation and these checks do not establish audible output on the PCB.
