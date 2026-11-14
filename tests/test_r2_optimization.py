"""Numeric and compiled-work regression gates for continuous R2 optimization."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class R2OptimizationTests(unittest.TestCase):
    def test_built_cpu_dsp_state_uses_dtcm_without_moving_npu_arena(self):
        maps = [ROOT/folder/'M55M1_GestureAI.axf.map' for folder in ('build', 'build-debug')]
        maps = [path for path in maps if path.exists()]
        if not maps:
            self.skipTest('Build an Arm image to verify actual linker placement')
        for path in maps:
            with self.subTest(path=path):
                text = path.read_text()
                region = re.search(r'Execution Region DTCM \(Exec base: (0x[0-9a-fA-F]+), '
                                   r'Load base: .*?, Size: (0x[0-9a-fA-F]+),', text)
                self.assertIsNotNone(region)
                start, length = (int(value, 16) for value in region.groups())
                self.assertLessEqual(start + length, 0x2001e000)
                for name in ('g_adapter', 'g_feature_state', 'g_fft_context', 'g_input_conditioner'):
                    symbol = re.search(r'::'+name+r'\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)\s+', text)
                    self.assertIsNotNone(symbol, name)
                    address, size = int(symbol[1], 16), int(symbol[2])
                    self.assertGreater(size, 0)
                    self.assertEqual(address % 16, 0)
                    self.assertGreaterEqual(address, start)
                    self.assertLessEqual(address + size, start + length)
                arena = re.search(r'::g_tensor_arena\s+(0x[0-9a-fA-F]+)\s+Data\s+(\d+)\s+', text)
                self.assertIsNotNone(arena)
                self.assertGreaterEqual(int(arena[1], 16), 0x20100000)
                self.assertLessEqual(int(arena[1], 16) + int(arena[2]), 0x20230000)
                self.assertRegex(text, r'Execution Region ARM_LIB_STACK \(Exec base: 0x2001e000, '
                                      r'Load base: .*?, Size: 0x00002000,')
                report = subprocess.run([sys.executable, str(ROOT/'tools/report_model_sram.py'),
                                         str(path)], capture_output=True, text=True)
                self.assertEqual(report.returncode, 0, report.stderr)
                # Debug adds probe fields/alignment; count the linked section, not Release sizeof.
                section = re.search(r'0x[0-9a-fA-F]+\s+-\s+(0x[0-9a-fA-F]+)\s+Zero\s+RW\s+\d+\s+'
                                    r'\.bss.DTCM.ZeroInit\s+deepfir_inference_service.o', text)
                self.assertIsNotNone(section)
                expected = int(section[1], 16)
                self.assertGreaterEqual(expected, 7432)
                self.assertRegex(report.stdout, r'CPU-only DeepFIR DTCM\s*:\s*' +
                                 re.escape(f'{expected:,} B'))

    def test_service_render_failure_atomicity_and_single_total_conversion(self):
        source = (ROOT/'services/deepfir/deepfir_inference_service.cpp').read_text()
        process = source[source.index('template <typename Sample>'):
                         source.index('extern "C" bool deepfir_inference_process(')]
        harness = r'''
        #include <cassert>
        #include <cmath>
        #include <cstdint>
        #include <cstring>
        #include "services/deepfir/deepfir_inference_service.h"
        #include "services/deepfir/deepfir_dsp.h"
        static bool g_initialized = true, g_faulted, g_processing;
        static deepfir_inference_timing_t g_last_timing;
        static unsigned conversions, cycles;
        static int failure, bad_lane;
        uint32_t audio_transport_cycles_now() { return ++cycles; }
        uint32_t elapsed_us(uint32_t start, uint32_t end) {
            ++conversions; return end - start;
        }
        template <typename Sample>
        int process_input(const Sample *, uint32_t, float *output) {
            for (unsigned i=0; i<16; ++i) output[i] = 0.5f;
            if (failure == 2) output[bad_lane] = NAN;
            return failure == 1 ? -2 : 0;
        }
        #define DEEPFIR_TRACE_HOP(...) ((void)0)
        ''' + process + r'''
        int main() {
            int32_t input[16] = {};
            int16_t output[16], before[16];
            assert(process_audio(input, output, 16));
            assert(conversions == 1 && !g_faulted && !g_processing);
            for (unsigned i=0; i<16; ++i) assert(output[i] == 16384);
            for (failure=1; failure<=2; ++failure) {
                for (bad_lane=0; bad_lane<16; ++bad_lane) {
                    g_faulted = false; conversions = 0;
                    g_last_timing.total_us = 999;
                    std::memset(output, 0x35, sizeof(output));
                    std::memcpy(before, output, sizeof(output));
                    assert(!process_audio(input, output, 16));
                    assert(conversions == 1 && g_last_timing.total_us != 999);
                    assert(g_faulted && !g_processing);
                    assert(!std::memcmp(output, before, sizeof(output)));
                    assert(!process_audio(input, output, 16)); // Latched until reset.
                }
            }
            g_faulted = false; conversions = 0;
            assert(!process_audio(input, output, 15));
            assert(!process_audio(input, reinterpret_cast<int16_t *>(input), 16));
            assert(conversions == 0 && !g_processing);
            return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'service.cpp').write_text(harness)
            result = subprocess.run([
                'g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                '-DM55M1_RUNTIME_TELEMETRY=0', '-DM55M1_AUDIO_RELEASE_PROBE=0',
                '-I', str(ROOT), str(path/'service.cpp'),
                str(ROOT/'services/deepfir/deepfir_dsp.c'), '-o', str(path/'service.exe')],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(path/'service.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_pcm_render_and_fir_preserve_preoptimization_arithmetic(self):
        for vector in (False, True):
            with self.subTest(vector=vector), tempfile.TemporaryDirectory() as directory:
                executable = Path(directory) / 'dsp_parity.exe'
                result = subprocess.run([
                    'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-ffp-contract=off', f'-DM55M1_DEEPFIR_MVE={int(vector)}',
                    *(['-D__ARM_FEATURE_MVE=2'] if vector else []),
                    '-I', str(ROOT), '-I', str(ROOT/'tests/mve_model'),
                    str(ROOT/'tests/r2_dsp_optimization_harness.c'),
                    str(ROOT/'services/deepfir/deepfir_dsp.c'), '-lm', '-o', str(executable)],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                print(f'vector={vector}: {result.stdout}', end='')

    def test_production_vector_branches_with_native_lane_semantics(self):
        # Runs the actual SIMD C branches; the explicit test-only lane model is
        # neither hardware MVE execution nor a timing/CMSIS FFT accuracy claim.
        with tempfile.TemporaryDirectory() as directory:
            objects = []
            for name, vector in (('tests/r2_mve_kernel_harness.c', True),
                                 ('services/deepfir/deepfir_reconstruction.c', True),
                                 ('services/deepfir/deepfir_dsp.c', False)):
                obj = Path(directory)/(Path(name).stem+'.o')
                command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                           '-ffp-contract=off', '-I', str(ROOT), '-I', str(ROOT/'tests/mve_model')]
                if vector:
                    command += ['-DM55M1_DEEPFIR_MVE=1', '-D__ARM_FEATURE_MVE=2']
                result = subprocess.run([*command, '-c', str(ROOT/name), '-o', str(obj)],
                                        capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                objects.append(str(obj))
            executable = Path(directory)/'lanes.exe'
            result = subprocess.run(['gcc', *objects, '-lm', '-o', str(executable)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
            print(result.stdout, end='')

    @unittest.skipUnless(shutil.which('armclang'), 'Arm Compiler required for target code-generation checks')
    def test_release_quantizer_and_reconstruction_have_vector_kernels(self):
        for name, function, instruction in (
            ('deepfir_features.c', 'deepfir_quantize_feature_block', r'vcvt\.s32\.f32'),
            ('deepfir_features.c', 'deepfir_feature_float', r'bl\s+arm_vlog_f32'),
            ('deepfir_reconstruction.c', 'deepfir_reconstruct_taps', r'vmul\.f32\s+q')):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                assembly = Path(directory) / 'kernel.s'
                result = subprocess.run([
                    'armclang', '--target=arm-arm-none-eabi', '-mthumb', '-mcpu=cortex-m55',
                    '-mfloat-abi=hard', '-O3', '-DNDEBUG', '-fno-finite-math-only', '-DM55M1_DEEPFIR_MVE=1',
                    '-DARM_MATH_MVEF=1', '-DDEEPFIR_DSP_CMSIS_FFT=1',
                    '-I', str(ROOT/'libs/CMSIS/DSP/Include'),
                    '-I', str(ROOT/'libs/CMSIS/Core/Include'), '-std=c11', '-S',
                    str(ROOT/'services/deepfir'/name), '-o', str(assembly)],
                    capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                kernel = re.search(r'(?ms)^'+function+r':.*?(?=^\s*\.size\s+'+function+r'\b)',
                                   assembly.read_text())
                self.assertIsNotNone(kernel, name+' has no vector kernel')
                self.assertRegex(kernel.group(), r'(?i)\b'+instruction)
                if function == 'deepfir_feature_float':
                    self.assertNotRegex(kernel.group(), r'(?i)\b(?:vsqrt\.f32|sqrtf)\b')

    @unittest.skipUnless(shutil.which('armclang'), 'Arm Compiler required for target code-generation checks')
    def test_release_feedback_uses_rounded_q31_vectors(self):
        with tempfile.TemporaryDirectory() as directory:
            assembly = Path(directory) / 'feedback.s'
            result = subprocess.run([
                'armclang', '--target=arm-arm-none-eabi', '-mthumb', '-mcpu=cortex-m55',
                '-mfloat-abi=hard', '-O3', '-DNDEBUG', '-fno-finite-math-only', '-DM55M1_DEEPFIR_MVE=1',
                '-DARM_MATH_MVEF=1', '-I', str(ROOT), '-I', str(ROOT/'libs/CMSIS/DSP/Include'),
                '-I', str(ROOT/'libs/CMSIS/Core/Include'), '-std=c11', '-S',
                str(ROOT/'services/deepfir/deepfir_model_adapter.c'), '-o', str(assembly)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            kernel = re.search(r'(?ms)^deepfir_feedback_q31:.*?(?=^\s*\.size\s+deepfir_feedback_q31\b)',
                               assembly.read_text())
            self.assertIsNotNone(kernel, 'Feedback still uses the generic scalar loop')
            self.assertRegex(kernel.group(), r'(?i)\bvqrdmulh\.s32\b')
            self.assertRegex(kernel.group(), r'(?i)\bvldrh\.s32\b')
            self.assertNotRegex(kernel.group(), r'\b(?:umull|smull|__aeabi_ldivmod)\b')

    def test_cmsis_reconstruction_avoids_libm_in_contract_phase_range(self):
        harness = r'''
        #include <assert.h>
        #include <math.h>
        #include "services/deepfir/deepfir_reconstruction.h"
        static unsigned libm_calls;
        float counted_sinf(float x) { ++libm_calls; return sinf(x); }
        float counted_cosf(float x) { ++libm_calls; return cosf(x); }
        static int finite_spectrum(float *p, uint32_t n, uint8_t inv, void *ctx) {
            (void)ctx; assert(n == 128 && inv == 1);
            for (unsigned i=0; i<256; ++i) assert(isfinite(p[i]));
            return 0;
        }
        int main(void) {
            int16_t gain[65], phase[65]; float taps[128];
            float scale[2] = {5.657631845679134e-05f, 7.72049679653719e-05f};
            int32_t zero[2] = {0};
            for (unsigned i=0; i<65; ++i) { gain[i]=32767; phase[i]=32767; }
            assert(deepfir_reconstruct_taps(gain, phase, scale, zero, finite_spectrum, 0, taps)==0);
            assert(libm_calls==0); // In-range phases must use the shared CMSIS sin/cos kernel.
            scale[1] = 1e30f;
            assert(deepfir_reconstruct_taps(gain, phase, scale, zero, finite_spectrum, 0, taps)==0);
            assert(libm_calls>0); // Arbitrary large public-API phases must avoid unsafe table indexing.
            return 0;
        }
        '''
        includes = ['-I', str(ROOT), '-I', str(ROOT/'libs/CMSIS/DSP/Include'),
                    '-I', str(ROOT/'libs/CMSIS/Core/Include'), '-D__GNUC_PYTHON__']
        with tempfile.TemporaryDirectory() as directory:
            folder = Path(directory)
            source = folder/'trig.c'
            source.write_text(harness)
            obj = folder/'reconstruction.o'
            result = subprocess.run(['gcc', '-std=c11', '-O2', *includes,
                '-DDEEPFIR_DSP_CMSIS_FFT=1', '-Dsinf=counted_sinf', '-Dcosf=counted_cosf',
                '-c', str(ROOT/'services/deepfir/deepfir_reconstruction.c'), '-o', str(obj)],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            executable = folder/'trig.exe'
            result = subprocess.run(['gcc', '-std=c11', '-O2', *includes, str(source), str(obj),
                str(ROOT/'libs/CMSIS/DSP/Source/ControllerFunctions/arm_sin_cos_f32.c'),
                str(ROOT/'libs/CMSIS/DSP/Source/CommonTables/arm_common_tables.c'),
                '-lm', '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_shared_normalized_frontend_quantizer_and_feedback_edges(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'optimization.exe'
            result = subprocess.run([
                'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT), str(ROOT/'tests/r2_optimization_harness.c'),
                *[str(ROOT/'services/deepfir'/name) for name in (
                    'deepfir_model_adapter.c', 'deepfir_reconstruction.c', 'deepfir_dsp.c')],
                '-lm', '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout, end='')

    def test_feedback_has_no_runtime_integer_division(self):
        # Assembly checks supplement exhaustive numeric coverage: a variable divide
        # in this 400-code/hop kernel is an accidental performance regression.
        with tempfile.TemporaryDirectory() as directory:
            assembly = Path(directory) / 'adapter.s'
            result = subprocess.run([
                'gcc', '-std=c11', '-O2', '-S', '-I', str(ROOT),
                str(ROOT/'services/deepfir/deepfir_model_adapter.c'),
                '-o', str(assembly)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            source = assembly.read_text()
            function = re.search(r'^deepfir_state_feedback:\n(.*?)(?=^\s*\.seh_endproc|^\s*\.size)',
                                 source, re.MULTILINE | re.DOTALL)
            self.assertIsNotNone(function, source[:500])
            self.assertNotRegex(function.group(), r'\b(?:idiv|div)[bwlq]?\b|__divdi3|__aeabi_ldivmod')


if __name__ == '__main__':
    unittest.main()
