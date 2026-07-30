# ERB-GRU Ethos-U55 reintegration handoff

Updated: 2026-07-30
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

The verified Vela report also supplies deployment performance metadata:

- MACs per inference: `45,056`.
- NPU operators: `30`.
- CPU operators: `0`.

The generator records these values in `deploy_audit.json` and emits them in
`erb_gru_deploy_contract.h`. They are model metadata, not measurements derived
from the Vela report's assumed 500 MHz clock.

`tools/generate_erb_gru_assets.py` verifies the package, both FlatBuffers,
Vela report, model header bytes, DSP constants and every golden header. It
generates `erb_gru_deploy_contract.h`, stable asset/model includes,
`deploy_audit.json`, sanitized compileable DSP constants, and golden headers.
`--check` currently passes.

## Diagnostic build mode

`M55M1_ERB_GRU_DIAGNOSTICS` is the single diagnostic switch and defaults to
`OFF`.

- `OFF` does not compile the UART1 board binding, IRQ-driven debug UART TX,
  fixed byte ring, acoustic accumulator, stream timestamp association, or
  telemetry formatter. Project boot, fatal, performance and acoustic logs are
  also absent.
- `ON` enables UART1 PA2/PA3 at 115200-8N1 and emits one atomic snapshot after
  each 100 processed hops. A pending DMIC block delays formatting. An enqueue
  failure retains the complete pending message and all statistics for retry.
- Golden scenarios require diagnostics `ON`. Release diagnostics may use
  golden scenarios and fault injection, while production Release
  (`DIAGNOSTICS=OFF`) rejects both.
- No diagnostic work was added to the DMIC, LPPDMA, PDMA, NPU, or UART ISR
  beyond the existing bounded UART FIFO drain.

Each snapshot has four CRLF-terminated records:

```text
[PERF] f=... enh_us=avg/max/p99 npu_us=avg/max/p99 dsp_us=... e2e_us=avg/max/p99 e2e_n=... e2e_gap=... e2e_40ms=... fallback=... deadline=... dmic_ovr=... dmic_dma=... dac_busy=... underrun=... dac_dma=... npu_error=... fault_injected=... uart_drop=...
[MODEL] mac_inf=45056 npu_ops=30 cpu_ops=0 active_mmac_s_x1000=... stream_mmac_s_x1000=4506 duty_permille=... invoke_count=... model_flash=59232 arena=used/131072 dsp_static=... stack_cfg=8192
[AUDIO] frames=... in_peak=... in_rms=... in_clip=... out_peak=... out_rms=... out_clip=... rms_ratio_milli=... feat_q=min/mean/max feat_sat=low/high gain_milli=min/mean/max gain_zero_pct=... gain_unity_pct=... state1_q=min/max state2_q=min/max state_sat=...
[DSP] valid=... bin_gain_milli=min/mean/max spec_ratio_milli=... ifft_rms=... wola_rms=... wola_peak=...
```

The enhancer accumulator excludes pre-roll, associates WOLA output with the
preceding capture hop, and observes feature tensors immediately after
quantization and gain/state tensors only after successful Invoke. Unity
fallback output remains in PCM statistics and is identified by the cumulative
fallback counter. Intentional `INVOKE_FAIL` events increment `fault_injected`,
not `npu_error`; the latter is reserved for a real TFLM Invoke failure. If no
Invoke succeeds in a window, `gain_milli=0/0/0` and gain count remains zero.

DSP diagnostics convert normalized spectrum, normalized IFFT and WOLA boundary
samples to clamped Q15 before accumulating Q30 energy. DC and Nyquist have
weight one; other spectrum bins have weight two. Per-frame collection performs
no division, square root, formatting, or allocation. Unity fallback records
bin gain 1000 and traverses the same spectrum/IFFT/WOLA instrumentation.

