# M55M1 Build and Debug Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Provide profile-aware Release/Debug builds and document the safe M55M1 USB-load plus WCH-Link debug workflow.

**Architecture:** `build.bat` and `build.sh` parse the same two positional arguments and map them to independent CMake binary directories. The checked-in Cortex-Debug configuration remains attach-only by default; README documents USB programming before attach.

**Tech Stack:** CMake, Ninja, Arm Compiler 6, Bash, Windows batch, pyOCD, Cortex-Debug.

## Global Constraints

- Release output directory is `build/`; Debug output directory is `build-debug/`.
- Debug builds must include DWARF through CMake build type `Debug`.
- Attach debugging must not write Flash, LDROM, or CONFIG.
- Scripts must preserve existing no-argument, `clean`, `config`, and `only` Release commands.

---

### Task 1: Profile-aware build scripts

**Files:**
- Modify: `build.bat`
- Modify: `build.sh`
- Test: `tests/test_build_scripts.py`

- [ ] Write tests that assert both scripts expose `release` and `debug`, retain legacy Release commands, and map Debug to `build-debug`.
- [ ] Run `python -m unittest tests/test_build_scripts.py` and observe failure before implementation.
- [ ] Implement matching positional parsing and profile-specific configure, build, clean, and compile database paths.
- [ ] Run `python -m unittest discover -s tests -p "test_*.py"` and shell syntax validation.

### Task 2: Project documentation and integration

**Files:**
- Modify: `README.md`
- Merge: worktree branch into main worktree

- [ ] Update README build, output, USB programming, safe WCH-Link Attach, and gated Flash Launch instructions.
- [ ] Verify README command examples match both scripts and checked-in launch configuration.
- [ ] Commit the worktree changes, merge that commit into main without overwriting unrelated main-worktree changes, and report verification results.
