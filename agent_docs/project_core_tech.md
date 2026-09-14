# Core Technology

- Cortex-M55 at 220 MHz with CMSIS-DSP MVEF and a bare-metal foreground loop.
- 16 kHz mono PCM16 audio with static DMA buffers, queueing, optional I2S/DAC,
  and stateless passthrough as the active firmware baseline.
- Ethos-U55 runtime foundation through the unchanged Nuvoton BSP core, enabled
  only by `M55M1_ENABLE_ETHOSU_RUNTIME=ON`; no model or command stream is active.
- TFLM is disabled by default and can enter only through the project-owned
  `ethosu` bridge plus an explicit validated model package.
- Future model packages must align Vela memory maps, cache policy, tensor bases,
  board IRQ behavior, realtime timing, soak, and capture-to-DAC evidence.