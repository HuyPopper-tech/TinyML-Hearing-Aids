# Core Technology

- Custom M55M1 PCB, Cortex-M55 at 220 MHz, CMSIS-DSP/MVE and bare-metal foreground loop.
- Mono PCM16 16 kHz/hop16; /32768 normalization; symmetric Hamming256 and FFT256; context [previous,current] [1,2,129,3].
- R2 INT16 controller: feature/c/h runtime inputs 0/1/2; h_next/c_next/phase/gain outputs 0/1/2/3. Encoded h/c each 200; both use exact int64 fixed-point feedback.
- Compiled 516848-byte model executes from SRAM through one TFLM interpreter and Ethos-U55 runtime. Immutable memory initialization precedes AllocateTensors.
- Gain65/phase65 -> Hermitian spectrum -> inverse FFT128/roll64/taper -> FIR128. Reset delta is tap64; old/new FIR share current input/history127 and midpoint-Hann crossfade16.
- Capture b, process b+1, scheduled playback b+2 with two initial silence slots; complete hop budget remains <1 ms. Faults stop/flush/reset/restart.
- Arena capacity is 64 KiB; linked size is not actual allocation/high-water use. Memory snapshot and stage/timestamp diagnostics are available for board validation.
- Boot MVE self-tests, host CMSIS scalar numeric tests and successful builds do not establish target parity, actual allocation, timing or acoustic performance.

See `docs/r2_custom_integration.md` for verified results and outstanding gates.
