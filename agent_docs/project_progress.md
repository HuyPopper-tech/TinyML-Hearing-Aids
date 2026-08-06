# Project Progress

## Active deployment: DeepFilterNet U55 firmware integration

- Route: Heavy.
- Status: host implementation complete; board/toolchain acceptance blocked.
- Objective: replace the ERB-GRU production path with the single
  `deepfilternet_u55_full_seed20260805` model, using a 512-sample frame and a
  256-sample hop at 16 kHz.
- Source package: `deepfilternet/` is user-owned and immutable.
- Work order: artifact audit; inference adapter; scalar DSP oracle; 256-sample
  transport and production replacement; diagnostics; MVE/parity; host/build/
  board verification and documentation.
- Acceptance: the approved plan in
  `docs/superpowers/plans/2026-08-05-deepfilternet-u55-firmware.md`.
- Board-only gates remain pending until real hardware logs are captured; no
  estimate may be recorded as a result.

### Reconciliation 2026-08-06

- DFN-01 through DFN-06 implemented: audited assets, inference adapter, scalar
  oracle, 256-sample production pipeline, full telemetry and CMSIS-DSP
  MVE/PARITY paths.
- Generator check passes. Focused DeepFilterNet tests pass. Full suite reports
  108 passes and one pre-existing missing `.codex/AGENTS.md` fixture error.
- Debug/SCALAR/PARITY/MVE configure passes. Arm build is blocked before project
  compilation because the installed license belongs to another user.
- `models/deepfilternet/board_gate.json` remains pending/null. Next action:
  restore license, build, flash, capture golden/parity/timing/map/stack/soak
  evidence, then finalize arena and acceptance docs.
