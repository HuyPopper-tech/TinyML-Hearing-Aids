# Core Technology

- Cortex-M55 at 220 MHz, CMSIS-DSP MVEF, bare-metal foreground loop.
- Ethos-U55 through TFLM `AddEthosU()` and a Vela custom-op model.
- 16 kHz, frame 512, hop 256, periodic sqrt-Hann, 257-bin real FFT.
- 32 ERB log-magnitude features plus 144 complex bins.
- 257 gain outputs, complex DF 5x144 and alpha, then IFFT/50% WOLA.
- Per-tensor INT8 scale/zero-point contract with transactional recurrent state.
- UART1 atomic 100-frame telemetry and board-owned stack high-water painting.
