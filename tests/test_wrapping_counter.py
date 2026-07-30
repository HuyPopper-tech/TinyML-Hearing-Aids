"""Host and source-contract tests for wrap-safe firmware cycle timing."""

from pathlib import Path
import re
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class WrappingCounterTests(unittest.TestCase):
    def test_elapsed_cycles_handles_24_bit_wrap(self):
        harness = textwrap.dedent(
            r"""
            #include <assert.h>
            #include <stdint.h>

            #include "common/wrapping_counter.h"

            int main(void)
            {
                assert(wrapping_counter_elapsed(100U, 498U, 0x00ffffffU) == 398U);
                assert(wrapping_counter_elapsed(
                    0x00ffff00U, 0x0000008eU, 0x00ffffffU) == 398U);
                assert(wrapping_counter_elapsed(
                    0x00ffffffU, 0U, 0x00ffffffU) == 1U);
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            source = temp / "wrapping_counter_test.c"
            executable = temp / "wrapping_counter_test.exe"
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    "gcc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
                    str(source),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run_result = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(run_result.returncode, 0, run_result.stderr)

    def test_realtime_consumers_use_board_elapsed_api(self):
        timebase_header = (
            ROOT / "board" / "m55m1_timebase.h"
        ).read_text(encoding="utf-8")
        timebase_source = (
            ROOT / "board" / "m55m1_timebase.c"
        ).read_text(encoding="utf-8")
        ethosu_board_header = (
            ROOT / "board" / "m55m1_ethosu_u55.h"
        ).read_text(encoding="utf-8")
        ethosu = (ROOT / "drivers" / "ethosu_u55" / "ethosu_u55.c").read_text(
            encoding="utf-8"
        )
        inference = (
            ROOT / "services" / "erb_gru" / "erb_gru_inference.cpp"
        ).read_text(encoding="utf-8")
        pipeline = (
            ROOT / "services" / "audio_pipeline" / "audio_pipeline.c"
        ).read_text(encoding="utf-8")

        self.assertIn(
            "m55m1_timebase_cycles_elapsed", timebase_header
        )
        self.assertIn("wrapping_counter_elapsed(", timebase_source)
        self.assertIn(
            "m55m1_ethosu_u55_board_cycles_elapsed", ethosu_board_header
        )
        self.assertRegex(
            ethosu,
            re.compile(
                r"m55m1_ethosu_u55_board_cycles_elapsed\(\s*start_cycle,",
                re.MULTILINE,
            ),
        )
        self.assertNotIn(
            "m55m1_timebase_cycles_elapsed",
            ethosu,
        )
        self.assertRegex(
            inference,
            re.compile(
                r"ethosu_u55_cycles_elapsed\(\s*start_cycle,",
                re.MULTILINE,
            ),
        )
        self.assertRegex(
            pipeline,
            re.compile(
                r"audio_transport_cycles_elapsed\(\s*"
                r"enhance_start,\s*audio_transport_cycles_now\(\)",
                re.MULTILINE,
            ),
        )


if __name__ == "__main__":
    unittest.main()
