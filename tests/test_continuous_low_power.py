"""Execute product power/timing/codec C with modeled hardware, not power measurement."""
from pathlib import Path
import subprocess
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def run_c(body, sources=(), defines=()):
    with tempfile.TemporaryDirectory() as directory:
        p = Path(directory)
        (p / "NuMicro.h").write_text(body[0])
        (p / "test.c").write_text(body[1])
        command = ["gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                   "-I", str(p), "-I", str(ROOT), *defines,
                   str(p / "test.c"), *[str(ROOT / s) for s in sources],
                   "-o", str(p / "test.exe")]
        result = subprocess.run(command, capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        result = subprocess.run([str(p / "test.exe")], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr


class ContinuousLowPowerTests(unittest.TestCase):
    def test_timebase_keeps_core_units_wrap_and_external_clock_through_delays(self):
        # Reverting to CPU-clock SysTick or letting a vendor delay reset it fails.
        header = r'''
        #pragma once
        #include <stdint.h>
        #include <assert.h>
        typedef struct { uint32_t LOAD, VAL, CTRL; } systick_t;
        extern systick_t tick;
        extern uint32_t SystemCoreClock, selected, enables, unlocks;
        #define SysTick (&tick)
        #define SysTick_CTRL_ENABLE_Msk 1u
        #define SysTick_CTRL_CLKSOURCE_Msk 4u
        #define CLK_STSEL_ST0SEL_ACLK_DIV2 2u
        #define ST0_MODULE 51u
        #define SYS_IsRegLocked() (!unlocks)
        #define SYS_UnlockReg() (unlocks = 1)
        #define SYS_LockReg() (unlocks = 0)
        #define CLK_SetSysTickClockSrc(x) (selected = (x))
        #define CLK_EnableModuleClock(x) (assert((x)==51u), ++enables)
        static inline void __NOP(void) { tick.VAL = (tick.VAL - 110u) & 0xffffffu; }
        '''
        body = r'''
        #include "NuMicro.h"
        #include "board/m55m1_timebase.h"
        systick_t tick;
        uint32_t SystemCoreClock = 220000000u, selected, enables, unlocks = TEST_UNLOCKED;
        int main(void) {
            m55m1_timebase_init();
            assert(tick.LOAD == 0xffffff && tick.CTRL == 1 && selected == 2);
            assert(enables == 1 && unlocks == TEST_UNLOCKED);
            tick.VAL = 0xffffffu - 110u;
            assert(m55m1_timebase_cycles_now() == 220u);
            assert(m55m1_timebase_cycles_to_us(220000u) == 1000u);
            assert(m55m1_timebase_cycles_elapsed(0x1fffff0u, 0x10u) == 32u);
            m55m1_timebase_init();
            assert(enables == 1 && tick.VAL == 0xffffffu - 110u);
            uint32_t before = m55m1_timebase_cycles_now();
            m55m1_timebase_delay_us(800000u);
            assert(m55m1_timebase_cycles_elapsed(before,
                m55m1_timebase_cycles_now()) == ((800000u * 220u) & 0x1ffffffu));
            assert(tick.CTRL == 1 && tick.LOAD == 0xffffff && unlocks == TEST_UNLOCKED);
            return 0;
        }
        '''
        for unlocked in (0, 1):
            run_c((header, body), ["board/m55m1_timebase.c"],
                  [f"-DTEST_UNLOCKED={unlocked}"])

    def test_wait_checks_work_and_faults_with_interrupts_masked(self):
        source = (ROOT / "drivers/audio_transport/audio_transport.c").read_text()
        self.assertIn("void audio_transport_wait_for_work(void)", source)
        start = source.index("void audio_transport_wait_for_work(void)")
        end = re.search(r"audio_transport_status_t\s+audio_transport_submit_tagged\(", source[start:]).start() + start
        body = r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include "drivers/audio_transport/audio_transport.h"
        static unsigned irq_mask, ipsr, basepri, faultmask, waits, pending_irq, serviced;
        static bool capture, inject;
        static unsigned faults;
        #define __get_PRIMASK() irq_mask
        #define __get_IPSR() ipsr
        #define __get_BASEPRI() basepri
        #define __get_FAULTMASK() faultmask
        #define __disable_irq() (irq_mask = 1)
        static void __set_PRIMASK(unsigned v) {
            irq_mask = v;
            if (!v && pending_irq) { ++serviced; pending_irq = 0; capture = true; }
        }
        bool audio_transport_capture_pending(void) {
            assert(irq_mask == 1);
            if (inject) { pending_irq = 1; }
            return capture;
        }
        void audio_transport_get_health(audio_transport_health_t *h) {
            assert(irq_mask == 1);
            *h = (audio_transport_health_t){
                .dmic_overrun_blocks = faults & 1u, .dmic_dma_errors = faults & 2u,
                .dac_underrun_blocks = faults & 4u, .dac_dma_errors = faults & 8u};
        }
        void m55m1_power_idle(void) {
            assert(irq_mask == 1); ++waits;
            /* Pending NVIC interrupt wakes WFI even before PRIMASK is cleared. */
        }
        ''' + source[start:end] + r'''
        int main(void) {
            audio_transport_wait_for_work(); assert(waits == 1 && irq_mask == 0);
            capture = true; audio_transport_wait_for_work(); assert(waits == 1);
            capture = false;
            for (faults = 1; faults <= 8; faults *= 2) {
                audio_transport_wait_for_work(); assert(waits == 1);
            }
            faults = 0; inject = true;
            audio_transport_wait_for_work();
            assert(waits == 2 && serviced == 1 && capture && irq_mask == 0);
            capture = false; inject = false;
            irq_mask = 1; audio_transport_wait_for_work(); assert(waits == 2 && irq_mask == 1);
            irq_mask = 0; ipsr = 1; audio_transport_wait_for_work(); assert(waits == 2);
            ipsr = 0; basepri = 32; audio_transport_wait_for_work(); assert(waits == 2);
            basepri = 0; faultmask = 1; audio_transport_wait_for_work(); assert(waits == 2);
            return 0;
        }
        '''
        run_c(("", body))

    def test_product_clock_gates_and_shallow_idle_preserve_audio_swd_and_debug(self):
        path = ROOT / "board/m55m1_power.c"
        self.assertTrue(path.exists(), "missing board power policy")
        source = path.read_text()
        # Bind all real module constants named by the implementation to unique IDs.
        import re
        names = sorted(set(re.findall(r"\b[A-Z][A-Z0-9]+_MODULE\b", source)) |
                       {"UART2_MODULE", "I2S1_MODULE", "I2C0_MODULE", "PDMA0_MODULE",
                        "LPPDMA0_MODULE", "DMIC0_MODULE", "NPU0_MODULE", "FMC0_MODULE",
                        "GPIOF_MODULE", "SRAM0_MODULE", "ST0_MODULE"})
        header = r'''
        #pragma once
        #include <stdint.h>
        #include <assert.h>
        typedef unsigned IRQn_Type;
        typedef struct { uint32_t SCR; } scb_t;
        extern scb_t scb;
        extern unsigned gated[256], irqs[256], waits, barriers;
        #define SCB (&scb)
        #define SCB_SCR_SLEEPDEEP_Msk 4u
        #define SCB_SCR_SLEEPONEXIT_Msk 2u
        #define __DSB() (++barriers)
        #define __ISB() (++barriers)
        #define __WFI() (assert(barriers && !(scb.SCR & 6u)), ++waits)
        #define CLK_DisableModuleClock(x) (++gated[(x)])
        #define NVIC_DisableIRQ(x) (++irqs[(x)])
        #define NVIC_ClearPendingIRQ(x) ((void)(x))
        '''
        header += "\n".join(f"#define {n} {i+1}u" for i, n in enumerate(names))
        header += "\n" + "\n".join(f"#define UART{i}_IRQn {i}u" for i in range(10))
        header += "\n#define LPUART0_IRQn 10u\n"
        body = r'''
        #include "NuMicro.h"
        #include "board/m55m1_power.h"
        scb_t scb = {.SCR = 6u};
        unsigned gated[256], irqs[256], waits, barriers;
        int main(void) {
            m55m1_power_configure();
            assert(gated[UART0_MODULE] && gated[UART1_MODULE]);
            assert(gated[UART2_MODULE] == !M55M1_DEBUG_UART_ENABLED);
            assert(irqs[2] == !M55M1_DEBUG_UART_ENABLED);
            assert(gated[LPUART0_MODULE] && gated[PDMA1_MODULE] && gated[I2S0_MODULE]);
            assert(!gated[I2S1_MODULE] && !gated[I2C0_MODULE] && !gated[PDMA0_MODULE]);
            assert(!gated[LPPDMA0_MODULE] && !gated[DMIC0_MODULE]);
            assert(!gated[NPU0_MODULE] && !gated[FMC0_MODULE] && !gated[GPIOF_MODULE]);
            assert(!gated[SRAM0_MODULE] && !gated[ST0_MODULE]);
            m55m1_power_idle(); assert(waits == 1);
            return 0;
        }
        '''
        for enabled in (0, 1):
            run_c((header, body), ["board/m55m1_power.c"],
                  [f"-DM55M1_DEBUG_UART_ENABLED={enabled}"])

    def test_codec_unused_blocks_exact_register_values_and_error_propagation(self):
        source = (ROOT / "drivers/tlv320dac3100/tlv320dac3100.c").read_text()
        a = source.index("static tlv320dac3100_status_t tlv320dac3100_configure_codec(void)")
        b = source.index("\ntlv320dac3100_status_t tlv320dac3100_init(", a)
        import re
        constants = "\n".join(re.findall(r"^#define TLV320DAC3100_(?:REG_\w+|PAGE_\w+|DAC_\w+|HEADPHONE_DRIVERS_\w+)\s+[^\n]+", source, re.M))
        body = r'''
        #include <assert.h>
        #include <stdint.h>
        #include <stdbool.h>
        typedef int tlv320dac3100_status_t;
        enum { TLV320DAC3100_STATUS_OK, TLV320DAC3100_STATUS_I2C_ERROR };
        static struct { struct { int8_t max_digital_gain_db; } config; } g_context;
        static unsigned writes, fail_at, values[2][128], seen[2][128], delay;
        static uint8_t tlv320dac3100_gain_to_register(int8_t db) { return (uint8_t)(db*2); }
        static int tlv320dac3100_write_register(unsigned page, unsigned reg, unsigned v) {
            if (++writes == fail_at) { return TLV320DAC3100_STATUS_I2C_ERROR; }
            values[page][reg] = v; seen[page][reg] = 1; return 0;
        }
        static void m55m1_timebase_delay_us(unsigned us) { delay += us; }
        #define CLK_SysTickDelay m55m1_timebase_delay_us
        ''' + constants + "\n" + source[a:b] + r'''
        int main(void) {
            g_context.config.max_digital_gain_db = -3;
            assert(tlv320dac3100_configure_codec() == 0);
            assert(seen[0][5] && values[0][5] == 0x11); /* PLL off, P/R reset fields */
            assert(seen[0][67] && values[0][67] == 0); /* headset detector off */
            assert(seen[0][116] && values[0][116] == 0); /* volume ADC off */
            assert(seen[1][32] && values[1][32] == 0x06); /* Class-D reserved bits */
            assert(seen[1][46] && values[1][46] == 0); /* MICBIAS off, device awake */
            assert(values[0][60] == 7 && values[0][11] == 0x81 && values[0][12] == 0x81);
            assert(values[1][31] == 0xc4 && values[1][35] == 0x44);
            assert(values[0][63] == 0xd4 && values[0][65] == 250 && values[0][66] == 250);
            assert(delay == 801000);
            unsigned total = writes;
            for (unsigned i = 1; i <= total; ++i) {
                writes = 0; fail_at = i;
                assert(tlv320dac3100_configure_codec() == TLV320DAC3100_STATUS_I2C_ERROR);
                assert(writes == i);
            }
            return 0;
        }
        '''
        run_c(("", body))
