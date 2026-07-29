"""Host behavior test for WOLA source-hop timestamp association."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ErbGruStreamTimingTests(unittest.TestCase):
    def test_output_uses_previous_capture_tag_and_reset_discards_pending_tag(self):
        harness = textwrap.dedent(
            r"""
            #include <stdint.h>

            #include "services/erb_gru/erb_gru_stream_timing.h"

            int main(void)
            {
                erb_gru_stream_timing_t timing;
                erb_gru_source_tag_t output;

                erb_gru_stream_timing_reset(&timing);
                if (erb_gru_stream_timing_advance(&timing, 10U, 1000U, &output)) return 1;
                if (!erb_gru_stream_timing_advance(&timing, 11U, 2000U, &output)) return 2;
                if (output.sequence != 10U || output.capture_completed_cycle != 1000U) return 3;
                if (!erb_gru_stream_timing_advance(&timing, 12U, 3000U, &output)) return 4;
                if (output.sequence != 11U || output.capture_completed_cycle != 2000U) return 5;
                erb_gru_stream_timing_reset(&timing);
                return erb_gru_stream_timing_advance(&timing, 20U, 4000U, &output) ? 6 : 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "stream_timing_test.c"
            executable = temporary / "stream_timing_test.exe"
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
                    str(
                        ROOT
                        / "services"
                        / "erb_gru"
                        / "erb_gru_stream_timing.c"
                    ),
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


if __name__ == "__main__":
    unittest.main()
