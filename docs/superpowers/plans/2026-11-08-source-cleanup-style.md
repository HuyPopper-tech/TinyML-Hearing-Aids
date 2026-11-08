# Product source cleanup and style plan

Approved scope: remove the four obsolete diagnostic applications and dependencies used only by them; retain the custom product and its replay/timing instrumentation. No model/DSP arithmetic, hardware pin/clock/DMA or vendor changes.

1. Preserve a local deletion inventory, hashes and archive; configure Debug.
2. Remove diagnostic entry points, standalone CMake roots and exclusive modules. Preserve product capture tests; remove only tests of deleted functionality. Keep APROM recovery tooling.
3. Reconcile docs/c_style_guide.md with AGENTS.md. Format project C/C++, document public contracts with Doxygen and explain concurrency/numeric steps inline. Use author Bui Minh Huy. Exclude generated model artifacts and vendor code.
4. Verify native numeric and regression tests, Debug/Release configure/build and diff checks. Record exact test removals and limits. Make tests runnable without committing training package directories.
5. Add ignore rules for local packages/evidence/generated outputs; inspect the staged allowlist and size/secrets audit, commit the firmware work and push to the configured remote without force.

Acceptance: app/main.c is the only application entry point; no stale diagnostic build references; numerical model/contract bytes unchanged; meaningful retained tests pass; both builds link when license permits; no forbidden package/build/vendor/user files staged. Hardware replay, timing, current and audio remain unmeasured; no flash.
