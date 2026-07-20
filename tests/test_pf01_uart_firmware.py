"""Static contract checks for the PF0/PF1 UART0 firmware in app/main.c."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


class PF01UartFirmwareTests(unittest.TestCase):
    def test_main_uses_uart0_on_pf0_and_pf1_without_a_firmware_mode_switch(self):
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        source = (REPO_ROOT / "app" / "main.c").read_text(encoding="utf-8")

        self.assertNotIn("M55M1_FIRMWARE_MODE", cmake)
        self.assertIn("set(M55M1_APP_SOURCES ${PROJ_ROOT}/app/main.c)", cmake)
        self.assertIn('"PF0=UART0_TXD, PF1=UART0_RXD, 115200-8-N-1\\r\\n"', source)
        self.assertNotIn('"PF0=UART0_TXD, PF1=UART0_RXD, 115200-8-N-1\\\\r\\\\n"', source)

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
