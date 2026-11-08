# M55M1R2LJAE with Nuvoton OpenOCD

The repository uses the official
[OpenNuvoton/OpenOCD-Nuvoton-CMSIS-DAP](https://github.com/OpenNuvoton/OpenOCD-Nuvoton-CMSIS-DAP)
distribution, including its `numicro_dap` driver and unchanged
`target/numicroM55.cfg`. The custom product Debug/Release flow uses this configuration.
The Windows runtime is pinned by version and SHA-256 in
[`tools/openocd/distribution.json`](../tools/openocd/distribution.json).
Its binaries, libraries, licenses and upstream scripts are installed under
`tools/openocd/.runtime/`, which is ignored by Git. No runtime path depends on
`build-swd-blink/`.

## Install

From the repository root, run the VS Code task **M55M1: Install OpenOCD Nuvoton**, or:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\openocd\install.ps1
```

An existing copy of the exact archive can be supplied with `-ArchivePath`.
The installer verifies the archive before extraction and checks the installed
executable and target script afterward. If the upstream `latest` release has
replaced the pinned asset, obtain that exact archive or deliberately update the
manifest after validating a newer Nuvoton build. A SHA mismatch stops installation.

Install the Cortex-Debug extension and Arm GNU GDB. Set the VS Code user setting
`cortex-debug.armToolchainPath` to the directory containing `arm-none-eabi-gdb`,
`arm-none-eabi-objdump` and `arm-none-eabi-nm`. The local workspace setting on the
current machine uses the existing STM32CubeCLT toolchain; machine paths stay ignored.
Arm Compiler 6 remains the firmware compiler.

## Debug initialization and first-hop logs

Debug enables UART2 TX PC5 / RX PC4 at 115200-8N1. Open the terminal before reset,
then Continue if the debugger is stopped at `main`. Release keeps UART disabled.

`[MODEL_MEMORY] status=ALLOCATED` confirms AllocateTensors succeeded. Initialization
continues with the following checkpoints; the list is an expected sequence, not a
recorded board run:

- `[INIT] stage=transport`: board clock/pin progress and codec initialization.
  `[INIT_TRANSPORT]` records board/codec status and actual clock snapshot values.
- `model.sram`, `ethosu.init`, `resolver.ethosu`, `model.schema`, `tensors.allocate`.
- `tensor.binding`: `[INIT_TENSOR]` prints expected and actual direction, semantic,
  port/index, INT16 flag, rank/shape, bytes, zero point and exact float scale bits.
  `tensor.input` / `tensor.output` reports OK/FAIL with the runtime port in `code`.
- `adapter.qparams`, `fft.init`, separate `mve.feature` and `mve.fir` self-tests.
  A required MVE failure also emits `mve.required FAIL`; no scalar fallback is added.
- `state.reset BEGIN`, then `inference OK` and `pipeline OK` after reset completes.
- `transport_start BEGIN`, then `stream RUNNING` only when start returns success.

Each initialization step emits BEGIN/OK/FAIL where applicable. A final BEGIN without
its result identifies the next operation to inspect, but does not distinguish a
CPU fault, debugger halt, wait or lost UART output by itself. Failure returns retain
the existing safe-stop behavior and emit an explicit stage before main stops.

After stream start, `[AUDIO_STAGE] stage=capture` marks the first completed input.
`[HOP_STAGE]` follows frontend/FFT, Invoke, state feedback, taps, FIR and PCM of the
first PCM call only. Boot FFT self-tests and compiled replay do not emit these hop
markers. No blocking UART drain occurs after DMA starts or on automatic recovery.
`[AUDIO_FAULT]` reports the first admitted message per fault category, including
sequence/deadline/submit/model/transport/restart failures and health counters.
Periodic `[AUDIO] frame=...` still requires 1000 processed hops; repeated recovery
can prevent that count from being reached, so its absence is not proof of a hang.

Cold-boot checkpoints are drained before proceeding so a later initialization
failure cannot hide behind queued output. These boot waits add startup time.
First-hop UART formatting/interrupt work also adds Debug overhead; this build is
for diagnosis, not evidence of the Release 1 ms deadline or measured power.

## VS Code operations

| Run and Debug profile | Behavior |
| --- | --- |
| M55M1: Debug: Flash + Debug (OpenOCD Nuvoton) | Builds the custom product Debug image, programs/verifies `build-debug/M55M1_GestureAI.bin`, resets and stops at `main`. |
| M55M1: Debug: Attach (OpenOCD Nuvoton) | Halts the existing custom product image using `build-debug/M55M1_GestureAI.axf`; no build, flash or automatic reset. |
| M55M1: Release: Flash + Debug (OpenOCD Nuvoton) | Builds the custom product Release image, programs/verifies `build/M55M1_GestureAI.bin`, resets and stops at `main`. |
| M55M1: Release: Attach (OpenOCD Nuvoton) | Halts the existing custom product image using `build/M55M1_GestureAI.axf`; no build, flash or automatic reset. |

Choose a profile and press F5. Cortex-Debug starts and owns its OpenOCD process;
do not start a second server task against the same probe. The server stays alive
during debugging. Use Continue, Pause, Step, Breakpoints and the Registers pane.
Stop ends the debug session and its server. Attach symbols must match the image
already in flash; using a different build AXF gives
incorrect source information.

The Run and Debug selector contains Debug and Release variants of Flash + Debug
and Attach. Both build configurations use the same custom PCB and OpenOCD target.
Release optimization can limit stepping and variable visibility.
Do not flash without separate authorization. See `docs/r2_custom_integration.md`.

The **Flash Debug/Release (APROM, verify)** tasks build the corresponding profile,
write and verify its BIN, leave the CPU halted, then exit OpenOCD. Reset the board
manually afterward to run independently. The **OpenOCD gdbserver** task instead
stays running for a manually managed GDB client.

Use **Terminal > Run Task** for these two programming choices:

| Task | Build directory | Programmed image |
| --- | --- | --- |
| M55M1: Flash Debug (APROM, verify) | `build-debug/` | `build-debug/M55M1_GestureAI.bin` |
| M55M1: Flash Release (APROM, verify) | `build/` | `build/M55M1_GestureAI.bin` |

Both tasks use the same pinned CH549/Nuvoton configuration. F5 uses the AXF and
BIN of the selected Debug or Release launch profile. Programming tasks only use
the corresponding BIN. The obsolete per-diagnostic build profiles and root build directories
were removed during the R2 integration; their evidence is preserved under
`diagnostics/r2_custom_integration_20261108_105000/obsolete_build_evidence.zip`.
The memory-only recovery configuration is retained. Obsolete `tools/debug/`
register-experiment scripts were archived locally and removed together with their
standalone diagnostic applications.

The shared `m55m1_program` command rejects missing, empty, undersized, non-BIN and
larger-than-2-MiB images. It selects Secure MEM-AP transactions, halts the core,
checks stable DHCSR (Secure debug enabled, halted, no reset/lockup) and PDID
`0xA2000120`, then performs `reset halt` and repeats those checks. This starts the
vendor RAM algorithm before application cache, MPU and DMA initialization. It
probes flash and programs only the image's sectors at physical APROM base
`0x00100000` before verification. Any operation error aborts the sequence. It does not issue a chip
erase or write CONFIG, OTP or LDROM. The unmodified vendor target also declares
the LDROM bank; that declaration does not program it.

The project configuration also selects Secure transactions after target examination
and runs the same `m55m1_halt` readiness check on GDB attach, before GDB's automatic
flash-bank probing. It enables work-area backup: the vendor's RAM work area starts
at `0x20000000`, which overlaps the application's DTCM vectors/data. These are
project overrides; the installed vendor target and flash driver remain unchanged.
At 100 kHz the backup/restore adds noticeable time. Successful live GDB tests
included approximately 10 s and 13 s gaps without keep-alive packets. The flash
profile therefore sets `remotetimeout` to 60 s. A keep-alive warning alone does not
mean verification failed; require the explicit verification-completed message.

## Manual commands

Run from the repository root. To keep a debug server alive:

```powershell
& .\tools\openocd\.runtime\bin\openocd.exe `
  -s .\tools\openocd `
  -s .\tools\openocd\.runtime\share\openocd\scripts `
  -f .\tools\openocd\m55m1-ch549.cfg
```

For a separately built raw BIN, substitute its path in this one-shot command:

```powershell
& .\tools\openocd\.runtime\bin\openocd.exe `
  -s .\tools\openocd `
  -s .\tools\openocd\.runtime\share\openocd\scripts `
  -f .\tools\openocd\m55m1-ch549.cfg `
  -c init -c "poll off" `
  -c "m55m1_program {build-debug/M55M1_GestureAI.bin}" `
  -c shutdown
```

`shutdown` intentionally exits after programming. Do not add it to a persistent
debug server. `m55m1-ch549-memap.cfg` is the separate memory-only diagnostic
configuration; it has no Cortex-M CPU control or flash bank and cannot debug or
program an application.

## Wiring and recovery limits

The tested CH549F probe uses CMSIS-DAP ARM mode, HID backend, serial
`0001A0000001`, SWD at 400 kHz in `m55m1-ch549.cfg` and no physical nRESET
output. The separate MEM-AP diagnostic configuration stays at 100 kHz. The actual DP ID is
`0x4c013477`, overriding the vendor script's default. `reset_config none` describes
the wiring; the vendor Cortex-M target uses SYSRESETREQ for software reset.

| Header | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- |
| Custom PCB J4 | VDD/3V3 | PF1/SWCLK | PF0/SWDIO | nRESET | GND |

PF0/PF1 remain ICE_DAT/ICE_CLK in the current custom product. Audio uses
I2S1 PA8/PA10/PA11/PB0 and I2C0 PA4/PA5; DMIC uses PA1/PA2,
UART2 PC5 TX / PC4 RX. The earlier NuMaker I2S0 SWD conflict has
been removed from the active source. Current board execution is unverified.

The custom PCB has recovered from the observed Reset/Secure-debug-blocked state
after a cold reset, permitting APROM programming again. If halt or memory access
fails, stop the session and use the demonstrated manual recovery: disconnect
power and SWDIO/SWCLK to avoid back-powering, reapply board power, wait for boot,
then reconnect GND and SWD. Keep nRESET released. The probe cannot perform
connect-under-reset. Do not keep retrying erase or change security configuration
to work around a failed halt. Cold reset alone is not proof that the chip is ready:
require stable Secure debug, a successful halt and the correct PDID. An erased or
invalid APROM image can also interfere with subsequent reset behavior; see
[SEGGER's M55M1 device notes](https://kb.segger.com/Nuvoton_M55M1).

## Live investigation on 2026-10-25

The connected custom PCB and CH549F reproduced a failed GDB programming attempt:
`CMSIS-DAP command mismatch`, followed by flash-algorithm lockup. The RAM vectors
at `0x20000000` were replaced by algorithm instructions; after recovery the APROM
vector words read `0xFFFFFFFF`. This explains why retrying halt or restarting the
debugger could not reliably restore normal execution. It does not identify the
USB/probe firmware cause of the command mismatch.

The previous helper programmed directly from a halted application without a
pre-program reset or a Secure-debug state check. It also inherited work-area
backup disabled. The current overrides address those programming preconditions
and preserve the RAM work area. Because the changes were tested together, the
investigation does not attribute every earlier failure to one of them alone.

After restoring Custom DMIC, two consecutive GDB programming/verification cycles
with the current configuration succeeded without a cold reset between them.
Both stopped at `main` (`0x00103ED8`); CFSR/HFSR were zero. Continue also reached
`dmic_diagnostic_poll`. The tested BIN was 20176 bytes, with vectors
`0x20020000 / 0x00102431` and SHA-256
`5ed06c5e468895de2c6a33d5ec9e8bbfa032b6808e8b38b04c3b9cf12c0efe8b`.
The board was resumed before the test server shut down; DHCSR was `0x01110001`
(running, Secure debug enabled), with CFSR/HFSR still zero.

These tests used Arm GNU GDB against the configured OpenOCD server, including
the same program/reset/breakpoint commands used by Cortex-Debug. The VS Code F5
UI was not operated in this investigation. A stopped `main` breakpoint is before
UART/DMIC initialization; press Continue to run the test. UART reception and
microphone sample quality were not established by these debugger checks.

## Verification evidence and remaining limits

The user's board logs show the pinned runtime programmed and verified the custom
PCB blink image at `0x00100000` (5892-byte image, 8-KiB sector).

The tooling migration was checked on 2026-10-24: all 100 repository tests passed,
`build.bat debug config` succeeded, and `git diff --check` passed. The installed
executable and vendor target matched the pinned SHA-256 values. Offline tests
used the actual OpenOCD Tcl interpreter with mocked target operations to check
APROM programming order and error handling; they did not access a probe.

On 2026-10-25 all 104 repository tests passed and `git diff --check` passed after
the readiness/reset/work-area changes. The added interpreter test rejects blocked,
running and lockup states, including failures after the pre-program reset, before
any flash command is issued. Live results are bounded to the Custom DMIC image;
long-term probe reliability, VS Code UI stepping and full-audio execution remain
unverified. The current custom pin binding preserves PF0/PF1; this R2 product has not been flashed or measured.

References: [Cortex-Debug attributes](https://github.com/Marus/cortex-debug/blob/master/debug_attributes.md),
[OpenOCD GDB integration](https://openocd.org/doc/html/GDB-and-OpenOCD.html),
[Nuvoton vendor target](https://github.com/OpenNuvoton/OpenOCD-Nuvoton-CMSIS-DAP/blob/ad7b6fa/tcl/target/numicroM55.cfg),
[Nuvoton flash driver](https://github.com/OpenNuvoton/OpenOCD-Nuvoton-CMSIS-DAP/blob/ad7b6fa/src/flash/nor/numicro_dap.c).
