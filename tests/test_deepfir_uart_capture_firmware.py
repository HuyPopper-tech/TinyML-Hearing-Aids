"""Finite firmware capture must pair PCM and defer UART until DMA stops."""

from pathlib import Path
import subprocess
import textwrap
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirUartCaptureFirmwareTests(unittest.TestCase):
    def test_capture_sends_paired_pcm_after_stop_and_fails_closed(self):
        harness = textwrap.dedent(r"""
            #include <stdbool.h>
            #include <stddef.h>
            #include <stdint.h>
            #include <stdio.h>
            #include <stdlib.h>
            #include <string.h>
            #include "drivers/audio_transport/audio_transport.h"
            #include "services/deepfir/deepfir_inference_service.h"
            #include "services/audio_pipeline/audio_pipeline.h"

            #define PACKET_CAPACITY 66000U
            static uint8_t packet[PACKET_CAPACITY];
            static uint32_t frame;
            static uint32_t stops;
            static uint32_t flushes;
            static uint32_t early_uart;
            static uint32_t max_uart_chunk;
            static uint32_t packet_length;
            static bool fail_inference;
            static bool command_sent;

            static uint16_t read_u16(const uint8_t *bytes)
            {
                return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U);
            }

            static uint32_t read_u32(const uint8_t *bytes)
            {
                return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8U) |
                       ((uint32_t)bytes[2] << 16U) | ((uint32_t)bytes[3] << 24U);
            }

            static uint32_t crc32(const uint8_t *bytes, uint32_t count)
            {
                uint32_t crc = 0xFFFFFFFFU;
                for (uint32_t index = 0U; index < count; ++index) {
                    crc ^= bytes[index];
                    for (uint32_t bit = 0U; bit < 8U; ++bit) {
                        crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
                    }
                }
                return ~crc;
            }

            bool audio_transport_init(audio_transport_init_report_t *report,
                                      audio_transport_progress_callback_t callback,
                                      void *context)
            {
                (void)callback;
                (void)context;
                report->clocks_valid = true;
                return true;
            }
            audio_transport_status_t audio_transport_start(void)
            {
                return AUDIO_TRANSPORT_STATUS_OK;
            }
            void audio_transport_wait_for_work(void) {}
            void audio_transport_stop_safe(void) { ++stops; }
            bool audio_transport_take_capture(
                int32_t destination[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
                audio_transport_capture_timing_t *timing)
            {
                ++frame;
                for (uint32_t index = 0U; index < AUDIO_TRANSPORT_SAMPLES_PER_BLOCK; ++index) {
                    destination[index] = (int32_t)(frame * AUDIO_TRANSPORT_SAMPLES_PER_BLOCK + index) * 256;
                }
                timing->sequence = frame;
                timing->completed_cycle = frame * 1000U;
                return true;
            }
            audio_transport_status_t audio_transport_submit_tagged(
                const int16_t samples[AUDIO_TRANSPORT_SAMPLES_PER_BLOCK],
                const audio_transport_block_tag_t *tag)
            {
                (void)samples;
                (void)tag;
                return AUDIO_TRANSPORT_STATUS_OK;
            }
            void audio_transport_get_health(audio_transport_health_t *health)
            {
                memset(health, 0, sizeof(*health));
            }
            uint32_t audio_transport_cycles_now(void) { return frame * 1000U; }
            uint32_t audio_transport_cycles_elapsed(uint32_t start, uint32_t end)
            {
                return end - start;
            }
            uint32_t audio_transport_cycles_to_us(uint32_t cycles) { return cycles; }
            bool deepfir_inference_init(void) { return true; }
            void deepfir_inference_reset(void) {}
            bool deepfir_inference_process_pcm24(const int32_t *input, int16_t *output,
                                           uint32_t count)
            {
                if (fail_inference && frame == 12U) return false;
                for (uint32_t index = 0U; index < count; ++index) {
                    output[index] = (int16_t)(input[index] / 256 + 100);
                }
                return true;
            }
            void deepfir_inference_get_last_timing(deepfir_inference_timing_t *timing)
            {
                memset(timing, 0, sizeof(*timing));
            }
            void deepfir_inference_get_dsp_status(
                bool *feature, bool *fir, uint32_t *test_us)
            {
                if (feature) *feature = true;
                if (fir) *fir = true;
                if (test_us) *test_us = 0U;
            }
            void debug_uart_tx_init(void) {}
            bool debug_uart_rx_try_read(uint8_t *value)
            {
                if (!command_sent && frame >= 2U) {
                    command_sent = true;
                    *value = 'C';
                    return true;
                }
                return false;
            }
            bool debug_uart_tx_try_enqueue(const char *bytes, size_t length)
            {
                if ((length != 7U) && (length != 15U) && (stops == 0U)) {
                    ++early_uart;
                }
                if (length > max_uart_chunk) max_uart_chunk = (uint32_t)length;
                if (length > PACKET_CAPACITY - packet_length) return false;
                memcpy(packet + packet_length, bytes, length);
                packet_length += (uint32_t)length;
                return true;
            }
            void debug_uart_tx_flush_blocking(void)
            {
                if (stops == 0U) ++early_uart;
                ++flushes;
            }
            uint32_t debug_uart_tx_drop_count(void) { return 0U; }

            int main(int argc, char **argv)
            {
                (void)argv;
                fail_inference = argc > 1;
                if (!audio_pipeline_init() || !audio_pipeline_start()) return 1;
                for (uint32_t index = 0U; index < 1105U; ++index) {
                    audio_pipeline_poll();
                }
                const uint32_t packet_offset = 22U;
                if (stops != 1U || early_uart != 0U || frame != (fail_inference ? 12U : 1002U)) return 2;
                if (memcmp(packet, "READY\r\nCAPTURE_ARMED\r\n", packet_offset) != 0) return 10;
                if (packet_length < packet_offset + 48U ||
                    memcmp(packet + packet_offset, "DFIRPCM1", 8U) != 0) return 3;
                if (read_u16(packet + packet_offset + 8U) != 1U ||
                    read_u16(packet + packet_offset + 10U) != 48U) return 4;
                if (fail_inference) {
                    if (read_u32(packet + packet_offset + 12U) != 1U ||
                        read_u32(packet + packet_offset + 20U) != 0U ||
                        packet_length != packet_offset + 48U) return 5;
                    return 0;
                }
                if (read_u32(packet + packet_offset + 12U) != 0U ||
                    read_u32(packet + packet_offset + 16U) != 16000U ||
                    read_u32(packet + packet_offset + 20U) != 16000U ||
                    packet_length != packet_offset + 64048U) return 6;
                if ((int16_t)read_u16(packet + packet_offset + 28U) != 30 ||
                    read_u32(packet + packet_offset + 32U) != 0U ||
                    read_u32(packet + packet_offset + 36U) != 0U ||
                    read_u32(packet + packet_offset + 40U) != 0U) return 11;
                if (read_u32(packet + packet_offset + 44U) !=
                    crc32(packet + packet_offset + 48U, 64000U)) return 7;
                if ((int16_t)read_u16(packet + packet_offset + 48U) != 48 ||
                    (int16_t)read_u16(packet + packet_offset + 48U + 15999U * 4U) != 16047) return 12;
                for (uint32_t sample_index = 0U; sample_index < 16000U; sample_index += 997U) {
                    const int16_t input = (int16_t)read_u16(
                        packet + packet_offset + 48U + sample_index * 4U);
                    const int16_t output = (int16_t)read_u16(
                        packet + packet_offset + 50U + sample_index * 4U);
                    if (output != input + 100) return 8;
                }
                if (max_uart_chunk > 2048U || flushes < 2U) return 9;
                return 0;
            }
        """)
        temporary = ROOT / "build" / "deepfir_uart_capture_test.c"
        executable = ROOT / "build" / "deepfir_uart_capture_test.exe"
        temporary.write_text(harness, encoding="utf-8")
        build = subprocess.run(
            ["gcc", "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-DM55M1_RUNTIME_TELEMETRY=0", "-DM55M1_AUDIO_RELEASE_PROBE=0",
             "-DM55M1_AUDIO_UART_PCM_CAPTURE=1", "-DM55M1_ENABLE_DAC_I2S_OUTPUT=1",
             "-DM55M1_DMIC_GAIN_DB=30", "-I", str(ROOT), str(temporary),
             str(ROOT / "services/audio_pipeline/audio_pipeline.c"),
             "-o", str(executable)],
            capture_output=True, text=True, check=False,
        )
        self.assertEqual(build.returncode, 0, build.stderr)
        for arguments in ([], ["fail"]):
            run = subprocess.run(
                [str(executable), *arguments], capture_output=True, check=False
            )
            self.assertEqual(run.returncode, 0, run.stderr)
        temporary.unlink(missing_ok=True)
        executable.unlink(missing_ok=True)


if __name__ == "__main__":
    unittest.main()
