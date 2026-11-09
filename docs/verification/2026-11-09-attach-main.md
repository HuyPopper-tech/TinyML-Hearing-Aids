# Debug and Release Attach stop at main — 2026-11-09

Acceptance: both existing Attach profiles restart the flashed image and stop at
`main` without building or programming flash. Symbols must match that image.

Cause: both profiles only issued `monitor halt 1000`, leaving PC anywhere in the
running application. Installed Cortex-Debug 1.12.1 package schema and adapter
explicitly exclude Attach from `runToEntryPoint` handling. Simply adding that
property would not correct this issue.

Change: both profiles issue `monitor reset halt`; after session startup execute
`thbreak main`, then `continue`. The temporary hardware breakpoint is removed on
hit. Existing vendor target uses `cortex_m reset_config sysresetreq`; project
`reset_config none` remains unchanged. This uses software reset without nRESET.
The entire audio session restarts. External codec rails are not power-cycled.
Ordinary Release still has UART disabled; Release Attach cannot enable boot logs.
Debug boot logs appear after Continue from main with UART reception already open.

Regression: revised profile assertions failed on both original Attach entries,
then passed after correction. Full `rtk python -m unittest discover -s tests -v`:
135 tests PASS, zero skips, exit 0. Raw host log/command receipt:
`diagnostics/submillisecond_runtime_20261108/091635/`.
`rtk git diff --check`: exit 0. No firmware code changed; images were not rebuilt.
No server/probe/flash/reset operation was performed against hardware.

Retest: select Debug Attach, F5, confirm PC at main, open UART and Continue.
Repeat with Release Attach and the matching AXF; expect main stop and silent
ordinary Release UART. Do not pause/single-step active audio for timing evidence.
If SYSRESETREQ cannot halt the board, use full-board power recovery; the probe
cannot assert nRESET. Revised live Attach behavior remains NOT_RUN.
