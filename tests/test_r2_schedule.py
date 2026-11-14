"""Native foreground continuity/deadline/recovery and custom-only profile checks."""
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class R2ScheduleTests(unittest.TestCase):
    def test_built_swd_stats_fit_noncached_region(self):
        maps = [ROOT / folder / 'M55M1_GestureAI.axf.map' for folder in ('build', 'build-debug')]
        maps = [path for path in maps if path.exists()]
        if not maps:
            self.skipTest('Build an Arm image to verify actual linker placement')
        for path in maps:
            with self.subTest(path=path):
                text = path.read_text()
                region = re.search(r'Execution Region SRAM_NONCACHEABLE \(Exec base: (0x[0-9a-fA-F]+), '
                                   r'Load base: .*?, Size: (0x[0-9a-fA-F]+),', text)
                symbol = re.search(r'^\s*g_stats\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)\s+', text, re.M)
                self.assertIsNotNone(region)
                self.assertIsNotNone(symbol)
                start, length = (int(value, 16) for value in region.groups())
                address, size = int(symbol[1], 16), int(symbol[2])
                self.assertGreater(size, 0)
                self.assertGreaterEqual(address, start)
                self.assertLessEqual(address + size, start + length)

    def test_active_audio_has_only_the_fixed_product_handoff(self):
        paths = ('board/m55m1_audio.c', 'drivers/tlv320dac3100/tlv320dac3100.c',
                 'drivers/audio_transport/audio_transport.c')
        for path in paths:
            source = (ROOT / path).read_text()
            for obsolete in ('M55M1_PRODUCT_AUDIO', 'M55M1_CUSTOM_DMIC_TEST',
                             'M55M1_DMIC_PRODUCT_STARTUP', 'PROFILE_32KHZ',
                             'has_queued_audio'):
                self.assertNotIn(obsolete, source, path)
        for path, obsolete in (('drivers/audio_transport/audio_transport.h', 'audio_transport_submit('),
                               ('drivers/tlv320dac3100/tlv320dac3100.h', 'tlv320dac3100_submit_mono_block(')):
            self.assertNotIn(obsolete, (ROOT / path).read_text(), path)
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        self.assertNotIn('M55M1_ENABLE_RTOS2', cmake)
        self.assertNotIn('services/audio_telemetry.c', cmake)
        self.assertFalse((ROOT / 'services/audio_telemetry.c').exists())
        self.assertFalse((ROOT / 'services/audio_telemetry.h').exists())

    def test_only_product_debug_release_profiles_use_custom_uart(self):
        presets = json.loads((ROOT / 'CMakePresets.json').read_text())
        for key in ('configurePresets', 'buildPresets'):
            self.assertEqual({p['name'] for p in presets[key]}, {'m55m1-debug', 'm55m1-release'})
        for profile in presets['configurePresets']:
            self.assertEqual(profile['cacheVariables']['M55M1_DEBUG_UART_IDX'], '2')
            for diagnostic in ('M55M1_AUDIO_RELEASE_PROBE', 'M55M1_DEEPFIR_COMPILED_REPLAY',
                               'M55M1_AUDIO_UART_PCM_CAPTURE'):
                self.assertEqual(profile['cacheVariables'][diagnostic], 'OFF')
        for wrapper in ('build.bat', 'build.sh'):
            text = (ROOT / wrapper).read_text()
            self.assertIn('-DM55M1_DEBUG_UART_IDX=2', text)
            self.assertNotIn('NUMAKER', text)
            self.assertIn('-DM55M1_AUDIO_UART_PCM_CAPTURE=OFF', text)
            self.assertIn('-DM55M1_DEEPFIR_COMPILED_REPLAY=OFF', text)

    def test_sequence_deadline_health_submit_and_full_restart(self):
        harness = r'''
        #include <assert.h>
        #include <string.h>
        #include "services/audio_pipeline/audio_pipeline.h"
        #include "services/deepfir/deepfir_inference_service.h"
        #include "drivers/audio_transport/audio_transport.h"
        static unsigned init_calls, resets, starts, stops, invokes, submits, waits;
        static uint32_t now, sequence = 1, duration = 100;
        static bool available, invoke_ok = true, health_error, restart_ok = true;
        static audio_transport_status_t submit_result;
        bool audio_transport_init(audio_transport_init_report_t *r,
            audio_transport_progress_callback_t cb, void *u) {
            (void)cb; (void)u; ++init_calls;
            *r = (audio_transport_init_report_t){.clocks_valid = true}; return restart_ok;
        }
        audio_transport_status_t audio_transport_start(void) { ++starts; return AUDIO_TRANSPORT_STATUS_OK; }
        void audio_transport_wait_for_work(void) { ++waits; }
        void audio_transport_stop_safe(void) { ++stops; }
        bool audio_transport_take_capture(int32_t pcm[16], audio_transport_capture_timing_t *t) {
            if (!available) { return false; } available = false;
            memset(pcm, 0, 16 * sizeof(*pcm));
            pcm[0] = -8388608; pcm[1] = 8388607; pcm[2] = -7680000;
            *t = (audio_transport_capture_timing_t){sequence, 0}; return true;
        }
        bool audio_transport_capture_pending(void) { return false; }
        uint32_t audio_transport_cycles_now(void) { return now; }
        uint32_t audio_transport_cycles_elapsed(uint32_t a, uint32_t b) { return (b-a)&0xffffff; }
        uint32_t audio_transport_cycles_to_us(uint32_t c) { return c; }
        void audio_transport_get_health(audio_transport_health_t *h) {
            *h = (audio_transport_health_t){.dmic_overrun_blocks = health_error};
        }
        bool deepfir_inference_init(void) { return true; }
        void deepfir_inference_reset(void) { ++resets; }
        bool deepfir_inference_process_pcm24(const int32_t *in, int16_t *out, uint32_t n) {
            assert((const void *)in != (const void *)out && n == 16);
            assert(in[0] == -8388608 && in[1] == 8388607);
            ++invokes; now += duration;
            memset(out, 0, 32); return invoke_ok;
        }
        audio_transport_status_t audio_transport_submit_tagged(const int16_t pcm[16],
            const audio_transport_block_tag_t *tag) {
            (void)pcm; assert(tag->sequence == sequence); ++submits; return submit_result;
        }
        static void capture(uint32_t seq) { sequence = seq; available = true; now = 0; audio_pipeline_poll(); }
        int main(void) {
            audio_pipeline_wait_for_work(); assert(waits == 0);
            assert(audio_pipeline_init() && audio_pipeline_start());
            audio_pipeline_wait_for_work(); assert(waits == 1);
            capture(1); assert(invokes == 1 && submits == 1 && !stops);
            capture(3); assert(stops == 1 && invokes == 1 && starts == 2);
            capture(1); capture(1); assert(stops == 2 && starts == 3);
            duration = 1000; capture(1); assert(stops == 3 && submits == 2);
            duration = 100; invoke_ok = false; capture(1); assert(stops == 4);
            invoke_ok = true; submit_result = AUDIO_TRANSPORT_STATUS_BUSY;
            capture(1); assert(stops == 5);
            submit_result = AUDIO_TRANSPORT_STATUS_ERROR; capture(1); assert(stops == 6);
            submit_result = AUDIO_TRANSPORT_STATUS_OK; health_error = true;
            audio_pipeline_poll(); assert(stops == 7 && starts == 8);
            health_error = false; restart_ok = false; capture(2);
            assert(stops == 8 && starts == 8);
            audio_pipeline_wait_for_work(); assert(waits == 1);
            unsigned prior = invokes; capture(1); assert(invokes == prior);
            assert(resets >= 8 && init_calls >= 9);
            audio_pipeline_stats_t stats;
            audio_pipeline_get_stats(&stats);
            assert(stats.capture_sample_position == 0 && stats.scheduled_playback_slot == 2);
            assert(stats.sequence_faults == 3 && stats.deadline_misses == 1);
            assert(stats.model_faults == 1 && stats.transport_faults == 3);
            assert(stats.restarts == 7 && stats.restart_failures == 1);
            assert(stats.input_samples == 96 && stats.input_clip_samples == 12);
            assert(stats.input_peak == 8388608);
            restart_ok = true;
            assert(audio_pipeline_init() && audio_pipeline_start());
            const unsigned prior_stops = stops, prior_invokes = invokes, prior_submits = submits;
            for (uint32_t seq = 1; seq <= 60005; ++seq) {
                capture(seq);
            }
            audio_pipeline_get_stats(&stats);
            assert(stops == prior_stops && invokes == prior_invokes + 60005);
            assert(submits == prior_submits + 60005 && stats.scheduled_playback_slot == 60006);
            assert(stats.capture_sample_position == 60004ULL * 16);
            assert(stats.input_samples == 96ULL + 60005ULL * 16);
            assert(stats.input_clip_samples == 12ULL + 60005ULL * 2);
            assert(stats.input_peak == 8388608);
            return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'schedule.c'
            binary = Path(directory) / 'schedule.exe'
            source.write_text(harness)
            build = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-DM55M1_ENABLE_DAC_I2S_OUTPUT=1', '-I', str(ROOT),
                                    str(source), str(ROOT / 'services/audio_pipeline/audio_pipeline.c'),
                                    '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)


if __name__ == '__main__':
    unittest.main()
