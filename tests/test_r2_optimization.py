"""Numeric and compiled-work regression gates for continuous R2 optimization."""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class R2OptimizationTests(unittest.TestCase):
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
