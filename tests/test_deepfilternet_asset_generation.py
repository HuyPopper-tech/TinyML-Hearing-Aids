"""Tests for deterministic DeepFilterNet firmware asset generation."""

from __future__ import annotations

import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "generate_deepfilternet_assets.py"
OUTPUT = ROOT / "models" / "deepfilternet" / "generated"
PACKAGE = ROOT / "models" / "deepfilternet" / "packages" / "deepfilternet_u55_full_seed20260805"


class DeepFilterNetAssetGenerationTests(unittest.TestCase):
    def run_generator(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            [sys.executable, str(GENERATOR), *arguments],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_generated_assets_are_current(self) -> None:
        result = self.run_generator("--check")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_deploy_identity_and_flatbuffer_contract_are_pinned(self) -> None:
        audit = json.loads((OUTPUT / "deploy_audit.json").read_text(encoding="utf-8"))
        self.assertEqual(audit["deploy_model"]["bytes"], 1197856)
        self.assertEqual(
            audit["deploy_model"]["sha256"],
            "7ee81189598812499796635c3f88ae4c4bca1ff9be81c79ceb4d1fd6d723e475",
        )
        self.assertEqual(audit["vela_contract"]["operator_names"], ["ethos-u"])
        self.assertEqual(audit["vela_contract"]["tensor_count"], 21)
        self.assertEqual(len(audit["vela_contract"]["inputs"]), 9)
        self.assertEqual(len(audit["vela_contract"]["outputs"]), 10)

    def test_frontend_and_fixtures_are_stripped_and_deterministic(self) -> None:
        dsp = (OUTPUT / "dsp_constants.h").read_text(encoding="utf-8")
        self.assertIn("dfn_erb_analysis_257x32", dsp)
        self.assertIn("dfn_sqrt_hann_512", dsp)
        self.assertNotIn("dfn_erb_expansion_32x257", dsp)
        self.assertNotRegex(dsp, r"(?<![\w.])[01]f(?!\w)")
        fixtures = sorted((OUTPUT / "golden").glob("golden_*.h"))
        self.assertEqual([path.stem for path in fixtures], [
            "golden_impulse", "golden_noise", "golden_silence",
            "golden_sine_3000", "golden_sine_440"
        ])
        self.assertEqual(json.loads((OUTPUT / "deploy_audit.json").read_text())["quality_scope"], "smoke_512_not_release_gate")

    def test_check_rejects_multiple_model_translation_units(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "generated"
            package_output = root / "package"
            generated = self.run_generator(
                "--output", str(output), "--package-output", str(package_output)
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            (output / "extra_model_data.c").write_text("/* stale */\n", encoding="utf-8")
            checked = self.run_generator(
                "--output", str(output), "--package-output", str(package_output), "--check"
            )
            self.assertNotEqual(checked.returncode, 0)

    def test_check_rejects_stale_model_hash_and_contract(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output = root / "generated"
            package_output = root / "package"
            generated = self.run_generator(
                "--output", str(output), "--package-output", str(package_output)
            )
            self.assertEqual(generated.returncode, 0, generated.stderr)
            package = package_output
            model = package / "deepfilternet_u55_int8_vela.tflite"
            model_bytes = model.read_bytes()
            model.write_bytes(b"stale" + model_bytes[5:])
            checked = self.run_generator(
                "--output", str(output), "--package-output", str(package_output), "--check"
            )
            self.assertNotEqual(checked.returncode, 0)
            model.write_bytes(model_bytes)

            contract = output / "deepfilternet_deploy_contract.h"
            contract_bytes = contract.read_bytes()
            contract.write_bytes(contract_bytes + b"\n/* stale mapping */\n")
            checked = self.run_generator(
                "--output", str(output), "--package-output", str(package_output), "--check"
            )
            self.assertNotEqual(checked.returncode, 0)


if __name__ == "__main__":
    unittest.main()
