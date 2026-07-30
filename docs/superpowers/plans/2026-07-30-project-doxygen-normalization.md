# Project Doxygen Normalization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Convert every project-owned public API Doxygen comment to the approved multiline, ownership-aware format without changing firmware behavior.

**Architecture:** Treat documentation as a public-header contract enforced by a host style test. Normalize one dependency layer at a time, preserving declarations byte-for-byte and excluding vendor/model-generated trees.

**Tech Stack:** C/C++ public headers, Python `unittest`, CMake/Ninja, Arm Compiler 6.24, Git.

## Global Constraints

- Include public headers under `app/`, `board/`, `common/`, `drivers/`, and `services/`.
- Exclude `libs/`, `models/`, build directories, and implementation-only comments.
- Preserve the six user-edited headers and use them as the style reference.
- Put `@brief`, every `@param`, and `@return` on separate lines.
- Document ownership/lifetime and foreground/ISR/reentrancy constraints where relevant.
- Use `@return None.` for void public functions.
- Do not change public declarations or runtime behavior.

---

### Task 1: Add the public-header Doxygen regression

**Files:**
- Modify: `tests/test_erb_gru_style_contract.py`

**Interfaces:**
- Consumes: project-owned public headers discovered under `app`, `board`, `common`, `drivers`, and `services`
- Produces: style-contract tests that reject single-line Doxygen and undocumented public function parameters/returns

- [ ] **Step 1: Add a failing multiline-format test**

Add a test that scans every in-scope `.h` file:

```python
PUBLIC_HEADER_ROOTS = ("app", "board", "common", "drivers", "services")

def test_public_doxygen_blocks_are_multiline(self):
    violations = []
    for root_name in PUBLIC_HEADER_ROOTS:
        for path in (ROOT / root_name).rglob("*.h"):
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), 1
            ):
                if re.search(r"/\*\*.*\*/", line):
                    violations.append(f"{path.relative_to(ROOT)}:{line_number}")
    self.assertEqual(violations, [])
```

- [ ] **Step 2: Add a failing declaration-consistency test**

Parse each Doxygen block immediately preceding a public function declaration.
For each named C parameter, require `@param <name>`. Require `@return` for every
function, including void functions:

```python
self.assertEqual(
    documented_params,
    declaration_params,
    f"{relative}:{line_number}",
)
self.assertIn("@return", comment, f"{relative}:{line_number}")
```

Skip preprocessor macros, function-pointer typedefs, and `extern "C"` syntax;
those are not function declarations.

- [ ] **Step 3: Run the focused test and verify RED**

Run:

```powershell
rtk python -m unittest tests.test_erb_gru_style_contract -v
```

Expected: failure listing current single-line Doxygen blocks, beginning with
`services/audio_telemetry.h` and `drivers/tlv320dac3100/tlv320dac3100.h`.

- [ ] **Step 4: Commit the test only after observing the expected failure**

```powershell
git add tests/test_erb_gru_style_contract.py
git commit -m "test: enforce multiline public doxygen"
```

### Task 2: Normalize board and common public headers

**Files:**
- Modify: `board/m55m1_audio.h`
- Modify: `board/m55m1_debug_uart.h`
- Modify: `board/m55m1_ethosu_u55.h`
- Modify: `board/m55m1_timebase.h`
- Modify: `common/fixed_byte_ring.h`
- Modify: `common/sha256.h`
- Modify: `common/wrapping_counter.h`

**Interfaces:**
- Consumes: existing declarations and user-edited multiline style
- Produces: detailed board/common API documentation with unchanged declarations

- [ ] **Step 1: Preserve and finish the user-edited board headers**

Keep the current edits in `m55m1_audio.h`, `m55m1_ethosu_u55.h`, and
`m55m1_timebase.h`. Fix whitespace and ensure closing `*/` is on its own line.
Expand `m55m1_debug_uart.h` comments and state foreground-before-use and IRQ
configuration context.

- [ ] **Step 2: Normalize common utility APIs**

Expand SHA-256 and ring/timebase comments. Document buffer ownership, exact
capacity/all-or-drop behavior, SPSC execution roles, wrap behavior, NULL
handling, and `@return None.` for void APIs.

- [ ] **Step 3: Run the focused style test**

```powershell
rtk python -m unittest tests.test_erb_gru_style_contract -v
```

Expected: board/common violations absent; remaining failures are only driver
and service headers.

### Task 3: Normalize driver public headers

