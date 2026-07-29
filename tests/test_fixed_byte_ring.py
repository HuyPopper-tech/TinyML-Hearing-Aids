"""Host tests for the foreground-producer/UART-ISR-consumer byte ring."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FixedByteRingTests(unittest.TestCase):
    def test_enqueue_is_all_or_drop_and_wraps_without_reordering(self):
        harness = textwrap.dedent(
            r"""
            #include <stdint.h>
            #include <string.h>

            #include "common/fixed_byte_ring.h"

            int main(void)
            {
                uint8_t storage[8];
                fixed_byte_ring_t ring;
                uint8_t value;
                const uint8_t first[] = {1, 2, 3, 4, 5, 6};
                const uint8_t rejected[] = {7, 8, 9};
                const uint8_t wrapped[] = {10, 11, 12, 13};
                const uint8_t expected[] = {4, 5, 6, 10, 11, 12, 13};
                unsigned int index;

                fixed_byte_ring_init(&ring, storage, sizeof(storage));
                if (!fixed_byte_ring_try_enqueue(&ring, first, sizeof(first))) return 1;
                if (fixed_byte_ring_try_enqueue(&ring, rejected, sizeof(rejected))) return 2;
                if (fixed_byte_ring_count(&ring) != sizeof(first)) return 3;

                for (index = 0; index < 3; ++index) {
                    if (!fixed_byte_ring_try_pop(&ring, &value) || value != first[index]) return 4;
                }
                if (!fixed_byte_ring_try_enqueue(&ring, wrapped, sizeof(wrapped))) return 5;
                for (index = 0; index < sizeof(expected); ++index) {
                    if (!fixed_byte_ring_try_pop(&ring, &value) || value != expected[index]) return 6;
                }
                if (fixed_byte_ring_try_pop(&ring, &value)) return 7;
                return 0;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temp_dir:
            temp = Path(temp_dir)
            source = temp / "ring_test.c"
            executable = temp / "ring_test.exe"
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
                    str(ROOT / "common" / "fixed_byte_ring.c"),
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
