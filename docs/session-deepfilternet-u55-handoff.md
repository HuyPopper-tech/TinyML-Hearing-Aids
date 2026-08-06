# DeepFilterNet U55 integration handoff

## Implemented

- Audited pre/post-Vela package and deterministic five-scenario fixtures.
- Single model-data translation unit and generated tensor/frontend contract.
- Transactional Ethos-U/TFLM adapter with recurrent reset/requantization.
- Exact scalar 512/256 frontend, DF postprocess, WOLA, preroll and fallback.
- Direct 256-sample DMIC/DAC transport and DeepFilterNet-only build graph.
- Bounded 100-frame diagnostics with 2048-byte UART ring and stack monitor.
- CMSIS-DSP MVE backend plus scalar/MVE PARITY metrics; Release requires MVE.

## Verified on host

- Asset generator `--check` succeeds.
- DeepFilterNet focused tests and scalar five-scenario oracle succeed.
- Full suite reaches 108 passing tests; the sole error is the pre-existing
  missing user-local `.codex/AGENTS.md` fixture.
- Debug, SCALAR, PARITY and MVE configurations generate successfully.
- MVE/PARITY source is host syntax-checked against the CMSIS-DSP API surface.
- `git diff --check` reports no whitespace error.

## External blockers and pending gates

Arm Compiler 6.24 refuses compilation with `The license found is assigned to
another user` (`https://lm.arm.com/400`), before any project source compiles.
Therefore AXF/map sizes and final CMSIS-MVE code generation are not verified.

No board was available in this session. `models/deepfilternet/board_gate.json`
contains only pending/null fields. Required next action: restore Arm license,
build Debug/Release, flash through the documented recovery setup, capture five
golden + PARITY scenarios, ten diagnostic windows and a 10-minute production
soak. Do not mark release-final until every board gate contains measured data.
