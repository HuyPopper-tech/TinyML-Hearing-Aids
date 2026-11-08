"""Board owns NPU address policy, including whole-range and overflow checks."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BoardNpuMemoryTests(unittest.TestCase):
    def test_entire_range_must_fit_accessible_sram(self):
        vendor = '''#pragma once
        #define FMC0_MODULE 0
        #define NPU0_MODULE 1
        #define SYS_NPURST 2
        #define NPU_IRQn 3
        #define NPU_BASE 0
        #define SYS_UnlockReg() ((void)0)
        #define SYS_LockReg() ((void)0)
        #define CLK_EnableModuleClock(x) ((void)(x))
        #define SYS_ResetModule(x) ((void)(x))
        #define NVIC_DisableIRQ(x) ((void)(x))
        #define NVIC_ClearPendingIRQ(x) ((void)(x))
        #define NVIC_SetPriority(x,y) ((void)(x),(void)(y))
        #define NVIC_EnableIRQ(x) ((void)(x))
        '''
        harness = '''#include <stdint.h>
        #include "board/m55m1_ethosu_board.h"
        void m55m1_timebase_init(void) {}
        uint32_t m55m1_timebase_cycles_now(void) { return 0; }
        uint32_t m55m1_timebase_cycles_elapsed(uint32_t a, uint32_t b) { return b-a; }
        int main(void) {
            if (!m55m1_ethosu_board_memory_is_accessible((void *)0x20100000u, 622128)) { return 1; }
            if (!m55m1_ethosu_board_memory_is_accessible((void *)0x2022ffffu, 1)) { return 2; }
            if (m55m1_ethosu_board_memory_is_accessible((void *)0x2022ffffu, 2) ||
                m55m1_ethosu_board_memory_is_accessible((void *)0x20230000u, 1) ||
                m55m1_ethosu_board_memory_is_accessible((void *)0x200fffffu, 1) ||
                m55m1_ethosu_board_memory_is_accessible((void *)0x20100000u, SIZE_MAX) ||
                m55m1_ethosu_board_memory_is_accessible((void *)UINTPTR_MAX, 2) ||
                m55m1_ethosu_board_memory_is_accessible((void *)0x20100000u, 0) ||
                m55m1_ethosu_board_memory_is_accessible(0, 1)) { return 3; }
            return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            (temporary / 'NuMicro.h').write_text(vendor)
            source = temporary / 'memory.c'
            source.write_text(harness)
            executable = temporary / 'memory.exe'
            build = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(temporary), '-I', str(ROOT), str(source),
                                    str(ROOT / 'board/m55m1_ethosu_board.c'), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)
