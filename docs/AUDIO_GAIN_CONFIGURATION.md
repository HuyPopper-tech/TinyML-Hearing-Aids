# Build-time audio gain configuration

All three gains are CMake cache strings, in **whole dB**. The defaults preserve
the fixed +21 dB input trial. They apply at firmware build/startup, not live
through SWD or BLE. Mono 16 kHz/hop16 and the compressor/limiter remain unchanged.

| CMake variable | Default | Accepted range | Position |
|---|---:|---:|---|
| `M55M1_DMIC_GAIN_DB` | 12 | -127 to 36 | DMIC digital DSP, before FIFO24 capture |
| `M55M1_SOFTWARE_GAIN_DB` | 21 | 0 to 36 | PCM24 normalization, before compressor and shared frontend/FIR |
| `M55M1_DAC_GAIN_DB` | -9 | -63 to 0 | Both DAC channels' startup digital volume and runtime ceiling |

CMake prints the selected value and range on every configure. Values outside
the inclusive limits, fractional values, empty strings and nonnumeric values
fail configuration. Use canonical decimal integers, e.g. `6`, not `06`.
Software gain is the project's selected range, not a hardware limitation.
Its precomputed `10^(dB/20)` coefficient adds no per-hop logarithm/power call,
buffer, runtime setting or state field. The +21 dB coefficient retains its
previous float value. The existing dynamics still determine the conditioned level.

## Change and build

From the repository root, for example:

```powershell
rtk proxy cmd /c build.bat release config
rtk proxy cmake -S . -B build -DM55M1_DMIC_GAIN_DB=12 -DM55M1_SOFTWARE_GAIN_DB=24 -DM55M1_DAC_GAIN_DB=-6
rtk proxy cmd /c build.bat release build
```

This example raises the pre-compressor software gain by 3 dB and changes DAC
volume from -9 to -6 dB. A less negative DAC value is louder. Software gain
before compression does not guarantee the same increase at the final output.
The existing driver initializes both DAC channels at the selected ceiling and
rejects later runtime requests above it; this setting does not change analog
headphone gain or mixer routing.

To restore the defaults:

```powershell
rtk proxy cmake -S . -B build -DM55M1_DMIC_GAIN_DB=12 -DM55M1_SOFTWARE_GAIN_DB=21 -DM55M1_DAC_GAIN_DB=-9
rtk proxy cmd /c build.bat release build
```

Use `build.bat debug config` first, then `build-debug` with
`build.bat debug build` for Debug. Probe has its own
`build-release-probe` cache. Configure the desired gains separately in each tree;
changing Release does not change Debug or Probe. Existing build scripts preserve
cached gain overrides. Editing the `set(... CACHE STRING ...)` default in
`CMakeLists.txt` affects fresh caches; use `-D` to update an existing cache.
The build script refreshes the root compilation database; restart clangd after
switching profiles. Program the resulting BIN with its matching AXF.
Previously delivered ZIP packages retain their recorded settings and hashes.

## Limits and sources

The [Nuvoton M55M1 datasheet](https://www.nuvoton.com/export/resource-files/en-us--DS_M55M1_Series_EN_Rev1.00.pdf)
specifies DMIC volume gain -128 to +36 dB, with finer register steps. This build
interface follows the whole-dB API in `libs/StdDriver/src/dmic.c`, but excludes
the -128 dB endpoint: that BSP function writes `0x8000` where the register
mapping in `libs/Device/Nuvoton/M55M1/Include/dmic_reg.h` requires `0x0000`.
The accepted firmware range is therefore **-127 to +36 dB**. The native test
executes the unchanged BSP encoder for every accepted whole-dB value and channel.
No vendor source is modified.

The [TI TLV320DAC3100 datasheet](https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf)
specifies digital volume -63.5 to +24 dB in 0.5-dB steps. This firmware exposes
the existing driver's whole-dB attenuation range **-63 to 0 dB**; it does not
enable the chip's positive digital boost. The adjustable ceiling is a software
policy, not a calibrated acoustic output limit.

Higher DMIC gain can saturate before software conditioning. Higher software
gain amplifies noise along with speech and can trigger more gain reduction.
Neither compression nor DAC volume can recover already clipped input. Use
[the uninterrupted SWD trial](INPUT_LONG_TRIAL.md) with the exact built settings
to assess clipping, deadline continuity, current and listening behavior.
