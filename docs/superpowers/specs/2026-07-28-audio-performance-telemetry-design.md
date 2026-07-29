# Audio Performance Telemetry Design

## Goal

Measure the ERB-GRU audio path from DMIC DMA completion to DAC DMA completion
without running logging or DSP work in interrupt context, and raise the codec
digital gain from -12 dB to -3 dB for bring-up evaluation.

## Design

The board layer owns the Cortex-M55 DWT cycle counter and records the cycle
timestamp when DMIC completes a 10 ms block. The codec driver accepts that
timestamp and a monotonically increasing block sequence when a PCM block is
queued. Its PDMA completion ISR records the matching DAC completion timestamp.
The app polls these completed records in its foreground loop.

The ERB-GRU TFLM adapter records the Ethos-U invocation cycle interval. The
app records the full enhance interval around the service call; foreground
telemetry therefore reports total enhance, Ethos-U, derived DSP/synthesis, and
capture-complete-to-DAC-complete latency. All values are aggregated as average
and maximum over 100 frames before one UART log is emitted.

The board headphone safety ceiling becomes -3 dB. This remains below the codec
0 dB maximum and is explicitly a bring-up setting pending SPL calibration.

## Constraints

- No printf, allocation, or inference in DMIC/I2S/PDMA ISRs.
- All timing uses unsigned 32-bit cycle deltas; each tracked interval is far
  below the DWT wrap interval.
- Audio data path remains DMIC DMA -> foreground ERB-GRU -> I2S PDMA.
- UART output is foreground-only and rate limited to one line per 100 frames.
