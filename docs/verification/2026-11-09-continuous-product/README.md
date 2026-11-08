# Continuous custom product finalization — 2026-11-09

Base: `99c617b`. The user requested ordinary Debug/Release integration, retained
Probe timing and removal of unused code. One application and one audio pipeline
already served these variants; this change removes the alternate legacy paths
and verifies that the ordinary service runs beyond the finite Probe limit.

## Result and usage

| Variant | Configure/build | Runtime behavior |
|---|---|---|
| Debug | `build.bat debug build` | Continuous audio; symbols, init/fault and periodic `[AUDIO]` UART2 logs |
| Release | `build.bat release build` | Continuous audio; UART disabled, same optimized model/DSP/DMA pipeline |
| Release Probe | VS Code `M55M1: Build Release Probe` | Same optimized pipeline, timing counters; stops after 60,000 hops or a fault, then prints startup/timing/result |

There are only two build directories: `build-debug/` and `build/`. Probe is an
opt-in of `build/`; its task first configures ordinary Release, then enables only
`M55M1_AUDIO_RELEASE_PROBE`. Ordinary scripts/presets turn Probe, PCM capture and
compiled replay OFF. Final local outputs have those opt-ins OFF; clangd points
to Debug. The existing three Flash + Debug launch choices select the matching
build tasks; Attach does not build or flash. No new launch profile is needed.

Windows reproduction:

```powershell
rtk proxy cmd /c build.bat debug build
rtk proxy cmd /c build.bat release build
# Probe without a device operation, matching the VS Code task chain:
rtk proxy cmd /c build.bat release config
rtk proxy cmake -S . -B build -DM55M1_AUDIO_RELEASE_PROBE=ON
rtk proxy cmd /c build.bat release only
# Restore continuous Release after saving the Probe image/hash:
rtk proxy cmd /c build.bat release build
rtk proxy cmd /c build.bat debug config
rtk python -m unittest discover -s tests -v
rtk git diff --check
```

Debug preserves the strict 1-ms fault guard and full recovery; unoptimized Debug
has no measured deadline guarantee. A breakpoint while audio is active can
cause a legitimate overrun/underrun. Use Probe/Release without later halts for
timing acceptance. Debug UART2 is PC5 TX / PC4 RX, 115200-8N1. UART admission is
bounded and nonblocking during audio; boot/stopped fault output may drain.

## Shared implementation and cleanup

- `app/main.c` continuously polls the public service and waits for work. The
  inference service owns one interpreter/arena and recurrent state; frontend
  and FIR ownership, encoded feedback, model identity and DSP math are unchanged.
- `board/m55m1_audio.*` now accepts only the 16-kHz, hop-16 custom product. Removed
  unreachable old warmup/clock and 32-kHz branches. Current pins, clocks, PDM
  edge/OSR, RX LPPDMA descriptors, priorities and recovery sequence are unchanged.
- `drivers/tlv320dac3100/*` retains the tested FIFO-aware terminal BASIC task
  handoff as the only playback implementation. Removed legacy scatter chaining,
  32-kHz constants/profile, untagged submit and write-only `has_queued_audio`.
  Submission now rejects NULL tags as well as bad size/pointer/sequence/state.
- `drivers/audio_transport/*` retains tagged publication, fault checks and WFI;
  the untagged wrapper and alternate product-selection guard are removed.
- CMake/scripts remove the duplicate SRAM spelling, unused microphone-gain
  definition and dormant RTOS build/link block. Removed uncalled historical
  `services/audio_telemetry.c/.h` and its private test. Current pipeline/inference
  logs and timing are retained. No package, golden, `libs/`, replay/capture tool,
  historical evidence or unrelated user file was deleted.

The already empty obsolete `app/npu/` and `drivers/ethosu_u55/` directories were
removed. Ninja `cleandead` removed stale generated objects from the two active
build directories; final BIN hashes remained unchanged.

The retired scatter-only C harness was replaced by retained product handoff
coverage plus invalid input/tag and actual C profile-rejection checks. Both
ordinary foreground harnesses now exceed 60,000 hops (60,005 each) without a
finite stop. This is actual pipeline/driver C with modeled transport/inference
or registers, not a device throughput or compiled-controller parity test.

## Verification and images

