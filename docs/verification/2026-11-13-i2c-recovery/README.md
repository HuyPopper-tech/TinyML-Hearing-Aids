# Bounded I2C startup recovery

The board initialization shared by startup and audio restart now resets I2C0
before temporarily owning PA4/SDA and PA5/SCL as open-drain GPIO. It first tries
at most nine clocks with 5 us phase delays. If SDA remains low while SCL can be
released, it makes one additional attempt with 2 ms phase delays and at most
nine more clocks. Every exit leaves the output latches released. A held SCL
or an unreleased bus still returns `M55M1_AUDIO_BOARD_INIT_I2C_STUCK`.

STOP setup now lowers SCL before lowering SDA, then releases SCL before SDA.
After successful GPIO recovery, the existing pin mux, controller reset,
400 kHz I2C configuration, and codec initialization continue. No new recovery
work runs in an ISR or while the audio stream remains active.

Recovery does not change the clocks, DSP, model, or audio scheduling. The
200 ms debugger pause and the unsuccessful 100 kHz address experiment were
not incorporated. The slow attempt follows the successful manual SWD
experiment, whose command overhead is not an electrical timing measurement.

## Final source configuration

- Core: 220 MHz; nominal APLL1: 73.728 MHz.
- Original complex IFFT, MVE frontend, and continuous-audio power policy retained.
- DMIC digital gain: +36 dB by default, configured by `M55M1_DMIC_GAIN_DB`.
- Left/right DAC digital gain: -12 dB at startup and as the configured ceiling.
- Gain values are digital settings, not calibrated acoustic output limits.

An existing CMake cache can retain an earlier gain. Configure the chosen build
tree with `-DM55M1_DMIC_GAIN_DB=36` when updating an older checkout. The board
and transport header defaults agree; capture metadata uses the transport
configuration instead of maintaining a separate fallback.

## Verification

- The new native test failed on the previous implementation, then passed
  with telemetry both disabled and enabled.
- Production recovery functions are exercised with modeled pins: idle bus,
  fast release at clocks 3 and 9, slow release at clocks 5 and 9, persistent
  SDA, persistent SCL, and SCL failure during a clock or STOP.
- Actual Debug trace code retains both attempts, cumulative pulse counts,
  controller state before reset, monotonic timestamps, unexpected SCL-low
  detection, and its fixed-capacity guard. Capacity is now 44 events.
- `python -m unittest discover -s tests -v`: 136 tests passed.
- Debug, Release Probe, and Release builds succeeded; `git diff --check` passed.
- The style/comment closeout uses the checked-in `.clang-format` configuration
  for the changed C/header files and embedded C recovery harnesses. File headers,
  C compatibility guards, whitespace, and architecture gates are also checked
  by the host suite.

Release BIN: `build/M55M1_GestureAI.bin`, 619264 bytes.

Final +36 dB / -12 dB BIN SHA256:
`c09f0dff10a171274dae6de0b5cb964b65b01cf7d9098bee7596a31e45daa557`.
Style/comment cleanup preserved these Release bytes exactly.

The first recovery-only image used +30 dB DMIC gain and had SHA256
`09b737602270865b541bf7e78d1ef85484fbdad1587716124d36b112af3eb6b4`.
Its matching local SWD symbol companion applies only to that historical image.
Use the AXF produced alongside the current BIN when debugging a newer build.

Requested delays are at most 115 us for the fast attempt plus 46 ms for the
slow attempt, excluding code and reset overhead. This is a startup/restart
bound, not a measured audio latency or current result.

## Board observations and remaining acceptance

The user reported audible output after integrating recovery into the main
Release flow. Five minutes of stable listening had also been reported for the
preceding diagnostic image; this does not attest every later build.

After raising the DMIC to +36 dB and the DAC to -9 dB, the user reported
intermittent high-pitched sound, including in quiet conditions. The DAC was
then reduced to -12 dB while holding the DMIC and processing configuration
fixed. The cause and the effect of this reduction remain unverified on-board.
No acoustic feedback cancellation or dynamic limiter was added.

Repeated standalone cold starts with SWD disconnected, matched current
measurements, and long-run timing/acoustic acceptance remain open. The agent
has not flashed the final image or measured the physical board.

Use the existing **M55M1: Release: Flash + Debug (OpenOCD Nuvoton)** profile,
disable old breakpoints, and continue after `main`. SWD stays on PF0/PF1. The
CH549 connection has no wired nRESET; a software reset does not power-cycle
the codec. The preceding known-working BIN and matching symbols are retained
locally in `diagnostics/release-before-i2c-recovery/`.
