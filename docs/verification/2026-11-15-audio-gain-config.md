# Build-time audio gain verification — 2026-11-15

## Change and acceptance

Expose `M55M1_SOFTWARE_GAIN_DB` and `M55M1_DAC_GAIN_DB` beside the existing
`M55M1_DMIC_GAIN_DB` in root CMake. Defaults remain 12/21/-9 dB. Validate inclusive
whole-dB ranges DMIC [-127,36], software [0,36], DAC [-63,0] and print each chosen
value/range during configure. The [configuration guide](../AUDIO_GAIN_CONFIGURATION.md)
documents hardware versus firmware limits, cache persistence and profile commands.

The board's existing startup/maximum DAC setting receives the configured value;
driver register encoding and runtime ceiling checks remain unchanged. DMIC and
DAC headers reject out-of-range direct compiler definitions. The conditioner
selects a precomputed `10^(dB/20)` float from a private table using the build
constant, with its own compile-time range gate. No per-hop power calculation,
new state field, buffer, runtime protocol, dynamic allocation or generic wrapper.
The +21 dB coefficient retains its original float literal.

16 kHz/hop16, PCM24 normalization, shared frontend/FIR samples, quantization,
compressor/limiter, clocks, idle policy and vendor/model files remain unchanged.
Gain changes can change compression activity/model input; equality to the old
output is required only for the preserved default configuration.

## Tests

The new focused tests first failed against the original hardcoded settings and
missing validation, then passed after implementation. After the endpoint
correction, the complete suite passed **154 tests in 87.065 s** using the existing
native-test environment launcher:

```powershell
rtk proxy python -X utf8 diagnostics/run_host_tests_pcm24.py discover -s tests -v
```

`git diff --check` also passes.

`tests/test_audio_gain_config.py` executes the actual CMake gain/definition
blocks through `cmake -P`. Check defaults and both inclusive endpoints; reject
both outside endpoints, empty/nonnumeric/boolean/fractional/scientific/list
values, leading-zero integers and excessive integer length. Compile the actual
board header for defaults, overrides and direct-definition range failures.
The board-to-codec binding continues to use the checked ceiling macro.
The board test also executes the actual BSP gain function against the actual
register layout for every whole-dB gain from -127 through +36 on all four
channels, checking the documented signed Q9.7 encoding and untouched channels.

Final review found that the bundled BSP encodes -128 dB as `0x8000`, although
the register's `CHxVOL = -128 - gain` mapping requires zero. The hardware
datasheet's -128 dB endpoint is therefore excluded without editing vendor code.
Boundary regression tests first failed on acceptance of -128, then passed with
the CMake and direct-definition lower bound changed to -127.

`tests/audio_gain_harness.c` runs the production conditioner for all 37 software
settings, and MVE lane-model variants at 0/12/21/36 dB: **41 builds/runs**.
For each, a quiet signal verifies the selected preamp coefficient with relative
error <=1e-7. An independent double streaming oracle then compares 2048 hops of
rails and seeded varying levels: finite output, ceiling <=10^(-1/20)+2e-7,
absolute error <=3e-5 and exact processed-sample count. Existing default
conditioner, adapter, quantizer, reset/fault and numeric assertions remain intact.
The MVE lane model does not execute the physical target.

## Target builds and identity

Arm Compiler for Embedded 6.24: ordinary Release, Debug and Release Probe builds
pass with defaults 12/21/-9. The real board and conditioner objects also compile
with DMIC=-127, software=36 and DAC=-63; their actual Arm compile definitions
were inspected. This boundary configuration was an object-compile check, not a
linked or flashed alternative image. Probe was restored and fully rebuilt at
defaults. The active root compilation database was restored to ordinary Release.

Default Release **BIN and AXF match the preceding image byte for byte**:

- BIN: 622848 bytes, SHA256 `0ef9f7479034410e3308e991b55ccdb64d76ec315acbf1a0858ad6571c9fe0dc`.
- AXF: SHA256 `6ce8eabf0b9a41a4a488838b62300521eeab16052ece2c7917664eb15dc8f3f9`.

Release and Probe pass all ten required linked MVE ranges, including PCM24
normalization. The default Release compiler folds the coefficient selection;
no additional default Release executable cost is inferred merely from source.
Exact image equality is stronger evidence here than an estimated operation count.

Raw receipts are Git-ignored under `diagnostics/gain_config_20261115-*`, including
red/green/final suites, three builds, boundary compile definitions and linked MVE.
Previously delivered packages retain their original hashes and fixed parameters.

No flashing, acoustic output calibration, distant-speech assessment, board
latency/current measurement or stack watermark was performed. The configured
DAC ceiling is a software limit, not a calibrated sound-pressure limit.
