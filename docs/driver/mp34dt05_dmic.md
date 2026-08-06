# MP34DT05 DMIC Capture Notes

## Purpose and Fixed Transport Rules

This document records the NuMaker GestureAI-M55M1 onboard MP34DT05 capture
path for later TinyML hearing-aid applications. It is transport/reference
firmware, not a complete hearing-aid front end. The board layer produces
signed-16 mono PCM blocks through DMIC0 and LPPDMA; application code
may inspect or process that block before submitting it to the DAC transport.

| PCM profile | Samples / block | Block time | DMIC OSR | PDM clock |
| --- | ---: | ---: | ---: | ---: |
| 16 kHz | 256 | 16 ms | OSR=128 | 2.048 MHz |
| 32 kHz | 320 | 10 ms | OSR=64 | 2.048 MHz |

APLL1 supplies the DMIC clock. Both profiles deliberately hold the PDM clock
at 2.048 MHz, inside the MP34DT05 normal-mode range of 1.2–3.25 MHz. The
capture profile must exactly match the active TLV320DAC3100 I2S profile; mixing
rates changes audio pitch and causes buffer-pressure symptoms.

## Board Wiring and Data Timing

| Signal | M55M1 pin | Function |
| --- | --- | --- |
| DMIC1_CLK | PB2 | PDM clock output to U4 CLK |
| DMIC1_DAT | PB3 | U4 DOUT input to DMIC0 channel 2 |

U4 is an MP34DT05-M and its L/R pin is pulled low. Data becomes valid after
the PDM clock falling edge and remains valid through the low phase. The M55M1
must select DMIC channel 2 and latch on the **rising edge**, at the end of this
valid window. Falling-edge latch samples the preceding high-Z/transition
period and produces distorted PCM even when clocks look correct.

## LPPDMA Ownership

LPPDMA0 channel 0 runs a two-descriptor scatter-gather ring of non-cacheable
buffers. Each descriptor copies exactly the selected profile's 256 or 320
16-bit FIFO words. The LPPDMA IRQ only clears transfer/abort flags, advances
the completed slot, and sets a ready flag. It must not write UART, call codec
I2C APIs, copy PCM, allocate, or perform TinyML/DSP.

Foreground code atomically claims the ready slot, copies it into caller-owned
storage, then processes/submits it. A new completion before foreground claims
the old one increments `overrun_blocks`; do not wait in the IRQ to avoid that
condition. The normal reference application treats DAC `BUSY` as an intentional
drop rather than blocking the next capture deadline.

## Debugging History and Fast Triage

- **OSR=64 at 16 kHz is wrong.** It creates a 1.024 MHz PDM clock, below the
  MP34DT05 normal range, and can sound distorted. Use OSR=128 at 16 kHz.
- **Latch falling edge is wrong for L/R-low U4.** Symptoms include malformed,
  low-level, or heavily distorted voice. Use channel-2 rising edge.
- **Never log from a DMA IRQ.** UART waits can consume a capture/refill
  budget and turn an observability change into an audio defect.
- **Distinguish mic from output timing.** A healthy capture reports the selected
  sample rate, PDM=2.048 MHz, and no LPPDMA errors/overruns. Compare capture
  completion rate to I2S PDMA completion rate: both should match the selected
  profile (62.5 blocks/s for 16 kHz DeepFilterNet, 100 blocks/s for 32 kHz).
  The earlier halfword I2S DMA bug produced `win_cap=100`, `win_out=50` and
  one-octave-low speech; it was not a microphone fault.
- **Ignore intentional output pre-roll silence.** DAC starts before DMIC so
  there can be silence while APLL1/capture initialises. Only underruns after
  the first queued audio block are runtime failures.

## Integration Checklist

Start DAC/I2S before DMIC so capture always has a consumer. Select the profile
at application init, calculate the exact sample count through
`tlv320dac3100_profile_samples_per_block`, and allocate at least 320 samples
for a shared 16/32 kHz foreground block. Measure PB2 PDM clock/data, verify
the matching I2S clocks, and run both profiles for 30 minutes while tracking
LPPDMA overrun/error and DAC queue/underrun counters. TinyML models, AGC,
feedback cancellation, and user gain policy belong between capture and DAC
submit; DeepFilterNet PCM-to-output-ready time must fit the 16 ms hop.
