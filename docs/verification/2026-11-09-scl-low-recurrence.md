# SCL-low recurrence, including reported power cycle — 2026-11-09

User-supplied Debug UART excerpts (not complete logs):

```text
First report:
[I2C_RECOVERY] result=SCL_LOW released=0 rststs=00000021 pulses=0 events=4 first_scl_low_event=0
[I2C_GPIO] stage=before mfp=00000000 mode=00000000 dout=0000ffff pin=0000f810 dinoff=00000000 pusel=00000000 smten=00000000
[I2C_GPIO] stage=after mfp=00000000 mode=00000a00 dout=0000ffff pin=0000f810 dinoff=00000000 pusel=00000000 smten=00000000
[I2C_REG] ctl0=00000000 status0=000000f8 clkdiv=00000000 gpio_clk=00000007 i2c_clk=00000001
[INIT_TRANSPORT] board_status=5 codec_status=0 clocks_valid=0 core_hz=0 apll0_hz=0 apll1_hz=0 i2s_src_hz=0 mclk_hz=0

After user-reported board power cycle:
[I2C_RECOVERY] result=SCL_LOW released=0 rststs=00000001 pulses=0 events=4 first_scl_low_event=0
[I2C_GPIO] stage=before mfp=00000000 mode=00000000 dout=0000ffff pin=0000f810 dinoff=00000000 pusel=00000000 smten=00000000
[I2C_GPIO] stage=after mfp=00000000 mode=00000a00 dout=0000ffff pin=0000f810 dinoff=00000000 pusel=00000000 smten=00000000
[I2C_REG] ctl0=00000000 status0=000000f8 clkdiv=00000000 gpio_clk=00000007 i2c_clk=00000001
[I2C_EDGE] index=0 phase=release pulse=0 cycle=1412 elapsed_us=0 sda=1 scl=0
[I2C_EDGE] index=1 phase=stop.sda_low pulse=0 cycle=2986 elapsed_us=7 sda=0 scl=0
[I2C_EDGE] index=2 phase=stop.scl_release pulse=0 cycle=4346 elapsed_us=13 sda=0 scl=0
[I2C_EDGE] index=3 phase=stop.sda_release pulse=0 cycle=5706 elapsed_us=19 sda=1 scl=0
[INIT_TRANSPORT] board_status=5 codec_status=0 clocks_valid=0 core_hz=0 apll0_hz=0 apll1_hz=0 i2s_src_hz=0 mclk_hz=0
```

## Interpretation and limits

- Failure is board bus recovery before codec driver init. `codec_status=0` is
  the zero-initialized report, not a successful codec operation. Zero clock fields
  mean transport returned before copying the board clock snapshot; PLL stage
  messages already occurred. This log does not indicate zero actual core clock.
- PA5 mode is input before recovery and open-drain afterwards (MODE bits10:11=2).
  DOUT bit5=1 requests release, PIN bit5=0 reads low; PA4 reads high. MFP=0 selects
  GPIO; DINOFF=0 leaves input enabled; CTL0=0 shows the I2C engine disabled at the
  captured pre-recovery instant. Project-owned PA5 references are confined to
  this GPIO recovery and later I2C mux. No project-owned IO-hold setup was found.
- All four SCL samples remain low across 19 us. This does not establish a
  seconds-long hold, its voltage, or which device/net caused it. Firmware's
  `stop.*` phase labels denote attempted actions; no valid STOP occurs if SCL
  does not reach high. Nine-clock SDA recovery is not evidence that SCL can rise.
- The second report invalidates an Attach-only explanation. MCU reset flags
  alone do not prove the external codec rails fully discharged. Flashed image
  identity, voltages, pull-ups, reset pin and alternate power paths are unknown.
- [NXP UM10204 section3.1.16](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)
  distinguishes SCL-low recovery (hardware reset or power cycle) from SDA-low
  recovery (nine clocks). Do not force push-pull high, bypass the fault, or
  assume that more software retries correct a line held low.

## Required next evidence

At the stopped fault, measure PA5/SCL to GND, PA4/SDA to GND and codec I/O supply
to GND. Record whether USB/SWD/other supplies remained connected during the
reported power cycle. A multimeter is sufficient for these DC checks; it cannot
verify rise time or waveform timing. Compare the MCU pad and codec-side SCL if
accessible to distinguish interconnect from the endpoint. These observations
determine whether to investigate supply/pull-up/connection/held-low conditions
or a discrepancy between digital readback and actual voltage.

Status: root cause UNCONFIRMED, physical recovery FAIL in supplied boot, agent
hardware operations NOT_RUN. No firmware, pin, clock, DMA or retry change was
made for this evidence. No new runtime tests/builds were needed for this
documentation-only update; prior host tests do not validate the physical bus.
