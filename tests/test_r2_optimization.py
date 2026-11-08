"""Numeric and compiled-work regression gates for continuous R2 optimization."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class R2OptimizationTests(unittest.TestCase):
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

    @unittest.skipUnless(shutil.which('armclang'), 'Arm Compiler required for target code-generation checks')
    def test_release_feedback_uses_rounded_q31_vectors(self):
        with tempfile.TemporaryDirectory() as directory:
            assembly = Path(directory) / 'feedback.s'
            result = subprocess.run([
                'armclang', '--target=arm-arm-none-eabi', '-mthumb', '-mcpu=cortex-m55',
                '-mfloat-abi=hard', '-O3', '-DNDEBUG', '-fno-finite-math-only', '-DM55M1_DEEPFIR_MVE=1',
                '-DARM_MATH_MVEF=1', '-I', str(ROOT/'libs/CMSIS/DSP/Include'),
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
