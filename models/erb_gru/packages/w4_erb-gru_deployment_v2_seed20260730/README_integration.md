# erb_gru firmware integration package

- Deploy `erb_gru_int8_vela.tflite` or matching aligned header.
- Verify SHA-256 from `integration_manifest.json` before build.
- Implement every state requantization entry marked `requantize=true`.
- Frontend/DSP constants come only from `dsp_constants.h`.
- Compile golden headers only with `MODEL_GOLDEN_TEST_DATA`.
- Board estimates valid: False.
- Release status remains pending until board parity and quality gates pass.
