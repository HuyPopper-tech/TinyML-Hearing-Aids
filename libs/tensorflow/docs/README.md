# TensorFlow Lite Micro source bundle

This directory is a source-only TensorFlow Lite Micro bundle for the M55M1
firmware. The CMSIS Pack descriptors, reference application, test fixtures,
and mocks are intentionally not retained.

## Layout

- `../runtime/` contains the TensorFlow Lite Micro runtime and signal source.
- `../third_party/` contains the FlatBuffers, gemmlowp, KissFFT, and Ruy
  source/header dependencies used by the runtime.
- `licenses/` preserves the upstream license text for every component.

The root CMake configuration builds the bundle as the `TensorFlowLiteMicro`
static library when `M55M1_ENABLE_TFLM` is enabled. The library includes the
Ethos-U custom operator and uses the Nuvoton-supplied Ethos-U driver.

This bundle does not include an application model, Tensor Arena, or inference
pipeline. Those remain product-owned firmware concerns.
