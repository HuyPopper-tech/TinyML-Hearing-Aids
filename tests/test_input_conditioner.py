"""Streaming input dynamics: independent double oracle and actual adapter integration."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InputConditionerTests(unittest.TestCase):
    def test_numeric_stream_and_adapter_integration(self):
        runs = []
        for vector, fast in ((False, False), (True, False), (True, True)):
            with self.subTest(vector=vector, fast=fast), tempfile.TemporaryDirectory() as directory:
                executable = Path(directory) / 'conditioner.exe'
                command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                           '-ffp-contract=off', f'-DM55M1_DEEPFIR_MVE={int(vector)}',
                           *(['-ffast-math', '-fno-finite-math-only'] if fast else []),
                           *(['-D__ARM_FEATURE_MVE=2'] if vector else []),
                           '-I', str(ROOT), '-I', str(ROOT/'tests/mve_model'),
                           str(ROOT/'tests/input_conditioner_harness.c'),
                           *[str(ROOT/'services/deepfir'/name) for name in (
                               'deepfir_input_conditioner.c', 'deepfir_model_adapter.c',
                               'deepfir_dsp.c', 'deepfir_reconstruction.c')],
                           '-lm', '-o', str(executable)]
                result = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                runs.append(result.stdout)
                print(f'vector={vector}, fast={fast}: {result.stdout.strip()}')
        self.assertEqual(len(runs), 3, 'Every arithmetic variant must finish successfully')
        self.assertEqual(runs[0], runs[1], 'Scalar/MVE lane model differ at the output bit level')

    def test_actual_service_routing_and_reset(self):
        source = (ROOT/'services/deepfir/deepfir_inference_service.cpp').read_text()
        reset_and_input = source[source.index('extern "C" void deepfir_inference_reset('):
                                 source.index('template <typename Sample>')]
        harness = r'''
        #include <cassert>
        #include <cstring>
        #include "services/deepfir/deepfir_input_conditioner.h"
        #include "services/deepfir/deepfir_model_adapter.h"
        #include "services/deepfir/deepfir_inference_service.h"
        static bool g_initialized = true;
        static deepfir_feature_state_t g_feature_state;
        static deepfir_model_adapter_t g_adapter;
        static deepfir_input_conditioner_t g_input_conditioner;
        static unsigned features_reset, adapter_reset, invoked;
        static float expected[16];
        extern "C" void deepfir_feature_reset(deepfir_feature_state_t *) { ++features_reset; }
        extern "C" void deepfir_model_adapter_reset(deepfir_model_adapter_t *) { ++adapter_reset; }
        extern "C" int deepfir_model_adapter_process_pcm16(deepfir_model_adapter_t *,
            const int16_t *, uint32_t, float *) { return 73; }
        extern "C" int deepfir_model_adapter_process_float(deepfir_model_adapter_t *,
            const float *input, uint32_t count, float *output) {
            assert(count == 16 && !std::memcmp(input, expected, sizeof(expected)));
            std::memcpy(output, input, sizeof(expected)); ++invoked; return 0;
        }
        ''' + reset_and_input + r'''
        int main() {
            int32_t input[16] = {};
            int16_t legacy[16] = {};
            float output[16];
            deepfir_input_conditioner_t oracle;
            deepfir_input_conditioner_reset(&oracle);
            deepfir_input_conditioner_reset(&g_input_conditioner);
            deepfir_inference_reset();
            assert(features_reset == 1 && adapter_reset == 1 && g_input_conditioner.gain == 1);
            for (unsigned hop=0; hop<100; ++hop) {
                for (unsigned i=0; i<16; ++i) input[i] = hop * 70000 - 3500000;
                assert(!deepfir_input_conditioner_process(&oracle, input, 16, expected));
                assert(!process_input(input, 16, output));
                assert(!std::memcmp(output, expected, sizeof(output)));
            }
            assert(invoked == 100 && process_input(legacy, 16, output) == 73);
            input[15] = 8388608;
            assert(process_input(input, 16, output) && invoked == 100);
            deepfir_inference_reset();
            assert(g_input_conditioner.gain == 1 && g_input_conditioner.processed_samples == 1600);
            assert(g_input_conditioner.compressor_gain == 1 && g_input_conditioner.limiter_gain == 1);
            assert(g_input_conditioner.rms_window_samples == 0);
            assert(features_reset == 2 && adapter_reset == 2);
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'service.cpp').write_text(harness)
            result = subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                     '-I', str(ROOT), str(path/'service.cpp'),
                                     str(ROOT/'services/deepfir/deepfir_input_conditioner.c'),
                                     '-o', str(path/'service.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path/'service.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
