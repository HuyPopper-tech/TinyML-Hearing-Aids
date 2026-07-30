# M55M1 220 MHz core and ERB-GRU timing design

Date: 2026-07-29
Worktree: `D:\TinyML-Hearing-Aids\.worktrees\erb-gru-ethosu`
Branch: `codex/erb-gru-ethosu`

## Goal

Run the M55 core and Ethos-U firmware at exactly 220 MHz without changing the
validated 16 kHz audio profile or its exact 4.096 MHz codec MCLK. Remove false
ERB-GRU deadline failures when the free-running 24-bit SysTick counter wraps.

## Clock ownership and configuration

Board code continues to own the complete clock tree. The PLL roles are:

- APLL0: 220 MHz, selected as SCLK with divider 1.
- APLL1: 475.136006 MHz, dedicated to the audio peripheral.
- I2S0 source: APLL1 divided by 2, then divided by 58 for the 16 kHz profile,
  producing exactly 4.096 MHz MCLK.
- The existing 32 kHz profile retains divider 29, producing exactly 8.192 MHz.

Clock transition order:

1. Switch SCLK to HIRC before changing either PLL.
2. Enable and validate the external 24 MHz crystal.
3. Configure APLL1 for the exact audio clock and wait for `APLL1STB`.
4. Configure APLL0 for 220 MHz and wait for `APLL0STB`.
5. Select APLL0 as SCLK with divider 1.
6. Verify `SystemCoreClock == 220000000`.
7. Bind I2S0 to APLL1 divided by 2 and the profile-specific divider.
8. Continue existing peripheral, pin, I2C, DMA and SysTick setup.

Failure of either PLL lock or any runtime clock contract stops boot before
audio DMA starts. The board init result distinguishes APLL0/core failure from
APLL1/audio failure.

## Runtime clock evidence

Before audio starts, polling boot logs report:

- measured core/SCLK frequency;
- measured APLL0 frequency;
- measured APLL1 frequency;
- calculated I2S0 source and codec MCLK frequency.

The clock values come from the configured hardware through the Nuvoton clock
APIs, not duplicated constants. Production telemetry remains non-blocking and
does not print clock information after audio starts.

## Wrap-safe timing

SysTick remains a free-running, non-interrupting 24-bit down-counter. Board
code is the sole owner of its representation and exposes:

- a masked cycle timestamp;
- wrap-safe elapsed cycles;
- conversion from masked elapsed cycles to microseconds.

For mask `0x00ffffff`, elapsed time is:

```text
elapsed = (end - start) & 0x00ffffff
```

All consumers use this shared elapsed calculation. No service or driver
performs an unmasked subtraction of SysTick timestamps. Inference deadline
acceptance compares the measured Invoke elapsed cycles directly with the
10 ms cycle budget. The same calculation covers normal timestamps, exact
wrap, and start-before/end-after-wrap cases.

At 220 MHz the counter wraps every 76.26 ms, but an approximately 0.4 ms Invoke
crossing that boundary remains correctly measured and is not rejected.

## ERB-GRU behavior

No model, tensor contract, quantization, DSP, WOLA, state or fallback behavior
changes. A genuine `Invoke()` error or genuine Invoke duration of at least
10 ms still:

- rejects output and next-state publication;
- uses unity-WOLA for the current frame;
- resets both recurrent states;
- increments the appropriate error/deadline counters.

False deadline failures caused only by SysTick wrap disappear.

## Verification

Host contract tests cover:

- APLL0 as 220 MHz SCLK with divider 1;
- APLL1 as the I2S0 audio source;
- exact 16 kHz and 32 kHz audio divider contracts;
- boot validation and clock reporting;
- elapsed cycles with no wrap, exact wrap and wrap crossing;
- a 398 µs Invoke crossing SysTick wrap remaining below the 10 ms deadline;
- a true duration at or above 10 ms being rejected.

Required verification:

1. Run the package generator reproducibility check.
2. Run the complete Python test suite and `git diff --check`.
3. Build Debug and Release with Arm Compiler 6.
4. Confirm boot reports 220 MHz core and exact audio clocks.
5. Confirm production telemetry has zero wrap-correlated deadline failures.
6. Re-run board golden parity before production soak.

An Arm Compiler license failure is reported as an external blocker rather than
a successful binary build.

## Hardware safety and recovery

This design does not alter the existing audio pin mux, DMA channels or IRQ
priorities. Before flashing, retain access to SWDIO PF0, SWCLK PF1, nRESET,
GND and VREF/3V3. UART debug remains PA2 RX and PA3 TX at 115200-8N1.
Because PF0/PF1 become I2S pins after board initialization, recovery must be
performed through reset before audio pin mux takes ownership.
