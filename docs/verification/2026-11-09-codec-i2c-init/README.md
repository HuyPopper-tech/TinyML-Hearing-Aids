# Codec I2C boot failure — 2026-11-09

Base commit: `029cd184ae4b55b3b549e72d97364b4ded69d432`.

## Observation and confirmed source defects

The user's ordinary Debug boot in [user-debug.log](user-debug.log) reaches
`board_status=0`, `BUS_FREE`, valid clocks, then `codec_status=5` (`I2C_ERROR`).
This differs from `board_status=5` (`I2C_STUCK`). GPIO bus clear precedes I2C
controller reset/open and codec writes; it does not prove a codec ACK.
The supplied image hash and failing codec register were not available.

Production `tlv320dac3100_write_one_reg_bounded()` previously requested STOP and
returned immediately. The next `I2C_START` macro preserves pending STO. The
native register model reproduced a new START before STOP completion. The vendor
[M55M1 StdDriver](https://github.com/OpenNuvoton/M55M1BSP/blob/master/Library/StdDriver/src/i2c.c)
`I2C_WriteByteOneReg` and `I2C_STOP` in local `libs/StdDriver/inc/i2c.h`
wait for STO clear; local `libs/Device/Nuvoton/M55M1/Include/i2c_reg.h` documents
hardware completion. Vendor files remain unchanged.

The old failure recorder read STATUS0/CTL0 after abort/retry delay, losing the
original state. Its existing fields never reached Debug UART. These source
defects are established; neither the register model nor the supplied UART log
establishes the physical cause of this particular intermittent failure.

## Change and ownership

- Codec driver owns bounded SI/STOP polling and the last failed-byte snapshot.
  It waits for STO clear before START and before reporting success. Aborts
  snapshot before STOP, then perform bounded cleanup. Pending STOP timeout
  prevents another START. Retry counts, 20-ms settle, 1-ms soft-reset delay,
  script, gain and de-pop are preserved. The unnecessary final readiness delay
  is removed; 20 attempts remain, separated by 19 delays.
- Transport copies the exhausted-operation snapshot into the caller-owned Debug
  report. No vendor type crosses into services.
- Pipeline prints `[CODEC_I2C]` only during stopped Debug boot and for an exhausted
  operation. No ISR/live-audio logging was added. Ordinary Release remains
  UART-off; Probe retains existing timing output.
- No pins, clocks, DMA, model, DSP, package or `libs/` changes. PA4 SDA / PA5 SCL
  and fixed codec address 0x18 remain unchanged.

## Verification

`host/red.log` captures the new native regression failing on the old code.
`host/full.log`: **135 tests PASS, zero skips**, including 15 codec register-model
scenarios covering delayed STOP, address/register/value NACK, SI timeout at each
step, STOP timeout at entry/exit, exhausted/transient readiness, target-register
phase and preservation of NACK when cleanup also times out. The harness compiles
extracted production C and actual status layout, and executes actual transport
init to verify every diagnostic field. The Debug pipeline harness checks codec
failure text and no blocking flush after streaming starts. Mocks do not establish
electrical behavior.

`build_evidence/commands.json` records sequential Debug, Release Probe, Release and editor
restore commands, all exit 0. Both optimized images pass nine scoped MVE gates.
Arm Compiler 6.24. Binary hashes and linker regions: [receipt.json](receipt.json).
Debug is 644512 B, Release 619392 B, Probe 626400 B. Raw AXF/BIN/maps remain in
local `diagnostics/submillisecond_runtime_20261108/fifo_build_085852/`; generated
images are excluded from Git. The receipt includes tested source hashes.
The collector verifies `[CODEC_I2C]` exists only in Debug. Ordinary outputs have
diagnostic opt-ins OFF; editor compilation database points at `build-debug/`.
`rtk git diff --check` passes. No device commands were run.
Tracked console transcripts normalize trailing whitespace/newlines only;
`console-transcripts.json` records original and curated hashes. Raw runs remain
unchanged in the referenced diagnostic directories.

| Acceptance area | Status | Evidence / limit |
|---|---|---|
| Artifact integrity | NOT_RUN | No model changes; prior full receipt in `../2026-11-09-continuous-product/` |
| Architecture/source integration | PASS | Driver → portable report → service; suite/builds |
| Host numeric/regression correctness | PASS | `host/full.log`; 135 tests, 15 codec scenarios |
| Debug/Release build/link | PASS | `build_evidence/commands.json`; Probe also built |
| Raw replay | NOT_RUN | Not rerun for I2C-only change; historical package receipt separate |
| Compiled replay | BLOCKED | No compiled execution backend/device run |
| Board initialization/memory | FAIL | Supplied pre-change boot fails codec init; revised image NOT_RUN; map sizes are linker evidence |
| Full-hop deadline/schedule | NOT_RUN | Revised image not measured; earlier user Probe is historical evidence |
| Physical audio/latency | NOT_RUN | No physical measurement |

## Retest and log interpretation

Use the existing ordinary Debug Flash + Debug profile, output
`build-debug/M55M1_GestureAI.axf`. UART2 PC5 TX / PC4 RX remains 115200-8N1.
PA4/PA5 and recovery configuration are unchanged. The probe has no nRESET:
do not assume OpenOCD can assert hardware reset. Physical full-board power
cycling remains the recovery path. The agent has not flashed or reset the board.

Repeat the original F5 → Continue sequence and separate power-cycle starts,
without later breakpoints. Save each full log, tested image hash, pass/fail count
and startup condition. One successful boot cannot close an intermittent fault.
New log schema (example only, not a measured result):

```text
[CODEC_I2C] failures=1 page=1 reg=40 phase=2 step=address attempts=3 status=20 expected=18 ctl=00000048 timeout=0 stop_timeout=0
```

- `phase=1`: page-select; `phase=2`: requested register write.
- `step`: start/address/register/value/stop. START accepts 0x08 or 0x10; address
  expects 0x18; register/value 0x28. Status values are hexadecimal without `0x`.
- `status=20`: address NACK; `30`: data NACK; `38`: arbitration loss. These are
  controller states, not proof of a particular wiring fault.
- `timeout=1`: bounded SI/STOP poll expired. Bound counts iterations, not us.
  For `step=stop`, STO clear is the success condition; `expected=f8` denotes
  usual idle, not an additional STATUS0 requirement.
- `stop_timeout=1`: STOP cleanup/completion failed, independently of the original
  byte failure. Retries cannot START over a pending STOP.

If address NACK persists after completed STOP, capture the new line before
considering reset/power, pull-ups or electrical timing. For SI/STOP timeouts,
investigate controller/pin state at failure. Do not change rate, retry count,
gain or audio contracts without evidence.
