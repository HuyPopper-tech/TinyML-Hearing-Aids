"""Actual vendored scalar CMSIS FFT plus the same portable production DSP tests.

This checks FFT convention and the optimized reconstruction algorithm; Cortex
M55 MVE execution and its error envelope still require the board self-test.
"""
import test_r2_integration


class R2CmsisTests(test_r2_integration.R2NativeTests):
    cmsis = True
