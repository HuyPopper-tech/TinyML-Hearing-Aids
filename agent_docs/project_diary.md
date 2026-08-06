# Project Diary

## 2026-08-06 — DeepFilterNet replaces ERB-GRU production path

- Chose one-model firmware; no runtime model switch or 160/256 FIFO.
- Inference adapter owns all neural state; enhancer owns STFT/WOLA and DF
  spectrum history.
- Scalar KissFFT remains oracle; production mode uses CMSIS-DSP MVEF. PARITY
  compares scalar and MVE postprocess from the same spectrum/tensors.
- Full diagnostics compile out by default and never format while capture waits.
- Board results remain explicit nulls until measured. Arm Compiler license and
  physical-board availability are external acceptance blockers.
