"""Exercise the real UART queue against a controlled UART/PDMA boundary."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DebugUartDmaTests(unittest.TestCase):
    def test_dma_preserves_inflight_bytes_wraps_and_stops_on_abort_or_stall(self):
        header = r'''
            #pragma once
            #include <stdbool.h>
            #include <stdint.h>
            #include <stddef.h>
            typedef struct { uint32_t DAT, INTEN; } UART_T;
            typedef struct { uint32_t CHCTL, td, abort; } PDMA_T;
            extern UART_T uart;
            extern PDMA_T dma;
            extern uint32_t SystemCoreClock;
            #define DEBUG_PORT (&uart)
            #define DEBUG_PORT_IRQHandler UART2_IRQHandler
            #define M55M1_DEBUG_UART_TX_PDMA (&dma)
            #define M55M1_DEBUG_UART_TX_DMA_CHANNEL 0U
            #define M55M1_DEBUG_UART_TX_DMA_REQUEST 8U
            #define NVT_NONCACHEABLE
            #define __ALIGNED(n)
            #define __DMB() ((void)0)
            #define UART_INTEN_THREIEN_Msk 1U
            #define UART_INTEN_TXPDMAEN_Msk 2U
            #define PDMA_WIDTH_8 0U
            #define PDMA_SAR_INC 0U
            #define PDMA_DAR_FIX 1U
            #define PDMA_REQ_SINGLE 1U
            #define PDMA_BURST_1 0U
            #define PDMA_INT_TRANS_DONE 1U
            #define PDMA_INT_TEMPTY 2U
            #define UART_GET_RX_EMPTY(p) 1U
            #define UART_READ(p) 0U
            #define UART_IS_TX_FULL(p) 0U
            #define UART_IS_TX_EMPTY(p) fake_uart_empty()
            #define UART_WRITE(p,v) fake_uart_write(v)
            #define PDMA_GET_TD_STS(p) ((p)->td)
            #define PDMA_GET_ABORT_STS(p) ((p)->abort)
            #define PDMA_CLR_TD_FLAG(p,m) ((p)->td &= ~(m))
            #define PDMA_CLR_ABORT_FLAG(p,m) ((p)->abort &= ~(m))
            void UART_EnableInt(UART_T *, uint32_t);
            void UART_DisableInt(UART_T *, uint32_t);
            void PDMA_Open(PDMA_T *, uint32_t);
            void PDMA_DisableInt(PDMA_T *, uint32_t, uint32_t);
            void PDMA_SetTransferCnt(PDMA_T *, uint32_t, uint32_t, uint32_t);
            void PDMA_SetTransferAddr(PDMA_T *, uint32_t, uintptr_t, uint32_t, uintptr_t, uint32_t);
            void PDMA_SetTransferMode(PDMA_T *, uint32_t, uint32_t, uint32_t, uint32_t);
            void PDMA_SetBurstType(PDMA_T *, uint32_t, uint32_t, uint32_t);
            void m55m1_debug_uart_init(void);
            void m55m1_debug_uart_enable_tx_irq(void);
            void m55m1_debug_uart_enable_tx_dma(void);
            unsigned fake_uart_empty(void);
            void fake_uart_write(uint8_t);
        '''
        harness = r'''
            #include <assert.h>
            #include <string.h>
            #include "NuMicro.h"
            #include "drivers/debug_uart_tx/debug_uart_tx.h"
            UART_T uart;
            PDMA_T dma;
            uint32_t SystemCoreClock = 100;
            static const uint8_t *source;
            static size_t count, emitted;
            static unsigned programmed, dma_clocks, irq_enables;
            static bool wire_empty = true;
            static uint8_t wire[8192];
            void m55m1_debug_uart_init(void) {}
            void m55m1_debug_uart_enable_tx_irq(void) { ++irq_enables; }
            void m55m1_debug_uart_enable_tx_dma(void) { ++dma_clocks; }
            void UART_EnableInt(UART_T *p, uint32_t mask) { p->INTEN |= mask; }
            void UART_DisableInt(UART_T *p, uint32_t mask) { p->INTEN &= ~mask; }
            void PDMA_Open(PDMA_T *p, uint32_t mask) { p->CHCTL |= mask; }
            void PDMA_DisableInt(PDMA_T *p, uint32_t ch, uint32_t mask) {
                (void)p; (void)mask; assert(ch == 0);
            }
            void PDMA_SetTransferCnt(PDMA_T *p, uint32_t ch, uint32_t width, uint32_t n) {
                (void)p; assert(ch == 0 && width == 0 && n > 0); count = n;
            }
            void PDMA_SetTransferAddr(PDMA_T *p, uint32_t ch, uintptr_t src,
                uint32_t inc, uintptr_t dst, uint32_t fixed) {
                (void)p; assert(ch == 0 && inc == 0 && fixed == 1);
                assert(dst == (uintptr_t)&uart.DAT); source = (const uint8_t *)src;
            }
            void PDMA_SetTransferMode(PDMA_T *p, uint32_t ch, uint32_t request,
                uint32_t scatter, uint32_t descriptor) {
                (void)p; assert(ch == 0 && request == 8 && !scatter && !descriptor);
                ++programmed;
            }
            void PDMA_SetBurstType(PDMA_T *p, uint32_t ch, uint32_t single, uint32_t burst) {
                (void)p; assert(ch == 0 && single == 1 && burst == 0);
            }
            unsigned fake_uart_empty(void) { return wire_empty; }
            void fake_uart_write(uint8_t v) { (void)v; assert(!"DMA path used CPU TX"); }
            static void complete(void) {
                assert(uart.INTEN & UART_INTEN_TXPDMAEN_Msk);
                assert(count > 0 && count <= 2048 && emitted + count <= sizeof(wire));
                memcpy(wire + emitted, source, count); emitted += count;
                dma.td = 1;
                debug_uart_tx_poll();
            }
            int main(int argc, char **argv) {
                (void)argv;
                debug_uart_tx_init();
                assert(dma_clocks == 1 && irq_enables == 0);
                assert(!(uart.INTEN & UART_INTEN_THREIEN_Msk));
                assert(!debug_uart_tx_try_enqueue(NULL, 1));
                if (argc > 1) {
                    assert(debug_uart_tx_try_enqueue("stalled", 7));
                    if (argc > 2) {
                        dma.abort = 1;
                        debug_uart_tx_poll();
                    } else {
                        debug_uart_tx_flush_blocking();
                    }
                    assert(debug_uart_tx_dma_error_count() == 1);
                    assert(!(uart.INTEN & UART_INTEN_TXPDMAEN_Msk));
                    assert(!debug_uart_tx_try_enqueue("retry", 5));
                    assert(programmed == 1 && emitted == 0);
                    return 0;
                }
                assert(debug_uart_tx_try_enqueue("hello", 5));
                assert(count == 5 && programmed == 1 && !memcmp(source, "hello", 5));
                assert(debug_uart_tx_try_enqueue(" world", 6));
                assert(programmed == 1 && !memcmp(source, "hello", 5));
                complete(); assert(count == 6 && programmed == 2);
                complete(); assert(emitted == 11 && !memcmp(wire, "hello world", 11));
                uint8_t block[2048]; memset(block, 'x', sizeof(block));
                assert(debug_uart_tx_try_enqueue((const char *)block, sizeof(block)));
                assert(!debug_uart_tx_try_enqueue("overflow", 8));
                complete(); complete();
                assert(emitted == 2059);
                for (size_t i = 11; i < emitted; ++i) assert(wire[i] == 'x');
                assert(debug_uart_tx_try_enqueue("end", 3));
                complete(); wire_empty = true;
                debug_uart_tx_flush_blocking();
                assert(emitted == 2062 && !memcmp(wire + 2059, "end", 3));
                assert(debug_uart_tx_drop_count() == 2);
                assert(debug_uart_tx_dma_error_count() == 0);
                assert(!(uart.INTEN & UART_INTEN_TXPDMAEN_Msk));
                /* A drained DMA queue is not a guarantee the UART stop bit left. */
                wire_empty = false;
                debug_uart_tx_flush_blocking();
                assert(debug_uart_tx_dma_error_count() == 1);
                return 0;
            }
        '''
        with tempfile.TemporaryDirectory() as directory:
            temp = Path(directory)
            (temp / "board").mkdir()
            (temp / "NuMicro.h").write_text(textwrap.dedent(header), encoding="utf-8")
            (temp / "board/m55m1_debug_uart.h").write_text('#include "NuMicro.h"\n')
            source = temp / "uart.c"
            source.write_text(textwrap.dedent(harness), encoding="utf-8")
            executable = temp / "uart.exe"
            compiled = subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-DM55M1_CUSTOM_DMIC_TEST=1", "-I", str(temp), "-I", str(ROOT),
                str(source), str(ROOT / "drivers/debug_uart_tx/debug_uart_tx.c"),
                str(ROOT / "common/fixed_byte_ring.c"), "-o", str(executable),
            ], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            for args in ([], ["stall"], ["abort", "now"]):
                with self.subTest(args=args):
                    result = subprocess.run([str(executable), *args], capture_output=True,
                                            text=True, timeout=5)
                    self.assertEqual(result.returncode, 0, result.stderr)
