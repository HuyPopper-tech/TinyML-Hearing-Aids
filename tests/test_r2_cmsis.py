"""Actual vendored scalar CMSIS FFT plus the same portable production DSP tests.

This checks FFT convention and the optimized reconstruction algorithm; Cortex
M55 MVE execution and its error envelope still require the board self-test.
"""
import test_r2_integration
import numpy as np


class R2CmsisTests(test_r2_integration.R2NativeTests):
    cmsis = True

    def test_all_phase_codes_preserve_reconstruction_tolerance(self):
        gain = np.full(65, 32767, np.int16)
        taper = .5 + .5 * (1 - np.abs(np.arange(128) - 64) / 64)
        maximum = 0.0
        for start in range(-32768, 32768, 65):
            phase = np.minimum(np.arange(start, start + 65), 32767).astype(np.int16)
            taps = np.empty(128, np.float32)
            self.assertEqual(self.lib.test_taps(gain.ctypes.data, phase.ctypes.data, taps.ctypes.data), 0)
            bins = gain.astype(float) * 5.657631845679134e-05 * np.exp(
                1j * phase.astype(float) * 7.72049679653719e-05)
            bins[[0, 64]] = bins[[0, 64]].real
            expected = np.roll(np.fft.irfft(bins, n=128), 64) * taper
            maximum = max(maximum, float(np.max(np.abs(taps - expected))))
            np.testing.assert_allclose(taps, expected, atol=2e-5, rtol=2e-5)
        self.assertLessEqual(maximum, 2e-5)
        print(f'CMSIS phase INT16 sweep: 65536 codes, max_abs_tap_error={maximum:.9g}')
