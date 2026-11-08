# Trace intermittent board_status=5 on the custom PCB

Date: 2026-11-08. Status: approved by the user in chat on 2026-11-08.

## Intent and evidence

Find why board initialization sometimes returns I2C_STUCK after OpenOCD
programming/debugging. The user has observed one successful power-cycle afterward;
this is a useful reproduction clue, not a verified reset/power correlation.
There is no logic analyzer. The CH549 probe has no physical nRESET connection.

Current firmware uses I2C0, PA4 SDA and PA5 SCL. Recovery switches to open-drain
GPIO, releases both lines, generates at most nine SCL pulses while SDA is low,
issues STOP, and returns failure if either line reads low. It does not report
individual line readings, GPIO configuration or recovery progress. The codec's
20 ms settling/ACK policy executes later and cannot handle a board-init failure.
The delay helper initializes its own SysTick timebase; it is not simply omitted
because the later board.timebase_ready marker has not appeared.

OpenOCD requests sysresetreq and reset halt. These are software reset requests;
neither their presence nor a missing nRESET wire proves their actual outcome.
Legacy driver documentation still contains NuMaker wiring and must be labeled
historical rather than used to infer the custom codec RESET connection.

## Approaches

1. Firmware snapshots and UART/debugger inspection: selected by the user because
   there is no logic analyzer. Observe the existing sequence first, then make a
   separate correction only where the evidence supports it.
2. Firmware plus passive waveform capture: would resolve electrical edge timing
   and voltage ambiguities, but is unavailable with the user's current equipment.

## Proposed implementation

- Board owns one fixed-capacity diagnostic snapshot per initialization attempt.
  Capture available reset flags without clearing them, initial PA4/PA5 pin mux,
  GPIO mode/latch/input-disable/pull configuration, relevant I2C0 status/control,
  and timebase configuration needed to interpret recovery delays. Reset flags
  may be sticky; report raw bits and never label them proof of a particular reset.
- Record SDA/SCL after release, each recovery low/high phase, and STOP; include
  pulse count, elapsed timestamps, final lines and exact failed phase. Use bounded
  static storage, no allocation, UART formatting or blocking UART drain between
  recovery edges. A readback is an MCU-observed logic level, not a voltage or
  waveform measurement. Do not read side-effecting registers merely for logging.
- Transport passes a portable diagnostic result to the audio service even when
  board init fails. The service formats Debug logs after recovery completes and
  before audio starts. Keep the snapshot debugger-visible if UART is unavailable.
  Keep ordinary Release UART disabled and exclude detailed trace storage there.
- First diagnostic revision preserves the existing recovery policy and pin/clock/
  DMA assignments. Investigate idle-bus glitches, low SCL, SDA release, GPIO
  ownership and delay validity using the trace; do not add arbitrary retries,
  delays, pull-ups or reset-pin control to hide a failure.
- Once a fault is reproducible, implement the smallest demonstrated software
  correction with a regression test. If the bus is physically held low, report
  the exact boundary and required wiring/power/reset checks; do not claim a
  firmware-only cure without evidence.

## Files and ownership

Expected changes: board/m55m1_audio.c/.h; drivers/audio_transport/audio_transport.c/.h;
services/audio_pipeline/audio_pipeline.c; cohesive existing/new I2C tests; relevant
driver documentation and the explicitly requested project progress/session handoff.
Read m55m1_timebase and OpenOCD configuration; edit them only if evidence establishes
a defect. Board owns registers/trace storage; transport owns copying; service owns
human-readable reporting. No application, model, DSP, vendored libs or source
training-package changes are required for this investigation.

## Acceptance and handoff

- Native C tests execute the actual recovery code with modeled line transitions:
  idle bus, SDA released on an early/final pulse, SDA still low after nine pulses,
  SCL held low, and complete STOP observations. Assert trace capacity, event order,
  retained failure evidence and no UART work between recovery edges. Mocked pins
  validate software behavior only, not physical bus operation.
- Failure diagnostics reach UART even when board init returns 5. Success remains
  distinguishable from a codec NACK/init failure. Caller/fixture coverage preserves
  layer dependencies and normal Release logging policy.
- Run repository host tests, git diff --check, Debug/Release configure/build, and
  record commands, exit codes and image hashes in fresh timestamped evidence.
- User reproduces the failing OpenOCD workflow and supplies its new Debug trace.
  Compare repeated OpenOCD restarts with repeated full power cycles; proposed
  observation target is 20 attempts of each, retaining failures and successes.
  A host pass or one successful boot is not acceptance of an intermittent fix.
- No agent flashing, power cycling or board changes. Preserve PA4/PA5 and SWD
  PF0/PF1; recovery remains physical reset/power cycle through the user's existing
  access, without assuming that the probe can assert nRESET.

## Sources and limits

- NXP UM10204 Rev. 7.0, section 3.1.16: nine clocks for SDA stuck low; hardware
  reset/power cycling if unsuccessful or SCL is stuck low.
  https://www.nxp.com/docs/en/user-guide/UM10204.pdf
- TI TLV320DAC3100 SLAS671C, sections 6.3.1-6.3.3: supply/reset prerequisites and
  startup lockout. No assumption that MCU reset also resets this custom codec.
  https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf

Without new device traces the physical root cause remains unproven. Full-hop
deadline/underrun debugging is a separate pending issue, not acceptance of this
I2C investigation. Deployment approval remains unchanged.
