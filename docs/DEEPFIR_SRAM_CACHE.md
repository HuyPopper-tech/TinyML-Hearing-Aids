# DeepFIR model residency and latency contract

The recurrent DeepFIR graph is invoked once per 16-sample hop. Its Vela
flatbuffer is immutable execution data: the persistent load image remains in
APROM, while the scatter file reserves `U55_MODEL_RAM` at `0x20100000` with
`0x000C0000` bytes for the execution copy. Startup copies the load image into
that cacheable SRAM region; runtime uses the execution pointer.

The remaining cacheable SRAM holds the 64 KiB TFLM tensor arena, recurrent
state, feature history, and FIR state. Linker placement and runtime checks keep
those buffers separate from `U55_MODEL_RAM`; stack remains in DTCM.

## Cache policy

The M55M1 D-cache line is 32 bytes. The runtime registers the model range as
immutable and cleans it once after startup. Per-invoke clean/invalidate is
skipped for that range because the NPU only reads model constants. Mutable
command-stream, activation, input, output, and state ranges still use normal
cache maintenance. SRAM placement alone is insufficient: repeated
model-sized cache walks can dominate an LSTM hop.

Vela `OnChipFlash`/`Axi1` labels describe its logical cost model, not physical
Flash reads. Physical residency is established by the scatter map, execution
pointer, startup copy, and runtime range check.

## Build profiles

`build.bat debug` and `build.bat release` (and the matching shell script) build
the same checked-in model with Ethos-U, TFLM, D-cache, and SRAM placement.
Debug includes the existing UART telemetry service and reports feature, NPU,
invoke-overhead, FIR/DSP, and capture-to-output latency. Release omits those
telemetry/UART sources and emits no diagnostic log. Neither profile changes the
model ABI or qparameters.

After a successful Debug build, the log also reports the model image,
reserved `U55_MODEL_RAM` space and headroom, tensor arena, adapter/state buffers,
and model-owned static SRAM from the linker map, including incremental builds.
The arena figure is its linked
capacity, not the peak allocation used by the interpreter at runtime. Release
does not run this report; its build and firmware behavior are unchanged.

Debug also prints one `[MODEL_MEMORY]` line on custom UART2 during boot, before audio
starts. `arena_used` is the TFLM allocator's actual use after `AllocateTensors()`,
`arena_capacity` is the linked 64 KiB buffer, and `arena_free` is their
difference. `ALLOCATE_FAILED` means `AllocateTensors()` failed; that alone does
not prove arena exhaustion, and its used-byte count is unavailable. This is not a
per-inference activation peak or a stack measurement. The build-time linker
report remains the source for model-region and whole-firmware static sizes.

The older September package measured 529 us invoke after cache optimization;
that result does not apply to the active R2 gain/phase model. Its current
516848-byte execution image is linked at `0x20100000`; model plus length metadata
uses 516852 / 786432 bytes. Actual R2 timing and arena use remain unmeasured.
Both profiles now run bounded feature/FIR MVE checks before audio starts.
See `r2_custom_integration.md` for current evidence, map totals and board gates.
