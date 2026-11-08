# Custom PCB: DMIC + UART2 bring-up

> 2026-11-08: Historical diagnostic/evidence guide. Its old build directories,
> wrappers and presets were removed. The custom PCB is now the product target
> in `build-debug/` and `build/`; use [current integration instructions](r2_custom_integration.md).
> Commands and numeric observations below describe the earlier session, not the current R2 acceptance.
> The diagnostic applications, exclusive modules and register-experiment scripts have now been removed.
> This document preserves historical observations; commands and source paths below are retired.


This image tests the microphone on `docs/NuGestureAI.pdf` (MCU, BLE-UART and
Microphone sheets). It runs independently of the product image: no DAC/I2S,
codec I2C, TinyML, NPU, or BLE initialization. `app/main.c` remains the product
entry point; the test entry point is `app/test_custom_dmic_main.c`.

The default custom capture startup resets the DMIC peripheral, clears FIFO and resets
DSP state, enables channel 2 for 10 ms of microphone clock, then clears startup
samples before configuring/enabling capture DMA. FIFO-not-empty or DSP-reset
timeout causes `START_FAILED`; DMA is not started on those failures. The vendor
clear/reset helpers have finite spin limits, not calibrated wall-clock timeouts.
The configuration log identifies this sequence with `startup=reset-warmup10ms`.
These steps are compiled only with `M55M1_CUSTOM_DMIC_TEST`; product startup is
unchanged. This removes an initialization variable; it does not establish that
the microphone or DATA connection works. After flashing, reset/power-cycle and
compare quiet/speech logs without earlier GDB register modifications.

## Compare the NuMaker configuration on the custom PCB

Build the known-working NuMaker DMIC configuration with only the custom pin
and UART bindings retained. In a terminal with Arm Compiler 6, CMake and Ninja:

```bat
cmake --preset custom-dmic-debug -DM55M1_CUSTOM_DMIC_NUMAKER_CONFIG=ON -DM55M1_DMIC_GAIN_DB=30
cmake --build build-custom-dmic-debug
```

This overwrites the existing `build-custom-dmic-debug/M55M1_CustomDmic.axf`
and `.bin`, so the existing custom DMIC flash/debug configuration still applies.
APLL1 uses the NuMaker request of 475136006 Hz, with the same DMIC module
clock, sample-rate setup and channel/DMA startup order as the NuMaker image.
There is no custom peripheral reset or 10 ms warm-up in this mode. The capture
format stays mono PCM16 at 16 kHz, OSR 128, channel 2, rising-edge latch and
16 samples per DMA block; the default driver HPF remains enabled. The log reads
`gain_db=30 startup=main config=numaker`.

DMIC remains PA1/PA2 and UART2 remains PC5/PC4 at 115200 8N1 with PDMA1 TX.
DAC/I2S is disabled, leaving PF0/PF1 available for SWD. The product configuration
and its preprocessed audio implementation are unchanged by this test option.
Reset after flashing; do not carry over the GDB pull/HPF modifications. Compare
with the original NuMaker gain-30 image, not the separate gain-0 image.

To restore the earlier custom configuration in the same build directory:

```bat
cmake --preset custom-dmic-debug -DM55M1_CUSTOM_DMIC_NUMAKER_CONFIG=OFF -DM55M1_DMIC_GAIN_DB=0
cmake --build build-custom-dmic-debug
```

For persistent zero PCM with L/R measured at GND, pause the CPU and run
`source D:/TinyML-Hearing-Aids/tools/debug/pa2_clock_high_test.gdb`.
The script temporarily makes PA1 a GPIO input with weak pull-up, checks that
CLK reads high, waits 15 seconds for a DC measurement, then runs the existing
PA2 weak-pull test: four cycles, 15 seconds per UP/DOWN level (about 136 seconds
in total including the initial CLK measurement). With L/R low the mic's DATA output is high impedance during
CLK high (MP34DT05-A datasheet, Table 6). Measure PA1 near VDD first, then PA2:
pull-up should approach VDD and read PIN=1; pull-down should approach 0 V and
read PIN=0. Neither pin is driven push-pull. The script restores the original
PA1 mux/mode, both pull settings and the register lock state. If interrupted,
run `pa2_clock_high_restore` or reset. Reset before collecting audio logs:
clock interruption and halting can invalidate the capture state. This checks
static electrical levels, not mic audio, PDM timing or continuity to the mic.

