# I2C stuck trace verification — 2026-11-08

Base: a615810dcd02bf3cac913f36b7fda77ff33e98b9. Design approved in chat.
Raw timestamped evidence and isolated host/compiler/format tools:
`diagnostics/i2c_stuck_trace_20261108_162019/` (ignored, local).

| Acceptance | Status | Evidence |
| --- | --- | --- |
| Board/transport/service trace integration | PASS | Source hashes in verification.json; preserved failure path and borrowed-pointer lifetime |
| Actual C recovery and native UART formatting | PASS | Six modeled bus cases; failed board-init log; 123/123 host tests, no skips |
| Debug configure/build/link | PASS | 20261108_162317_debug_* logs/receipt; Arm Compiler 6.24 |
| Release configure/build/link | PASS | 20261108_162317_release_* logs/receipt |
| Release exclusion of trace/console additions | PASS | All six I2C markers and trace symbol absent; BIN and AXF hashes identical to prior verified revision |
| Formatting/diff check | PASS | clang-format 18.1.8, six C/header files; final_diff_check.log |
| Physical reproduction/root cause | NOT_RUN | No device operation by agent; awaiting user trace of failing OpenOCD workflow |
| Permanent hardware/firmware fix acceptance | NOT_RUN | This revision adds observations and preserves the recovery policy |

Debug BIN: 634400 bytes, SHA-256
`3704fe5ab26ffed171174837ab2833c7464cad5d09a344545141a2205ad24b82`.
Release BIN: 612800 bytes, SHA-256
`e2f535d2e0b2b7a407941fd5a3ecb4c6575f41f6e5cecb1a3276ce3f3922a34f`.
Images remain in build-debug/ and build/; generated binaries are not committed.

Debug map: trace284 bytes at0x201d3050, RW_RAM78456 bytes, configured stack8192
bytes; trace formatter's own reported stack frame1688 bytes. These are linker
values, not measured stack high-water or proof of the full call chain's usage.
Release RW_RAM77936 bytes and no trace storage. Headers/array alignment and
bounded22-event storage were reviewed. No dynamic allocation or UART formatting
occurs between bus recovery edges; Debug RAM recording still adds timing overhead.

The new native test was first observed failing because its helpers/API were
absent. Retained red logs also document the missing service logging/header guard
and a fixture time-unit mistake corrected without changing expected elapsed_us.
verification.json records environment failures and the corrected sequential
verification workflow. Final green receipts identify the commands that succeeded.
Curated log copies use LF with trailing whitespace removed for Git; original raw
logs remain unchanged locally. log_normalization.json records their input hashes.

Self-review: register access stays in board, common types are portable, service
only reads completed snapshots, each init invalidates stale evidence before
argument/clock failures, reset flags are not cleared or treated as causal proof,
and side-effecting SysTick CTRL is not read for diagnostics. Native models cannot
establish physical edge timing, pull-ups, voltage, codec RESET wiring or power.

Reproduction and decoding: [guide](../../i2c_stuck_trace.md). Full-hop deadline,
runtime underrun, parity and power remain outside this acceptance. No flash,
pin/clock/DMA changes or deployment approval were performed.
