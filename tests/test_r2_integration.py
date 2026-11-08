"""R2 identity and native numeric checks; controller mocks are never parity evidence."""
import ctypes as ct
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / 'deepfir_r2_scratch_ptq_20261107_151431_366715' / 'firmware_deepfir_r2_C_34c5bd074ae6_69c1c3d2'
COMPILED = '69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27'


class R2IdentityTests(unittest.TestCase):
    def test_active_model_and_four_output_abi(self):
        source = (ROOT / 'models/deepfir/deepfir_model_data.cc').read_text()
        blob = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', source))
        self.assertEqual(len(blob), 516848)
        self.assertEqual(hashlib.sha256(blob).hexdigest(), COMPILED)
        manifest = json.loads((ROOT/'models/deepfir/integration_manifest.json').read_text())
        self.assertEqual(manifest['compiled_sha256'], COMPILED)
        for name, digest in manifest['files'].items():
            self.assertEqual(hashlib.sha256((ROOT/'models/deepfir'/name).read_bytes()).hexdigest(), digest, name)
        if PACKAGE.is_dir():
            self.assertEqual(blob, (PACKAGE/'controller_16x8_vela.tflite').read_bytes())
        header = (ROOT / 'services/deepfir/deepfir_model_adapter.h').read_text()
        self.assertRegex(header, r'DEEPFIR_MODEL_OUTPUT_COUNT\s+4u')

    @unittest.skipUnless(PACKAGE.is_dir(), 'Source package/golden audit requires the local Git-ignored package')
    def test_source_hashes_trace_schema_and_counts(self):
        manifest = json.loads((PACKAGE / 'firmware_manifest.json').read_text())
        for name, digest in manifest['artifacts'].items():
            self.assertEqual(hashlib.sha256((PACKAGE / name).read_bytes()).hexdigest(), digest, name)
        for name, count in [('176f4e875a6dd550', 11324), ('c07ec5e74f1bd3dc', 15117)]:
            with np.load(PACKAGE / 'integer_replay' / (name + '.npz'), allow_pickle=False) as trace:
                self.assertEqual(set(trace.files), {'feature_codes', 'gain_codes', 'phase_codes', 'states'})
                for key, width in [('feature_codes', 774), ('gain_codes', 65), ('phase_codes', 65), ('states', 400)]:
                    self.assertEqual(trace[key].shape, (count, width))
                    self.assertEqual(trace[key].dtype, np.int16)


