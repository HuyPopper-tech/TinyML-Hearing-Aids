# M55M1 220 MHz Core and Audio Clock Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Run the M55 core at exactly 220 MHz, keep the codec audio clock exact, and eliminate false ERB-GRU deadline failures at 24-bit SysTick wrap.

**Architecture:** Board code configures APLL0 as the 220 MHz SCLK and APLL1 as the 475.136006 MHz audio source. A dependency-free wrapping-counter helper provides testable modular elapsed arithmetic; board APIs own the SysTick mask and all realtime consumers use the board elapsed API.

**Tech Stack:** C11, Arm Compiler 6.24, Nuvoton M55M1 StdDriver, Python `unittest`, Ninja/CMake.

## Global Constraints

- Work only in `D:\TinyML-Hearing-Aids\.worktrees\erb-gru-ethosu`; do not modify `main`.
- Do not edit `libs/`.
- APLL0 is exactly 220 MHz and SCLK divider is 1.
- APLL1 is 475.136006 MHz and I2S0 uses APLL1 divided by 2.
- Keep the 16 kHz divider 58 and 32 kHz divider 29.
- Keep all existing pin mux, DMA channels and IRQ priorities.
- No logging, allocation or blocking work after audio starts.
- Preserve SWD recovery through PF0, PF1, nRESET, GND and VREF/3V3 before audio pin mux takes ownership.

---

### Task 1: Add testable wrap-safe cycle arithmetic

**Files:**
- Create: `common/wrapping_counter.h`
- Create: `tests/test_wrapping_counter.py`
- Modify: `board/m55m1_tlv320dac3100_test.h`
- Modify: `board/m55m1_tlv320dac3100_test.c`
- Modify: `drivers/ethosu_u55/ethosu_u55.c`
- Modify: `app/main.c`

**Interfaces:**
- Produces: `uint32_t wrapping_counter_elapsed(uint32_t start, uint32_t end, uint32_t mask)`
- Produces: `uint32_t m55m1_tlv320dac3100_test_cycles_elapsed(uint32_t start_cycle, uint32_t end_cycle)`
- Consumes: board-owned SysTick mask `0x00ffffff`.

- [ ] **Step 1: Write the failing host test**

Create a small C harness from `tests/test_wrapping_counter.py` which includes
`common/wrapping_counter.h` and asserts:

```c
assert(wrapping_counter_elapsed(100U, 498U, 0x00ffffffU) == 398U);
assert(wrapping_counter_elapsed(0x00ffff00U, 0x0000008eU, 0x00ffffffU) == 398U);
assert(wrapping_counter_elapsed(0x00ffffffU, 0U, 0x00ffffffU) == 1U);
```

Also add source-contract assertions that inference deadline and application
timing call `m55m1_tlv320dac3100_test_cycles_elapsed` rather than subtracting
timestamps directly.

- [ ] **Step 2: Run the test and verify RED**

Run:

```powershell
rtk python -m unittest tests.test_wrapping_counter -v
```

Expected: failure because `common/wrapping_counter.h` and the board elapsed API
do not exist.

- [ ] **Step 3: Implement minimal wrap-safe arithmetic**

Create `common/wrapping_counter.h` with project header conventions and:

```c
static inline uint32_t wrapping_counter_elapsed(
    uint32_t start, uint32_t end, uint32_t mask)
{
    return (end - start) & mask;
}
```

Make the board API call this helper with its private SysTick mask. Replace:

```c
ethosu_u55_cycles_now() - start_cycle
```

and application timestamp subtraction with the board/driver elapsed API. Make
`ethosu_u55_deadline_exceeded()` compare the already masked elapsed count to:

```c
(SystemCoreClock / 1000U) * milliseconds
```

- [ ] **Step 4: Run focused tests and verify GREEN**

Run:

```powershell
rtk python -m unittest tests.test_wrapping_counter tests.test_erb_gru_firmware_contract -v
```

Expected: all tests pass.

- [ ] **Step 5: Commit**

```powershell
rtk git add common/wrapping_counter.h tests/test_wrapping_counter.py board/m55m1_tlv320dac3100_test.h board/m55m1_tlv320dac3100_test.c drivers/ethosu_u55/ethosu_u55.c app/main.c
rtk git commit -m "fix: make audio timing wrap safe"
```

---

### Task 2: Separate 220 MHz core and exact audio PLLs

**Files:**
- Modify: `board/m55m1_tlv320dac3100_test.h`
- Modify: `board/m55m1_tlv320dac3100_test.c`
- Modify: `app/main.c`
- Modify: `tests/test_tlv320dac3100_driver.py`

**Interfaces:**
- Produces: `m55m1_tlv320dac3100_clock_snapshot_t`
- Produces: `bool m55m1_tlv320dac3100_test_get_clock_snapshot(m55m1_tlv320dac3100_clock_snapshot_t *snapshot)`
- Consumes: `CLK_EnableAPLL`, `CLK_GetAPLL0ClockFreq`, `CLK_GetAPLL1ClockFreq`, `CLK_GetSCLKFreq`.

