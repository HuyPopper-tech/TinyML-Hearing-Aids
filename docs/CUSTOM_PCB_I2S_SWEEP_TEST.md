# Custom PCB I2S sweep test

> 2026-11-08: Historical diagnostic/evidence guide. Its old build directories,
> wrappers and presets were removed. The custom PCB is now the product target
> in `build-debug/` and `build/`; use [current integration instructions](r2_custom_integration.md).
> Commands and numeric observations below describe the earlier session, not the current R2 acceptance.
> The diagnostic applications, exclusive modules and register-experiment scripts have now been removed.
> This document preserves historical observations; commands and source paths below are retired.


This standalone image tests only the TLV320DAC3100 headphone output on the
custom PCB in `docs/NuGestureAI.pdf`. It generates its own PCM; microphone,
UART, BLE and TinyML are absent. The NuMaker product source, root CMake,
presets, build wrappers, main board binding and codec driver are unchanged.
The existing custom DMIC image is also unchanged.

## Build and run

From the repository root:

```powershell
.\tools\build_custom_i2s.bat debug
.\tools\build_custom_i2s.bat release
```

The independent CMake source is `cmake/custom_i2s/`. Outputs are
`build-custom-i2s-debug/M55M1_CustomI2s.axf` and `.bin`, with corresponding
Release outputs under `build-custom-i2s-release/`. The build does not replace
the root compilation database; its own `compile_commands.json` is in its build
directory.

In VS Code Run and Debug select:

- `M55M1: Custom I2S Sweep Flash + Debug (OpenOCD Nuvoton)` to build, program
  APROM, verify and enter `main`. Continue execution to start audio.
- `M55M1: Custom I2S Sweep Attach (OpenOCD Nuvoton)` to inspect an already
  programmed Debug image without writing flash.

These configurations follow the existing CH549/OpenOCD workflow. This session
did not program, reset or attach to hardware, so their operation on the board
remains unverified. Flashing replaces the custom DMIC image in APROM; the two
images are separate programs. If programming reports a transport/restore
error, use the existing recovery guidance in `docs/M55M1_OPENOCD_DEBUG.md` and
collect state before retrying.

J4 retains PF0=SWDIO, PF1=SWCLK, nRESET, VREF/3V3 and GND. The sweep does not
change PF0/PF1. A CH549 probe without a physical reset connection cannot
assert nRESET through an OpenOCD setting.

## Pin and audio contract

Verified visually against the MCU and DAC sheets of `docs/NuGestureAI.pdf`:

| Signal | Custom PCB pin | Expected running signal |
| --- | --- | --- |
| Codec DIN | PA8, I2S1_DO | Signed-16 stereo, same PCM in L/R |
| MCLK | PA10, I2S1_MCLK | 4.096 MHz |
| BCLK | PA11, I2S1_BCLK | 512 kHz |
| WCLK / LRCK | PB0, I2S1_LRCK | 16 kHz |
| SDA | PA4, I2C0_SDA | 400 kHz control bus, codec address 0x18 |
| SCL | PA5, I2C0_SCL | 400 kHz control bus |

The source uses the current main driver's actual 16 kHz profile: Philips I2S,
master, 16-bit stereo, PDMA0 channel 0, two scatter-gather descriptors and
**16 mono samples / 1 ms per descriptor**. Each FIFO transfer is a 32-bit
stereo frame. Some older driver prose still mentions 256 samples; that is not
the current header/implementation contract.

The core uses 220 MHz APLL0. APLL1 is requested at 475136006 Hz, divided by
two and then 58 for I2S1, matching the main clock plan. Initialization rejects
unready HXT/APLLs, wrong calculated clocks or stuck I2C lines. GPIO recovery
uses up to nine open-drain SCL pulses and a STOP before I2C0 takes ownership.
The unchanged codec driver checks actual I2S/MCLK results and uses bounded
I2C retries.

U6 RESET is pulled up to VDD through R18; the schematic does not connect it to
MCU nRESET. The test uses the driver's existing software reset and startup
settle sequence. A codec that remains unresponsive may require a board power
cycle.

The MCU datasheet Figure 4.1-1 (page 39) puts PA4/PA5 in VDDIO0; the schematic
connects VDDIO0 to VDD_MCU. PA8/PA10/PA11/PB0 use the normal VDD domain. The
schematic's separate 1.8 V VDDIO1 rail therefore does not imply 1.8 V I2S on
these pins. Actual rail and waveform levels still require measurement.

## Generated audio and debugger evidence

Each six-second cycle contains a linear 200 Hz to 6000 Hz sine sweep lasting
five seconds, followed by one second of zero PCM. The first and last 10 ms
fade to/from silence. PCM peak is 2048 (about -24 dBFS); the codec retains the
main board's -12 dB digital gain ceiling. Start with headphones off the ears
and confirm a comfortable measured output level before listening.

All synthesis and refill happen in foreground. The PDMA ISR only runs the
existing codec completion handler. A BUSY submission retains the exact
pending PCM block. A DMA or submission failure stops playback without an
automatic restart. The initial DMA descriptors contain intentional silence.

Add `g_custom_i2s_output_status` to the debugger Watch view. It is stored in
non-cacheable SRAM so MEM-AP reads do not depend on CPU cache writeback:

- `board_status`, `codec_status`, `start_status`: zero means success;
  `-1` means that step has not been attempted. `stop_status` stays `-1`
  until a stop is requested.
- `board_stage`: last board boundary, from `m55m1_audio_board_stage_t`.
- `core_hz=220000000`, `mclk_hz=4096000`, `running=true` after startup.
- `completed_blocks`: should rise by about 1000 per second while running.
- `queued_blocks`: should follow completed blocks as foreground refills slots.
- `underrun_blocks`, `dma_errors`, `i2c_failures`: should remain zero.
- `last_i2c_page/register/phase/status`: retained context after an I2C failure.

`g_custom_i2s_fault_cfsr` and `g_custom_i2s_fault_hfsr` retain hardware fault
flags without UART output. They start at zero. Halting/stepping during audio
can itself starve foreground refill and increase underruns; use free-running
observations for acceptance.

## Verification on 2026-10-25

- Arm Compiler 6.24 Debug and Release images linked and produced BINs.
- All 103 repository unittest cases passed, including an executed host test
  for sweep frequency windows, amplitude/fades, silent gap, repeatability,
  backpressure and failure-stop behavior.
- `git diff --check` passed.
- SHA-256 comparison confirmed the inspected main source/configuration files
  were unchanged; the baseline is `diagnostics/custom-i2s/main-before.json`.
- Debug link map places codec DMA descriptors, stereo buffers and the debugger
  snapshot in non-cacheable SRAM (`0x20230000` region).

These are source, host-test and build results. Board acceptance is still open:
measure all three clocks and signal levels at U6, confirm I2C ACK, hear the
sweep on both jack channels and the one-second gap, then run for at least
60 seconds with no growing DMA error or underrun count. No board clock,
acoustic output, output level or continuous playback timing was measured here.
