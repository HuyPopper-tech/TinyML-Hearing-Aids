# Release Probe launcher verification — 2026-11-08

Added the requested VS Code/OpenOCD Release Probe launch profile and standalone
APROM flash task. Both depend on the existing product Release configuration,
then enable probe, then use build-only. No new build directory/CMake preset,
firmware behavior, pin/clock or OpenOCD reset/programming policy was introduced.

| Check | Status | Evidence |
| --- | --- | --- |
| Launch/task JSON and image/dependency contracts | PASS | tests.log; five launch profiles including one Release Probe, three flash launch paths checked by the offline Tcl harness |
| Actual probe task build chain | PASS | commands.json; configure Release → probe ON → build-only, each exit0 |
| Probe image flags/content | PASS | probe.json;620896B SHA7a6df1b8e6655216d1bae78d6f62289e74278713b9bf8dc8081efb87d7482e61, probe ON, replay/capture OFF, both result marker strings present |
| Full host regression | PASS | tests.log;128 tests,35.299s, no skips |
| Ordinary Release restore | PASS | restore.json; probe/replay/capture OFF,615392B SHA72ea67e693846caa642286c2a7e9e90ecfaf03f196b3542f9fc5867384e2acbf, no probe result strings |
| Debug editor restore | PASS | Configure_Debug.log and restore.json; root compile database points to build-debug |
| VS Code F5 UI / target flash / board timing | NOT_RUN | Task commands executed directly from tasks.json; no live debugger, programming or board operation |

Raw logs: diagnostics/release_probe_profile_20261108/210929/. Text copies here
normalize line endings/trailing whitespace; binaries and compiler files are not
committed. The checked-in task chain can be run directly with Terminal → Run Task
→ **M55M1: Build Release Probe**, which builds only and does not flash.

For actual programming, use **M55M1: Release Probe: Flash + Debug (OpenOCD Nuvoton)**
or **M55M1: Flash Release Probe (APROM, verify)** as described in
[the OpenOCD guide](../../M55M1_OPENOCD_DEBUG.md). Probe and product share build/;
run their builds/programming sequentially. Ordinary Release tasks turn probe OFF.
CH549 still lacks physical nRESET. No deployment approval or device acceptance
is inferred from these configuration/build tests.
