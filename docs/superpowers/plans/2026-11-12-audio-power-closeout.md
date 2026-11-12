# Audio Power Closeout Implementation Plan

**Goal** Finalize the continuous-audio firmware and publish the repository cleanup.

**Architecture** Keep the board clock plan in `board/m55m1_audio.c` and the existing clock acceptance checks in the codec driver. Keep the regular DeepFIR processing path and its Probe instrumentation.

**Tech stack** C, CMSIS-DSP, CMake, Python unittest, Git.

**Spec** `docs/superpowers/specs/2026-11-12-audio-power-closeout-design.md`

## Constraints

Preserve 16 kHz RX/TX, complex IFFT128, MVE feature optimization, Debug, Release, and Release Probe. Preserve local `.codex/` contents and unrelated files. Leave vendor sources unchanged.

## Task 1 — Firmware

- [ ] Update the existing clock contract test to expect `220000000UL` and run it before the clock edit.
- [ ] Set `M55M1_AUDIO_CORE_HZ` to `220000000UL`. Retain the 73.728 MHz target, fractional readback, and divider settings. Update `agent_docs/project_core_tech.md`.
- [ ] Remove `M55M1_AUDIO_POWER_PASSTHROUGH` and the dedicated wait helper, build rules, and test branches. Keep the continuous playback and Release Probe regression checks.
- [ ] Run the host suite, whitespace check, and three firmware builds. Verify complex IFFT128 and the MVE kernels in the linked Probe.
- [ ] Commit firmware and its design records with a `perf` subject.

## Task 2 — Repository cleanup

- [ ] Remove the tracked `.codex/AGENTS.md` from the index, retaining the file locally. Verify the existing ignore rule.
- [ ] Point `tests/test_codex_agent_guidance.py` at the tracked root `AGENTS.md` and run the guidance tests so a fresh checkout needs only tracked files.
- [ ] Commit the index cleanup with a `chore` subject and push normally to `origin/main`.
- [ ] Verify the remote tip and the absence of `.codex/` in its tree.

## Hardware follow-up

The combined clock configuration requires a board Probe and a Release current measurement. Host tests and the linked image establish build and firmware evidence.