134/134 tests pass, no skips. Full regression, native numeric results, the
initial expected cleanup-gate failure and the stale-fixture failure are indexed
in `evidence_index.json`; earlier raw logs remain in `diagnostics/`. Numeric
tolerances and canonical data are unchanged. Arm Compiler 6.24 Debug, Release
Probe and ordinary Release all configure/build/link successfully. The nine
scoped MVE disassembly gates pass for Release and Probe. Debug links with symbols;
the optimized disassembly gates do not apply to its unoptimized wrapper ranges.

Final local images/maps: `diagnostics/submillisecond_runtime_20261108/fifo_build_001119/`.
Images are not committed. Full hashes and map summaries: `memory_and_images.json`.

| Variant | BIN bytes | BIN SHA-256 |
|---|---:|---|
| Debug | 643552 | `1b7861e65c5c520d92a30ef642a5681dfdffabdc07a109bad7dd6d3f18565254` |
| Release | 618528 | `c124cf7b37b38c1f9c0bf4fd0ace1c75cd400c4409f7bc0127ae28d13e61a044` |
| Release Probe | 625536 | `e2c6cf28e912fdf070fee5ff57e3b624da78b2d7d6f5ce906a402775b33895a4` |

RW_RAM is Debug 78,712 / Release 77,168 / Probe 78,216 bytes, unchanged from the
preceding candidate. These are region sizes, not total RAM. The maps separately
reserve model SRAM 516,852 bytes (516,848-byte image plus length), arena 65,536
bytes within RW_RAM, uncached DMA 320 bytes, DTCM UART storage 2,048 bytes where
enabled, stack 8,192 bytes and heap 2,048 bytes. See full linker totals before
summing regions; do not add arena twice. Static Debug stack is 6,656 bytes plus
unknown indirect/cyclic paths, not a measured watermark.

All 47 source manifest artifact hashes and destination manifest hashes match;
the C++ array equals the compiled model byte for byte. Each final BIN contains
exactly one compiled image. Compile databases have one main, inference service,
model, Ethos-U core driver and device implementation; the old telemetry object
is absent from maps. Model SRAM remains at 0x20100000 and arena is aligned in
0x201c0000 SRAM. Immutable model initialization remains before AllocateTensors.
`artifact_integrity.json` and `linkage.json` record the checks.

## Acceptance boundaries

| Gate | Status | Evidence / remaining boundary |
|---|---|---|
| 1 Artifact integrity | PASS | `artifact_integrity.json`; source package unchanged |
| 2 Architecture/source integration | PASS | Native suite, tagged ownership, single link inputs, two profiles |
| 3 Host numeric correctness | PASS | `host/001119/tests.log`; existing float/integer assertions retained |
| 4 Debug/Release build/link | PASS | `build_evidence/commands.json`, image/map hashes, Release/Probe MVE logs |
| 5 Raw replay rerun | NOT_RUN | Historical package Linux receipt only |
| 6 Compiled replay | BLOCKED | Existing runner retained; no authorized running backend/probe execution |
| 7 Board initialization/memory | NOT_RUN | Final images not executed; earlier user Debug reported arena 6976/65536 bytes |
| 8 Full-hop deadline/schedule | NOT_RUN | Final images need retest; earlier supplied Probe timing subgate PASS, physical slot alignment/long soak not measured |
| 9 Physical audio/latency | NOT_RUN | No new acoustic/packing/latency/power measurements |

The [supplied Probe snapshot](../2026-11-08-board-probe-60000/README.md) reached
60,000 fault-free hops, max total 863 us, capture-to-progress 885 us and
capture-to-publication 884 us. It passes that run's reported timing subgate with
115 us headroom. Its flashed identity was not supplied; it does not attest the
new binaries. `e2e` and TX DMA completion timestamps are software/FIFO timing,
not acoustic latency. STOP's modular `elapsed_us` is not run wall time.

Next hardware steps: record the chosen final BIN hash; run Probe without
post-start breakpoints, then ordinary Release for at least 30 minutes with
fault/continuity observations; check Debug boot/recovery separately. Capture
stack watermark, physical slot/sample packing and microphone-to-headphone
latency using an authorized measurement setup. Full compiled trace parity and
power measurements remain separate. Do not change goldens, relax deadlines,
add a queue hop or set `deployment_approved=true` to close missing evidence.

Custom pins remain DMIC PA1/PA2; I2S1 PA8/PA10/PA11/PB0; I2C0 PA4/PA5; debug
UART2 PC4/PC5; SWD PF0/PF1. The CH549 probe has no nRESET connection. Preserve
the existing reset-less OpenOCD procedure and physical power-cycle recovery;
never assume the probe can reset external components. The agent performed
no flash, reset, probe connection or board operation in this task.
