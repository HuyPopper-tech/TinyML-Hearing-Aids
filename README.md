# TinyML Hearing Aids — NuMaker-GestureAI-M55M1 firmware

Firmware foundation for the **TinyML Hearing Aids** project on the
**NuMaker-GestureAI-M55M1** board (chip **M55M1R2LJAE**, LQFP64, 2 MB Flash /
1.5 MB SRAM, Cortex-M55 @ 220 MHz). The current application exercises the
on-board DMIC microphone and provides the platform layer for TinyML audio
capture, inference, and hearing-aid processing.

Built with **CMake + Arm Compiler 6 (armclang)** — no CMSIS-Toolbox, no vcpkg,
no Open-CMSIS-Pack tooling required. Works identically on **Windows and Linux**.

---

## Project layout

```
TinyML-Hearing-Aids/
├── CMakeLists.txt          # build definition
├── cmake/
│   └── armclang.cmake      # Arm Compiler 6 toolchain file
├── app/
│   └── main.c              # DMIC microphone functional test + platform init
├── libs/                   # copied from BSP (self-contained)
│   ├── CMSIS/Core/Include/ # CMSIS-Core headers (core_cm55.h, tz_context.h, ...)
│   ├── CMSIS/DSP/          # CMSIS-DSP (Helium/MVE kernels)  [enabled by default]
│   ├── NN/                 # CMSIS-NN (neural-network kernels) [opt-in]
│   ├── RTOS2/              # CMSIS-RTOS2 API (cmsis_os2.h)    [opt-in]
│   ├── RTX/                # RTX source variant (RTOS2 backend)[opt-in]
│   ├── Device/Nuvoton/M55M1/{Include,Source}/
│   └── StdDriver/{inc,src}/
├── build/                  # generated (gitignored)
└── README.md
```

The `libs/` tree was copied from `Library/` of the M55M1BSP so this project has
no dependency on the BSP checkout — you can move/copy this folder anywhere.

---

## Prerequisites

### 1. Arm Compiler for Embedded (armclang / AC6)

Install **Arm Compiler for Embedded 6.16 or newer** (the BSP targets ^6.24).
After install, make sure these are on your `PATH`:

| Binary    | Role                          |
|-----------|-------------------------------|
| `armclang`| C/C++ compiler                |
| `armasm`  | assembler                     |
| `armlink` | linker (reads the .scf file)  |
| `armar`   | archiver                      |
| `fromelf` | ELF -> bin/hex converter      |

**Activate the license** (one-time, per machine). The exact code depends on
your Arm account / entitlement — see
https://developer.arm.com/Tools%20and%20Software/Arm%20Compiler%20for%20Embedded

```bash
armlm list            # show available licenses
armlm activate <code> # activate your entitlement
```

Verify:
```bash
armclang --version
```

### 2. CMake (>= 3.20) and Ninja

- **CMake**: https://cmake.org/download
- **Ninja** (recommended generator, fastest):
  https://github.com/ninja-build/ninja/releases

```bash
cmake --version
ninja --version
```

> Ninja is optional — `MinGW Makefiles` / `Unix Makefiles` also work, but Ninja
> is strongly recommended for fast incremental builds on both OSes.

---

## Build profiles

### Easiest way — use the helper scripts

Both scripts use the same interface:

```text
[release|debug] [build|config|only|clean]
```

- `release` uses `build/` and is the default.
- `debug` uses `build-debug/` and adds DWARF symbols for source-level debug.
- `build` configures then builds; `config` only configures; `only` only builds
  an existing tree; `clean` removes only that profile tree then rebuilds it.

Existing commands remain valid: no argument, `clean`, `config`, and `only`
mean Release.

**Windows** (from the project root, in cmd or PowerShell):
```bat
build.bat                    :: Release configure + build
build.bat debug              :: Debug configure + build
build.bat debug config       :: configure Debug only
build.bat debug only         :: build existing Debug tree only
build.bat release clean      :: clean + rebuild Release
build.bat debug clean        :: clean + rebuild Debug
```

**Linux / macOS / WSL**:
```bash
./build.sh                    # Release configure + build
./build.sh debug              # Debug configure + build
./build.sh debug config       # configure Debug only
./build.sh debug only         # build existing Debug tree only
./build.sh release clean      # clean + rebuild Release
./build.sh debug clean        # clean + rebuild Debug
```

The helper scripts:
- Use an **absolute path** for the toolchain file (avoids the PowerShell
  issue where `cmake/armclang.cmake` is split at the `/`).
