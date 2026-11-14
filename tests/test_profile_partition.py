"""Execute actual service callbacks/wrapper under each product profile."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ProfilePartitionTests(unittest.TestCase):
    def test_release_skips_stage_work_and_diagnostic_profiles_retain_it(self):
        source = (ROOT/'services/deepfir/deepfir_inference_service.cpp').read_text()
        callbacks = source[source.index('int feature_callback('):source.index('} // namespace')]
        process = source[source.index('template <typename Sample>'):
                         source.index('extern "C" bool deepfir_inference_process(')]
        getter = source[source.index('extern "C" void deepfir_inference_get_last_timing('):
                        source.index('extern "C" void deepfir_inference_get_memory(')]
        harness = r'''
        #include <cassert>
        #include <cmath>
        #include <cstring>
        #include "services/deepfir/deepfir_model_adapter.h"
        #include "services/deepfir/deepfir_inference_service.h"
        #define DIAGNOSTIC (M55M1_RUNTIME_TELEMETRY || M55M1_AUDIO_RELEASE_PROBE)
        #define DEEPFIR_TRACE_HOP(...) ((void)0)
        unsigned clock_reads, conversions, snapshots, features, invokes;
        int inference_status, feature_status, bad_lane = -1;
        bool g_initialized = true, g_faulted, g_processing;
        deepfir_inference_timing_t g_last_timing;
        #if DIAGNOSTIC
        uint32_t g_feature_start_cycle, g_feature_fft_end_cycle;
        bool g_feature_fft_called;
        uint32_t g_adapter_stage_cycles[DEEPFIR_PROBE_STAGE_COUNT];
        #endif
        #if M55M1_RUNTIME_TELEMETRY
        unsigned g_hop_event_count, g_hop_reported_events;
        bool g_hop_trace_overflow;
        #endif
        uint32_t audio_transport_cycles_now() { return ++clock_reads; }
        uint32_t audio_transport_cycles_to_us(uint32_t value) { ++conversions; return value; }
        uint32_t elapsed_us(uint32_t begin, uint32_t end) { ++conversions; return end-begin; }
        struct ethosu_runtime_stats_t { uint32_t last_duration_cycles; };
        constexpr int ETHOSU_RUNTIME_OK = 0;
        int ethosu_runtime_get_stats(ethosu_runtime_stats_t *stats) {
            ++snapshots; stats->last_duration_cycles = 1; return ETHOSU_RUNTIME_OK;
        }
        int deepfir_feature_float(const float *, uint32_t, int16_t *, void *) {
            ++features; return feature_status;
        }
        using TfLiteStatus = int;
        constexpr int kTfLiteOk = 0;
        struct Tensor { struct { int16_t *i16; } data; };
        Tensor *g_inputs[3], *g_outputs[4];
        struct Interpreter { int Invoke() { ++invokes; return inference_status; } } interpreter;
        Interpreter *g_interpreter = &interpreter;
        ''' + callbacks + r'''
        template <typename Sample>
        int process_input(const Sample *, uint32_t, float *output) {
            for (unsigned i=0; i<16; ++i) output[i] = (int)i == bad_lane ? NAN : .5f;
            return feature_status;
        }
        ''' + process + getter + r'''
        int main() {
            int16_t tensor_storage[7][4] = {};
            Tensor tensors[7];
            deepfir_tensor_map_t inputs[3] = {}, outputs[4] = {};
            int16_t values[4] = {1,2,3,4};
            for (unsigned i=0; i<7; ++i) tensors[i].data.i16 = tensor_storage[i];
            for (unsigned i=0; i<3; ++i) {
                g_inputs[i] = tensors+i; inputs[i].data = values; inputs[i].elements = 4;
            }
            for (unsigned i=0; i<4; ++i) g_outputs[i] = tensors+3+i;
            float pcm[16] = {};
            int16_t feature[387], output[16], saved[16];
            assert(!feature_callback(pcm,16,feature,nullptr) && features == 1);
            assert(!infer_callback(inputs,3,outputs,4,nullptr) && invokes == 1);
            for (unsigned i=0; i<3; ++i) assert(!std::memcmp(tensor_storage[i],values,sizeof(values)));
            for (unsigned i=0; i<4; ++i) assert(outputs[i].data == tensor_storage[3+i]);
            inference_status = 1;
            assert(infer_callback(inputs,3,outputs,4,nullptr) == -2 && invokes == 2);
            assert(infer_callback(inputs,2,outputs,4,nullptr) == -1 && invokes == 2);
            assert(process_audio(pcm,output,16));
            for (unsigned i=0; i<16; ++i) assert(output[i] == 16384);
            for (unsigned lane=0; lane<16; ++lane) {
                bad_lane = lane; g_faulted = false;
                std::memset(output,0x35,sizeof(output)); std::memcpy(saved,output,sizeof(output));
                assert(!process_audio(pcm,output,16) && g_faulted && !g_processing);
                assert(!std::memcmp(saved,output,sizeof(output)));
                assert(!process_audio(pcm,output,16));
            }
            g_faulted = false; bad_lane = -1; feature_status = -7;
            assert(!process_audio(pcm,output,16) && g_faulted && !g_processing);
            deepfir_inference_timing_t timing;
            std::memset(&timing,0x35,sizeof(timing));
            deepfir_inference_get_last_timing(&timing);
            deepfir_inference_get_last_timing(nullptr);
            #if DIAGNOSTIC
            assert(clock_reads > 0 && conversions > 0 && snapshots == 2 && timing.total_us > 0);
            #else
            const deepfir_inference_timing_t zero = {};
            assert(clock_reads == 0 && conversions == 0 && snapshots == 0);
            assert(!std::memcmp(&timing,&zero,sizeof(timing)));
            #endif
        }
        '''
        for debug, probe in ((0, 0), (1, 0), (0, 1)):
            with self.subTest(debug=debug, probe=probe), tempfile.TemporaryDirectory() as directory:
                path = Path(directory)
                (path/'profile.cpp').write_text(harness)
                built = subprocess.run([
                    'g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                    f'-DM55M1_RUNTIME_TELEMETRY={debug}', f'-DM55M1_AUDIO_RELEASE_PROBE={probe}',
                    '-I', str(ROOT), str(path/'profile.cpp'),
                    str(ROOT/'services/deepfir/deepfir_dsp.c'), '-o', str(path/'profile.exe')],
                    capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                run = subprocess.run([str(path/'profile.exe')], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
