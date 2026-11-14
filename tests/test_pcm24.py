"""PCM24 signed FIFO decoding, normalization and explicit PCM16 export."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class Pcm24Tests(unittest.TestCase):
    def test_dma32_descriptor_and_foreground_signed_capture(self):
        board = (ROOT / 'board/m55m1_audio.c').read_text()
        descriptor = board[board.index('static void m55m1_audio_configure_dmic_descriptor('):
                           board.index('m55m1_audio_board_init_result_t m55m1_audio_board_init(')]
        take = board[board.index('bool m55m1_audio_dmic_take_block_timed('):
                     board.index('void m55m1_audio_dmic_get_status(')]
        code = r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stddef.h>
        #include <stdint.h>
        #include "common/audio_pcm.h"
        #define M55M1_AUDIO_LPPDMA_CH 0
        #define LPPDMA_OP_SCATTER 1u
        #define LPPDMA_REQ_SINGLE 2u
        #define LPPDMA_WIDTH_32 8u
        #define LPPDMA_SAR_FIX 16u
        #define LPPDMA_DAR_INC 32u
        #define LPPDMA_TBINTDIS_ENABLE 64u
        #define LPPDMA_DSCT_CTL_TXCNT_Pos 16
        typedef struct { uint32_t CTL, SA, DA, NEXT; } descriptor_t;
        typedef struct { uint32_t sequence, completed_cycle; } m55m1_audio_dmic_block_timing_t;
        static struct {
            bool block_ready;
            uint8_t completed_slot;
            uint16_t samples_per_block;
            uint32_t completed_blocks, last_completed_cycle, overrun_blocks;
        } g_dmic_state;
        static uint32_t g_dmic_buffers[2][16];
        static descriptor_t g_dmic_descriptors[2];
        static struct { uint32_t FIFO; } dmic;
        #define DMIC0 (&dmic)
        static unsigned masked, requests, busy, pending;
        #define __get_PRIMASK() masked
        #define __disable_irq() (masked = 1)
        #define __set_PRIMASK(p) (masked = (p))
        #define __DSB() ((void)0)
        #define __DMB() ((void)0)
        #define DMIC_DISABLE_LPPDMA(d) ((void)(d), requests = 0)
        #define DMIC_ENABLE_LPPDMA(d) ((void)(d), requests = 1)
        #define LPPDMA_IS_CH_BUSY(d, ch) busy
        #define LPPDMA_GET_TD_STS(d) pending
        ''' + descriptor + take + r'''
        int main(void) {
            g_dmic_state.samples_per_block = 16;
            m55m1_audio_configure_dmic_descriptor(0);
            assert((g_dmic_descriptors[0].CTL & 8u) != 0);
            assert((g_dmic_descriptors[0].CTL >> 16) == 15);
            assert(g_dmic_descriptors[0].DA == (uint32_t)(uintptr_t)g_dmic_buffers[0]);
            assert(g_dmic_descriptors[0].NEXT == (uint32_t)(uintptr_t)&g_dmic_descriptors[1]);
            const uint32_t words[] = {0, 1, 0x7fffff, 0x800000, 0xff800000,
                                      0xffffff, 0xffffffff, 0xab0000ff};
            const int32_t decoded[] = {0, 1, 8388607, -8388608, -8388608, -1, -1, 255};
            for (unsigned i = 0; i < 16; ++i) { g_dmic_buffers[1][i] = words[i % 8]; }
            g_dmic_state.completed_slot = 1;
            g_dmic_state.completed_blocks = 42;
            g_dmic_state.last_completed_cycle = 1234;
            g_dmic_state.block_ready = true;
            requests = 1;
            int32_t out[16];
            m55m1_audio_dmic_block_timing_t timing = {0};
            assert(!m55m1_audio_dmic_take_block_timed(NULL, 16, &timing));
            assert(!m55m1_audio_dmic_take_block_timed(out, 15, &timing));
            assert(m55m1_audio_dmic_take_block_timed(out, 16, &timing));
            for (unsigned i = 0; i < 16; ++i) { assert(out[i] == decoded[i % 8]); }
            assert(timing.sequence == 42 && timing.completed_cycle == 1234);
            assert(!masked && requests && !g_dmic_state.block_ready);
            assert(!m55m1_audio_dmic_take_block_timed(out, 16, &timing));
            assert(!masked && requests);
            g_dmic_state.block_ready = true;
            busy = 1;
            out[0] = 99;
            assert(!m55m1_audio_dmic_take_block_timed(out, 16, &timing));
            assert(out[0] == 99 && !requests && !masked && g_dmic_state.overrun_blocks == 1);
            return 0;
        }
        '''
        self.compile_and_run(code)

    def test_fifo_sign_normalization_and_export(self):
        code = r'''
        #include <assert.h>
        #include "common/audio_pcm.h"
        int main(void) {
            assert(audio_pcm24_decode(0) == 0);
            assert(audio_pcm24_decode(0x007fffff) == 8388607);
            assert(audio_pcm24_decode(0x00800000) == -8388608);
            assert(audio_pcm24_decode(0xff800000) == -8388608);
            assert(audio_pcm24_decode(0x00ffffff) == -1);
            assert(audio_pcm24_decode(0xffffffff) == -1);
            assert(audio_pcm24_to_float(-8388608) == -1.0f);
            assert(audio_pcm24_to_float(1) == 1.0f / 8388608.0f);
            assert(audio_pcm24_to_float(8388607) < 1.0f);
            for (int32_t x = -32768; x <= 32767; ++x) {
                assert(audio_pcm24_to_float(x * 256) == (float)x / 32768.0f);
                assert(audio_pcm24_to_pcm16(x * 256) == x);
            }
            assert(audio_pcm24_to_pcm16(-1) == -1);
            assert(audio_pcm24_to_pcm16(255) == 0);
            assert(audio_pcm24_to_pcm16(8388607) == 32767);
            return 0;
        }
        '''
        self.compile_and_run(code)

    def compile_and_run(self, code):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'pcm24.c'
            binary = Path(directory) / 'pcm24.exe'
            source.write_text(code)
            built = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT), str(source), '-o', str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
