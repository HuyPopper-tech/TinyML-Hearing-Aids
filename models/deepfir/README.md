# Active R2 gain/phase package

Immutable source: `../../deepfir_r2_scratch_ptq_20261107_151431_366715/firmware_deepfir_r2_C_34c5bd074ae6_69c1c3d2`.
All 47 firmware-manifest hashes were verified before import. The source package
remains unchanged. `source_integration_manifest.json` is its original manifest;
`integration_manifest.json` records these destination files separately.

Compiled runtime model: 516848 bytes, SHA-256
`69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27`.
The C++ array is identical byte-for-byte, with alignment raised to 32 bytes.
Raw model (555600 bytes) and checkpoint remain evidence in the source package;
neither raw model, checkpoint nor golden traces is linked into production.

| Semantic order | Runtime ports | Shapes |
| --- | --- | --- |
| feature_context, h, c | 0, 2, 1 | [1,2,129,3], [1,200], [1,200] |
| gain, phase, h_next, c_next | 3, 2, 0, 1 | [1,65], [1,65], [1,200], [1,200] |

All ports are INT16. Exact scales/zero points are in the source headers and
`compiled_tensor_binding.json`. `services/deepfir` owns all executable binding,
frontend, encoded state, reconstruction and FIR processing. Both h and c use
INT64 Q31 feedback; neither lane is a direct copy. No firmware coordinate
permutation or radius transform is applied beyond the graph.

Vela 5.2.0 and its recorded FullyConnected/bias patches were not rerun.
Kaggle paths are historical provenance: resolve their artifact basenames and
package-relative suffixes against the source package, then verify manifest hashes.
See `docs/r2_custom_integration.md` for verification/replay and board gates.
`deployment_approved` remains false.
