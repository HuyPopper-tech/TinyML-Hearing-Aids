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

    def test_pf01_uart_test_uses_uart0_and_never_restores_ice(self):
        source_path = REPO_ROOT / "app" / "pf01_uart_test.c"
        self.assertTrue(source_path.exists())
        source = source_path.read_text(encoding="utf-8")

        for required in (
            "SET_UART0_TXD_PF0();",
            "SET_UART0_RXD_PF1();",
            "CLK_EnableModuleClock(UART0_MODULE);",
            "CLK_UARTSEL0_UART0SEL_HIRC",
            "UART_Open(UART0, 115200UL);",
            "UART_Write(UART0,",
        ):
            self.assertIn(required, source)

        for forbidden in ("SET_ICE_DAT_PF0", "SET_ICE_CLK_PF1", "DPM_SetDebug"):
            self.assertNotIn(forbidden, source)


if __name__ == "__main__":
    unittest.main()
