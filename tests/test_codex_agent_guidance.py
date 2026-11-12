"""Regression checks for the repository-local Codex agent guidance."""

from __future__ import annotations

import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
GUIDANCE_PATH = REPO_ROOT / "AGENTS.md"


class CodexAgentGuidanceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.guidance = GUIDANCE_PATH.read_text(encoding="utf-8")

    def test_architecture_and_vendor_scope_are_documented(self):
        self.assertIn("app -> services -> drivers -> board -> libs", self.guidance)
        self.assertIn("`libs/` is vendored", self.guidance)
        self.assertIn("Do not edit `libs/`", self.guidance)
        self.assertIn("optional and dependency-free", self.guidance)
        self.assertRegex(self.guidance, r"generated or\s+read-only")

    def test_hardware_and_build_constraints_are_documented(self):
        self.assertIn("BLE configuration only; do not add OTA", self.guidance)
        self.assertIn("dedicated UART TX, RX, RTS, and CTS", self.guidance)
        self.assertIn("WKP, LINK, MOD, and DISC", self.guidance)
        self.assertRegex(self.guidance, r"SWDIO, SWCLK, nRESET,\s+GND, and VREF/3V3")
        self.assertIn("Do not use ESP-IDF APIs or ESP-IDF CMake", self.guidance)

    def test_isr_and_settings_persistence_rules_are_documented(self):
        self.assertIn("bare-metal event loop", self.guidance)
        self.assertIn("CMSIS-RTOS2 is not used currently", self.guidance)
        self.assertIn("ISRs must not block, allocate memory, log", self.guidance)
        self.assertRegex(
            self.guidance,
            r"pending -> active at an audio frame\s+boundary -> explicit commit -> deferred NVM write",
        )

    def test_c_style_rules_are_documented(self):
        self.assertIn("`snake_case`", self.guidance)
        self.assertIn("`snake_case_t`", self.guidance)
        self.assertIn("module-prefixed `UPPER_CASE`", self.guidance)
        self.assertIn("four spaces", self.guidance)

    def test_validation_guidance_is_documented(self):
        self.assertIn("`python -m unittest discover -s tests -v`", self.guidance)
        self.assertIn("`git diff --check`", self.guidance)
        self.assertIn("`build.bat debug build`", self.guidance)
        self.assertIn("`./build.sh debug build`", self.guidance)


if __name__ == "__main__":
    unittest.main()
