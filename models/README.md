# Model packages

The firmware baseline is model-neutral PCM16 passthrough. This directory contains no active model package, generated binary, or CPU fallback.

Future packages are opt-in only. A package must provide:

- `integration_manifest.json` with immutable artifact identity and validation status.
- `m55m1_model_package.cmake` defining the package sources, include paths, and compile definitions.

The package must pass package-contract, host parity, Vela, board numerical parity, realtime, soak, and capture-to-DAC gates before it can be enabled.
