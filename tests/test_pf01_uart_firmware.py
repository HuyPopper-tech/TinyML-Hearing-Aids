"""Static contract checks for the opt-in PF0/PF1 UART0 test firmware."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class PF01UartFirmwareTests(unittest.TestCase):
    def test_pf01_uart_mode_is_opt_in_and_keeps_dmic_as_the_default(self):
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn('set(M55M1_FIRMWARE_MODE "dmic" CACHE STRING', cmake)
        self.assertIn('if(M55M1_FIRMWARE_MODE STREQUAL "dmic")', cmake)
        self.assertIn('${PROJ_ROOT}/app/main.c', cmake)
        self.assertIn('elseif(M55M1_FIRMWARE_MODE STREQUAL "pf01_uart")', cmake)
        self.assertIn('${PROJ_ROOT}/app/pf01_uart_test.c', cmake)
        self.assertIn('Unsupported M55M1_FIRMWARE_MODE', cmake)


if __name__ == "__main__":
    unittest.main()
