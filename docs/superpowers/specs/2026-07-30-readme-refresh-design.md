# README refresh design

Date: 2026-07-30

## Goal

Replace the outdated DMIC-test-oriented README with an accurate entry point for
the current ERB-GRU hearing-aid firmware on M55M1 and Ethos-U55.

## Audience and ordering

The README serves developers who need to understand, build, flash, or validate
the firmware. It uses a quick-start-first structure:

1. current firmware purpose and validation status;
2. canonical Debug and Release build commands and artifacts;
3. architecture and realtime audio data path;
4. deployed model contract and build-time options;
5. diagnostics, flashing, recovery, and validation;
6. project layout and links to detailed documents.

## Content contract

- State that production processes 16 kHz audio in 160-sample hops with ERB-GRU
  inference on Ethos-U55.
- Describe the required dependency direction:
  `app -> services -> drivers -> board -> libs`.
- Identify the immutable deployment package, Vela model size and SHA-256.
- Explain that the current model outputs 32 band gains and firmware performs
  the audited 32-to-161 CPU expansion with a loop-local Armclang 6.24
  vectorization workaround.
- Document canonical `build.bat` and `build.sh` commands and the `.bin` output
  used for flashing.
- Document production defaults and the diagnostics, golden-scenario, input-gain
  and fault-injection CMake options without duplicating every CMake detail.
- Keep UART, SWD, TrustZone and recovery information needed to avoid losing
  board access.
- Separate verified host/build results from pending board acceptance. Do not
  call the firmware production-ready before board parity, memory measurements
  and the 10-minute soak pass.
- Link to `docs/erb-gru-model-deployment.md` and
  `docs/session-erb-gru-ethosu-handoff.md` for detailed model and validation
  evidence.

## Removed or condensed content

- Remove the obsolete statement that the current application is a DMIC
  functional test.
- Remove the long CMSIS/RTOS tutorial; the project remains bare-metal and the
  build configuration is authoritative.
- Condense raw manual-CMake, IDE and memory-map explanations where canonical
  scripts or specialist documentation already cover them.
- Do not copy telemetry schemas, Release investigation logs or the full
  replacement-model checklist into README.

## Validation

- Check every repository-relative Markdown link.
- Check documented build options against `CMakeLists.txt`.
- Check build commands against `build.bat`, `build.sh`, and `CMakePresets.json`.
- Run the existing Python test suite and `git diff --check`.
- Because the change is documentation-only, firmware rebuilds are unnecessary
  unless a command or configuration discrepancy requires a source change.
