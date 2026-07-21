"""Static contracts for the standalone DMIC and DAC hardware diagnostics."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class IsolatedAudioTestAppContractTests(unittest.TestCase):
    """Ensure both copy-and-paste diagnostic applications remain isolated."""

    def read_app(self, name: str) -> str:
        """Return a diagnostic source file after asserting that it exists."""
        path = ROOT / "app" / name
        self.assertTrue(path.is_file(), f"missing standalone test app: {path}")
        return path.read_text(encoding="utf-8")

    def test_dmic_uart5_app_captures_and_reports_without_i2s(self) -> None:
        """The microphone diagnostic owns UART5 and does not start audio output."""
        source = self.read_app("test_dmic_uart5_main.c")

        self.assertIn("SET_UART5_TXD_PB5();", source)
        self.assertIn("SET_UART5_RXD_PB4();", source)
        self.assertIn("UART_Open(UART5, DMIC_UART5_BAUD);", source)
        self.assertIn("m55m1_tlv320dac3100_test_dmic_start", source)
        self.assertIn("m55m1_tlv320dac3100_test_dmic_take_block", source)
        self.assertIn("UART_Write(UART5", source)
        self.assertNotIn("tlv320dac3100_start", source)

    def test_dac_tone_app_submits_i2s_tone_without_uart_or_dmic(self) -> None:
        """The DAC diagnostic owns I2S0 and avoids the conflicting UART5 pins."""
        source = self.read_app("test_dac_i2s_tone_main.c")

        self.assertIn("TLV320DAC3100_TONE_HZ", source)
        self.assertIn("m55m1_tlv320dac3100_test_board_init", source)
        self.assertIn("tlv320dac3100_init", source)
        self.assertIn("tlv320dac3100_set_digital_gain_db(-12)", source)
        self.assertIn("tlv320dac3100_start", source)
        self.assertIn("tlv320dac3100_submit_mono_block", source)
        self.assertIn("g_tone_block", source)
        self.assertNotIn("UART5_MODULE", source)
        self.assertNotIn("m55m1_tlv320dac3100_test_dmic_start", source)


if __name__ == "__main__":
    unittest.main()
