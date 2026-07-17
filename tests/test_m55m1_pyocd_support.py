"""Regression tests for the repository-local M55M1 pyOCD safety override."""

from __future__ import annotations

import hashlib
import importlib.util
import unittest
from pathlib import Path

from pyocd.core.memory_map import FlashRegion, MemoryMap, RamRegion


REPO_ROOT = Path(__file__).resolve().parents[1]
SCRIPT_PATH = REPO_ROOT / "tools" / "pyocd" / "m55m1r2ljae.py"


def load_support_module():
    spec = importlib.util.spec_from_file_location("m55m1_pyocd_support", SCRIPT_PATH)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class M55M1PyOcdSupportTests(unittest.TestCase):
    def test_map_allows_only_secure_aprom_and_dtcm_flash_workspace(self):
        support = load_support_module()
        memory_map = MemoryMap(
            FlashRegion(name="IROM1", start=0x00100000, length=0x00200000, blocksize=0x2000),
            FlashRegion(name="IROM2", start=0x10100000, length=0x00200000, blocksize=0x2000),
            FlashRegion(name="IROM3", start=0x0F100000, length=0x00002000, blocksize=0x2000),
            RamRegion(name="IRAM1", start=0x20100000, length=0x00150000),
        )

        support.configure_memory_map(memory_map)

        dtcm = memory_map.get_region_for_address(0x20000000)
        self.assertIsNotNone(dtcm)
        self.assertEqual((dtcm.start, dtcm.length), (0x20000000, 0x00020000))

        flash_regions = [region for region in memory_map if region.is_flash]
        self.assertEqual(len(flash_regions), 1)
        aprom = flash_regions[0]
        self.assertEqual((aprom.start, aprom.length), (0x00100000, 0x00200000))
        self.assertEqual((aprom.page_size, aprom.sector_size), (0x2000, 0x2000))
        self.assertEqual(Path(aprom.flm), support.FLM_PATH)

    def test_vendored_flm_matches_the_pinned_sha256(self):
        support = load_support_module()

        self.assertTrue(support.FLM_PATH.is_file())
        self.assertEqual(
            hashlib.sha256(support.FLM_PATH.read_bytes()).hexdigest(),
            support.FLM_SHA256,
        )


if __name__ == "__main__":
    unittest.main()
