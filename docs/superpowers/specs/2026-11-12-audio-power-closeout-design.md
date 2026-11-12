# Audio power experiment closeout

## Intent

Finalize the continuous DeepFIR firmware with a 220 MHz core (PL0), nominal 73.728 MHz APLL1, and the reference complex inverse FFT for FIR tap reconstruction. Keep capture and playback at 16 kHz using the validated audio divider settings.

## Approaches

- Selected: retain the tested MVE post-FFT feature optimization and remove the temporary raw PCM power-measurement path and its dedicated test cases.
- Alternative: also revert the post-FFT feature optimization. This would move the DSP source closer to the reference implementation and discard a tested reduction in frontend work.

## Scope

Keep Debug, Release, and Release Probe, including the Probe's timing and transport checks. Preserve the original complex IFFT128, model, and board pin assignments. Update clock expectations and the firmware overview. Preserve unrelated working-tree changes and local diagnostic artifacts.

Remove `.codex/` from the tracked tree on `main` while preserving its local copy and the existing ignore rule. Publish the firmware and repository cleanup in separate commits with conventional subjects and a normal push.

## Verification

Run the host suite and whitespace checks, then build Debug, Release, and Release Probe. Check that the Probe links the complex inverse FFT and that the temporary passthrough flag is gone. Confirm that the remote tip no longer tracks `.codex/`. Board timing and supply current for the combined 220 MHz and 73.728 MHz configuration require a hardware run.