The DSP static field is a `sizeof` total for enhancer working
buffers; stack and arena capacity fields are configured/static values, not
runtime high-water measurements.

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
  `align_up(arena_used + 1024, 16)` cannot be selected until runtime board
  measurement is available.
- First 160-sample hop produces silence and does not run FFT/inference or
  update recurrent state. The next hop creates the first 320-sample frame.
- Invoke failure uses unity gains through the same IFFT/WOLA path, resets both
  recurrent states, and is reported as `UNITY_FALLBACK`.
- Production input gain defaults to 0 dB. The 12 dB option is diagnostic-only.
- UART1 PA2/PA3 uses a 1,024-byte IRQ-driven SPSC TX ring. Messages are admitted
  whole or dropped whole. UART IRQ priority is 3, below audio DMA priority 1
  and NPU priority 2.
- No polling `printf` remains after audio start. Telemetry is formatted only
  in foreground when no DMIC block is pending, every 100 frames. It reports
  exact 100-frame p99/max timing, audio/NPU errors and drops.
- Fault modes exist for `INVOKE_FAIL`, `INVOKE_SLOW`, and `DAC_BUSY`. A late
  invocation is rejected before output/state publication.
- Golden builds compile exactly one selected corpus and run 24 stateful frames
  plus the final overlap flush.
- Core and audio clock ownership is now separated:
  - APLL0 is the 220 MHz SCLK source with divider 1;
  - APLL1 is 475.136006 MHz and supplies I2S0 through `/2`, then `/58` or
    `/29` for exact 4.096 MHz or 8.192 MHz codec MCLK;
  - DMIC shares the validated audio APLL1 when normal firmware is running and
    configures its former 196.608 MHz diagnostic clock only when started
    independently.
- Boot validates and reports `core_hz`, `apll0_hz`, `apll1_hz`,
  `i2s_src_hz`, and `mclk_hz` before codec/audio start.
- All SysTick intervals use 24-bit modular elapsed arithmetic. An Invoke that
  crosses the approximately 76.26 ms wrap at 220 MHz no longer produces a
  false 10 ms deadline failure.

## Validation completed

- Generator full audit: pass with TensorFlow 2.20.
- Generator reproducibility `--check`: pass.
- Host DSP parity: pass for silence, sine 1 kHz, clean speech, speech plus
  noise, and near-full-scale; 24 frames without state reset plus flush.
- Host inference-failure recovery: pass; WOLA fallback is used and both states
  are reset before the next successful frame.
- Fixed byte ring whole-message/drop/wrap host test: pass.
- `git diff --check`: pass.
- Full Python suite: 67 tests pass. The two remaining errors are isolated
  worktree infrastructure files absent from this worktree:
  `.codex/AGENTS.md` and `.vscode/launch.json`.
- Diagnostic host tests cover literal PCM energy/peak/clip statistics, feature
  saturation, gain thresholds, state saturation, pre-roll exclusion,
  accumulator reset, 100-frame scheduling, DMIC deferral, one-message enqueue,
  retry without data loss, p99, wrap-safe E2E timing, sequence gaps and prior-hop
  WOLA timestamp association. Optimized host tests also cover 0.9921875 band
  gain expansion, unity bin gain, unity spectrum ratio and the instrumented
  FFT/IFFT/WOLA fallback path.
- Configure-level tests prove that production Release rejects fault injection,
  diagnostic Release accepts it, and clean defaults exclude diagnostic objects
  and the removed temporary DSP-probe controls.
- Release production and Release diagnostic CMake configurations both pass.
  The production build graph contains none of the six diagnostic source
  objects; the diagnostic graph contains all six.
- Debug configure: pass.
- Release configure: pass.
- The UART1 vendor preprocessor failure is fixed without modifying `libs/`.
  `system_M55M1.c` alone compiles its unsupported weak debug-UART helpers
  empty; project board code supplies strong `SetDebugUartMFP()` and
  `InitDebugUart()` for PA2/PA3 at 115200 baud.
