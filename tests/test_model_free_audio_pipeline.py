"""Host contract test for the model-free PCM16 audio service."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ModelFreeAudioPipelineTests(unittest.TestCase):
    def test_passthrough_preserves_pcm16_values_and_overlap(self):
        harness = textwrap.dedent(
            r"""
            #include <stdint.h>
            #include <string.h>

            #include "services/enhancement_passthrough/enhancement_passthrough.h"

            int main(void)
            {
                const int16_t input[] = {0, 1, -1, 32767, -32768, 1234, -2345};
                int16_t output[sizeof(input) / sizeof(input[0])] = {0};
                int16_t overlap[] = {10, 20, 30, 40, 50};

                if (!enhancement_passthrough_process(input, output,
                                                     sizeof(input) / sizeof(input[0]))) {
                    return 1;
                }
                if (memcmp(input, output, sizeof(input)) != 0) {
                    return 2;
                }
                if (!enhancement_passthrough_process(NULL, output, 0U)) {
                    return 3;
                }
                if (enhancement_passthrough_process(NULL, output, 1U)) {
                    return 4;
                }
                if (!enhancement_passthrough_process(overlap, overlap + 1, 4U)) {
                    return 5;
                }
                if (overlap[1] != 10 || overlap[2] != 20 || overlap[3] != 30 ||
                    overlap[4] != 40) {
                    return 6;
                }
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            source = temp / "passthrough_test.c"
            executable = temp / "passthrough_test.exe"
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
                    str(ROOT / "services" / "enhancement_passthrough" /
                        "enhancement_passthrough.c"),
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
