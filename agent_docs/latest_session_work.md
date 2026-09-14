# Latest Session Work

Completed the model-neutral cleanup baseline on 2026-09-13.

Verified: external archive SHA-256 gate; explicit legacy target deletion; stateless PCM16 passthrough vectors; model-free CMake package policy; retained-source audit; repository hygiene/style gates; full host suite (`58/58`); Debug/Release configure; Debug/Release build/link (`826/826`, exit `0` each); and `git diff --check`. Generated build output was removed after evidence capture. No board flashing is requested for cleanup.

## Runtime foundation implementation - 2026-09-14

Implemented on `codex/ethosu-runtime-foundation` without staging existing dirty
files. The archive preflight was verified before edits. The new board binding
owns M55M1 clock/reset/IRQ setup; the runtime owns static core state, validation,
cache hooks, IRQ forwarding, health/timing stats, and lifecycle. CMake excludes
all BSP NPU sources by default and adds only the two required core files when
runtime opt-in is enabled. TFLM is additionally opt-in and uses the project
bridge. The standalone smoke image has no audio/model source.

Verified: host runtime mock contract and full host suite (`66/66`); runtime/CMake static contracts; model package duplicate-core/IRQ guard; model-free Debug/Release configure and build/link; runtime-enabled Debug/Release configure and build/link; Armclang C++11 bridge compilation; model-free link-map audit with no Ethos-U runtime/core objects. No board was flashed or executed; no model package or valid command stream was introduced. `git diff --check` passed.