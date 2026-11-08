# Continuous audio: approved low-power design

User approved approach 1 and immediate implementation on 2026-11-08. Model runs
every 16-sample hop at 16 kHz on the custom M55M1 PCB, core 220 MHz. No VAD gating,
frequency/voltage change, model/DSP change, flash, or board operation. Light route,
single implementer, no agents, commits, or vendored edits. This explicit execution
instruction supersedes skill review/commit/worktree prompts.

## Source findings

- M55M1 datasheet Rev1.00 pp95-98,102: Idle stops CPU only; ACLK, PLLs and peripheral
  clocks remain available. Deep power-down stops the audio PLLs and is excluded.
- M55M1 p171 and `libs/StdDriver/inc/lppdma.h`: DMIC is a LPPDMA request, I2S is not.
  Capture already uses LPPDMA0 ch0; I2S1 playback stays on PDMA0 ch0.
- CLK STSEL register documentation: external SysTick can use ACLK/2, with ST0CKEN.
  Use this source so WFI/WFE does not hide elapsed time. Return twice the 24-bit
  count as core-cycle units (25-bit modular counter, 2-cycle resolution).
- Vendor CLK_SysTickDelay reprograms externally clocked SysTick. Codec delays must
  instead use the board-owned timebase, in bounded chunks across counter wraps.
- TLV320DAC3100 SLAS671C pp21,46,66,76,79,81,85: retain PRB_P7, direct 256Fs MCLK,
  stereo headphone drive, depop and gain; explicitly keep PLL, headset detector,
  volume ADC, Class-D and MICBIAS off, preserving reserved bits. These blocks were
  already off after reset: no incremental codec current saving is asserted.
- MP34DT05 DocID029902 Rev1 pp6-7: normal clock 1.2-3.25 MHz, startup up to10ms;
  retain continuous 2.048 MHz PDM, no inter-hop microphone power cycling.

Primary URLs: [MCU](https://www.nuvoton.com/export/resource-files/en-us--DS_M55M1_Series_EN_Rev1.00.pdf),
[power AN0082](https://www.nuvoton.com.cn/export/resource-files/en-us--AN_0082_M55M1_M5531_Power_Management_EN_Rev1.00.pdf),
[DAC](https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf),
[microphone](https://www.st.com/resource/en/datasheet/mp34dt05.pdf).
Local PDFs in docs/ are the inspected component evidence; hashes/excerpts go in
the timestamped diagnostics receipt.

## Ownership and scope

`app/main.c` calls service poll then wait. Service waits only while running.
Transport masks interrupts, rechecks capture and both DMA health counters, then
calls board shallow-idle and restores PRIMASK. An IRQ arriving between check and
WFI remains pending and wakes WFI, then runs after unmask. Never wait in ISR or
an already-masked caller. NPU's existing WFE semaphore remains the single invoke
wait path. Board owns SCR, unused clock policy and timing. No history/state/DMA
buffer ownership changes. Release has no debug UART clock, IRQ or linked console;
Debug retains UART2 PC4/PC5. Opt-in UART diagnostics remain explicitly separate.
Keep SWD PF0/PF1, GPIO pin mux, clocks for audio/NPU/SRAM/FMC and watchdogs intact.
Only disable named unused peripheral gates, never an entire clock register/domain.

## Acceptance

1. Native C tests: idle pending work/fault/IRQ race/masked/ISR cases; shallow SCR
   and peripheral allowlist; Release UART off and Debug UART2 retained.
2. Native timebase test: external source, core units, wrap, conversion, idempotent
   init and long delay. Codec register script exact values/reserved bits and write
   error propagation. Existing numerical, schedule and recovery tests pass.
3. Debug and Release configure/build/link using existing two script profiles;
   maps and model identity retained; Release console code absent.
4. Document current/energy, sleep residency, wake jitter, full-hop deadline and
   audio continuity as NOT_RUN until measured on the board. Do not claim a percent
   saving from source or builds. Preserve baseline images for matched Release A/B.
5. Keep source model/package/libs immutable and deployment_approved=false; no flash.