**Files:**
- Modify: `drivers/audio_transport/audio_transport.h`
- Modify: `drivers/debug_uart_tx/debug_uart_tx.h`
- Modify: `drivers/ethosu_u55/ethosu_u55.h`
- Modify: `drivers/tlv320dac3100/m55m1_tlv320dac3100_test_compat.h`
- Modify: `drivers/tlv320dac3100/tlv320dac3100.h`

**Interfaces:**
- Consumes: existing driver API declarations
- Produces: multiline Doxygen covering transport ownership, DMA/ISR context,
  codec lifecycle, and timing semantics

- [ ] **Step 1: Preserve and finish the three user-edited driver headers**

Retain current wording where accurate. Wrap long lines, remove trailing spaces,
and put every tag and closing delimiter on dedicated lines.

- [ ] **Step 2: Normalize codec constants and public types**

Convert single-line type/constant/member Doxygen in `tlv320dac3100.h` to
multiline blocks. Keep comments concise for constants; detail lifecycle,
buffer-copy semantics, DMA ownership, valid ranges, and foreground/ISR behavior
for functions and state records.

- [ ] **Step 3: Normalize compatibility APIs**

Document that compatibility entry points are diagnostic/test-only, their
foreground context, and caller ownership without introducing new abstractions.

- [ ] **Step 4: Run the focused style test**

```powershell
rtk python -m unittest tests.test_erb_gru_style_contract -v
```

Expected: only service-header violations remain.

### Task 4: Normalize service public headers

**Files:**
- Modify: `services/audio_pipeline/audio_pipeline.h`
- Modify: `services/audio_telemetry.h`
- Modify: `services/erb_gru/erb_gru_audio_diagnostics.h`
- Modify: `services/erb_gru/erb_gru_audio_enhancer.h`
- Modify: `services/erb_gru/erb_gru_frontend_assets.h`
- Modify: `services/erb_gru/erb_gru_golden_runner.h`
- Modify: `services/erb_gru/erb_gru_inference.h`
- Modify: `services/erb_gru/erb_gru_stream_timing.h`

**Interfaces:**
- Consumes: established service lifecycle and realtime contracts
- Produces: detailed public documentation for foreground audio, diagnostics,
  inference state, golden-only APIs, and WOLA timing

- [ ] **Step 1: Normalize audio pipeline and telemetry**

Document initialization/start/poll/terminal-stop ordering, non-blocking
foreground execution, snapshot ownership, rate limiting, and fatal-report
constraints.

- [ ] **Step 2: Normalize ERB-GRU enhancer and diagnostics**

Document fixed 160-sample buffers, deterministic outputs, preroll/fallback
results, copy-reset semantics, static storage, non-reentrancy, and absence of
post-start allocation.

- [ ] **Step 3: Normalize inference, golden, assets, and timing**

Document raw INT8 state ownership/commit rules, output validity, static TFLM
lifetime, test-only terminal flush/corpus behavior, frontend asset ownership,
and prior-hop WOLA tag attribution.

- [ ] **Step 4: Run the focused test and verify GREEN**

```powershell
rtk python -m unittest tests.test_erb_gru_style_contract -v
```

Expected: all style tests pass with no in-scope single-line Doxygen.

### Task 5: Review, validate, commit, and push

**Files:**
- Verify all files changed by Tasks 1-4
- Do not modify firmware implementation files or model assets

**Interfaces:**
- Consumes: normalized documentation and regression tests
- Produces: verified commits on `main` and synchronized `origin/main`

- [ ] **Step 1: Confirm declarations did not change**

Review `git diff --word-diff=porcelain` and ensure changes are limited to
comments, whitespace inside comments, the style test, spec, and plan.

- [ ] **Step 2: Run full host validation**

```powershell
rtk python -m unittest discover -s tests -v
rtk git diff --check
```

Expected: all tests pass and whitespace check exits zero.

- [ ] **Step 3: Build both canonical profiles**

```powershell
rtk cmd /c build.bat debug build
rtk cmd /c build.bat release build
```

Run outside the sandbox so Arm Compiler 6.24 can use the licensed Windows
account. Expected: both builds exit zero and regenerate AXF/BIN/map files.

- [ ] **Step 4: Commit documentation normalization**

```powershell
git add app board common drivers services tests
git commit -m "docs: normalize project public doxygen"
```

- [ ] **Step 5: Verify and push**

```powershell
git status --short
git fetch origin main
git rev-list --left-right --count origin/main...main
git push origin main
git rev-list --left-right --count origin/main...main
```

Expected: clean status, no remote-only commits before push, and `0 0` after
push.