- A Release build made after the UART1 fix completed and produced AXF/BIN/map.
  That map reports 176.76 KiB total ROM, 162.31 KiB total RW, one 59,232-byte
  `erb_gru_vela_model`, a 128 KiB tensor arena, an 8 KiB reserved stack, and
  5,184 bytes in non-cacheable SRAM. The arena/DSP buffers in cacheable SRAM,
  stack in DTCM, and DMA buffers in non-cacheable SRAM do not overlap.
- Debug then exposed microlib process-exit destructor references from the
  namespace-scope TFLM resolver. The resolver now uses static aligned storage
  and placement construction, so firmware does not require `atexit` support.
  Contract tests cover both UART ownership and resolver lifetime.

## External blockers and remaining board acceptance

The Arm Compiler license remains at error 400 ("assigned to another user").
Both Release production and Release diagnostic configurations pass, but both
current-source builds stop at the first licensed compiler jobs. Therefore the
existing Release artifacts and map prove the earlier UART1 fix only and are
stale; neither new telemetry binary has been produced.
The following runtime values remain unmeasured and must not be inferred:

- final arena size and arena high-water;
- stack high-water and 2 KiB headroom requirement;
- on-board five-scenario parity;
- fault boundary discontinuity;
- diagnostic enhance/NPU/E2E timing and acoustic statistics;
- the cause of the perceived unity-like audio path;
- 10-minute production soak and p99/max/counter acceptance.

Before flashing, preserve the recovery path: SWDIO PF0, SWCLK PF1, nRESET,
GND and VREF/3V3. UART debug remains PA2 RX / PA3 TX at 115200-8N1. No pin mux
or recovery behavior was changed.

After restoring the Arm Compiler license, build Release once with diagnostics
`OFF` and once with diagnostics `ON`, inspect both maps, then build each
golden/fault configuration and flash through the recovery connection above.
Capture at least one clean-speech and one speech-plus-noise diagnostic run
before assigning the unity-like sound to feature scaling, model gains, WOLA, or
the output path. Only after the measured arena and full board acceptance pass
should the 128 KiB arena be reduced.

## Optimized Release DSP investigation

The Release-only approximately 0.5 PCM ratio is not yet assigned to
auto-vectorization. Debug normal inference and Debug unity fallback do not show
the fixed attenuation. The successful whole-file `NO_VECTORIZE` investigation
has been replaced by a scoped Armclang loop pragma on the `expand_gains()`
32-term reduction; all other firmware remains at Release `-O3`. The diagnostic
Release build identifies itself before audio start without a temporary probe
field:

```text
[BUILD] type=Release diagnostics=1 gain_db=0 fault=OFF git=... dirty=...
```

After the Arm Compiler license is restored, use a stable 1 kHz tone, discard
startup snapshots, and capture at least 10 snapshots for each diagnostic
Release scenario:

1. Release, diagnostics ON, `INVOKE_FAIL`. A PCM ratio near 1000
   clears the common optimized FFT/IFFT/WOLA fallback path; a ratio near 500
   places the first fault in that common path.
2. Release, diagnostics ON, fault OFF. Compare band gain, bin gain,
   spectrum ratio, IFFT RMS, WOLA RMS and PCM ratio. The first boundary that
   falls from near 1000 to near 500 identifies the faulty stage.
3. Confirm the installed loop-local workaround on the board before removing
   it. Board loop-local A/B validation has not yet been completed.

The first board boot after restoring the license must include:

```text
[BOOT] clocks core_hz=220000000 apll0_hz=220000000 apll1_hz=475136006 i2s_src_hz=237568003 mclk_hz=4096000
```

The exact APLL1 readback may reflect the BSP's fractional rounding, but
`mclk_hz` must be exactly 4,096,000 for the 16 kHz profile. During a production
run, `fallback`, `deadline`, and `npu_error` must no longer rise at the former
SysTick-wrap cadence.
