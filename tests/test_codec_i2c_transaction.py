"""Execute production codec I2C code with a deterministic register model."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class CodecI2CTransactionTests(unittest.TestCase):
    def test_stop_completion_and_failure_snapshot(self):
        source = (ROOT / 'drivers/tlv320dac3100/tlv320dac3100.c').read_text()
        header = (ROOT / 'drivers/tlv320dac3100/tlv320dac3100.h').read_text()
        # Extract actual production functions and status layout, not a second algorithm.
        record = source[source.index('static void tlv320dac3100_record_i2c_failure'):
                        source.index('static const tlv320dac3100_profile_definition_t *')]
        writers = source[source.index('static bool tlv320dac3100_wait_for_i2c_si'):
                         source.index('static uint32_t tlv320dac3100_get_pdma_request')]
        status_end = header.index('} tlv320dac3100_driver_status_t;')
        status_start = header.rfind('typedef struct {', 0, status_end)
        status = header[status_start:status_end + len('} tlv320dac3100_driver_status_t;')]
        transport = (ROOT / 'drivers/audio_transport/audio_transport.c').read_text()
        init = transport[transport.index('bool audio_transport_init('):
                         transport.index('audio_transport_status_t audio_transport_start(')]
        defines = '\n'.join(re.findall(r'^#define TLV320DAC3100_(?:I2C_\w+|REG_PAGE_SELECT|PAGE_0) .+$',
                                       source + '\n' + header, re.M))
        harness = r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <stdlib.h>
        #include <string.h>
        #define I2C_CTL0_SI_Msk 8U
        #define I2C_CTL0_STO_Msk 16U
        #define I2C_CTL_SI 8U
        #define I2C_CTL_STO_SI 24U
        typedef struct { uint32_t control[1]; uint8_t status; } I2C_T;
        static I2C_T controller;
        static unsigned step, starts, stop_reads, delays, nack_step, timeout_step, ready_nacks;
        static bool stuck_stop, target_only;
        static unsigned read_control(void) {
            if (controller.control[0] & I2C_CTL0_STO_Msk) {
                ++stop_reads;
                if (!stuck_stop && stop_reads >= 4U) controller.control[0] = 0U;
            }
            return 0U;
        }
        // Each volatile CTL0 read advances only the peripheral model's STOP.
        #define CTL0 control[read_control()]
        static void start(I2C_T *i2c) {
            assert(!(i2c->control[0] & I2C_CTL0_STO_Msk));
            ++starts; step = 1; stop_reads = 0;
            i2c->status = 0x08;
            i2c->control[0] = timeout_step == step ? 0U : I2C_CTL0_SI_Msk;
        }
        static void control(I2C_T *i2c, unsigned value) {
            if (value == I2C_CTL_STO_SI) {
                i2c->control[0] = I2C_CTL0_STO_Msk;
                i2c->status = 0xf8; // STOP destroys the original NACK evidence.
                stop_reads = 0;
            } else {
                ++step;
                i2c->status = step == 2 ? 0x18 : 0x28;
                if (nack_step == step && (!target_only || starts % 2U == 0U)) {
                    i2c->status = step == 2 ? 0x20 : 0x30;
                }
                if (step == 2 && starts <= ready_nacks) i2c->status = 0x20;
                i2c->control[0] = timeout_step == step ? 0U : I2C_CTL0_SI_Msk;
            }
        }
        #define I2C_START(i2c) start(i2c)
        #define I2C_SET_CONTROL_REG(i2c, value) control(i2c, value)
        #define I2C_GET_STATUS(i2c) ((i2c)->status)
        #define I2C_SET_DATA(i2c, value) ((void)(i2c), (void)(value))
        static void m55m1_timebase_delay_us(unsigned value) { (void)value; ++delays; }
        typedef enum { TLV320DAC3100_STATUS_OK, TLV320DAC3100_STATUS_I2C_ERROR = 5 }
            tlv320dac3100_status_t;
        '''
        harness += status + r'''
        static struct {
            struct { I2C_T *i2c; } config;
            tlv320dac3100_driver_status_t status;
        } g_context;
        ''' + defines + '\n' + record + '\n' + writers + r'''
        #define M55M1_ENABLE_DAC_I2S_OUTPUT 1
        #define M55M1_RUNTIME_TELEMETRY 1
        #include "drivers/audio_transport/audio_transport.h"
        enum { M55M1_AUDIO_PROFILE_16KHZ, M55M1_AUDIO_BOARD_INIT_OK = 0,
               TLV320DAC3100_AUDIO_PROFILE_16KHZ = 0 };
        typedef int m55m1_audio_board_init_result_t;
        typedef struct {
            void *i2c, *i2s, *pdma, *block_callback, *callback_context;
            unsigned pdma_channel, profile;
            int max_digital_gain_db;
        } tlv320dac3100_config_t;
        typedef tlv320dac3100_config_t m55m1_audio_playback_binding_t;
        typedef audio_transport_init_report_t m55m1_audio_clock_snapshot_t;
        typedef struct { audio_transport_progress_callback_t callback; void *context; }
            audio_transport_progress_context_t;
        static void audio_transport_playback_irq(uint32_t cycle, void *context) {
            (void)cycle; (void)context;
        }
        static void audio_transport_board_progress(int stage, void *context) {
            (void)stage; (void)context;
        }
        static int m55m1_audio_board_init(unsigned profile, m55m1_audio_playback_binding_t *binding,
                void (*irq)(uint32_t, void *), void *irq_context,
                void (*progress)(int, void *), void *progress_context) {
            (void)profile; (void)irq; (void)irq_context; (void)progress; (void)progress_context;
            *binding = (m55m1_audio_playback_binding_t){0};
            return 0;
        }
        static const i2c_recovery_trace_t *m55m1_audio_get_i2c_trace(void) { return NULL; }
        static bool m55m1_audio_get_clock_snapshot(m55m1_audio_clock_snapshot_t *snapshot) {
            *snapshot = (m55m1_audio_clock_snapshot_t){.core_hz = 220000000}; return true;
        }
        static tlv320dac3100_status_t tlv320dac3100_init(const tlv320dac3100_config_t *config) {
            (void)config; return TLV320DAC3100_STATUS_I2C_ERROR;
        }
        static void tlv320dac3100_get_status(tlv320dac3100_driver_status_t *status) {
            *status = g_context.status;
        }
        ''' + init + r'''
        int main(int argc, char **argv) {
            assert(argc == 2);
            unsigned mode = (unsigned)atoi(argv[1]);
            g_context.config.i2c = &controller;
            if (mode == 0) {
                assert(tlv320dac3100_write_register(0, 1, 1) == TLV320DAC3100_STATUS_OK);
                assert(starts == 2 && !(controller.control[0] & I2C_CTL0_STO_Msk));
                assert(!g_context.status.i2c_failures);
            } else if (mode <= 4) {
                nack_step = mode;
                assert(tlv320dac3100_write_register(1, 40, 6) == TLV320DAC3100_STATUS_I2C_ERROR);
                assert(starts == 3 && delays == 2);
                assert(g_context.status.i2c_failures == 1);
                assert(g_context.status.last_i2c_page == 1);
                assert(g_context.status.last_i2c_register == 40);
                assert(g_context.status.last_i2c_attempts == 3);
                assert(g_context.status.last_i2c_status == (mode == 2 ? 0x20 : 0x30));
                assert(g_context.status.last_i2c_control & I2C_CTL0_SI_Msk);
                assert(g_context.status.last_i2c_step == mode);
                assert(g_context.status.last_i2c_expected_status == (mode == 2 ? 0x18 : 0x28));
                assert(!g_context.status.last_i2c_timed_out);
                assert(!g_context.status.last_i2c_stop_timed_out);
                assert(!(controller.control[0] & I2C_CTL0_STO_Msk));
            } else if (mode == 5 || mode == 14) {
                stuck_stop = true;
                if (mode == 14) controller.control[0] = I2C_CTL0_STO_Msk;
                assert(tlv320dac3100_write_register(0, 1, 1) == TLV320DAC3100_STATUS_I2C_ERROR);
                assert(starts == (mode == 5 ? 1U : 0U));
                assert(g_context.status.last_i2c_step == TLV320DAC3100_I2C_STEP_STOP);
                assert(g_context.status.last_i2c_timed_out);
                assert(g_context.status.last_i2c_stop_timed_out);
                assert(g_context.status.last_i2c_control & I2C_CTL0_STO_Msk);
            } else if (mode <= 9) {
                timeout_step = mode - 5;
                assert(tlv320dac3100_write_register(0, 1, 1) == TLV320DAC3100_STATUS_I2C_ERROR);
                assert(starts == 3 && delays == 2);
                assert(g_context.status.last_i2c_step == timeout_step);
                assert(g_context.status.last_i2c_timed_out);
                assert(!g_context.status.last_i2c_stop_timed_out);
                assert(!(g_context.status.last_i2c_control & I2C_CTL0_SI_Msk));
            } else if (mode == 10) {
                target_only = true;
                nack_step = 4;
                assert(tlv320dac3100_write_register(1, 40, 6) == TLV320DAC3100_STATUS_I2C_ERROR);
                assert(starts == 6 && delays == 2);
                assert(g_context.status.last_i2c_phase == TLV320DAC3100_I2C_PHASE_TARGET_REGISTER);
                assert(g_context.status.last_i2c_status == 0x30);
            } else if (mode == 11 || mode == 12) {
                ready_nacks = mode == 11 ? 20U : 2U;
                tlv320dac3100_status_t result = tlv320dac3100_wait_for_i2c_ready();
                assert(starts == (mode == 11 ? 20U : 3U));
                assert(delays == (mode == 11 ? 19U : 2U));
                if (mode == 11) {
                    assert(result == TLV320DAC3100_STATUS_I2C_ERROR);
                    assert(g_context.status.i2c_failures == 1);
                    assert(g_context.status.last_i2c_status == 0x20);
                    assert(g_context.status.last_i2c_attempts == 20);
                } else {
                    assert(result == TLV320DAC3100_STATUS_OK);
                    assert(!g_context.status.i2c_failures);
                }
            } else if (mode == 13) {
                controller.control[0] = I2C_CTL0_STO_Msk;
                assert(tlv320dac3100_write_register(0, 1, 1) == TLV320DAC3100_STATUS_OK);
                assert(starts == 2 && !g_context.status.i2c_failures);
            } else if (mode == 15) {
                nack_step = 2;
                stuck_stop = true;
                assert(!tlv320dac3100_write_one_reg_bounded(&controller, 0x18, 0, 0));
                assert(g_context.status.last_i2c_status == 0x20);
                assert(g_context.status.last_i2c_step == TLV320DAC3100_I2C_STEP_ADDRESS);
                assert(!g_context.status.last_i2c_timed_out);
                assert(g_context.status.last_i2c_stop_timed_out);
            }
            if (mode != 0 && mode != 12 && mode != 13) {
                audio_transport_init_report_t report;
                memset(&report, 0xff, sizeof(report));
                assert(!audio_transport_init(&report, NULL, NULL));
                assert(!audio_transport_init(NULL, NULL, NULL));
                assert(report.board_status == 0 && report.codec_status == 5);
                assert(report.clocks_valid && report.core_hz == 220000000);
                assert(report.codec_i2c_failures == g_context.status.i2c_failures);
                assert(report.codec_i2c_page == g_context.status.last_i2c_page);
                assert(report.codec_i2c_register == g_context.status.last_i2c_register);
                assert(report.codec_i2c_phase == g_context.status.last_i2c_phase);
                assert(report.codec_i2c_attempts == g_context.status.last_i2c_attempts);
                assert(report.codec_i2c_step == g_context.status.last_i2c_step);
                assert(report.codec_i2c_status == g_context.status.last_i2c_status);
                assert(report.codec_i2c_expected_status == g_context.status.last_i2c_expected_status);
                assert(report.codec_i2c_control == g_context.status.last_i2c_control);
                assert(report.codec_i2c_timed_out == g_context.status.last_i2c_timed_out);
                assert(report.codec_i2c_stop_timed_out == g_context.status.last_i2c_stop_timed_out);
            }
            (void)tlv320dac3100_wait_for_i2c_ready;
            return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary)
            (path / 'test.c').write_text(harness)
            binary = path / 'test.exe'
            build = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT), str(path / 'test.c'), '-o', str(binary)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            for mode in (0, *range(2, 16)):
                with self.subTest(mode=mode):
                    run = subprocess.run([str(binary), str(mode)], capture_output=True, text=True)
                    self.assertEqual(run.returncode, 0, run.stderr)
