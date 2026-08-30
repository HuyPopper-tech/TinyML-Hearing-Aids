"""Notebook-derived NeuralAid DSP contract checks."""

import unittest

import numpy as np

from tools import generate_neuralaid_golden as reference


class NeuralAidDspReferenceTests(unittest.TestCase):
    def test_context_zero_pads_lookback_and_keeps_64_sample_lookahead(self):
        waveform = np.arange(reference.CHUNK_SAMPLES * 2, dtype=np.float32)
        contexts = reference.make_stream_contexts(waveform)
        pre = reference.pre_emphasis_sequence(waveform)

        np.testing.assert_array_equal(contexts[0, :96], np.zeros(96, np.float32))
        np.testing.assert_allclose(contexts[0, 96:192], pre[:96])
        np.testing.assert_allclose(contexts[0, 192:], pre[96:160])
        np.testing.assert_allclose(contexts[1, :96], pre[:96])

    def test_cmsis_packed_adapter_round_trips_dc_nyquist_and_complex_bins(self):
        packed = np.arange(reference.FFT_LENGTH, dtype=np.float32)
        np.testing.assert_array_equal(reference.tf_to_packed(reference.packed_to_tf(packed)), packed)

    def test_window_ola_reconstructs_preemphasized_identity_stream(self):
        waveform = np.sin(np.arange(768, dtype=np.float32) * 0.07)
        contexts = reference.make_stream_contexts(waveform)
        tail = np.zeros(reference.LOOKAHEAD_SAMPLES, np.float32)
        weights = np.zeros(reference.LOOKAHEAD_SAMPLES, np.float32)
        emitted = []

        for context in contexts[:8]:
            short = np.fft.irfft(np.fft.rfft(context), n=reference.FFT_LENGTH)[
                reference.LOOKBACK_SAMPLES:
            ] * reference.SYNTHESIS_WINDOW
            output, tail, weights = reference.synthesis_stream(short, tail, weights)
            emitted.append(output)
        np.testing.assert_allclose(
            np.concatenate(emitted)[: waveform.size],
            reference.pre_emphasis_sequence(waveform),
            atol=1.0e-5,
        )


if __name__ == "__main__":
    unittest.main()