- On Windows, **auto-add** `C:\Program Files\ArmCompilerforEmbedded6.24\bin`
  to PATH for the session if `armclang` is not already on it — so a fresh
  terminal works without manual PATH setup.

### Manual way (if you prefer raw cmake commands)

From the project root:

```bash
# Configure (run once, or after changing CMake variables)
# IMPORTANT on PowerShell: use an ABSOLUTE path for the toolchain file,
# otherwise PowerShell splits "cmake/armclang.cmake" at the "/" and CMake
# fails with "Could not find toolchain file: cmake/armclang".
cmake -B build -G Ninja \
      -DCMAKE_TOOLCHAIN_FILE=<absolute>/cmake/armclang.cmake \
      -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build
```

**Output** (in `build/` for Release or `build-debug/` for Debug):

| File                  | Description                     |
|-----------------------|---------------------------------|
| `M55M1_GestureAI.axf` | ELF firmware (for debugging)    |
| `M55M1_GestureAI.axf.bin` | Raw binary (for flashing)       |
| `M55M1_GestureAI.axf.map` | Linker map (sizes, callgraph)   |

> If you see `CMake Error: CMAKE_C_COMPILER not set, after EnableLanguage` and
> `Could NOT find ARMCLANG_BIN` — it means `armclang` is not on the PATH of the
> current terminal. Open a **new terminal** (so the persistent PATH loads), or
> run `build.bat` which auto-fixes the PATH for the session.

### Build types

- `Release` — `--no_debug`, optimized for size/speed.
- `Debug` — DWARF symbols plus extra linker stack information. Build Debug
  with `build.bat debug` or `./build.sh debug`; use the resulting AXF with
  Cortex-Debug and load the matching `.axf.bin` through USB first.

### Common overrides (add to the `cmake -B build ...` line)

| Variable                       | Default        | Purpose                                            |
|--------------------------------|----------------|----------------------------------------------------|
| `-DM55M1_DEBUG_UART_IDX=0`     | `5`            | Use UART0 (PB12/PB13) instead of UART5 (PB4/PB5)   |
| `-DM55_CPU=cortex-m55+cdecp2`  | `cortex-m55`   | Enable ACI custom-datapath-extension coprocessor 2 |
| `-DM55M1_STACK_SIZE=0x2000`    | scatter default| Override stack size (bytes, hex)                   |
| `-DM55M1_HEAP_SIZE=0x2000`     | scatter default| Override heap size (bytes, hex)                    |
| `-DM55M1_ENABLE_FULL_STDDRIVER=ON` | `OFF`      | Build **all** StdDriver .c instead of the minimal set |

### Reconfigure after changing an option

```bash
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/armclang.cmake -D<NEW_VAR>=<val>
cmake --build build
```

---

## Code navigation (clangd / VS Code — Ctrl+click go-to-definition)

The build exports a `compile_commands.json` (CMake `CMAKE_EXPORT_COMPILE_COMMANDS=ON`)
that maps every translation unit to its exact armclang command line — include
paths, `-DARM_MATH_MVE*` Helium macros, `-mcpu=cortex-m55`, etc. The language
server **clangd** reads it to power:

- **Ctrl+click** on a function / macro / header → jump to its definition
- Hover for docs/signatures, autocomplete, rename, find references
- In-editor diagnostics

After a configure, `build/compile_commands.json` exists and the helper scripts
also mirror it to `./compile_commands.json` at the project root.

### One-time VS Code setup

1. Install the **clangd** extension (`llvm-vs-code-extensions.vscode-clangd`).
   If you also use the Microsoft C/C++ extension, disable its IntelliSense for
   this workspace to avoid double-indexing.
2. Open the project folder in VS Code. clangd starts, reads `.clangd` +
   `compile_commands.json`, and begins a background index.
3. Build once (`./build.sh`) so the database is current, then Ctrl+click works
   across the app, StdDriver, CMSIS-DSP/NN and RTX sources.

### Project files for the IDE

| File | Role |
|---|---|
| `.clangd` | clangd config: strips a few armclang-only flags so vanilla clangd parses cleanly and suppresses the benign f16-infinity note |
| `compile_commands.json` | generated (symlinked to `build/`); gitignored because it holds machine-specific absolute paths |

> armclang is clang-based, so clangd understands `--target=arm-arm-none-eabi`,
> `-mcpu=cortex-m55`, `-mfloat-abi=hard` and resolves the Helium/MVE headers
> (`arm_mve.h`, `arm_helium_utils.h`) just like the real compiler.

