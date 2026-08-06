# Project Overview

Bare-metal M55M1 hearing-aid research firmware. Production path captures mono
16 kHz PCM in 256-sample hops, runs one DeepFilterNet model on Ethos-U55, and
submits one 256-sample output block to TLV320DAC3100.

Dependency direction is `app -> services -> drivers -> board -> libs`.
Realtime ISRs only acknowledge DMA and publish descriptors; DSP, inference and
telemetry formatting run in foreground with static storage.

Current package `deepfilternet_u55_full_seed20260805` remains engineering-ready
smoke quality, not release-final. Board timing, parity, memory and soak evidence
are mandatory before acceptance.