If that test passes, test the DMIC routing without reflashing: reset/run the
custom image, pause, and source `tools/debug/dmic_dc_input_test.gdb` using its
absolute path. It holds CLK high with weak pull-up, disables channel-2/3 HPF
and weakly pulls PA2 high. Check `DC_UP: PA1 CLK=1 PA2 DATA=1`, then Continue
and collect a few UART windows. Pause, run `dmic_dc_down`, check DATA=0 and
Continue for more windows. With HPF off, constant high/low PDM inputs should
produce positive/negative DC respectively; do not use these logs as audio.
Both levels remain set until changed manually. Finish while paused with
`dmic_dc_restore`, then reset. The script restores HPF, pin fields and lock
state; it leaves DMIC clocks/DMA running during the test. Correct polarity
response checks static input routing through the PCM path, not MHz timing or
the microphone output. If interrupted, restore or reset before other tests.

## Wiring

| Signal | MCU pin | PCB connection |
| --- | --- | --- |
| DMIC1_CLK | PA1 | U4 pin 3, CLK |
| DMIC1_DAT | PA2 | U4 pin 4, DOUT |
| UART2 TX | PC5 | `UART2-TXD`, U7 BLE RXD pad 18; connect USB-UART RX |
| UART2 RX | PC4 | `UART2-RXD`, U7 BLE TXD pad 19; optional USB-UART TX |
| Ground | GND | USB-UART GND and board GND |

Use a **3.3 V TTL** USB-UART adapter, 115200 baud, 8 data bits, no parity,
1 stop bit, no flow control. RX+GND on the adapter is enough to receive logs;
the test does not need commands. U7 may remain fitted for RX-only logging:
PC5 drives its RX input. Its TX output shares PC4 with adapter TX, so isolate
that output before also connecting adapter TX. Do not connect RS-232 levels.