---

## USB programming and WCH-Link debugging

The daily workflow deliberately separates programming from debugging:

1. Build the Debug firmware: `build.bat debug` or `./build.sh debug`.
2. Copy `build-debug/M55M1_GestureAI.axf.bin` to the board USB Mass Storage
   device named `M55M1`.
3. Connect WCH-Link Mini to SWD on J2: `VTref`, `SWDIO`, `SWCLK`, and `GND`.
   Do **not** cross-power the board; VTref is a reference only. NRST is not
   required.
4. In VS Code select `M55M1: Attach (WCH-Link)` and press F5.

The Attach configuration starts pyOCD through the WCH CMSIS-DAP probe
`0001A0000001` at 100 kHz, uses `--connect attach`, and loads symbols from the
Debug AXF. It performs a SWD `SYSRESETREQ`, runs to a temporary breakpoint at
`main`, and does **not** erase or program Flash.

`M55M1: Launch (WCH-Link, verified)` remains intentionally locked behind the
one-page Flash verification task. Do not bypass that gate: the override only
permits APROM Secure (`0x00100000-0x002FFFFF`) and intentionally excludes
LDROM and CONFIG to protect the USB bootloader and device configuration.

GNU Arm GDB is used for debugging even though the firmware is compiled by Arm
Compiler 6: GDB consumes the ARM ELF/DWARF debug information and does not need
to be the compiler's debugger. The checked-in VS Code configuration uses the
GDB bundled with STM32CubeCLT because it is an available `arm-none-eabi-gdb`
binary, not because this is an STM32 target.

---

## IMPORTANT — TrustZone / NSCBA note

The M55M1's Cortex-M55 always implements Armv8.1-M **TrustZone** (Security
Extension). This project builds in **TrustZone-OFF** mode (secure-only): no
`-mcmse`, no Secure/Non-secure partition split, and `system_M55M1.c`
**skips** `FMC_NSCBA_Setup()`. The firmware boots in Secure state and can
access the entire 2 MB Flash / 1.5 MB SRAM.

### Risk: a previously-programmed Non-Secure boundary (NSCBA)

If the chip was **ever flashed with a TrustZone / SecureApplication sample**
from the BSP, the **Non-Secure Code Base Address (NSCBA)** may already be
written into Flash CONFIG. That splits Flash into a small Secure region and a
large Non-Secure region. A secure-only firmware (this project) loaded into the
Non-Secure region **will not execute correctly** — the processor resets into
Secure state and the reset handler must live in the Secure region.

### Recovery note

The normal USB-load/WCH-Link Attach workflow never writes CONFIG, LDROM, or
NSCBA. If a board is already configured with an incompatible NSCBA boundary,
stop and back up its configuration before using a vendor recovery tool; do not
attempt to clear CONFIG through the checked-in pyOCD target override.

---

## Memory map (from `libs/Device/Nuvoton/M55M1/Source/ARM/M55M1.scf`)

| Region   | Base       | Size    | Notes                          |
|----------|------------|---------|--------------------------------|
| ITCM     | 0x00000000 | 64 KB   | instruction TCM                |
| **FLASH**| 0x00100000 | **2 MB**| code + RO (Secure, all)        |
| DTCM     | 0x20000000 | 128 KB  | stack lives here               |
| **SRAM** | 0x20100000 | ~1.19 MB| .data/.bss/heap (cacheable)    |
| SRAM_NC  | after SRAM | ~0.31 MB| NonCacheable region            |
| LPSRAM   | 0x20310000 | 8 KB    | low-power SRAM                 |

I/D-Cache is **enabled by default** (`NVT_DCACHE_ON=1`). The BSP does **not**
guarantee cache coherence, so for DMA/shared buffers either:
- place them in the NonCacheable region (`NVT_NONCACHEABLE`), or
- explicitly `SCB_CleanDCache_by_Addr` / `SCB_InvalidateDCache_by_Addr`.

---

## CMSIS libraries (DSP / NN / RTOS2+RTX)

The project ships the CMSIS libraries from the BSP under `libs/` and wires them
up as CMake targets. They are controlled by three options (set with `-D` on the
cmake command line):

| Option | Default | What it builds | Target |
|---|---|---|---|
| `M55M1_ENABLE_DSP`   | **ON**  | CMSIS-DSP (Helium/MVE-accelerated kernels) | `CMSISDSP`  |
| `M55M1_ENABLE_NN`    | OFF     | CMSIS-NN (needs DSP)                       | `CMSISNN`   |
| `M55M1_ENABLE_RTOS2` | OFF     | CMSIS-RTOS2 API (RTX source variant)       | `CMSISRTOS2`|

