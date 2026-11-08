# FIFO-aware product DMA handoff — 2026-11-08

Base: `1c9b950`. Follow-up to the user's startup trace in `user_trace.txt`.
The flashed image hash was not supplied; the trace is user-provided evidence,
not an agent device run. No flash/reset/probe connection occurred in this work.

## Observed failure and cause

| Observation | Relative time | Meaning |
|---|---:|---|
| TX TD1, FIFO16, RX sequence0 | 4us | First silence task already transferred into FIFO |
| DMIC enabled | 7us | Capture starts after the immediate TX completion |
| TX TD2, FIFO16, RX sequence0 | 1021us | Second silence is in FIFO; capture1 not delivered |
| STOP, FIFO16, software underrun1 | 1026us | Driver rejects the next empty descriptor |

The old IRQ increments `completed_blocks` on DMA TD and, from TD2, stops if
the next descriptor lacks audio. This treats FIFO transfer as serial playback.
Source code plus this ordering explain the software startup fault. TXUDIF
bit16 is clear in the supplied STATUS0 observations; the trace does not show a
hardware FIFO starvation. The old free-running scatter ring also permits the
next silence source to be fetched before foreground publication; physical stale
playback from that race was not measured here.

No capture/invoke completed: frame0/capture_last_seq0 and all last stage times0.
`mve_test_us=38076` means38.076ms of boot self-tests, not a hop/Invoke time.
This log cannot establish whether the optimized pipeline meets1ms.

## Implementation and ownership

Product-only, hop16:

1. With serial TX disabled, write16 packed zero frames into FIFO (silence0).
   Bounded startup wait confirms the CPU writes before enabling TX. It does not
   assume DMA requests work while TX is disabled.
2. Arm one terminal BASIC DMA task for silence1. As silence0 drains, it fills
   FIFO. The first DMA TD now represents silence1; it releases its source and
   leaves DMA idle while FIFO drains. It does not require capture audio yet.
3. Capture sequence1, processed in the existing next slot, publishes audio block2
   to its free stereo buffer, then explicitly arms its terminal task. Repeat for
   later sequences. No additional silence, queue hop, sample dropping/repetition,
   sample-rate change, DSP change or relaxed deadline.

Driver alone owns uncached templates/buffers and DMA arming. BASIC descriptors
have NEXT0 and cannot automatically fetch another source. Foreground gates
requests, rejects busy/pending TD/incorrect tags and rechecks FIFO faults before
publishing. Sequence1 maps to logical block2; completion timestamps still mean
FIFO transfer, not physical playback completion. Legacy large diagnostic blocks
retain the prior scatter path; there is one compiled product path/interpreter.

Real TX underflow is no longer inferred from an unfilled future descriptor:
I2S1 underflow IRQ uses the existing board-to-driver callback, acknowledges the
sticky flag, latches the count and stops TX/requests/PDMA interrupts. Foreground
health checks also observe the sticky flag. Abort and publication faults remain
fail-stop; full service reset/restart is required, with two startup silences.
IRQs never run inference, allocate or format UART. The board masks the old I2S
source before enabling its NVIC binding, important for reset-less debug reloads.

Existing custom pins unchanged: DMIC PA1/PA2, I2S1 PA8/PA10/PA11/PB0,
I2C0 PA4/PA5, UART2 PC4/PC5, SWD PF0/PF1. I2S1 IRQ priority1 matches the
existing DMA priority. CPU/NPU/audio clocks, RX DMA and TX thresholds unchanged.
CH549 has no physical nRESET; recovery remains stop/disconnect and power cycle.

## Verification and limits

- Native test first reproduced the old TD1/TD2-before-RX failure at its actual
  underrun assertion (`host/232343`). A fixture macro shadowed an argument in
  the first new test build; corrected (`host/232507`, then PASS).
- Final134/134 host tests PASS, no skips (`host/233413`), including real C start/
  submit/IRQ code, 60000 modeled handoffs, PCM packing, two initial silences,
  logical block2, pending/busy/wrong sequence rejection, real underflow with no
  DMA TD, fault arriving while IRQs are masked, DMA abort, prefill timeout/reset.
  These are transactions in a register model, not60000 real audio hops.
- Arm6.24 Debug, ordinary Release and Release Probe build/link PASS. Both
  optimized images pass the existing nine scoped MVE gates. Actual C underflow
  behavior is tested with register stubs; new IRQ linking and source routing are
  checked, but target exception dispatch and FIFO behavior need a board run.
