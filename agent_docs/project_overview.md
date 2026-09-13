# Project Overview

The repository is in the 2026-09 model-reboot state. The active firmware baseline captures mono 16 kHz PCM16 blocks and copies them through a stateless passthrough service to the optional TLV320DAC3100 output.

No neural model is active or deployment-approved. `M55M1_MODEL_PACKAGE_DIR` is empty by default; future DeepFIR or Wave-U-Net packages must be supplied explicitly and pass package, host-parity, Vela, board-parity, realtime, soak, and capture-to-DAC gates.

Dependency direction is `app -> services -> drivers -> board -> libs`. Realtime ISRs only acknowledge DMA, move descriptors, and signal the bare-metal foreground loop. `libs/` is vendored and unchanged.
