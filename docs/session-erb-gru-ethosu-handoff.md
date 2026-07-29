# ERB-GRU Ethos-U55 reintegration handoff

Updated: 2026-07-29  
Worktree: `D:\TinyML-Hearing-Aids\.worktrees\erb-gru-ethosu`  
Branch: `codex/erb-gru-ethosu`

## Deployment package

- Package: `w4_erb-gru_deployment_v2_seed20260730`
- Source package is preserved unchanged under
  `models/erb_gru/packages/w4_erb-gru_deployment_v2_seed20260730/`.
- Vela model: 59,232 bytes.
- Vela SHA-256:
  `45f59d59598d74d36daebbb0d61b4d05020e164e4f78a62fcc31b4bb6b7d8fe8`.
- Generated contract uses Vela tensor ordinals and raw indices, not the
  pre-Vela indices recorded in the raw manifest.
- State output is copied byte-for-byte to the matching next state input.
  State1 reset value is `-1`; state2 reset value is `0`.

`tools/generate_erb_gru_assets.py` verifies the package, both FlatBuffers,
Vela report, model header bytes, DSP constants and every golden header. It
generates `erb_gru_deploy_contract.h`, stable asset/model includes,
`deploy_audit.json`, sanitized compileable DSP constants, and golden headers.
`--check` currently passes.

The raw package PCM output was generated from float input, while the board
receives rounded PCM16. This causes raw-output differences up to 55 LSB for
the near-full-scale corpus even when the firmware math is correct. The package
was not modified. The generator records this mismatch in `deploy_audit.json`
and appends a clearly identified `*_pcm_output_board_int16` reference derived
from PCM16, quantized gains, and the exact package DSP constants.

## Firmware state

- Dependency ownership is separated:
  - board: NPU clock/reset/base and IRQ priority;
  - Ethos-U driver: `ethosu_driver`, cache callbacks, `NPU_IRQHandler`;
  - ERB-GRU service: TFLM interpreter, model validation, recurrent state and
    DSP/WOLA.
- Boot validation checks hash, size, schema, one subgraph, one `ethos-u`
  custom op, raw tensor indices, ordinals, types, ranks, shapes, bytes and
  per-tensor quantization.
- Tensor arena remains the 128 KiB bring-up arena. Final
  `align_up(arena_used + 1024, 16)` cannot be selected until a licensed board
  build and measurement are available.
- First 160-sample hop produces silence and does not run FFT/inference or
  update recurrent state. The next hop creates the first 320-sample frame.
- Invoke failure uses unity gains through the same IFFT/WOLA path, resets both
  recurrent states, and is reported as `UNITY_FALLBACK`.
- Production input gain defaults to 0 dB. The 12 dB option is diagnostic-only.
- UART1 PA2/PA3 uses a 1,024-byte IRQ-driven SPSC TX ring. Messages are admitted
  whole or dropped whole. UART IRQ priority is 3, below audio DMA priority 1
  and NPU priority 2.
- No polling `printf` remains after audio start. Telemetry is formatted only
  in foreground when no DMIC block is pending, every 500 frames. It reports a
  100-us-bucket p99 estimate, max timing, audio/NPU errors and drops.
- Fault modes exist for `INVOKE_FAIL`, `INVOKE_SLOW`, and `DAC_BUSY`. A late
  invocation is rejected before output/state publication.
- Golden builds compile exactly one selected corpus and run 24 stateful frames
  plus the final overlap flush.

## Validation completed

- Generator full audit: pass with TensorFlow 2.20.
- Generator reproducibility `--check`: pass.
- Host DSP parity: pass for silence, sine 1 kHz, clean speech, speech plus
  noise, and near-full-scale; 24 frames without state reset plus flush.
- Host inference-failure recovery: pass; WOLA fallback is used and both states
  are reset before the next successful frame.
- Fixed byte ring whole-message/drop/wrap host test: pass.
- `git diff --check`: pass.
- Full Python suite: 51 tests pass. The two remaining errors are isolated
  worktree infrastructure files absent from this worktree:
  `.codex/AGENTS.md` and `.vscode/launch.json`.
- Debug configure: pass.
- Release configure: pass.

## External blockers and remaining board acceptance

Debug and Release compilation both stop immediately because Arm Compiler
license 400 is assigned to another user. No new AXF/BIN/map file was produced.
Consequently these values remain unmeasured and must not be inferred:

- final arena size and arena high-water;
- Flash/cacheable/non-cacheable SRAM map usage and single model-copy check;
- stack high-water and 2 KiB headroom requirement;
- on-board five-scenario parity;
- fault boundary discontinuity;
- 10-minute production soak and p99/max/counter acceptance.

Before flashing, preserve the recovery path: SWDIO PF0, SWCLK PF1, nRESET,
GND and VREF/3V3. UART debug remains PA2 RX / PA3 TX at 115200-8N1. No pin mux
or recovery behavior was changed.

After restoring the Arm Compiler license, rerun Debug and Release builds,
inspect the map, then build each golden/fault configuration and flash through
the recovery connection above. Only after the measured arena and full board
acceptance pass should the 128 KiB arena be reduced.
