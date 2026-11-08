"""Static contract checks for the PF0/PF1 I2S0 headphone smoke test."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class PF01I2SFirmwareTests(unittest.TestCase):
    def test_selected_audio_firmware_avoids_the_pf0_pf1_uart0_conflict(self):
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        app = (REPO_ROOT / "app" / "main.c").read_text(encoding="utf-8")
        board = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )
        pipeline = (
            REPO_ROOT / "services" / "audio_pipeline" / "audio_pipeline.c"
        ).read_text(encoding="utf-8")

        self.assertIn("drivers/tlv320dac3100/tlv320dac3100.c", cmake)
        self.assertIn("board/m55m1_audio.c", cmake)
        self.assertIn("SET_I2S1_DO_PA8();", board)
        self.assertIn("SET_I2S1_LRCK_PB0();", board)
        self.assertIn("audio_transport_submit", pipeline)

        for forbidden in ("SET_I2S0_DO_PF0", "SET_I2S0_LRCK_PF1", "UART0PF01_Init", "SET_UART0_TXD_PF0", "SET_UART0_RXD_PF1", "UART_Open(UART0"):
            self.assertNotIn(forbidden, app + board + pipeline)


if __name__ == "__main__":
    unittest.main()
