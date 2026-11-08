"""Host behavior and build isolation for the custom-PCB product capture."""

from pathlib import Path
import json
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CustomCaptureTests(unittest.TestCase):
    def test_product_capture_start_preserves_custom_pins_and_order(self):
        board = (ROOT / "board/m55m1_audio.c").read_text()
        clock_config = board[board.index("#define M55M1_AUDIO_CORE_HZ"):
                             board.index("/* TLV320DAC3100")]
        start = board[board.index("bool m55m1_audio_dmic_prepare("):
                      board.index("void m55m1_audio_dmic_stop(")]
        harness = textwrap.dedent(r'''
            #include <assert.h>
            #include <stdbool.h>
            #include <stdint.h>
            #include <stdlib.h>
            #define TEST_WARMUP 0
            #define M55M1_AUDIO_PROFILE_16KHZ 0
            #define M55M1_AUDIO_LPPDMA_CH 0
            #define M55M1_AUDIO_MAX_SAMPLES_PER_BLOCK 16U
            #define CLK_SRCCTL_HXTEN_Msk 1
            #define CLK_STATUS_HXTSTB_Msk 1
            #define CLK_STATUS_APLL1STB_Msk 128
            #define CLK_APLLCTL_APLLSRC_HXT 0
            #define DMIC_APLL1_FREQ_196608KHZ 196608000
            #define CLK_APLL1_SELECT 1
            #define DMIC0_MODULE 1
            #define LPPDMA0_MODULE 2
            #define GPIOA_MODULE 3
            #define CLK_DMICSEL_DMIC0SEL_APLL1_DIV2 1
            #define CLK_DMICDIV_DMIC0DIV(n) ((n) - 1)
            #define SYS_DMIC0RST 7
            #define DMIC_FIFOWIDTH_16 0
            #define DMIC_CTL_CHEN2_Msk 4
            #define DMIC_CTL_SWRST_Msk (1u << 24)
            #define DMIC_DOWNSAMPLE_128 1
            #define DMIC_DOWNSAMPLE_64 0
            #define DMIC_LATCHDATA_CH23RF 512
            #define LPPDMA_DMIC0_RX 1
            #define LPPDMA_OP_SCATTER 2
            #define LPPDMA_INT_TRANS_DONE 1
            #define LPPDMA_IRQn 8
            typedef int m55m1_audio_profile_t;
            typedef struct { uint16_t samples_per_block; } m55m1_audio_dmic_state_t;
            static m55m1_audio_dmic_state_t g_dmic_state;
            static unsigned g_dmic_descriptors[2];
            static bool g_clock_snapshot_valid = true;
            static struct { unsigned STATUS; } clock_regs = {128};
            static struct { unsigned CTL; bool empty; } dmic_regs;
            #define CLK (&clock_regs)
            #define DMIC0 (&dmic_regs)
            #define LPPDMA 0
            static unsigned scenario, resets, dsp_resets, clears, waited, descriptors, pins;
            static bool unlocked, channel, dma_open, irq, requests, closed;
            #define SYS_UnlockReg() (unlocked = true)
            #define SYS_LockReg() (unlocked = false)
            #define CLK_EnableXtalRC(mask) ((void)(mask))
            #define CLK_WaitClockReady(mask) ((void)(mask), 1)
            #define CLK_EnableModuleClock(module) ((void)(module))
            #define CLK_EnableAPLL(src, hz, pll) ((void)(src), (void)(pll), (hz))
            #define CLK_SetModuleClock(module, src, div) ((void)(module), (void)(src), (void)(div))
            #define SET_DMIC1_CLK_PA1() (pins |= 1)
            #define SET_DMIC1_DAT_PA2() (pins |= 2)
            #define SET_DMIC1_CLK_PB2() (pins |= 4)
            #define SET_DMIC1_DAT_PB3() (pins |= 8)
            #define DMIC_SetFIFOWidth(d, width) ((void)(d), (void)(width))
            #define DMIC_SetDSPGainVolume(d, ch, gain) \
                assert((d) == DMIC0 && (ch) == 4 && (gain) == M55M1_DMIC_GAIN_DB)
            #define DMIC_SET_DOWNSAMPLE(d, osr) assert((d) == DMIC0 && (osr) == 1)
            #define DMIC_SET_LATCHEDGE_CH23(d, edge) assert((d) == DMIC0 && (edge) == 512)
            #define DMIC_IS_FIFOEMPTY(d) ((d)->empty)
            #define DMIC_Close(d) ((void)(d), channel = false, closed = true)
            static uint32_t m55m1_audio_profile_sample_rate_hz(int p) {
                return p == 0 ? 16000 : 0;
            }
            static uint16_t m55m1_audio_profile_samples_per_block(int p) {
                return p == 0 ? 16 : 0;
            }
            void SYS_ResetModule(unsigned module) {
                assert(unlocked && module == SYS_DMIC0RST);
                ++resets; dmic_regs.CTL = 0;
            }
            static void DMIC_Open(void *d) {
                assert(d == DMIC0 && resets == (TEST_WARMUP ? 1u : 0u));
                assert(pins == 3u);
            }
            static uint32_t DMIC_SetSampleRate(void *d, unsigned fs) {
                assert(d == DMIC0 && fs == 16000); return scenario == 4 ? 0 : fs;
            }
            void DMIC_ClearFIFO(void *d) {
                assert(d == DMIC0 && !dma_open && !irq && !requests);
                ++clears;
                dmic_regs.empty = !((scenario == 1 && clears == 1) ||
                                    (scenario == 3 && clears == 2));
                if (clears == 2 && TEST_WARMUP) { assert(channel && waited >= 10000); }
            }
            void DMIC_ResetDSP(void *d) {
                assert(d == DMIC0 && !channel && clears == 1);
                ++dsp_resets;
                dmic_regs.CTL = scenario == 2 ? DMIC_CTL_SWRST_Msk : 0;
            }
            static void DMIC_EnableChMsk(void *d, unsigned mask) {
                assert(d == DMIC0 && mask == 4);
                if (dma_open) {
                    assert(irq && descriptors == 2 && requests);
                } else if (TEST_WARMUP) {
                    assert(dsp_resets == 1 && !dma_open);
                } else {
                    assert(dsp_resets == 0 && dma_open && irq && descriptors == 2);
                }
                channel = true;
            }
            void m55m1_timebase_delay_us(unsigned us) {
                assert(channel && !dma_open && !requests); waited += us;
            }
            static void m55m1_audio_configure_dmic_descriptor(uint8_t slot) {
                if (TEST_WARMUP) {
                    assert(!channel && waited >= 10000 && clears == 3 && dmic_regs.empty);
                } else {
                    assert(!channel && waited == 0 && clears == 1);
                }
                assert(slot < 2 && g_dmic_state.samples_per_block == 16); ++descriptors;
            }
            #define LPPDMA_Open(d, mask) ((void)(d), (void)(mask), dma_open = true)
            #define LPPDMA_SetTransferMode(d, ch, req, mode, next) \
                ((void)(d), (void)(ch), (void)(req), (void)(mode), (void)(next))
            #define LPPDMA_EnableInt(d, ch, mode) ((void)(d), (void)(ch), (void)(mode))
            #define NVIC_SetPriority(n, p) ((void)(n), (void)(p))
            #define NVIC_EnableIRQ(n) ((void)(n), irq = true)
            static void enable_requests(void) {
                assert(!channel && dma_open && irq && descriptors == 2);
                assert(clears == (TEST_WARMUP ? 3u : 1u));
                requests = true;
            }
            #define DMIC_DisableChMsk(d, mask) ((void)(d), (void)(mask), channel = false)
            #define LPPDMA_CLR_TD_FLAG(d, mask) ((void)(d), (void)(mask))
            #define LPPDMA_CLR_ABORT_FLAG(d, mask) ((void)(d), (void)(mask))
            #define __DMB() ((void)0)
            #define DMIC_ENABLE_LPPDMA(d) ((void)(d), enable_requests())
        ''')
        harness += clock_config + start + textwrap.dedent(r'''
            int main(int argc, char **argv) {
                assert(M55M1_AUDIO_AUDIO_APLL_HZ == (TEST_WARMUP ? 196608000u : 475136006u));
                assert(argc == 2); scenario = (unsigned)atoi(argv[1]);
                bool ok = m55m1_audio_dmic_start(scenario == 5 ? 99 : 0);
                assert(!unlocked);
                if (scenario == 0 || (!TEST_WARMUP && scenario < 4)) {
                    assert(ok && requests && !closed);
                    assert(waited == (TEST_WARMUP ? 10000u : 0u));
                } else {
                    assert(!ok && !requests && !dma_open && !irq && !channel);
                    assert(scenario == 5 ? resets == 0 : closed);
                }
                return 0;
            }
        ''')
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "start.c"
            binary = Path(directory) / "start.exe"
            source.write_text(harness)
            compiled = subprocess.run([
                "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-pointer-to-int-cast", "-DM55M1_DMIC_GAIN_DB=30",
                str(source), "-o", str(binary),
            ], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            for scenario in range(6):
                with self.subTest(scenario=scenario):
                    result = subprocess.run([str(binary), str(scenario)],
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
