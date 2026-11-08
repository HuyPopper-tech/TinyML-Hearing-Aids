"""Host tests for the foreground-producer/UART-ISR-consumer byte ring."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class FixedByteRingTests(unittest.TestCase):
    def test_spsc_publication_uses_acquire_release_operations(self):
        """Catch a producer publication that can expose unwritten bytes."""
        header = (ROOT / "common" / "fixed_byte_ring.h").read_text(encoding="utf-8")
        source = (ROOT / "common" / "fixed_byte_ring.c").read_text(encoding="utf-8")

        self.assertNotIn("volatile uint32_t head", header)
        self.assertNotIn("volatile uint32_t tail", header)
        self.assertIn("__atomic_load_n((pointer), __ATOMIC_ACQUIRE)", source)
        self.assertIn("__atomic_store_n((pointer), (value), __ATOMIC_RELEASE)", source)

        enqueue = source[source.index("bool fixed_byte_ring_try_enqueue"):source.index("bool fixed_byte_ring_try_pop")]
        self.assertLess(
            enqueue.index("ring->storage[(head + (uint32_t)index)"),
            enqueue.index("FIXED_BYTE_RING_STORE_RELEASE(&ring->head"),
        )
        self.assertLess(
            enqueue.index("FIXED_BYTE_RING_LOAD_ACQUIRE(&ring->tail)"),
            enqueue.index("ring->storage[(head + (uint32_t)index)"),
        )

        pop = source[source.index("bool fixed_byte_ring_try_pop"):]
        self.assertLess(
            pop.index("FIXED_BYTE_RING_LOAD_ACQUIRE(&ring->head)"),
            pop.index("*value = ring->storage[tail % ring->capacity]"),
        )
        self.assertLess(
            pop.index("*value = ring->storage[tail % ring->capacity]"),
            pop.index("FIXED_BYTE_RING_STORE_RELEASE(&ring->tail"),
        )

    def test_enqueue_is_all_or_drop_and_wraps_without_reordering(self):
        harness = textwrap.dedent(
            r"""
            #include <stdint.h>
            #include <string.h>
            #include <assert.h>

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

                /* DMA must retain unread storage until completion, including wrap. */
                fixed_byte_ring_init(&ring, storage, sizeof(storage));
                assert(fixed_byte_ring_try_enqueue(&ring, first, sizeof(first)));
                size_t length = 0;
                const uint8_t *span = fixed_byte_ring_peek(&ring, &length);
                assert(length == 6 && memcmp(span, first, 6) == 0);
                assert(!fixed_byte_ring_try_enqueue(&ring, rejected, 3));
                assert(!fixed_byte_ring_consume(&ring, 7));
                assert(fixed_byte_ring_consume(&ring, 3));
                assert(fixed_byte_ring_try_enqueue(&ring, wrapped, 4));
                span = fixed_byte_ring_peek(&ring, &length);
                const uint8_t end[] = {4, 5, 6, 10, 11};
                assert(length == 5 && memcmp(span, end, 5) == 0);
                assert(fixed_byte_ring_consume(&ring, length));
                span = fixed_byte_ring_peek(&ring, &length);
                assert(length == 2 && span[0] == 12 && span[1] == 13);
                assert(fixed_byte_ring_consume(&ring, length));
                assert(fixed_byte_ring_peek(&ring, &length) == NULL && length == 0);
                assert(fixed_byte_ring_peek(NULL, &length) == NULL && length == 0);
                assert(fixed_byte_ring_peek(&ring, NULL) == NULL);
                assert(!fixed_byte_ring_consume(NULL, 1));
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
