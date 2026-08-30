"""Capture-only build contract for Speech Boosting Vela measurements."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class SpeechBoostingCaptureOnlyTests(unittest.TestCase):
    def test_cmake_can_exclude_dac_i2s_output_driver(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn('option(M55M1_ENABLE_DAC_I2S_OUTPUT', cmake)
        self.assertIn('if(M55M1_ENABLE_DAC_I2S_OUTPUT)', cmake)
        self.assertIn('drivers/tlv320dac3100/tlv320dac3100.c', cmake)
        self.assertIn('M55M1_ENABLE_DAC_I2S_OUTPUT=', cmake)

    def test_capture_only_path_excludes_dac_i2s_initialization_and_submission(self):
        transport = (ROOT / "drivers/audio_transport/audio_transport.c").read_text(
            encoding="utf-8"
        )
        board = (ROOT / "board/m55m1_audio.c").read_text(encoding="utf-8")
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(
            encoding="utf-8"
        )

        self.assertIn('#if M55M1_ENABLE_DAC_I2S_OUTPUT', transport)
        self.assertIn('#if M55M1_ENABLE_DAC_I2S_OUTPUT', board)
        self.assertIn('#if M55M1_ENABLE_DAC_I2S_OUTPUT', pipeline)
        self.assertIn('audio_output_disabled', pipeline)


if __name__ == "__main__":
    unittest.main()