```bash
# Default: DSP only
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/armclang.cmake ...

# DSP + NN + RTOS2 (everything)
cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/armclang.cmake \
      -DM55M1_ENABLE_NN=ON -DM55M1_ENABLE_RTOS2=ON ...
```

### CMSIS-DSP (Helium / MVE)

Built as the `CMSISDSP` static library from every `arm_*.c` under
`libs/CMSIS/DSP/Source/*/`. The "umbrella" `<Module>.c` files (which only
`#include` the siblings) are excluded to avoid duplicate symbols.

Cortex-M55 + armclang (`-mcpu=cortex-m55 -mfloat-abi=hard`) already defines
`__ARM_FEATURE_MVE` and `__ARM_FEATURE_DSP`, which `arm_math_types.h` maps to
`ARM_MATH_MVEI` / `ARM_MATH_MVEF` / `ARM_MATH_DSP` automatically. These are also
set **explicitly** (PUBLIC on `CMSISDSP`) so that `clangd` /
`compile_commands.json` — which do not see compiler built-ins — resolve the
Helium kernels. Just `#include "arm_math.h"` in the app and call any DSP
function; the MVE-vectorized implementation is selected at compile time
(`#if defined(ARM_MATH_MVEF) && !defined(ARM_MATH_AUTOVECTORIZE)`).

> A `target_compile_options(... -flax-vector-conversions=integer)` is applied so
> armclang accepts the integer-vector bitcasts the MVE kernels use for lane
> access via NEON-compatible intrinsics (`vsetq_lane_s32` etc.). The upstream
> `configDsp.cmake` "strict" default (`=none`) is too strict for armclang 6.24.

`app/main.c` includes a tiny `arm_dot_prod_f32` demo so the Helium kernel is
actually pulled from `libCMSISDSP.a` (otherwise `--ffunction-sections` + linker
GC strip all unused DSP code). The final `.axf` is only ~13 KB because only the
referenced kernels survive.

### CMSIS-NN

`CMSISNN` static library from `libs/NN/Source/*/arm_*.c`. It links `CMSISDSP`
publicly (NN reuses `arm_math.h` and the DSP tables), so enabling NN implicitly
brings DSP in — you do not link DSP separately (the CMakeLists uses `elseif` to
avoid the `L6304W: Duplicate input file` warning). Include `arm_nnfunctions.h`.

### CMSIS-RTOS2 + RTX (source variant)

The `CMSISRTOS2` **OBJECT** library compiles the RTX kernel C sources
(`rtx_*.c`), `RTX_Config.c`, the `os_systick.c` tick source, and the RTX
context-switch handler in GNU assembler (`libs/RTX/Source/GCC/irq_armv8mml.S`,
Armv8-M Mainline = Cortex-M55). The `.S` is routed through **armclang's
integrated assembler** (`-x assembler-with-cpp`) because the project toolchain
uses `armasm` (legacy) for ASM, which cannot parse GNU `.syntax unified` sources.

Why an **OBJECT** library (not STATIC): the RTX handlers (`SVC_Handler`,
`PendSV_Handler`, `SysTick_Handler`) are defined strongly in the asm and must
override the **weak** aliases in `startup_M55M1.c`. A static archive is only
extracted on strong reference, but the vector table references those handlers
only weakly — so the archive would never be pulled and the scheduler would hang
on the first tick. An OBJECT library links all objects unconditionally, so the
strong RTX handlers win. Unused RTX functions are still removed by
`--ffunction-sections` + armlink section GC, so there is no code bloat.

TrustZone is **OFF** → `OS_TZ_CONTEXT` stays `0` (`tz_context.h` is not pulled
in) and the firmware runs secure-only. A minimal `RTE_Components.h` stub
(`libs/RTX/Config/RTE_Components.h`) defines `CMSIS_device_header "M55M1.h"` so
the RTX sources resolve the device header without Keil's RTE manager.

`handlers.c` is **excluded** (it pulls in Keil-RTE fault diagnostics that are
not needed for a working scheduler; `startup`'s weak `Default_Handler` covers the
faults). Add it back if you want RTX fault callbacks. To use the RTOS, include
`cmsis_os2.h` and call `osKernelInitialize()` / `osKernelStart()` from `main()`.
