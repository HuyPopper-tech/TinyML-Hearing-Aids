# README Refresh Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the obsolete DMIC-test README with an accurate quick-start and project overview for the current ERB-GRU Ethos-U55 firmware.

**Architecture:** Keep README as the concise entry point and link to the deployment guide and handoff for detailed evidence. Validate human-facing prose directly instead of adding a brittle source-text contract test.

**Tech Stack:** Markdown, Python `unittest`, CMake/Arm Compiler build documentation.

## Global Constraints

- Modify only `README.md` after this plan.
- Do not duplicate detailed telemetry schemas, investigation logs, or the replacement-model checklist.
- Do not claim production readiness while board parity, runtime memory measurements, and soak remain pending.
- Do not modify firmware, CMake, `libs/`, model packages, or generated assets.

---

### Task 1: Rewrite README

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

### Task 2: Validate and publish README

**Files:**
- Verify: `README.md`

**Interfaces:**
- Consumes: rewritten README and repository filesystem.
- Produces: verified documentation on `origin/main`.

- [ ] **Step 1: Check relative links and documented identifiers**

Use a read-only PowerShell check to extract Markdown links, ignore HTTP and
anchor targets, and fail when a repository-relative target does not exist.
Compare documented CMake option names and build commands with their source
files.

- [ ] **Step 2: Run the full host suite**

```powershell
python -m unittest discover -s tests -v
```

If the ignored `.codex/AGENTS.md` fixture is absent, temporarily copy the root
`AGENTS.md` for this command and remove it immediately afterward.

- [ ] **Step 3: Review scope and commit**

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

- [ ] **Step 4: Synchronize and push**

Fetch `origin/main`, require zero commits behind, push without force, then
verify local `HEAD` equals `origin/main`.
