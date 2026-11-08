# Firmware source cleanup verification — 2026-11-08

The custom product now has one application entry point, app/main.c, and only Debug
(build-debug/) and Release (build/). The source training packages, checkpoints,
full traces, generated builds and raw local diagnostics are not committed.

## Changes and ownership

- Removed the four old clock/I2S/DMIC/passthrough diagnostic apps, standalone CMake
  roots, exclusive services/board/driver/common code and unused forwarding wrappers.
- Removed old register-experiment GDB scripts and the unused future-model slot.
  Kept the CH549/Nuvoton APROM-only programmer and memory recovery configuration.
- Removed the obsolete in-source CMake cache/directory after a local archive.
- Reconciled c_style_guide.md with AGENTS.md; formatted 47 product C/C++ files and
  the native numeric harness with clang-format 18.1.8. Doxygen file authors are
  Bui Minh Huy. Public contracts and inline comments describe histories, buffer
  ownership, lifecycle, rounding, reconstruction and publish ordering.
- Token equivalence on product files preserves executable logic after reversing
  four documented constant/table renames, ignoring physical line splices and
  accounting for one duplicate include removal. This is a source check, not
  compiled controller parity. Generated model artifacts and libs were not edited.
- Tests for deleted diagnostics were removed; production capture-start behavior
  remains tested. Native DSP/Q31/binding/FIR/schedule/recovery tests remain.
- Imported hashes and synthetic replay-comparator tests no longer require the
  training folders. A small immutable verifier fixture is checked against its
  source-manifest hash before executing four portable functions. The full local
  package/golden audit is explicitly optional when that package is absent.
- .gitattributes preserves model/helper bytes on Windows checkout. Ignore rules
  exclude all three named training folders and local evidence; they stay on disk.

## Verified results

Local full suite: 120/120 PASS, including the full package/golden artifact audit.
Git-index checkout without training folders: 119 PASS and 1 explicit source-audit
SKIP out of 120. The pinned OpenOCD runtime was copied only as test tooling.
clang-format --dry-run --Werror passed all 48 files.

See the timestamped regression and build receipts in this directory. Debug and
Release configure/build/link all exited 0 using Arm Compiler 6.24. Product BINs
remain 627360 and 612736 bytes; exact SHA-256 values are in build_products.json.
The regenerated BINs are not byte-identical to the previous build: renamed
constant tables change linker placement (confirmed from Debug ELF symbols).
Numerical behavior is checked independently; no binary parity claim is made.
The 64 KiB arena capacity and linked SRAM totals do not establish runtime usage.

The final acceptance receipt records the latest full suite, formatting, staged
content audit and checkout verification. Raw logs, previous failed test attempts,
deleted-source archive and compiler-runtime identity are retained locally in
 diagnostics/source_cleanup_20261108_143748/ and
 diagnostics/continuous_low_power_20261108_122920/.

## Reproduce

Install the existing firmware toolchain, Python with NumPy, and a working native
GCC/G++ toolchain. Install the pinned OpenOCD runtime using tools/openocd/install.ps1
for its offline interpreter tests. No board or TensorFlow is needed for the host
numeric/protocol tests. Run from the repository root:

```powershell
rtk python -m unittest discover -s tests -v
rtk proxy cmd /c build.bat debug config
rtk proxy cmd /c build.bat debug build
rtk proxy cmd /c build.bat release config
rtk proxy cmd /c build.bat release build
rtk git diff --check
```

On this Windows host the installed GCC subprocesses lacked their DLL search path;
verification used byte-identical installed compiler copies in an isolated local
runtime and host-only -B/-static flags. Firmware toolchain/libs were not modified.
Without the local training package the source/golden audit reports SKIP; without
installed OpenOCD its interpreter tests report SKIP. Those are not parity passes.

No flash or hardware manipulation. Compiled controller replay, actual arena/stack,
full-hop timing, power/current savings and physical audio/latency are NOT_RUN or
BLOCKED. deployment_approved remains false. No force push is used.

Build-log copies use LF line endings; the original local diagnostic logs remain unchanged. Model and fixture bytes are preserved, with CRLF recognized by the whitespace checker.
