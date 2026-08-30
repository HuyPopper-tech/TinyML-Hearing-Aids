# Speech Boosting M55/U55 firmware export

Generated from the QAT raw-INT8 model and Vela-compiled model.

- `models/speech_boosting/generated/`: model bytes, tensor mapping,
  quantization, raw-INT8 golden trace and generated contract.
- `models/speech_boosting/speech_boosting_dsp_contract.h`:
  PCM16 to packed INT8 `[64, 2]` frontend contract.
- `integration/`: Vela configuration and compilation report.

The target firmware must use the semantic ordinal mapping in
`stream_io_contract.json`, preserve all 20 state tensors between invocations,
and consume audio in 64-sample chunks with 16-sample look-ahead.
