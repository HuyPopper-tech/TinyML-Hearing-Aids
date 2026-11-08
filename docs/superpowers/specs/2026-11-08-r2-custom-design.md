# DeepFIR R2 on the custom M55M1 PCB

The user's full specification and instruction to proceed without routine confirmations authorize this design and inline execution. Default Light route; no agents, commits, flashing, external research directories, library edits, retraining, PTQ or Vela reruns.

## Intent and constraints

Replace the active Complex3 direct-tap controller with candidate C from `deepfir_r2_scratch_ptq_20261107_151431_366715/firmware_deepfir_r2_C_34c5bd074ae6_69c1c3d2`. Source package is immutable. Preserve existing unrelated work; archive the previous destination package before import. Only Debug (`build-debug`) and Release (`build`) remain advertised build profiles; remove obsolete generated root build trees and NuMaker build paths.

The product is mono PCM16, 16 kHz, hop 16. Contract: feature/h/c semantic inputs map to ports 0/2/1; gain/phase/h_next/c_next outputs to 3/2/0/1. Exact INT16 qparams, Q31 feedback and encoded recurrent coordinates come from the package. No second interpreter or extra state transform. Controller identity is checkpoint 34c5bd074ae6, raw 9d11584db9ce, compiled 69c1c3d2f249 (full hashes in evidence/manifest).

## Approaches

1. Selected engineering decision: adapt the existing service and retain its frontend/FIR after numeric verification. Add gain/phase reconstruction using CMSIS inverse FFT128, mathematically equivalent to the package IDFT, with explicit floating tolerance. This keeps the existing lifecycle, timing hooks and tested MVE FIR and avoids a second pipeline.
2. Replace all service code with the exported adapter: fewer initial binding edits, but duplicates/loses existing lifecycle and timing integration and uses an expensive scalar IDFT. Rejected for this integration.
3. Replace only the blob: incompatible direct-tap ABI and insufficient streaming/recovery. Rejected.

## Ownership and interfaces

`services/deepfir/deepfir_inference_service.cpp` owns one static interpreter/resolver/64 KiB arena and the service context. It validates model schema, exact ports/names/rank/dimensions/bytes/INT16/qparams and accessible SRAM before streaming. Ethos-U immutable memory is bound before AllocateTensors. Expose allocation/use/capacity and replay entry point through the inference service, not model access from app.

`deepfir_features.c` owns analysis history 256 (240 retained) and symmetric Hamming. `deepfir_model_adapter.c` owns previous/current feature context, encoded h/c and raw outputs, and commits after invoke. `deepfir_reconstruction.c` owns only gain/phase to taps; no stream history. `deepfir_dsp.c` owns shared FIR history, previous/current taps and midpoint-Hann. One aggregate service lifecycle resets all owners together; any failure latches fault and prevents context reuse until full recovery. Reject input/output overlap, NaN and partial publication.

`services/audio_pipeline` owns working PCM, expected capture sequence and foreground fault policy/telemetry. On continuity/deadline/invoke failure: stop/mute first, discard buffers, full model reset and reinitialize transport, restart with two silence slots. No retry of partial context or stale output. Always-on bounded counters; opt-in stage/slot records, no realtime blocking logging.

`drivers/audio_transport` composes capture/playback lifecycle. `board/m55m1_audio.c` owns custom pins/clocks/capture DMA. Codec driver owns TX descriptors/PCM, validates tagged publish sequence against completed TX slot and guards ownership/publish. Initial two descriptors are silence. Both rates derive from audio APLL1; capture/TX startup skew and FIFO prefetch remain explicit board acceptance requirements, never inferred from host tests. ISR only hardware acknowledgement, descriptors, counters/signals.

Custom pinout: DMIC0 CH2 PA1 CLK/PA2 DATA; I2S1 PA8 DO, PA10 MCLK, PA11 BCLK, PB0 LRCK; I2C0 PA4 SDA/PA5 SCL; UART2 PC5 TX/PC4 RX. Core 220 MHz, APLL1 request 475136006 Hz, MCLK 4.096 MHz, PDM 2.048 MHz/OSR128. Preserve existing hardware mic gain 30 dB and codec ceiling -12 dB; no new software gain. LPPDMA0 channel0 RX, PDMA0 channel0 TX, PDMA1 channel0 UART. PF0/PF1 SWD untouched. Recover using existing OpenOCD/CH549 procedure/manual reset; do not assume probe nRESET.

## Acceptance

Artifact integrity: every 47 source manifest entries verified; imported C array equals compiled bytes and manifest links source hashes. Production links no raw model/checkpoint/goldens and no duplicate runtime.

Integer binding/feedback exact, including every INT16 code for h/c; malformed tensor bindings rejected. Floating comparisons fixed before testing: FFT reference relative 2e-5 plus absolute 2e-5; reconstructed taps absolute 2e-5 plus relative 2e-5 against independent float64 IDFT and package C; FIR float32 absolute 2e-5 plus relative 2e-6; frontend code error <=1 away from discontinuous half-code rounding, report maximum and inspect exceptions. Integer replay tolerance zero. Synthetic PCM verifies frontend since packaged traces contain controller features only.

Verify C/C++ host execution for reset, context, FFT, reconstruction endpoints/roll/taper, crossfade/history, PCM rounding/saturation, faults/ownership/schedule. Full unittest suite and diff check. Sequential Debug/Release configure/build with map, hashes and memory report when compiler works; allocation/actual arena use requires target execution. Compiled replay runner/comparator must carry feedback and cover both traces (26441 hops); external backend identity mandatory. Missing compiled backend is BLOCKED, never mock PASS. Raw Linux TF2.20 receipt is historical evidence, rerun only in matching environment.

Nine independent acceptance rows: integrity, architecture/source, host numeric, Debug/Release link, raw replay, compiled replay, board init/memory, full-hop deadline/schedule, physical audio/latency. No deployment approval without remaining board/parity evidence. No acoustic latency claim from 64-sample alignment or b+2 schedule.
