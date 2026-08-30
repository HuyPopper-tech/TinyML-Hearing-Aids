# Vela and Memory Optimisation for Speech Boosting Chunk64

## 1. Objective and scope

This document defines deployment optimisation for Speech Boosting Chunk64 on
M55M1/Ethos-U55. It does not change model topology, model quality, sample rate,
streaming ABI, or training.

The priorities are:

1. Keep all 197 operators on Ethos-U55 with no central processing unit fallback.
2. Reduce `Ethos-U55 neural network invocation` time and jitter.
3. Reduce central processing unit time around invocation, especially the copy and
   requantisation of 20 state tensors.
4. Preserve 16 kHz real-time operation, 64-sample chunks, and 16-sample
   look-ahead.
5. Use SRAM0/1 deliberately so direct memory access and central-processing-unit-only
   data do not contend unnecessarily with the model or Ethos-U55 arena.

This scope excludes architecture changes, retraining, sample-rate changes,
chunk-size changes, state-ABI changes, and custom model paging.

## 2. Fixed deployment contract

| Property | Value |
|---|---:|
| Sample rate | 16,000 Hz |
| Current chunk | 64 PCM16 samples = 4 ms |
| Look-ahead | 16 samples = 1 ms of signal context |
| Input/output tensors | 21 / 21 |
| Persistent state tensors | 20 |
| Persistent stream state | 5,632 bytes |
| Stream data type | INT8 |
| Ethos-U55 operators | 197 |
| Central processing unit operators | 0 |
| Raw INT8 regression | 100-chunk golden trace |

An output chunk is released only after the first 16 samples of the following
chunk are available. Because the digital microphone delivers 64-sample blocks,
`capture_to_model_output_ready` includes almost one 4 ms block of look-ahead
waiting time in addition to processing time. It is not Ethos-U55 invocation
latency by itself.

## 3. Verified measurements

### 3.1 `aprom_x100`

The model remains in APROM while the tensor arena is in SRAM. Board measurements
showed invocation near 6.13 ms, with a maximum of 6.153 ms. The firmware handled
about 166 invocations per second and continuously produced capture gaps and
digital-microphone overruns. This profile exceeds the 4 ms chunk budget.

### 3.2 `sram01_x100`

The 959,920-byte Vela FlatBuffer is stored in SRAM0/1, followed by a 64 KiB
tensor arena. Sixty one-second board windows measured:

| Metric | Result |
|---|---:|
| Ethos-U55 invocation minimum / mean / maximum | 3.150 / 3.151-3.152 / 3.164 ms |
| Look-ahead assembly + PCM quantisation | 73-80 microseconds |
| Input state-tensor copy | 43-48 microseconds |
| Output state-tensor copy + requantisation | 343-389 microseconds |
| Output dequantisation | 2-9 microseconds |
| Capture gaps / digital-microphone overruns / invocation errors | 0 / 0 / 0 |
| Invocations | 14,999; one pre-roll chunk makes this one lower than 15,000 captures |

These results identify APROM constant/model access as the main `aprom_x100`
bottleneck. State copying is not the largest bottleneck, but output-state copy
is the largest central processing unit stage after invocation, at about 0.35 ms.

## 4. M55M1 physical memory regions

| Region | Capacity | Firmware use |
|---|---:|---|
| SRAM0 + SRAM1 | 1 MiB | Ethos-U55-visible Vela model and TensorFlow Lite Micro tensor arena |
| SRAM2 | 320 KiB | Non-cacheable direct-memory-access descriptors/buffers and large central-processing-unit-only buffers when Ethos-U55 visibility is not required |
| DTCM | 128 KiB | Stack and central-processing-unit-only foreground data with deterministic latency |
| ITCM | 64 KiB | Explicitly selected hot code; not model storage |
| APROM | 2 MiB | Firmware code, constants, and model load image |

