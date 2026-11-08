"""Exercise the firmware SHA-256 implementation with the full Vela model."""

from __future__ import annotations

import ctypes
import _ctypes
import hashlib
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
MODEL = ROOT / 'models/deepfir/deepfir_model_data.cc'


class Sha256LongInputTests(unittest.TestCase):
    def test_firmware_sha256_matches_hashlib_for_full_model(self):
        compiler = shutil.which("gcc")
        if compiler is None:
            self.skipTest("gcc is unavailable")

        with tempfile.TemporaryDirectory() as temporary:
            library = Path(temporary) / "sha256.dll"
            subprocess.run(
                [
                    compiler,
                    "-shared",
                    "-O0",
                    "-I",
                    str(ROOT),
                    str(ROOT / "common" / "sha256.c"),
                    "-o",
                    str(library),
                ],
                check=True,
            )
            module = ctypes.CDLL(str(library))
            sha256 = module.sha256_compute
            sha256.argtypes = [
                ctypes.POINTER(ctypes.c_uint8),
                ctypes.c_size_t,
                ctypes.POINTER(ctypes.c_uint8),
            ]
            model = bytes(int(code, 16) for code in re.findall(r'0x([0-9a-fA-F]{2})\b', MODEL.read_text()))
            self.assertEqual(len(model), 516848)
            source = (ctypes.c_uint8 * len(model)).from_buffer_copy(model)
            digest = (ctypes.c_uint8 * 32)()
            sha256(source, len(model), digest)
            self.assertEqual(hashlib.sha256(model).digest(), bytes(digest))
            del sha256
            _ctypes.FreeLibrary(module._handle)


if __name__ == "__main__":
    unittest.main()
