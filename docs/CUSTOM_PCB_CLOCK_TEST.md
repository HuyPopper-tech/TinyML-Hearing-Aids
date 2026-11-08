# Custom PCB: replay the DMIC board initialization

> 2026-11-08: Historical diagnostic/evidence guide. Its old build directories,
> wrappers and presets were removed. The custom PCB is now the product target
> in `build-debug/` and `build/`; use [current integration instructions](r2_custom_integration.md).
> Commands and numeric observations below describe the earlier session, not the current R2 acceptance.
> The diagnostic applications, exclusive modules and register-experiment scripts have now been removed.
> This document preserves historical observations; commands and source paths below are retired.


The current M55M1_CustomClock image calls the unchanged
`m55m1_audio_board_init()` from `board/m55m1_audio.c`, compiled with the same
custom-DMIC definitions: M55M1_CUSTOM_DMIC_TEST=1, DAC off, HXT 24 MHz,
UART2, and DMIC gain 0 dB. It does not link the TinyML app or models.
This replaces the earlier four-case HXT/HIRC suite in the clock-test build.

## Build and run

From the repository root:

```bat
tools\build_custom_clock.bat debug
```

Outputs: `build-custom-clock-debug/M55M1_CustomClock.axf` and `.bin`.
Select **M55M1: Custom Clock Flash + Debug (OpenOCD Nuvoton)** in Cortex-Debug.
At main, open UART2 at **115200 8N1**, then Continue without stepping through
the clock operations. TX is PC5; RX is PC4. The console uses PDMA1 channel 0,
as in the existing custom-DMIC image. SWD remains PF0/PF1 and the existing
nRESET header. Capture is not started, so no PDM clock is generated on PA1.

Programming replaces the current APROM image. Reflash the custom-DMIC or
product image to return to it. The replay does not write CONFIG or LDROM.
The Attach profile never flashes.

`tools\build_custom_clock.bat debug config` configures without building and
updates the root compilation database for this image. Restart clangd after
switching profiles. `build.bat debug config` restores the product editor profile.

## What is reproduced

1. Initialize the existing UART2/PDMA1 transmitter and flush the original DMIC
   boot banner before entering board initialization.
2. Call the actual DMIC board bootstrap once, with the same 16 kHz profile and
   null playback binding/callbacks used by the DMIC-only transport.
3. The existing board source switches CPU to HIRC, waits for HXT, calls the BSP
   to configure APLL1 at 196.608 MHz, then APLL0 at 220 MHz and switches CPU to it.
4. The observer stores a register snapshot at each existing progress callback.
   It sends only the same boot-stage line and flushes through the same
   UART DMA driver as the DMIC diagnostic. Register dumps are deferred until
   board initialization returns.
5. Preserve the final return code and register state immediately. Print the trace,
   then stay in the foreground loop. There are no retries, extra PLL
   ready waits, 1 ms off delays, or alternative clock recipes in the replay.

The initialization source is shared, not copied or patched. Observer snapshot
writes add a small amount of execution time, so this is not cycle-identical to
the original image. The UART banner intentionally still says custom DMIC to
preserve its pre-init traffic; [REPLAY] and [TRACE] identify the new image.

## Read the result

UART prints the original boot stages, then:

```text
[REPLAY] board_status=... stages=... overflow=... UART_drop=... UART_dma_err=...
[TRACE] stage=... core_hz=... SRCCTL=... STATUS=...
[REG] APLL0CTL=... APLL0SEL=... APLL1CTL=... APLL1SEL=... SCLKSEL=... SCLKDIV=...
...
[FINAL] stage=... core_hz=... SRCCTL=... STATUS=...
[DONE] DMIC init replay only; capture not started; reset to repeat
```

Watch **g_custom_clock_replay_status**, replacing the previous
g_custom_clock_status watch expression:

- board_status: -1 pending, 0 success, 1 HXT timeout, 2 APLL1 timeout,
  3 APLL0 timeout, 4 clock mismatch. These are the real board return codes.
- completed_stages, stages[]: ordered snapshots from the real progress callbacks.
- final: snapshot immediately after board init returns, before further logging.
- done: 1 after return, including on initialization failure.
- trace_overflow: must be 0; bounded storage holds all 16 defined stages.

The status is in non-cacheable memory for SWD inspection. Core frequency in the
snapshot is the BSP's SystemCoreClock, not an independent frequency measurement.

Success should reach stage 15 and final.core_hz=220000000. An APLL1 timeout
should leave the last stage at 4 and preserve the failed APLL1 state in final.
The firmware never converts that failure to success by retrying.

Send the complete log from [BOOT] through [DONE]. If the isolated replay
passes, repeat with reset or a power cycle to investigate intermittent startup.
This test does not start DMIC/LPPDMA capture or establish microphone health.
