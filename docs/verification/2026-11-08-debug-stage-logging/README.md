# Debug stage logging verification — 2026-11-08

The user observed only `[MODEL_MEMORY] status=ALLOCATED` with arena use 6976/65536 B.
That line identifies successful tensor allocation; it does not identify the next
failure. No new board run has been performed by the agent.

Changes are confined to foreground initialization/diagnostic logging in the
inference and audio pipeline services. Expected/actual tensor metadata remains
strictly validated; no model, qparam expectation, MVE acceptance threshold, DSP
arithmetic, sample/hop schedule or hardware settings were changed.

- Cold boot logs board progress, transport report, model/runtime/schema/allocation,
  all seven tensor bindings, adapter qparams, FFT and feature/FIR MVE self-tests.
- Startup failure emits an explicit stage. Only cold boot drains UART; the
  existing driver blocking drain is not called after stream start or on recovery.
- The first PCM call logs frontend/FFT/Invoke/feedback/taps/FIR/PCM progress through
  nonblocking queue writes. Boot self-tests and replay do not emit these markers.
- DMA stops before fault formatting. Runtime faults log once per admitted fault category, retaining existing restart
  behavior. UART full admission remains counted/droppable; no blocking retry.

`20261108_153342_regression.log` records the new test failing before implementation.
`20261108_154554_regression.log` records **121/121 passing tests** on the final
formatted sources. Native tests execute production C pipeline paths and C++ tensor
lookup/formatting with transport/interpreter metadata doubles; they are not compiled
model parity. They cover init/start failures, exact binding rejection, boot versus
first-hop scope, no active-audio blocking drain and repeated-fault suppression.

`20261108_154645_build_receipt.json` records sequential Debug and Release configure
and build commands, all exit 0, Arm Compiler 6.24. `binary_receipt.json` includes
BIN/AXF/map hashes. Debug BIN is 631680 B; Release BIN is 612800 B. Six trace marker
strings are present in Debug and absent from Release. Model and fixture integrity
checks passed in the full suite. Maps remain in the ignored build directories.

Log copies have LF line endings and omit trailing whitespace; original timestamped diagnostics remain unchanged.
A reproducible local runner isolates installed host GCC/DLL bytes to avoid the
machine DLL search issue; this does not change Arm firmware compiler flags.

Use the existing Debug launch/task to build/program when authorized, open UART2
115200-8N1 and Continue if stopped at main. See `docs/M55M1_OPENOCD_DEBUG.md` for
stage interpretation. Supply the new log to determine the actual failing step.
Cold boot waits and first-hop logging add Debug overhead; no new deadline, current,
physical audio, stack high-water or compiled parity result is claimed.