class R2NativeTests(unittest.TestCase):
    cmsis = False
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        dll = Path(cls.temp.name) / 'r2.dll'
        sources = ['tests/r2_numeric_harness.c', 'services/deepfir/deepfir_model_adapter.c',
                   'services/deepfir/deepfir_reconstruction.c', 'services/deepfir/deepfir_features.c',
                   'services/deepfir/deepfir_dsp.c', 'services/deepfir/deepfir_tensor_binding.c']
        extra = []
        if cls.cmsis:
            sources += ['services/deepfir/deepfir_cmsis_fft_adapter.c',
                'libs/CMSIS/DSP/Source/ControllerFunctions/arm_sin_cos_f32.c',
                'libs/CMSIS/DSP/Source/TransformFunctions/arm_cfft_f32.c',
                'libs/CMSIS/DSP/Source/TransformFunctions/arm_cfft_init_f32.c',
                'libs/CMSIS/DSP/Source/TransformFunctions/arm_cfft_radix8_f32.c',
                'libs/CMSIS/DSP/Source/TransformFunctions/arm_bitreversal2.c',
                'libs/CMSIS/DSP/Source/CommonTables/arm_common_tables.c',
                'libs/CMSIS/DSP/Source/CommonTables/arm_const_structs.c']
            extra = ['-DR2_TEST_CMSIS=1', '-DDEEPFIR_DSP_CMSIS_FFT=1', '-D__GNUC_PYTHON__', '-I', str(ROOT/'libs/CMSIS/DSP/Include'),
                     '-I', str(ROOT/'libs/CMSIS/DSP/PrivateInclude'), '-I', str(ROOT/'libs/CMSIS/Core/Include')]
        result = subprocess.run(['gcc', '-shared', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', *extra,
                                 '-I', str(ROOT), '-I', str(ROOT / 'services/deepfir'),
                                 *[str(ROOT / p) for p in sources], '-lm', '-o', str(dll)],
                                capture_output=True, text=True)
        if result.returncode:
            cls.temp.cleanup()
            raise AssertionError(result.stderr)
        cls.lib = ct.CDLL(str(dll))
        cls.lib.test_feedback.argtypes = [ct.c_int, ct.c_int]
        cls.lib.test_feedback.restype = ct.c_int
        cls.lib.test_step.argtypes = [ct.c_void_p, ct.c_void_p]
        cls.lib.test_taps.argtypes = [ct.c_void_p, ct.c_void_p, ct.c_void_p]
        cls.lib.test_features.argtypes = [ct.c_void_p, ct.c_void_p]
        cls.lib.test_context.restype = ct.c_int

    @classmethod
    def tearDownClass(cls):
        # Windows retains loaded DLLs until process exit; other temp files are removable.
        import _ctypes
        _ctypes.FreeLibrary(cls.lib._handle)
        cls.temp.cleanup()

    def setUp(self):
        self.lib.test_reset()

    def test_q31_all_int16_codes_and_binding_rejections(self):
        for lane, multiplier, shift in [(0, 2147371134, 31), (1, 1075624845, 30)]:
            for code in range(-32768, 32768):
                value = code * multiplier
                expected = (abs(value) + (1 << (shift-1))) >> shift
                expected = min(32767, max(-32768, -expected if value < 0 else expected))
                self.assertEqual(self.lib.test_feedback(lane, code), expected, (lane, code))
        self.assertEqual(self.lib.test_binding_rejections(), 0)

    def test_gain_phase_idft_endpoints_roll_taper(self):
        rng = np.random.default_rng(8192)
        scale = [5.657631845679134e-05, 7.72049679653719e-05]
        cases = [(np.zeros(65, np.int16), np.zeros(65, np.int16)),
                 (np.full(65, 17675, np.int16), np.zeros(65, np.int16))]
        cases += [(rng.integers(0, 32768, 65, dtype=np.int16), rng.integers(-32768, 32768, 65, dtype=np.int16)) for _ in range(12)]
        for gain, phase in cases:
            taps = np.empty(128, np.float32)
            self.assertEqual(self.lib.test_taps(gain.ctypes.data, phase.ctypes.data, taps.ctypes.data), 0)
            bins = gain.astype(float)*scale[0]*np.exp(1j*phase.astype(float)*scale[1])
            bins[[0, 64]] = bins[[0, 64]].real
            taper = .5+.5*(1-np.abs(np.arange(128)-64)/64)
            expected = np.roll(np.fft.irfft(bins, n=128), 64)*taper
            np.testing.assert_allclose(taps, expected, atol=2e-5, rtol=2e-5)

    def test_frontend_startup_history_fft_and_context(self):
        history = np.zeros(256)
        rng = np.random.default_rng(256)
        for hop in range(22):
            pcm = rng.integers(-32768, 32768, 16, dtype=np.int16) if hop else np.zeros(16, np.int16)
            actual = np.empty(387, np.int16)
            self.assertEqual(self.lib.test_features(pcm.ctypes.data, actual.ctypes.data), 0)
            history = np.concatenate([history[16:], pcm.astype(float)/32768])
            spectrum = np.fft.rfft(history*np.hamming(256))
            q = spectrum.real**2+spectrum.imag**2
            values = np.stack([np.sqrt(q)**.3, spectrum.real*(q+1e-16)**-.35,
                               spectrum.imag*(q+1e-16)**-.35], axis=1).reshape(-1)
            codes = values/9.672559826867655e-05
            expected = np.clip(np.sign(codes)*np.floor(np.abs(codes)+.5), -32768, 32767).astype(np.int16)
            self.assertLessEqual(np.max(np.abs(actual.astype(int)-expected)), 1)
        self.assertEqual(self.lib.test_context(), 0)

    def test_fir_crossfade_pcm_fault_latch_and_no_partial_publish(self):
        self.assertEqual(self.lib.test_fir_pcm(), 0)
        pcm = np.arange(16, dtype=np.int16)
        out = np.full(16, 1234, dtype=np.float32)
        self.lib.test_failure(1)
        self.assertNotEqual(self.lib.test_step(pcm.ctypes.data, out.ctypes.data), 0)
        np.testing.assert_array_equal(out, 1234)
        self.lib.test_failure(0)
        self.assertNotEqual(self.lib.test_step(pcm.ctypes.data, out.ctypes.data), 0)
        self.lib.test_reset()
        self.lib.test_failure(2)
        self.assertNotEqual(self.lib.test_step(pcm.ctypes.data, out.ctypes.data), 0)
        np.testing.assert_array_equal(out, 1234)
        self.lib.test_reset()
        self.assertEqual(self.lib.test_step(pcm.ctypes.data, out.ctypes.data), 0)
        self.assertNotEqual(self.lib.test_step(pcm.ctypes.data, pcm.ctypes.data), 0)


if __name__ == '__main__':
    unittest.main()