- `build_evidence/commands.json`, logs, image hashes and `memory.json` describe
  the final candidate. Earlier prefill design/builds are preserved under
  `attempts/`; they were never flashed. Raw timestamped build artifacts remain
  under ignored diagnostics. A same-second evidence directory collision aborted
  one helper before any build; separate build prefixes corrected it.
- Ordinary outputs restored with probe/replay/capture OFF, editor Debug. Release
  UART remains disabled. No model/package/libs/golden/numeric source edits.

RW_RAM remains Debug78712/Release77168/Probe78216 bytes. Arena65536, DMA320,
stack8192 bytes remain reserved. Linker maps are static evidence only; actual
arena use and stack watermark of this image remain unmeasured. Debug callgraph
includes Unknown paths, not a guaranteed stack bound. No new DSP replay was
needed: DSP code is unchanged and the full numeric regression suite reran.

| Final BIN | Bytes | SHA-256 |
|---|---:|---|
| Debug | 643744 | `a8888468ca85ca832581f69ed24aa6b11046a352eedb2783fb2f5f7e132c806e` |
| Release | 618688 | `6be144e428ba916b9852f51e4b574deb4f61bdce630be4fa71753c73b7a126d4` |
| Release Probe | 625696 | `b6ead4bfeb58b033d6f7eff15a3bca35e7ed9bed30d0ee124c7f916fa4a165d6` |

Final images/maps/callgraphs: `diagnostics/submillisecond_runtime_20261108/fifo_build_233653/`.

| Acceptance item | Status | Boundary |
|---|---|---|
| Artifact integrity | PASS | Package/hash regression tests; no model edits |
| Architecture/source integration | PASS | Existing driver/board callback, ownership tests, style and dependency checks |
| Host numeric correctness | PASS | Full134 tests; FIFO model is not a physical scheduler |
| Debug/Release build/link | PASS | Also Probe; final receipts and MVE gates |
| Raw controller replay | NOT_RUN | Prior Linux receipt retained |
| Compiled controller replay | BLOCKED | No execution backend/device run in this work |
| Board initialization/memory | NOT_RUN | Static maps available, final candidate not run |
| Full-hop deadline/schedule | NOT_RUN | Old trace fails startup; new physical phase/b+2 needs measurement |
| Physical audio/latency/power | NOT_RUN | No flash or physical measurement |

## Reproduce and next board run

```powershell
rtk python -m unittest discover -s tests -p test_r2_dma_ownership.py -v
rtk python -m unittest discover -s tests -v
rtk proxy cmd /c build.bat debug config
rtk proxy cmd /c build.bat debug build
rtk proxy cmd /c build.bat release build
rtk git diff --check
```

Use the existing **M55M1: Release Probe: Flash + Debug (OpenOCD Nuvoton)**
profile: its prelaunch tasks rebuild Probe in `build/`. F5, Continue, no later
halts. Save the flashed BIN hash, all AUDIO_STARTUP lines and both complete
AUDIO_RELEASE_PROBE/RESULT lines. The ordinary Release BIN currently in build/
is intentionally silent; profile selection must enable Probe before flashing.

Expected new observation: TX_ENABLED starts with FIFO preloaded; the immediate
4us TD is removed. First TX TD is near the end of silence0, with silence1 in
FIFO. Capture may still lag slightly; driver waits for a valid publication while
FIFO drains. Later TDs require published audio. Real underflow remains a fault.
These are expectations, not measured results for this candidate.

Require60000 continuous hops, fault/deadline/overrun/underrun0, sequence continuity,
physical b+2 alignment, total and capture-to-publish <1000us (target <=900us).
RX DSP startup delay, serial/FIFO phase and interrupt jitter still reduce physical
margin; terminal tasks do not create a guaranteed extra compute budget. If the
new run reaches inference but misses the deadline, inspect its measured stage
split. Do not insert silence or alter the model/deadline to hide failure.

Manufacturer evidence: M55M1 I2S has a16-level TX FIFO
([datasheet, p25](https://www.nuvoton.com/export/resource-files/en-us--DS_M55M1_Series_EN_Rev1.00.pdf)).
The installed vendor `pdma_reg.h` OPMODE field documents terminal BASIC versus
SCATTER fetch; `i2s_reg.h` STATUS0 bit16 documents real TX underflow and W1C.
Their exact hashes and changed-source hashes are in `sources.json`. No other
MCU's TRM was used to infer the request rule.
