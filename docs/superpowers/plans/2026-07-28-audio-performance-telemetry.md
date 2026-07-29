# Audio Performance Telemetry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add foreground-only ERB-GRU latency/performance telemetry and raise output gain to -3 dB.

**Architecture:** Board code owns DWT and DMIC timestamps; the DAC driver carries a block tag to its completion ISR; the TFLM adapter owns Ethos-U timing. The app aggregates and prints all observations without doing UART work in interrupts.

**Tech Stack:** Cortex-M55 DWT, Nuvoton PDMA, TLV320DAC3100, TensorFlow Lite Micro/Ethos-U, Python unittest.

## Global Constraints

- ISR work only records fixed-size timestamp/state data.
- UART logs run in the app foreground loop once per 100 frames.
- Default codec gain is -3 dB, below the hardware maximum of 0 dB.

---

### Task 1: Board and codec transport timing

**Files:**
- Modify: `board/m55m1_tlv320dac3100_test.[ch]`
- Modify: `drivers/tlv320dac3100/tlv320dac3100.[ch]`
- Test: `tests/test_tlv320dac3100_driver.py`

- [ ] Add DWT cycle-counter initialization/read/conversion APIs and DMIC completion timestamp capture.
- [ ] Add a tagged DAC-submit API plus one completed-tag record populated by PDMA ISR.
- [ ] Add source-contract tests that prove DWT/sequence timestamps are ISR-only and the DAC record is pollable.

### Task 2: Inference and foreground telemetry

**Files:**
- Modify: `drivers/ethosu_tflm/ethosu_tflm.[ch]`
- Modify: `app/main.c`
- Test: `tests/test_erb_gru_firmware_contract.py`

- [ ] Add last/max Ethos-U invoke cycle statistics to the adapter.
- [ ] Aggregate enhance, DSP-derived, and end-to-end intervals in `main.c`; print one `[PERF]` line per 100 frames.
- [ ] Add source-contract tests proving no printf appears in ISRs and telemetry reports required metrics.

### Task 3: Bring-up gain and verification

**Files:**
- Modify: `board/m55m1_tlv320dac3100_test.h`
- Test: `tests/test_tlv320dac3100_driver.py`

- [ ] Change the board bring-up gain ceiling from -12 dB to -3 dB and document its calibration limitation.
- [ ] Run targeted tests, project tests, CMake configure, build, and `git diff --check`.
