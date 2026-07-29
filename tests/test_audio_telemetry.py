"""Host test for bounded, deferred, rate-limited telemetry formatting."""

from pathlib import Path
import subprocess
import tempfile
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class AudioTelemetryTests(unittest.TestCase):
    def test_snapshot_waits_for_500_frames_and_no_pending_dmic(self):
        harness = textwrap.dedent(
            r"""
            #include <stdbool.h>
            #include <stddef.h>
            #include <stdint.h>
            #include <string.h>
            #include "services/audio_telemetry.h"

            static unsigned int messages;
            static char last_message[400];

            void debug_uart_tx_init(void) {}
            uint32_t debug_uart_tx_drop_count(void) { return 7U; }
            bool debug_uart_tx_try_enqueue(const char *message, size_t length)
            {
                if (length >= sizeof(last_message)) return false;
                memcpy(last_message, message, length);
                last_message[length] = '\0';
                ++messages;
                return true;
            }

            int main(void)
            {
                audio_telemetry_snapshot_t snapshot = {0};
                audio_telemetry_init();
                for (unsigned int i = 0; i < 500U; ++i) {
                    audio_telemetry_record_processing_us(1000U + i);
                }
                snapshot.frame_count = 499U;
                audio_telemetry_poll(&snapshot, false);
                if (messages != 0U) return 1;
                snapshot.frame_count = 500U;
                audio_telemetry_poll(&snapshot, true);
                if (messages != 0U) return 2;
                audio_telemetry_poll(&snapshot, false);
                if (messages != 1U) return 3;
                if (!strstr(last_message, "p99_us=") ||
                    !strstr(last_message, "telemetry_drop=7")) return 4;
                audio_telemetry_poll(&snapshot, false);
                return (messages == 1U) ? 0 : 5;
            }
            """
        )
        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary = Path(temporary_directory)
            source = temporary / "telemetry_test.c"
            executable = temporary / "telemetry_test.exe"
            source.write_text(harness, encoding="utf-8")
            result = subprocess.run(
                [
                    "gcc",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-I",
                    str(ROOT),
                    str(source),
                    str(ROOT / "services" / "audio_telemetry.c"),
                    "-o",
                    str(executable),
                ],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run(
                [str(executable)], capture_output=True, text=True, check=False
            )
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == "__main__":
    unittest.main()
