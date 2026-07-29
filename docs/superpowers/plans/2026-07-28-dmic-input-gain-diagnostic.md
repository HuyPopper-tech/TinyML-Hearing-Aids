# DMIC Input-Gain Diagnostic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a temporary +12 dB DMIC PCM gain experiment before ERB-GRU inference, with evidence-rich foreground telemetry that distinguishes low capture level from model attenuation.

**Architecture:** `app/main.c` retains the raw DMIC block, writes a saturated 4x copy into a dedicated inference block, and sends only that copy to the enhancer. Per-window peak, integer RMS, and clipping counters are accumulated with the existing timing counters and printed from the existing 100-frame foreground log.

**Tech Stack:** C11-compatible bare-metal firmware, Nuvoton CMSIS/StdDriver, Python `unittest` source contracts.

## Global Constraints

- Fixed test gain is +12 dB, implemented as exact 4x signed PCM scaling.
- Preserve 16 kHz/160-sample transport, SysTick timing, Ethos-U model artifacts, and -3 dB DAC gain.
- No logging, RMS calculation, gain processing, or allocation in ISR context.
- Clamp to int16 and count every clipped input sample.

---

### Task 1: Contract-test the diagnostic data path

**Files:**
- Modify: `tests/test_erb_gru_firmware_contract.py`
- Modify: `app/main.c`

**Interfaces:**
- Consumes: `g_dmic_block` containing raw 160-sample DMIC PCM.
- Produces: `g_inference_block` passed to `erb_gru_audio_enhancer_process`.

- [ ] **Step 1: Write the failing test**

```python
def test_dmic_input_gain_diagnostic_is_saturating_and_precedes_inference(self):
    app = (ROOT / "app" / "main.c").read_text(encoding="utf-8")
    self.assertIn("#define APP_INPUT_TEST_GAIN_DB 12", app)
    self.assertIn("audio_apply_test_gain", app)
    self.assertIn("g_inference_block", app)
    self.assertIn("gain_clip", app)
    self.assertLess(app.index("audio_apply_test_gain("),
                    app.index("erb_gru_audio_enhancer_process("))
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.test_erb_gru_firmware_contract.ErbGruFirmwareContractTests.test_dmic_input_gain_diagnostic_is_saturating_and_precedes_inference -v`

Expected: FAIL because no diagnostic gain path exists.

- [ ] **Step 3: Write minimal implementation**

```c
#define APP_INPUT_TEST_GAIN_DB 12
#define APP_INPUT_TEST_GAIN_MULTIPLIER 4

static uint32_t audio_apply_test_gain(const int16_t *source, int16_t *destination,
                                      uint16_t sample_count)
{
    uint32_t clipped = 0U;
    for (uint16_t index = 0U; index < sample_count; ++index) {
        const int32_t scaled = (int32_t)source[index] * APP_INPUT_TEST_GAIN_MULTIPLIER;
        if (scaled > INT16_MAX) { destination[index] = INT16_MAX; ++clipped; }
        else if (scaled < INT16_MIN) { destination[index] = INT16_MIN; ++clipped; }
        else { destination[index] = (int16_t)scaled; }
    }
    return clipped;
}
```

Call it once per foreground DMIC block before `erb_gru_audio_enhancer_process`.

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.test_erb_gru_firmware_contract.ErbGruFirmwareContractTests.test_dmic_input_gain_diagnostic_is_saturating_and_precedes_inference -v`

Expected: PASS.

### Task 2: Add peak/RMS/clip observability without changing timing fields

**Files:**
- Modify: `app/main.c`
- Modify: `tests/test_erb_gru_firmware_contract.py`

**Interfaces:**
- Consumes: raw and 4x PCM blocks from Task 1.
- Produces: `[PERF]` fields `in_pk`, `in_rms`, `gain_pk`, `gain_rms`, `gain_clip`, and `out_pk`.

- [ ] **Step 1: Write the failing test**

```python
def test_perf_log_reports_raw_and_gain_adjusted_capture_levels(self):
    app = (ROOT / "app" / "main.c").read_text(encoding="utf-8")
    for field in ("in_rms=", "gain_pk=", "gain_rms=", "gain_clip="):
        self.assertIn(field, app)
    self.assertIn("audio_pcm_rms", app)
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python -m unittest tests.test_erb_gru_firmware_contract.ErbGruFirmwareContractTests.test_perf_log_reports_raw_and_gain_adjusted_capture_levels -v`

Expected: FAIL because these fields do not exist.

- [ ] **Step 3: Write minimal implementation**

Implement an integer square-root RMS helper using a 64-bit sum of squares;
accumulate per-block RMS values and clipping count over 100 frames. Preserve
the existing timing field names and rates. Print the fixed gain at boot.

- [ ] **Step 4: Run test to verify it passes**

Run: `python -m unittest tests.test_erb_gru_firmware_contract.ErbGruFirmwareContractTests.test_perf_log_reports_raw_and_gain_adjusted_capture_levels -v`

Expected: PASS.

### Task 3: Regression verification

**Files:**
- Modify: `app/main.c`
- Modify: `tests/test_erb_gru_firmware_contract.py`

- [ ] **Step 1: Run focused contracts**

Run: `python -m unittest tests.test_erb_gru_firmware_contract tests.test_tlv320dac3100_driver -v`

Expected: all tests PASS.

- [ ] **Step 2: Check patch formatting**

Run: `git diff --check`

Expected: exit code 0.

- [ ] **Step 3: Configure and build Debug firmware**

Run: `build.bat debug config` then `build.bat debug build`

Expected: Debug configure succeeds; report an Arm Compiler license failure as
an external blocker rather than claiming a binary was built.
