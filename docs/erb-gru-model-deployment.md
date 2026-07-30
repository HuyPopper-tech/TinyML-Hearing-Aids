# ERB-GRU model deployment and CPU post-processing

Updated: 2026-07-30

This note defines the firmware/model boundary for the current ERB-GRU package
and the migration path for later experimental models. The package under
`models/erb_gru/packages/` is immutable; all firmware contracts are generated
and audited from it.

## Current deployment contract

- Package: `w4_erb-gru_deployment_v2_seed20260730`.
- Vela model: 59,232 bytes, one `ethos-u` custom operator and no CPU operator.
- Outputs 0 and 1 are the next recurrent states.
- Output 2 is 32 INT8 ERB-band gains with scale `1/256` and zero-point `-128`.
- The recurrent outputs are copied raw to the matching next-frame inputs.
- Firmware expands the 32 gains to 161 FFT-bin gains on the CPU with
  `erb_gain_expansion_32x161[band][bin]`.

The expansion matrix contains 5,152 floats (20,608 bytes). Each bin column
sums approximately to one, so equal band gains should remain equal after
expansion. The matrix is interpolation/post-processing, not a second learned
model.

## Optimized Release failure

Board telemetry isolated the fixed attenuation before the IFFT:

| Release profile | Band gain | Bin gain | Spectrum/PCM result |
| --- | ---: | ---: | --- |
| Unity fallback, `-O3` | not applicable | `~1.000` | ratio `~1.000` |
| Model, `-O3` | `~0.988–0.992` | `~0.497–0.498` | ratio `~0.48–0.50` |
| Model, whole-file `NO_VECTORIZE` | `~0.99` | `~0.995` | restored near unity |
| Model, enhancer `-O2` | valid input | `0` | unusable |

The first measured bad boundary is the CPU 32-to-161 reduction. Unity fallback
uses the same optimized FFT, IFFT, WOLA and transport path and does not show
the factor-of-two loss, so those stages and the Ethos-U model are not the
source of the fixed attenuation.

The current evidence implicates Armclang 6.24 vectorized code generation for
the stride-161 reduction. That compiler-loop diagnosis remains an inference
until the installed loop-local pragma is compared on the board. Documentation
must not describe the board A/B as complete before it is measured.

## Current workaround

Firmware preserves the source matrix and arithmetic but disables vectorization
and interleaving only for the 32-term reduction in `expand_gains()`. All other
enhancer, TFLM, NPU and driver code remains Release `-O3`.

Do not replace this with either of the investigation-wide settings:

- whole-enhancer `NO_VECTORIZE` gives up unrelated optimization;
- per-file `-O2` produced zero bin gains and is not a valid workaround.

Remove the loop pragma only after all of these pass with the replacement
compiler or implementation:

1. host constant-gain expansion within one milli for every bin;
2. host golden parity and unity spectrum ratio `999–1001`;
3. diagnostic Release board A/B with a stable tone;
4. production fault recovery and 10-minute soak.

## Medium-term CPU implementation

If the expansion remains on the CPU, the generator may emit the immutable
matrix as `[161][32]`, so the inner reduction reads 32 contiguous weights:

```text
bin_gain[bin] += band_gain[band] * expansion_bin_major[bin][band]
```

Only one layout may be linked into production; keeping both layouts wastes
20,608 bytes of Flash. The generated layout and hash must be recorded in
`deploy_audit.json`. Re-enable vectorization only after host golden and board
A/B validation for that exact compiler and layout.

## Preferred long-term model contract

For a replacement model, append the fixed, non-trainable expansion as the
final TFLite layer:

```text
bin_gain[bin] = sum(band_gain[band] * expansion[band][bin])
```

Use a `[161,32]` fully-connected weight matrix with zero bias, derived from the
audited package constant. The production output contract becomes:

- output ordinal 0: `next_state2`;
- output ordinal 1: `next_state1`;
- output ordinal 2: `bin_gain`, INT8 shape `[1,1,161]`.

Do not add a fourth production output for the 32 band gains. If experiment
analysis needs it, export a separate host/diagnostic model. Firmware then
dequantizes 161 bin gains and multiplies the spectrum directly; it no longer
links the CPU expansion matrix or the 32-band float buffer.

The fixed layer adds 5,152 theoretical MAC/inference. The current model has
45,056 MAC/inference, so the expected total is about 50,208 MAC/inference or
5.02 MMAC/s at 100 frames/s. The new Vela report, not this estimate, becomes
the authoritative metadata.

## Replacement-package checklist

For every new experimental package:

1. Keep the prior package immutable and assign a new package ID/hash.
2. Generate, never hand-edit, tensor contracts and frontend constants.
3. Verify `bin_gain` name, ordinal, raw index, type, shape, bytes and
   quantization for pre-Vela and Vela models.
4. Require the Vela model to contain one `ethos-u` custom op and zero CPU ops.
5. Take MAC/operator counts from the audited Vela report.
6. Keep the old expansion constant only as a host reference; do not compile it
   into firmware when the model outputs 161 gains.
7. Re-run state/feature/gain INT8 parity, all five PCM golden scenarios,
   fault fallback and finite-corpus flush.
8. Re-measure model Flash, tensor-arena used/capacity, DSP static memory,
   stack headroom and Release timing.
9. Inspect the map for one model copy and absence of the CPU expansion matrix.
10. Run board diagnostic A/B and production soak before declaring the package
    deployable.

If Vela leaves the fixed expansion on the CPU, reject the package instead of
silently accepting a CPU fallback. Quantization error is also a package
failure: use an appropriate per-axis weight strategy rather than relaxing the
one-LSB or PCM acceptance gates without evidence.

## Non-goals

- Do not modify DMIC gain, WOLA normalization or codec gain to conceal a
  compiler/model contract error.
- Do not retrain the model merely to compensate for CPU post-processing.
- Do not introduce a runtime multi-model framework until a second production
  model actually requires one. Package selection remains compile-time.
