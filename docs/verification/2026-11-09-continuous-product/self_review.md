# Scoped self-review — 2026-11-09

Reviewed the diff against99c617b, directly connected callers, native harnesses,
ordinary build compile databases, final maps and binary content. Light route;
the user prohibited subagents/explorers, so no independent reviewer was dispatched.

- The removed PRODUCT guard was always1 in both production configurations.
  Keeping that branch makes terminal DMA/WFI/power binding identical across
  ordinary Debug/Release and Probe. No second interpreter or pipeline was added.
- Removed32-kHz/diagnostic warmup paths have no active caller. Invalid enum
  values now return zero/NULL and init rejects them before hardware programming;
  actual C getters were checked for0,1,2,255,-1. Existing accepted0 remains16k/hop16.
- Removing untagged submit prevents bypassing sequence ownership. NULL input,
  NULL tag and wrong sample count fail before queue/DMA publication. Runtime
  guards, pending/busy/underflow/abort rejection and full reset are preserved.
- RX/TX storage remains static, aligned and non-cacheable; no current pins,
  clocks, gain, DMA channels or priorities changed. Foreground only copies
  released buffers. Terminal NEXT0 prevents speculative task chaining; IRQ
  still only acknowledges, hands off, records bounded raw events and signals.
- Normal Debug and Release each execute60005 host foreground hops without the
  Probe's finite stop; Debug enqueue remains bounded/nonblocking during audio.
  Probe's existing stopped report/fault behavior passes the suite. Strict1ms
  guards were not relaxed for Debug; its unoptimized target timing is unmeasured.
- Caller search found no production use of old audio_telemetry; only its own
  test and CMake listing remained. Deleted source/header/private test, not current
  reporting. The old scatter-only harness was retired with its implementation;
  packing/sequence/fault coverage remains in the current60000-handoff C harness.
  No canonical values or numeric tolerances changed to pass tests.
- Full134-test suite, three Arm builds, scoped MVE checks and package/destination
  hashes pass. One model/service/Ethos-U core/device source each; old telemetry
  object absent from maps. Reserved RAM is unchanged, not measured peak usage.
- Historical user Probe PASS remains limited to its un-attested image identity;
  it cannot prove final-image hardware timing, acoustic latency, compiled code
  parity, stack watermark or power. These limits are explicit in acceptance.
- Empty obsolete directories were checked before removal. Ninja deleted only
  dead generated outputs; retained BIN hashes are unchanged. No recursive
  source/package cleanup, libs modification, flash/reset or probe access occurred.

No unresolved source correctness issue was found in this scoped review.
Final hardware retest/compiled parity and physical measurements remain open;
deployment_approved stays false. Commit scope excludes all local training packages,
generated images/maps/caches, vendor/unrelated user files and diagnostics archives.
