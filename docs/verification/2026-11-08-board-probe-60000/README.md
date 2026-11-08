# User-reported custom-board Probe run

Source: the user's complete UART text, saved as `user_log.txt`. The supplied
image has no BIN hash or firmware revision in this log. Commit `99c617b` and
Probe BIN `b6ead4bfeb58b033d6f7eff15a3bca35e7ed9bed30d0ee124c7f916fa4a165d6`
are the associated candidate, **not an independently attested flashed identity**.
The agent did not connect a probe, reset, flash or operate the board.

Run `rtk proxy python -X utf8 docs/verification/2026-11-08-board-probe-60000/verify_log.py`
to verify the snapshot/receipt without changing an existing receipt.

| Reported observation | Result |
|---|---:|
| Processed frames / capture sequence | 60,000 / 60,000 |
| Samples / nominal duration at 16 kHz | 960,000 / 60 seconds |
| Total hop mean / maximum | 839 / 863 us |
| Capture-to-progress mean / maximum (`e2e`) | 855 / 885 us |
| Capture-to-publication maximum | 884 us |
| NPU / Invoke maximum | 411 / 474 us |
| Deadline, DMA, overrun, underrun, model and submit failures | 0 |
| Final status / logical playback slot | COMPLETE / 60,001 |
| Input rail samples / output clipping | 17 / 0 |

The 1-ms timing subgate passes for this supplied run: 115 us maximum reported
capture-to-progress headroom, 15 us below the 900-us engineering target.
Stage maxima are independent; do not add them to infer a simultaneous hop.
Input rail samples are 17/960,000 (~0.00177%); the summary cannot identify their
cause. Output peak is 7,224. Gain/calibration is unchanged.

The first TX TD is at 1,066 us with RX sequence1, after RX TD at1,048 us and
capture take at1,051 us. The old immediate FIFO-prefetch startup fault is absent
in this run. This is observed ordering, not physical microphone-to-DAC alignment.

`mve_test_us=38222` is the boot self-test, not inference time. `STOP elapsed_us=60233`
is modulo the 25-bit core-equivalent SysTick cycle counter, whose period is
~152.52 ms at configured220 MHz. It is **not** elapsed wall time for the run.
The 60-second duration is nominal from sample count; the clock was not independently
measured. Single-hop intervals are shorter than that counter period.

Probe intentionally stops after60,000 hops and prints only after audio stops.
Ordinary Debug/Release have no finite Probe stop. Physical acoustic latency,
sample/slot packing, long-duration ordinary Release/Debug soak, stack watermark,
power and compiled-controller bit-exact replay are not established by this log.
`deployment_approved` remains false. Later cleanup binaries need their own board
retest; this result does not attest their image hashes.