- [ ] **Step 1: Write failing clock-contract tests**

Extend `tests/test_tlv320dac3100_driver.py` to require:

```python
self.assertIn("M55M1_TLV320DAC3100_CORE_HZ 220000000UL", board)
self.assertIn("M55M1_TLV320DAC3100_AUDIO_APLL_HZ 475136006UL", board)
self.assertIn("CLK_APLL1_SELECT", board)
self.assertIn("CLK_STATUS_APLL1STB_Msk", board)
self.assertIn("CLK_SET_SCLKDIV(1UL)", board)
self.assertIn("CLK_I2SSEL_I2S0SEL_APLL1_DIV2", board)
self.assertNotIn("CLK_I2SSEL_I2S0SEL_APLL0_DIV2", board)
```

Require distinct init results/stages for APLL1 audio readiness, APLL0 core
readiness and clock-contract mismatch. Require boot output fields `core_hz`,
`apll0_hz`, `apll1_hz`, `i2s_src_hz` and `mclk_hz`.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
rtk python -m unittest tests.test_tlv320dac3100_driver -v
```

Expected: clock-contract assertions fail against the old APLL0-only setup.

- [ ] **Step 3: Implement the board clock transition**

In board code:

```c
#define M55M1_TLV320DAC3100_CORE_HZ       220000000UL
#define M55M1_TLV320DAC3100_AUDIO_APLL_HZ 475136006UL
```

While SCLK is HIRC:

```c
(void)CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT,
                     M55M1_TLV320DAC3100_AUDIO_APLL_HZ,
                     CLK_APLL1_SELECT);
(void)CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT,
                     M55M1_TLV320DAC3100_CORE_HZ,
                     CLK_APLL0_SELECT);
CLK_SET_SCLKDIV(1UL);
CLK_SetSCLK(CLK_SCLKSEL_SCLKSEL_APLL0);
```

Check both stable bits and validate measured frequencies. Bind I2S0 to:

```c
CLK_I2SSEL_I2S0SEL_APLL1_DIV2
```

Add the snapshot struct/API and calculate I2S source and MCLK from measured
APLL1 plus the selected profile divider. Extend boot stage names and print one
bounded clock-contract line before codec/audio start.

- [ ] **Step 4: Run clock and audio contract tests**

Run:

```powershell
rtk python -m unittest tests.test_tlv320dac3100_driver tests.test_erb_gru_firmware_contract -v
```

Expected: all tests pass and existing pin/DMA/profile assertions remain green.

- [ ] **Step 5: Commit**

```powershell
rtk git add board/m55m1_tlv320dac3100_test.h board/m55m1_tlv320dac3100_test.c app/main.c tests/test_tlv320dac3100_driver.py
rtk git commit -m "fix: separate 220mhz core and audio plls"
```

---

### Task 3: Validate generated assets, firmware builds and handoff

**Files:**
- Modify: `docs/session-erb-gru-ethosu-handoff.md`

**Interfaces:**
- Consumes: the clock snapshot and wrap-safe timing implemented above.
- Produces: reproducible validation evidence and current board handoff.

- [ ] **Step 1: Run package and host verification**

```powershell
rtk python tools/generate_erb_gru_assets.py --package models/erb_gru/packages/w4_erb-gru_deployment_v2_seed20260730 --output models/erb_gru/generated --check
rtk python -m unittest discover -s tests -v
rtk git diff --check
```

Record the exact pass/error counts. The known absent worktree-owned
`.codex/AGENTS.md` and `.vscode/launch.json` files remain external test errors;
do not create replacements.

- [ ] **Step 2: Build current-source Debug and Release**

```powershell
rtk cmd /c build.bat debug config
rtk cmd /c build.bat debug build
rtk cmd /c build.bat release config
rtk cmd /c build.bat release build
```

Expected: both binaries link without UART1, `atexit`, or clock errors. If Arm
license error 400 recurs, record it as an external blocker.

- [ ] **Step 3: Inspect Release map**

Confirm one `erb_gru_vela_model`, separate DTCM/cacheable/non-cacheable regions,
128 KiB bring-up arena and 8 KiB stack reservation. Do not infer runtime arena
or stack high-water from the map.

- [ ] **Step 4: Update handoff**

Record the new PLL ownership, 220 MHz core contract, wrap-safe deadline fix,
test results, build results and the required next board evidence:

```text
[BOOT] clocks core_hz=220000000 ...
fallback/deadline counters do not increment at the former wrap cadence
```

- [ ] **Step 5: Commit validation documentation**

```powershell
rtk git add docs/session-erb-gru-ethosu-handoff.md
rtk git commit -m "docs: record 220mhz clock validation"
```
