# PF0/PF1 UART-to-SWD Recovery Test Design

## Goal

Prove on the NuMaker-GestureAI-M55M1 that PF0 and PF1 can operate as UART0 pins before adding a separate firmware revision that restores their ICE/SWD functions.

## Scope

This first revision intentionally configures PF0 as UART0 TXD and PF1 as UART0 RXD. It continuously transmits a recognisable text banner at 115200 baud on PF0. It does not restore ICE in firmware; return-to-SWD is a second, separately validated revision after the user observes the UART signal on hardware.

## Hardware Procedure

1. Connect a 3.3 V USB-to-TTL adapter's RX input to J2.2 (PF0 / ICE_DAT) and connect its GND to J2.5 (VSS). Do not connect the adapter's VCC pin.
2. Optionally connect the adapter TX input to J2.3 (PF1 / ICE_CLK), but the first revision only requires TX observation.
3. Open the adapter at 115200-8-N-1, load the test binary through the board's USB bootloader, and reset the board.
4. Confirm the repeating UART0 banner appears. Disconnect the WCH-Link during this UART test so it does not electrically contend with PF0/PF1.

## Firmware Design

- Preserve the existing DMIC application as the default firmware source.
- Add a dedicated PF0/PF1 UART0 test source selected through a CMake cache option. The default remains the current DMIC firmware; setting the option selects the test binary.
- In the test source, unlock protected registers, enable GPIOF and UART0 clocks, select a safe UART clock source, and mux PF0/PF1 using `SET_UART0_TXD_PF0()` and `SET_UART0_RXD_PF1()`.
- Initialize UART0 to 115200 baud and write a fixed banner directly through UART0 every second. Do not use `printf`, because the project's `printf` retarget remains UART5 on PB4/PB5.
- Configure the user LED on PA7 as a heartbeat so a missing serial connection can be distinguished from a firmware that did not boot.

## Safety and Recovery

While this firmware runs, PF0/PF1 are application UART pins rather than the normal SWD transport. A WCH-Link without NRST might not attach after the mux executes. Recovery is therefore by power-cycle during the early boot interval or by entering the board's USB bootloader (hold User, reset, release User) and loading the default firmware. The first revision must not set DPM debug-disable or debug-lock bits.

## Verification

- Host-side unit tests statically check that the CMake option preserves the DMIC default and selects the UART test source only when requested.
- Static tests check that the UART test uses PF0/PF1 UART0 mux macros, UART0 at 115200, direct UART0 transmission, and no ICE/DPM configuration.
- Hardware acceptance is the user's observation of the banner on PF0 with a 3.3 V USB-to-TTL adapter.

## Follow-up

After hardware UART confirmation, a second revision will add a bounded UART test window and execute `SET_ICE_DAT_PF0()` and `SET_ICE_CLK_PF1()` to return the pins to SWD. That revision will be tested by attaching the WCH-Link after the return window.
