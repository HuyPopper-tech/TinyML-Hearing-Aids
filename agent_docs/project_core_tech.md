# Core Technology

- Custom M55M1 PCB with a 220 MHz Cortex-M55 (PL0), nominal 73.728 MHz audio APLL1, CMSIS-DSP/MVE, and a bare-metal foreground loop.
- Mono 16 kHz/hop16: production FIFO24/DMA32 -> signed PCM24 /8388608 -> fixed +21 dB software preamp, soft-knee compression and independent limiting. Raw PCM16 replay retains /32768. Analysis and FIR share the same normalized hop.
- Symmetric Hamming256 and FFT256; ring storage preserves the causal chronological frame and startup zeros. Model context remains [previous,current] [1,2,129,3]; guarded double fallback preserves half-code quantization decisions.
- R2 INT16 controller: feature/c/h runtime inputs 0/1/2; h_next/c_next/phase/gain outputs 0/1/2/3. Encoded h/c each 200; both use exact int64 fixed-point feedback.
- Compiled 516848-byte model executes from SRAM through one TFLM interpreter and Ethos-U55 runtime. Immutable memory initialization precedes AllocateTensors.
- Gain65/phase65 -> Hermitian spectrum -> inverse FFT128/roll64/taper -> FIR128. Reset delta is tap64; old/new FIR share current input/history127 and midpoint-Hann crossfade16.
- Capture b, process b+1, scheduled playback b+2 with two initial silence slots; complete hop budget remains <1 ms. Faults stop/flush/reset/restart.
- Arena capacity is 64 KiB; linked size is not actual allocation/high-water use. Memory snapshot and stage/timestamp diagnostics are available for board validation.
- Boot MVE self-tests, host CMSIS scalar numeric tests and successful builds do not establish target parity, actual allocation, timing or acoustic performance.
- Normal Release excludes service stage bookkeeping and UART logs, but retains pipeline deadlines, health checks and SWD counters. Debug/Release Probe retain stage timing. Existing WFI idle, NPU WFE/SEV, clock gates, DTCM/SRAM placement and cache ownership remain in force.

See `docs/r2_custom_integration.md` for verified results and outstanding gates.
