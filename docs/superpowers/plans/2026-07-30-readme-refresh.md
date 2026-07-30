# README Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the obsolete DMIC-test README with an accurate quick-start and project overview for the current ERB-GRU Ethos-U55 firmware.

**Architecture:** Keep README as the concise entry point and link to the deployment guide and handoff for detailed evidence. Add one repository contract test for essential facts and relative-link validity so the document cannot silently regress.

**Tech Stack:** Markdown, Python `unittest`, CMake/Arm Compiler build documentation.

## Global Constraints

- Modify only `README.md` and the README contract test after this plan.
- Do not duplicate detailed telemetry schemas, investigation logs, or the replacement-model checklist.
- Do not claim production readiness while board parity, runtime memory measurements, and soak remain pending.
- Do not modify firmware, CMake, `libs/`, model packages, or generated assets.

---

### Task 1: Add the README contract test

**Files:**
- Create: `tests/test_readme_contract.py`

**Interfaces:**
- Consumes: repository files referenced by Markdown links.
- Produces: a `unittest` contract for current firmware identity, canonical commands, safety status, and valid relative links.

- [ ] **Step 1: Add a failing test**

Create a standard-library-only test that requires:

```python
for expected in (
    "ERB-GRU",
    "Ethos-U55",
    "app -> services -> drivers -> board -> libs",
    "build.bat release build",
    "build.bat debug build",
    "M55M1_GestureAI.axf.bin",
    "M55M1_ERB_GRU_DIAGNOSTICS",
    "not production-ready",
):
    self.assertIn(expected, readme)
```

Extract repository-relative Markdown link targets, ignore URL/anchor links, and
assert that each target exists below the repository root.

- [ ] **Step 2: Run the focused test and verify RED**

Run:

```powershell
python -m unittest tests.test_readme_contract -v
```

Expected: failure because the old README lacks the current ERB-GRU contract.

- [ ] **Step 3: Commit the failing contract**

```powershell
git add tests/test_readme_contract.py
git commit -m "test: define current firmware readme contract"
```

### Task 2: Rewrite and validate README

**Files:**
- Modify: `README.md`

**Interfaces:**
- Consumes: `CMakeLists.txt`, `CMakePresets.json`, `build.bat`, `build.sh`,
  `docs/erb-gru-model-deployment.md`, and
  `docs/session-erb-gru-ethosu-handoff.md`.
- Produces: the repository entry point for building, flashing, understanding,
  and validating the current firmware.

- [ ] **Step 1: Replace obsolete content**

Write the approved quick-start-first sections:

1. firmware overview and explicit validation status;
2. prerequisites and canonical Debug/Release commands;
3. build outputs and main CMake options;
4. architecture and realtime data path;
5. deployed package and CPU gain expansion;
6. diagnostic UART and production defaults;
7. flashing, SWD/TrustZone recovery, and pin ownership;
8. host validation commands, project layout, and specialist-doc links.

- [ ] **Step 2: Run focused validation**

```powershell
python -m unittest tests.test_readme_contract -v
git diff --check
```

Expected: all focused tests pass and no whitespace errors.

- [ ] **Step 3: Run the full host suite**

Run:

```powershell
python -m unittest discover -s tests -v
```

If the ignored `.codex/AGENTS.md` fixture is absent, temporarily copy the root
`AGENTS.md` for this command and remove it immediately afterward.

- [ ] **Step 4: Review scope and commit**

Verify:

```powershell
git diff --name-only
git diff --check
```

Only `README.md` must remain for the implementation commit.

```powershell
git add README.md
git commit -m "docs: refresh erb-gru firmware readme"
```

- [ ] **Step 5: Synchronize and push**

Fetch `origin/main`, require zero commits behind, push without force, then
verify local `HEAD` equals `origin/main`.
