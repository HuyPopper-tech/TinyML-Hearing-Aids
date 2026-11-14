# M55/U55 arithmetic-preserving pipeline cleanup

The user requested implementation, per-change commits and remote push, then
confirmed continuing after the numerical discussion. Preserve guarded double
quantization and the current numerical contracts. Execute inline on the Light
route with self-review and no agents; preserve unrelated local documents.

## Approach

Use arithmetic-preserving memory and profile optimizations. The frontend can
replace its 240-float history shift with a 256-sample ring, copying only the
current 16 samples. Populate the exact same chronological Hamming/FFT frame;
leave FFT, powers, compression exponents and quantization unchanged.
Remove stage timing and runtime-stat snapshot work from ordinary Release;
Debug and Release Probe retain diagnostics. Public last-stage timing returns
zero in ordinary Release; mandatory pipeline timestamps/deadlines remain.

Alternatives considered: approximate float math risks changing feature codes
and recurrent inference; the user accepted retaining the double safeguards.
Changing Vela/model artifacts or memory/cache routes requires a separate target
parity and physical comparison; leave the approved model and coherent mappings.
No claim of latency/current savings follows from instruction or byte counts.

## Boundaries

Keep 16 kHz/hop16, FIFO24/DMA32, DMIC +12 dB, software +21 dB,
compressor 3/120 ms, independent -1 dBFS limiter and DAC -9 dB.
Preserve power level/clocks, peripheral gating, WFI/WFE/SEV, ownership/barriers,
finite checks, restart policy, SWD counters and raw replay numerical APIs.
Do not edit libs, generated model bytes or deployment-only handoff documents.

Remove common/audio_pcm.h as requested. FIFO sign extension belongs at the
existing board capture copy, normalized conversion in DSP/adapter/conditioner,
and the legacy PCM16 UART export conversion at its existing diagnostic owner.
No replacement generic PCM header or wrapper is needed.

Audit all product app/service/driver/board paths and compile-time diagnostics.
Code disabled for a profile can still serve another profile or compiled replay;
retain those functions where they have a valid owner. Correct stale comments,
units, include order, naming and architecture documentation without rewriting
vendor/framework conventions or creating gratuitous abstractions.

## Acceptance and delivery

Fresh baseline suite; independent chronological-frame oracle over ring wraps,
resets, silence/impulses/rails/random PCM24 and invalid-state/input checks.
Require exact windowed FFT-frame equality in scalar and native MVE lanes.
Retain existing numerical tolerances, quantizer exhaustive/half-code tests and
conditioner oracle. Test Release/Debug/Probe observable behavior separately.
Build all three profiles in separate build directories; inspect final MVE,
map/stack, diagnostics presence/absence, immutable model hashes and diff hygiene.

Archive the initial worktree and Release under ignored diagnostics. Split the
previously uncommitted capture, DSP, conditioning and integration work into
dependency-ordered commits, then new optimizations/refactors and verified docs.
Push the new codex branch without force; do not add local PDFs or generated
artifacts. Repackage the final single Release BIN/AXF with checksums and evidence.
Board timing, current, stack watermark, acoustic quality and physical replay
remain explicitly unmeasured; no flashing is authorized by this task.