U4 is MP34DT05TR-A; R10 pulls L/R low and R9 is unpopulated. The test uses
DMIC0 channel 2 (DMIC1 pin pair), rising-edge latch, mono PCM16, 16 kHz,
16 samples per DMA block, OSR 128, PDM clock nominally 2.048 MHz. This PDM clock
is within the 1.2..3.25 MHz normal-mode range in the
[ST datasheet](https://www.st.com.cn/resource/en/datasheet/mp34dt05-a.pdf).

The clock plan uses the PCB's Y1 **24 MHz crystal**, APLL0 for the **220 MHz
core**, and APLL1 at nominally 196.608 MHz for DMIC. The board switches SCLK to
HIRC before reprogramming APLL0, and uses the vendor clock API for power level,
bus dividers and flash access cycles. HXT/APLL readiness and calculated core
frequency are checked before capture. The UART stays on HIRC, so early boot
logs remain available even when HXT fails. Clock logs are register-derived
calculations using `__HXT=24000000UL`, not an external frequency measurement.

## UART TX through DMA

The custom image uses **PDMA1 channel 0**, request `PDMA_UART2_TX`, to copy
8-bit bytes from a fixed 2048-byte SRAM queue into UART2 DAT. PC5/PC4 and
115200 8N1 remain unchanged. The transfer setup follows Nuvoton's
[UART_PDMA example](https://github.com/OpenNuvoton/M55M1BSP/blob/master/SampleCode/StdDriver/UART_PDMA/main.c),
with a dedicated PDMA1 binding for this image.

The TX queue lives in the existing non-cacheable SRAM region and is aligned
to 32 bytes. Foreground copies each whole message before publishing it; a
memory barrier precedes enabling the UART DMA request. Bytes remain reserved
until transfer-done, including when the queue wraps. No DMA clean is needed
for this non-cacheable storage. The foreground loop polls completion and
starts the next contiguous span without waiting. Neither UART byte IRQs nor
PDMA TX IRQs are used. Microphone capture retains LPPDMA0; the NuMaker
product keeps its existing UART IRQ transmitter and I2S PDMA0 binding.

`debug_uart_tx_flush_blocking()` waits for queued DMA spans and the final UART
stop bit only during boot. Its finite spin budget prevents an endless wait;
the budget is not a calibrated wall-clock timeout. It does not run during
capture. DMA abort or an exhausted boot-flush budget latches
`g_dma_errors=1` in the TX driver, disables further TX DMA requests and rejects
new messages until reset; it does not silently retry a partially sent message.
When TX is faulted, inspect the DMA error count with a cache-coherent/core-side
debugger read; an error message cannot be delivered over the failed transmitter.

The boot banner identifies `TX=PDMA1`. Periodic reports include
`tx_dma_err=0` as well as `tx_drop`. Press Continue when Cortex-Debug stops at
`main`; the application has not initialized UART/DMIC at that breakpoint.

## Build

From the repository root in PowerShell, with CMake available:

```powershell
$env:Path = "C:\Program Files\ArmCompilerforEmbedded6.24\bin;C:\ST\STM32CubeCLT_1.20.0\Ninja\bin;$env:Path"
cmake --preset custom-dmic-debug
cmake --build --preset custom-dmic-debug
Copy-Item .\build-custom-dmic-debug\compile_commands.json .\compile_commands.json -Force
```

Restart clangd after replacing the root compilation database. The dedicated
build directory contains `M55M1_CustomDmic.axf` (symbols), `M55M1_CustomDmic.bin`
(raw APROM image), and the linker map. Release uses the corresponding
`custom-dmic-release` configure/build preset and `build-custom-dmic-release/`.
Debug uses `-g`; Release uses `-O3 -DNDEBUG`. Both use identical pins and clocks.
These presets return before model-package configuration and use only the BSP
sources needed by capture and UART; they do not load the model package.

The initial microphone DSP gain is **0 dB**, deliberately lower than the product
build's +30 dB. If needed after checking supply, PDM and clipping, configure a
different hardware gain and rebuild, for example:

```powershell
cmake --preset custom-dmic-debug -DM55M1_DMIC_GAIN_DB=12
cmake --build --preset custom-dmic-debug
```

The chosen gain persists in this test directory's CMake cache. Pass
`-DM55M1_DMIC_GAIN_DB=0` to restore the baseline. More gain amplifies both signal
and noise; it does not establish improved SNR.

## Flash and SWD recovery

Custom PCB J4: **1=VDD/3V3, 2=PF1/SWCLK, 3=PF0/SWDIO, 4=nRESET, 5=GND**.
This image leaves PF0/PF1 as ICE pins and never initializes the old I2S0 binding.
The CH549 probe in `tools/openocd/m55m1-ch549.cfg` has no physical nRESET output;
`reset_config none` and software SYSRESETREQ remain unchanged.

Close any other OpenOCD/debug session using the same probe. To program the test
image with the existing APROM-only, verified programming procedure:

```powershell
& .\tools\openocd\.runtime\bin\openocd.exe `
  -s .\tools\openocd `
  -s .\tools\openocd\.runtime\share\openocd\scripts `
  -f .\tools\openocd\m55m1-ch549.cfg `
  -c init -c "poll off" `
  -c "m55m1_program {build-custom-dmic-debug/M55M1_CustomDmic.bin}" `
  -c shutdown
```

This overwrites the currently installed APROM application. After successful
verify, open the UART terminal then manually reset/power-cycle the board to
run the image.

For Cortex-Debug in VS Code:

1. Stop any other OpenOCD server/debug session using the probe.
2. Connect board power and J4 SWD, plus PC5 to USB-UART RX and common GND.
   Open the serial terminal at 115200 8N1.
3. In **Run and Debug**, select
   **M55M1: Custom DMIC Flash + Debug (OpenOCD Nuvoton)** and press **F5**.
4. The pre-launch tasks configure/build `custom-dmic-debug`. Cortex-Debug starts
   the existing Nuvoton OpenOCD configuration, programs/verifies the custom BIN
   through `m55m1_program`, and stops at `main` with matching custom AXF symbols.
5. Press **Continue/F5** to initialize the clocks, UART2 and microphone. Startup
   and one-second microphone logs appear in the serial terminal.

The custom launch profile uses the existing Cortex-Debug GDB/toolchain settings
and leaves OpenOCD running for debugging. Stop ends that session without erasing
the image. **M55M1: Custom DMIC Attach (OpenOCD Nuvoton)** uses the matching custom
AXF without building or programming. The profiles without the Custom DMIC prefix
and the standalone Debug/Release APROM flash tasks target the NuMaker model
pipeline. Select the board-specific profile before F5.

The GDB monitor command deliberately uses
`monitor m55m1_program {build-custom-dmic-debug/M55M1_CustomDmic.bin}` relative
to the configured workspace `cwd`. Expanding `${workspaceFolder}` inside that
command can introduce Windows backslashes that GDB MI consumes before OpenOCD
receives the filename (for example `D:\TinyML-Hearing-Aids` becomes
`D:TinyML-Hearing-Aids`). The executable/symbol path is still absolute.

If halt/access fails, follow `docs/M55M1_OPENOCD_DEBUG.md`: disconnect board
power and SWDIO/SWCLK, reapply board power, then reconnect GND and SWD. Keep
nRESET released. This probe cannot perform connect-under-reset. Do not change
security configuration or mass-erase to work around a halt failure.

## Read the logs

Before capture, `[BOOT] stage=` lines trace the existing board initialization:
0=entry, 1=HIRC selected, 2=HXT wait, 3=HXT ready, 4/5=APLL1 wait/ready,
6/7=APLL0 wait/ready, 8=220 MHz SCLK selected, 9=module clocks,
14=timebase, 15=peripherals ready. Stages 10..13 belong to DAC setup and are
skipped. `INIT_FAILED board_status=` uses 1=HXT timeout, 2=APLL1 timeout,
3=APLL0 timeout, 4=frequency mismatch. `START_FAILED` means DMIC could not
start with the requested configuration; no capture is active after that error.

`[CLOCK]` prints the calculated core/APLL clocks. The `[DMIC] fs_hz=...` line
announces requested capture settings; periodic reports begin only after the
driver accepts the sample rate and starts DMA. A report looks like this
**illustrative example, not a measurement**:

```text
[DMIC] DATA samples=16000 min=-800 max=900 peak=900 mean_abs=120 clip=0 overrun=0 dma_err=0 tx_drop=0 tx_dma_err=0
```

- Each report covers approximately one second. `samples` should be near 16000;
  the 1 ms boundary can shift a window by one block (16 samples).
- `min`, `max`, `peak`, and `mean_abs` are raw PCM16 levels over that whole
  window. Speech/clapping near the microphone should change the levels;
  these values are not calibrated SPL. A flat zero signal can still have
  `samples=16000`, so data flow alone does not prove a working microphone.
- `clip` counts samples with absolute magnitude >=32760 in the current window.
- `overrun` and `dma_err` are cumulative DMIC counters; `tx_drop` is the
  cumulative rejected UART-message count. After startup these should remain 0.
- `NO_DATA samples=0` means no DMA block reached the foreground during the
  window. This heartbeat continues even when capture stalls.

Logging is queued without waiting while microphone capture runs. UART TX uses
the separate non-cacheable byte ring described above. Capture DMA retains its
existing non-cacheable two-slot ring; foreground copies each completed capture
block before calculating levels.

## DMA TX verification, 2026-10-25

All 101 host tests passed. The Custom DMIC Debug/Release and NuMaker
Debug/Release builds completed; all 966 OpenOCD files matched their pre-change
hashes. The Custom DMIC DMA Debug image was programmed once and verified.
Its SHA-256 is `407f3eca2c1d803358e50130c49430267dc3307e79069aaeaf1e72e48e8d93c0`.

With that image running, PC snapshots reached `dmic_diagnostic_poll` and the
TX polling path. PDMA1 was read at its BSP-defined base `0x40201000`.
Its completed channel-0 descriptor had OPMODE=STOP, destination
`0x4024E000` (UART2 DAT) and source addresses `0x20230C0B` then
`0x20230C76` in separate snapshots. Request selection was 8 (UART2 TX),
channel 0 remained enabled and abort flags were zero. CFSR/HFSR were zero.

The non-cacheable TX storage at `0x20230520` contained periodic
`[DMIC] DATA` reports with 16000/16016 samples and
`overrun=0 dma_err=0 tx_drop=0 tx_dma_err=0`. Inspected PCM levels were zero;
sample counts alone do not establish microphone response.

COM13 at 115200 8N1 received zero bytes during an eight-second capture of the
resumed IRQ image and another eight-second capture of the resumed DMA image.
The change demonstrates MCU-side DMA progress, not successful external UART
reception or a resolved pin/probe/baud fault. The core was resumed and COM13
released after inspection. Raw evidence is under
`diagnostics/uart-dma-2026-10-25/`.

The normal SRAM queue metadata and driver state may remain dirty in D-cache;
zero debugger reads of those variables are not reliable proof of an empty
queue or uninitialized driver. Use the non-cacheable bytes, hardware
registers and CPU-side execution evidence when interpreting state.

## Firmware investigation on the custom PCB, 2026-10-25

The observations below used the earlier UART-IRQ image, before the DMA change.
Its 20176-byte image and UART2 interrupt breakpoint are historical evidence.

An attach-only investigation read the installed APROM without programming or
resetting the MCU. All 20176 bytes matched the local Custom DMIC Debug BIN;
both SHA-256 values were
`5ed06c5e468895de2c6a33d5ec9e8bbfa032b6808e8b38b04c3b9cf12c0efe8b`.

The non-cacheable DTCM TX storage contained complete periodic reports with
16000 or 16016 samples, nonzero bipolar levels, and zero reported overruns,
DMA errors and rejected TX messages in the inspected windows. This storage
retains bytes after transmission; its contents do not mean the queue is stuck.
A hardware breakpoint in `UART2_IRQHandler` then caught ASCII `[` (`r0=0x5B`)
immediately before `str r0,[r1]`, with `r1=0x4024E000` (UART2 DAT).
Single-stepping advanced past that store. The call stack led back to
`dmic_log` and `dmic_diagnostic_poll`. CFSR and HFSR were zero in the snapshots.

Runtime pin mux was `SYS_GPC_MFP1=0x00000808` (PC4 RX/PC5 TX), UART2 LINE was
`0x3`, and BAUD was `0x30000066`. A temporary 50-baud test also produced
alternating digital PC5 readings through SWD. LINE and BAUD were restored,
breakpoints were removed, the core was resumed, and OpenOCD was closed.
COM13 still received zero bytes during the capture, including an eight-second
capture with OpenOCD closed and the application running.

These observations establish capture progress, log generation, and execution
of UART TX writes. They do not establish external UART waveform quality,
physical baud accuracy, acoustic response, or the cause of the missing COM13
data. No firmware fix or further flash was justified by this evidence. The
next separating test is an external capture of PC5 and probe RX at 115200, or
reception with an independent USB-UART adapter.

A separate direction check temporarily made PC5 a GPIO input with a weak
pull-up while the core was halted. It read high in all 60 samples per phase:
host idle, host BREAK, host transmitting `0x00`, and host transmitting `0xFF`.
This did not reveal a connection to a probe TX output that followed host data;
it does not prove continuity to the probe's actual receive input. The original
PC5 mux, pull configuration and SYS register lock were restored before resume.

Raw session evidence is under `diagnostics/uart-probe/`:
`firmware-runtime-2026-10-25.json`, `uart2-irq-gdb-2026-10-25.txt`,
`pc5-slow-uart-2026-10-25.json`, `probe-uart-direction-2026-10-25.json`,
and `aprom-readback-2026-10-25.bin`.
Normal SRAM is cacheable in this image: debugger reads of `g_message` or the
ring metadata can be stale while the CPU sees dirty cache lines. An empty
debugger string alone is therefore not evidence that formatting failed; the
DTCM bytes and CPU breakpoint provide the stronger observations here.

### UART pin and clock initialization check

Stepping the installed Debug image through `m55m1_debug_uart_init()` confirmed
that the linked function executes both vendor pin-mux macros. On the LQFP64
package, PC4 (pin 36) is UART2 RX and PC5 (pin 35) is UART2 TX. Both pins use
the normal VDD domain. The post-init register snapshot was:

| Register | Value | Interpretation |
| --- | --- | --- |
| `SYS_GPC_MFP1` | `0x00000808` | PC4 RX and PC5 TX, function 8 |
| `SYS_GPC_MFOS` | `0x00000000` | Peripheral outputs, including PC5, use push-pull |
| `CLK_GPIOCTL` | `0x00000004` | GPIOC clock enabled |
| `CLK_UARTCTL` | `0x00000004` | UART2 clock enabled |
| `CLK_UARTSEL0` | `0x11111111` | UART2 selects HIRC 12 MHz |
| `CLK_UARTDIV0` | `0x00000000` | UART2 divider is 1 |
| `UART2_LINE` | `0x00000003` | 8 data bits, no parity, 1 stop bit, BREAK off |
| `UART2_BAUD` | `0x30000066` | Nominal 115384.6 baud from 12 MHz |

HIRC enable and stable bits were set. The pin mux remained `0x808` and output
mode remained push-pull after DMIC initialization. GPIOC `MODE=0` alone does
not indicate a disabled peripheral TX output: peripheral output type is
selected by `SYS_GPC_MFOS`. Nuvoton's UART examples configure the mux without
adding `GPIO_SetMode(..., GPIO_MODE_OUTPUT)` for UART TX.

An earlier snapshot with zero mux and UART registers was taken while the core
was halted immediately before `SetDebugUartMFP()`, not after initialization.
After stepping through init and resuming into the diagnostic loop, the user
still reported no COM13 logs. This check found no pin-mux or clock-selection
error to justify a firmware change. It does not measure physical bit timing
or prove reception by the probe. Evidence:
`uart-init-initial-pc-2026-10-25.txt` and
`uart-init-step-gdb-2026-10-25.txt` under `diagnostics/uart-probe/`.

## Board acceptance still required

### Historical PC4/PC5 GPIO check

The temporary GPIO voltage-test source and build option have been removed.
The only retained custom-PCB image is the DMIC-to-UART2 diagnostic above.
The following records the earlier GPIO experiment, not the current firmware.

On 2026-10-25, all 10676 APROM bytes read back matched this diagnostic BIN.
Running MEM-AP samples showed both opposite pin states with transitions five
seconds apart; CFSR and HFSR were zero. OpenOCD was then closed. Physical VOM
confirmation remains separate. Before programming, 24576 bytes of the existing
DMIC APROM were saved as
`diagnostics/uart-probe/before-uart-gpio-aprom-2026-10-25.bin`.
The programming session reported a RAM work-area restore error; a fresh
MEM-AP connection confirmed intact programmed bytes and running GPIO output
without another flash. This does not resolve the flash driver's restore error.

Local verification on 2026-10-24: all 102 repository unittest cases passed,
`git diff --check` passed, the custom Debug and Release images linked with
Arm Compiler 6.24, and the existing product Debug image also linked. The custom
maps resolve the TX handler to `UART2_IRQHandler` and place DMIC descriptors
at `0x20230000`, sample buffers at `0x20230020`, in non-cacheable SRAM. The raw
BIN sizes are 20176 bytes (Debug) and 18932 bytes (Release). No board was flashed
or measured during this verification.

1. Confirm U4 supply and common ground, then observe PA1 PDM clock near
   2.048 MHz and PA2 PDM activity with an oscilloscope/logic analyzer.
2. Confirm the startup clock line reports 220000000 Hz core. Check UART timing
   and, when precision matters, measure clocks externally.
3. Run quiet, speaking and clapping checks; verify bipolar PCM response without
   persistent clipping, flatline, `NO_DATA`, DMA errors or growing overruns.
4. Keep running for at least 60 seconds and confirm SWD remains accessible.

Host tests and successful Debug/Release links establish source/build behavior
only. They do not establish physical mic function, clock accuracy, audio
quality, acoustic latency or a passed board soak. DAC/I2S1 testing is a later
independent block test.
