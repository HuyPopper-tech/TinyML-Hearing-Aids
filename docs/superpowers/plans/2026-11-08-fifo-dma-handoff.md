# FIFO-aware product DMA handoff implementation plan

Execution: inline `superpowers:executing-plans`, no agents by user instruction.
Goal: repair the demonstrated pre-capture startup failure while retaining hop16,
two initial silence blocks, b+2 output, one state owner and the strict1ms guard.

## Diagnosis and selected design

User trace from the optimized candidate: TX TD at4us with FIFO16, then1021us
with FIFO16 and RX sequence0; STOP1026us. TXUDIF bit16 is clear. Existing driver
stops at TD2 because descriptor0 has no processed audio. Its scatter ring also
permits loading the next silence source before foreground publication. This is
a software scheduling/ownership defect; the trace does not measure inference.

Use terminal BASIC tasks for product descriptors (16 samples), explicitly armed
only when owned data is ready. CPU-prefill silence block0 while TX is disabled;
the first DMA task fills silence block1 as block0 drains. Its TD leaves the
channel idle with block1 still in FIFO while capture1 is processed; submit1
arms audio block2. Repeat without another silence
or queue. Descriptor templates and stereo buffers remain driver-owned/uncached.
Legacy large diagnostic transfers keep their existing scatter implementation.

Alternatives rejected: ignore software underrun while keeping the ring (can
prefetch silence/stale data); change TX threshold alone (trace shows full-block
prefetch, and threshold does not establish ownership); add silence hop (violates
b+2). Terminal tasks follow installed vendor OPMODE documentation: BASIC has one
task and completion clears mode to idle. Re-arm through existing StdDriver.

Prefill the first16 silence words by CPU before enabling serial TX, under a bounded
startup wait. This avoids assuming TX DMA requests work while TX is disabled.
Enable real I2S TX underflow IRQ; board dispatch uses the existing
playback callback, driver acknowledges/counts/stops and service performs recovery.
Foreground health also detects a sticky underflow before publishing. IRQ has no
DSP, formatting or allocation. Preserve PF0/PF1 and CH549 power-cycle recovery.
Custom audio pins and clocks unchanged; DMA operation changes are source-only,
with no flash. Actual target phasing, drift and continuous timing need a new run.

## Scope and acceptance

- [x] Extend native driver ownership tests with the measured TD1/TD2-before-RX
  order: reproduce FAIL on current code, then assert CPU-prefilled silence0,
  first-DMA silence1/output slot2,
  no speculative third transfer, packing, wrong/duplicate tags, pending TD,
  busy hardware, real TX underflow fail-stop and reset. Keep legacy cases.
- [x] Driver BASIC task/rearm/prefill/underflow lifecycle, conditional product
  binding through board I2S1 IRQ; update callers/mocks and public units/ownership.
- [x] Full native tests, Debug/Release/Probe builds, MVE linkage and hashes/maps;
  ordinary opt-ins OFF/editor Debug restore. No shared-output builds in parallel.
- [x] Curated timestamped source/user-log/build/test evidence and handoffs;
  board60000-hop timing/schedule/audio and compiled parity remain NOT_RUN/BLOCKED.

Baseline:1c9b950. Release configured before source edits. No package/model/libs
edits or numeric contract changes. Self-review substitutes for agents.

Verified completion:134 native tests; three Arm builds; Release/Probe MVE gates.
Evidence: docs/verification/2026-11-08-fifo-dma-handoff/. Hardware acceptance
remains NOT_RUN; checkboxes record implementation/handoff, not target acceptance.