The current Ethos-U55 deployment uses SRAM0/1 for model and arena. Do not move
the FlatBuffer, TensorFlow Lite Micro arena, or tensors accessed directly by
Ethos-U55 to DTCM or SRAM2. The Vela configuration, linker, memory protection
unit/cache configuration, and driver setup must agree: Vela describes and
schedules logical memory; it does not configure firmware hardware on its own.
[Arm CMSIS-Ethos-U documentation](https://github.com/ARM-software/CMSIS-Ethos-U/blob/main/documentation/doxygen/vela/src/main.md)

### 4.1 Current `sram01_x100` layout

```text
0x20100000  g_speech_boosting_u55_vela_model      959,920 B
0x201EA5B0  g_tensor_arena                         65,536 B
             generic read-write data + heap in SRAM0/1
0x20200000  end of SRAM0/1

0x2000xxxx  DTCM: stack, audio working set 6,048 B,
             UART transmit ring 2,048 B, TensorFlow Lite Micro
             interpreter/resolver storage

0x20230000  SRAM2 non-cacheable: digital microphone/I2S direct-memory-access
             buffers and descriptors
```

`sram01_x100` retains an APROM load image so startup can copy the model into
SRAM. At runtime, TensorFlow Lite Micro and Ethos-U55 use the SRAM0/1 model
pointer. The SRAM model data cache is cleaned once before `AllocateTensors()`
and the first invocation.

### 4.2 Correct state-copy placement

`speech_boosting_stream_state_t` is a 5,632-byte central-processing-unit-owned
state object. Each invocation:

1. Copies state into the TensorFlow Lite Micro input tensor in the SRAM0/1 arena.
2. Runs Ethos-U55 only on the Ethos-U55-visible model and arena.
3. Reads output state from the arena, requantises it using the semantic mapping,
   and updates persistent state.

Ethos-U55 does not receive a pointer to `speech_boosting_stream_state_t`.
Therefore the state object, capture/rolling/playback buffers, and UART ring may
be placed in DTCM. This reduces SRAM0/1 pressure, but does not eliminate copies
into or out of the TensorFlow Lite Micro arena and should not materially reduce
Ethos-U55 invocation duration.

## 5. Vela 5.1: factors that actually affect latency

Vela compiles TFLite ahead of time for Ethos-U. The following `[Memory_Mode]`
fields determine logical memory roles:

| Field | Contents |
|---|---|
| `const_mem_area` | Command stream, encoded weights, scales, constants, and read-only model data |
| `arena_mem_area` | Feature maps, inputs/outputs, intermediate tensors, and run-time read-write scratch |
| `cache_mem_area` | Staging or fast scratch when it is physically distinct from the arena |

`arena_cache_size` is a scheduling budget in **bytes** with
`--optimise Performance`. It is not a direct synonym for
`SPEECH_BOOSTING_TENSOR_ARENA_BYTES`: firmware must still allocate a sufficient
TensorFlow Lite Micro arena and the linker must place all memory regions
correctly. `--optimise Size` ignores this cache budget to reduce peak SRAM, but
may increase traffic and latency.
[Vela options and memory mapping](https://arm-software.github.io/CMSIS-Ethos-U/main/vela/index.html)

`Axi0`, `Axi1`, `Sram`, and `OffChipFlash` are logical Vela names. The project
uses them as follows:

```ini
[System_Config.Ethos_U55_M55M1_FLASH_X0.300]
axi0_port=Sram
axi1_port=OffChipFlash

[Memory_Mode.Shared_1024K]
const_mem_area=Axi1
arena_mem_area=Axi0
cache_mem_area=Axi0

[Memory_Mode.M55M1_sram01_x100]
const_mem_area=Axi0
arena_mem_area=Axi0
cache_mem_area=Axi0
```

`OffChipFlash` is Vela's bandwidth label; it neither moves physical APROM to an
external flash device nor increases the M55M1 APROM clock. The M55M1 core and
neural-processing unit are configured for 220 MHz.

## 6. Tiered optimisation strategy

### Tier A - preserve model and ABI; apply first

1. **Keep `sram01_x100` as the production baseline.** It is the only verified
   profile with maximum invocation below 3.5 ms and no capture gap over 60
   seconds.
2. **Keep `--optimise Performance` and `tensor-allocator HillClimb`.** Do not
   switch to `Size` while minimum latency is the objective.
3. **Keep model, arena, and direct Ethos-U55 tensors in SRAM0/1.** The standard
   TensorFlow Lite Micro/Ethos-U runtime consumes a contiguous FlatBuffer;
   custom splitting or paging needs a new runtime/driver and can add
   direct-memory-access jitter.
4. **Move central-processing-unit-only read-write data to DTCM.** Keep
   direct-memory-access targets in non-cacheable SRAM2.
5. **Maintain cache discipline.** Model data is writable only during the startup
   copy; clean its data cache once before Ethos-U55 access. Avoid cache
   maintenance in the real-time path.
6. **Do not format UART text in an interrupt service routine.** UART logging is
   foreground-only; interrupt handlers move descriptors/events only.

### Tier B - optimise central processing unit state transactions

The output-state stage contains 20 semantic mappings and per-byte
requantisation. Evaluate changes in this order:

1. Measure audio-output `memcpy`, output-state requantisation, and input-state
   `memcpy` separately to identify the component responsible for the 0.35 ms
   stage.
2. Preserve manifest-defined semantic mapping; never copy by tensor ordinal.
3. If floating-point `round` is the bottleneck, implement equivalent fixed-point
   requantisation per scale/zero-point pair. Every byte of the 100-chunk golden
   trace, final state after each chunk, and PCM16 output must remain
   byte-identical to baseline before use.
4. Replace the floating-point loop only after parity passes. Do not merge state
   output/input copies, skip zero-point reset, or change the state-quantisation
   contract.

This tier can reduce central processing unit time and jitter, but affects the
approximately 3.15 ms Ethos-U55 invocation only indirectly.

### Tier C - controlled Vela compiler experiments

Every experiment must create an independent package/profile and must never
overwrite the baseline.

| Variable | Values to evaluate | Purpose |
|---|---|---|
| `arena_cache_size` | 64, 128, 256, 512 KiB, 1 MiB | Trade-off between on-chip reuse and memory traffic |
| `tensor-allocator` | HillClimb, Greedy, LinearAlloc | Intermediate-tensor layout and peak arena use |
| `max-block-dependency` | 0, 1, 2, 3 | Trade-off between interrupt/dependency latency and throughput |
| `--optimise` | Performance first; Size only as a memory benchmark | Performance prioritises latency; Size prioritises SRAM |
| Constant placement | APROM versus SRAM0/1 | SRAM0/1 is the verified dominant choice |

Change only one variable per profile. The profile name must identify that
variable, for example `sram01_perf_cache256k_hillclimb`.

Do not use `--debug-force-legacy-core`: it introduces central processing unit
operators for the current raw model and violates the Ethos-U55-only contract.

## 7. Profile-generation and verification procedure

### 7.1 Immutable baseline

Input model:

```text
models/speech_boosting/generated/speech_boosting_qat_raw_int8.tflite
```

`aprom_x030` must reproduce this SHA-256:

```text
c0c2a641f2b317925003632b5eabcf1f4b1f4f9018f3ed5386e76f105be1f6c7
```

Current generator:

```powershell
python tools/generate_speech_boosting_vela_profiles.py sram01_x100
```

Each generated profile package must contain the Vela TFLite file, C/C++ model
array, manifest, Vela command and report, contracts, tensor map, quantisation
metadata, and golden trace.

### 7.2 Command for one new profile experiment

```powershell
python -m ethosu.vela `
  models/speech_boosting/generated/speech_boosting_qat_raw_int8.tflite `
  --config models/speech_boosting/integration/vela_m55m1_profiles.ini `
  --system-config M55M1_sram01_x100 `
  --memory-mode M55M1_sram01_x100 `
  --accelerator-config ethos-u55-256 `
  --optimise Performance `
  --tensor-allocator HillClimb `
  --arena-cache-size 262144 `
  --verbose-config --verbose-cycle-estimate --show-cpu-operations `
  --output-dir build/vela_experiments/sram01_perf_cache256k
```

Do not select generated output manually. Add the profile to the generator,
produce a complete package, and select it through
`M55M1_SPEECH_BOOSTING_MEMORY_PROFILE` in CMake.

### 7.3 Mandatory gates

Host checks:

1. SHA, manifest, ordinal, shape, and INT8 quantisation contracts pass.
2. All 20 mappings, zero-state byte values, and the 100 raw-INT8 golden chunks
   pass.
3. The Vela report contains 197 neural-processing-unit operators and zero
   central-processing-unit operators.
4. `python -m unittest discover -s tests -v` and `git diff --check` pass.

Firmware/map checks:

1. `U55_MODEL_RAM` and `U55_TENSOR_ARENA` lie entirely within
   `0x20100000-0x201FFFFF`.
2. Model, arena, read-write data, and heap do not exceed the SRAM0/1 linker
   assertion.
3. DTCM contains only central-processing-unit-only data; direct-memory-access
   memory remains non-cacheable SRAM2.
4. The SRAM model data cache is cleaned before first allocation/invocation.

Board checks after 50 warm-up chunks and for at least 60 seconds:

1. Maximum Ethos-U55 invocation is at most 3.5 ms in **every** one-second
   window.
2. There are zero central-processing-unit operators and zero invocation errors.
3. There are zero capture gaps, digital-microphone overruns, and
   direct-memory-access errors.
4. The log reports memory ranges, minimum/mean/maximum per stage, and
   end-to-end latency when digital-to-analogue/I2S output is enabled.
5. Invocation/interrupt counts advance consistently; neural-processing-unit and
   multiply-accumulate counters contain valid data when utilisation is assessed.

## 8. Interpreting results and selecting a profile

Use board measurements rather than Vela cycle estimates alone.

- If a profile reduces model SRAM but its maximum invocation exceeds 3.5 ms, do
  not select it.
- If `--optimise Size` reduces the arena but raises invocation time or produces
  capture gaps, retain the report but do not make it the production profile.
- If a smaller cache budget keeps maximum invocation at or below 3.5 ms while
  increasing map headroom, choose the smallest profile that passes every gate.
- If fixed-point state requantisation preserves golden parity and reduces the
  output-state stage, select it independently of the Vela profile because it is
  a central-processing-unit transaction optimisation.
- If no profile improves `sram01_x100`, retain `sram01_x100`; the next work is
  model redesign or retraining, outside this document.

## 9. Checklist before changing the production profile

- [ ] Independent profile package with complete command, reports, and manifest.
- [ ] Recorded Vela SHA and raw-model provenance.
- [ ] 197 Ethos-U55 / 0 central processing unit operators.
- [ ] Passing contract for 21 inputs/21 outputs and 20 state mappings.
- [ ] Passing 100-chunk raw-INT8 golden trace.
- [ ] Passing map, linker, and cache validation.
- [ ] Passing board warm-up and at least 60 seconds of measurements.
- [ ] UART boot messages are not truncated; split memory fields across lines when necessary.
- [ ] Re-measured digital-to-analogue/I2S end-to-end latency when hardware is available.