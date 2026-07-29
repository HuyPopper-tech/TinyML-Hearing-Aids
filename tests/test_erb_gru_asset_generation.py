"""Behavior tests for ERB-GRU package verification and asset generation."""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = (
    ROOT
    / "models"
    / "erb_gru"
    / "packages"
    / "w4_erb-gru_deployment_v2_seed20260730"
)
GENERATOR = ROOT / "tools" / "generate_erb_gru_assets.py"


class ErbGruAssetGenerationTests(unittest.TestCase):
    def test_only_erb_gru_service_includes_model_artifacts(self):
        """Moving model-specific code into a driver must violate project layering."""
        violations = []
        for directory in ("app", "board", "common", "drivers", "services"):
            for path in (ROOT / directory).rglob("*"):
                if path.suffix not in {".c", ".cpp", ".h"}:
                    continue
                source = path.read_text(encoding="utf-8")
                if '"models/erb_gru/' not in source:
                    continue
                relative = path.relative_to(ROOT).as_posix()
                if not relative.startswith("services/erb_gru/"):
                    violations.append(relative)

        self.assertEqual(violations, [])

    @unittest.skipUnless(
        importlib.util.find_spec("tensorflow") is not None,
        "TensorFlow is required only for full model-package generation",
    )
    def test_generator_uses_vela_tensor_indices_and_verifies_generated_assets(self):
        """Wrong pre-Vela indices or stale generated files must fail package integration."""
        with tempfile.TemporaryDirectory() as temporary_directory:
            output = Path(temporary_directory)
            command = [
                sys.executable,
                str(GENERATOR),
                "--package",
                str(PACKAGE),
                "--output",
                str(output),
            ]

            generated = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(generated.returncode, 0, generated.stderr)

            contract = (output / "erb_gru_deploy_contract.h").read_text(encoding="utf-8")
            self.assertIn("#define ERB_GRU_INPUT_STATE1_INDEX 4", contract)
            self.assertIn("#define ERB_GRU_INPUT_STATE2_INDEX 3", contract)
            self.assertIn("#define ERB_GRU_INPUT_FEATURE_INDEX 5", contract)
            self.assertIn("#define ERB_GRU_OUTPUT_STATE2_INDEX 7", contract)
            self.assertIn("#define ERB_GRU_OUTPUT_STATE1_INDEX 6", contract)
            self.assertIn("#define ERB_GRU_OUTPUT_GAIN_INDEX 8", contract)
            self.assertIn("#define ERB_GRU_STATE1_ZERO_POINT -1", contract)
            self.assertIn("#define ERB_GRU_STATE2_ZERO_POINT 0", contract)

            audit = json.loads((output / "deploy_audit.json").read_text(encoding="utf-8"))
            self.assertEqual(audit["deploy_model"]["bytes"], 59232)
            self.assertEqual(
                audit["deploy_model"]["sha256"],
                "45f59d59598d74d36daebbb0d61b4d05020e164e4f78a62fcc31b4bb6b7d8fe8",
            )
            self.assertEqual(
                [tensor["index"] for tensor in audit["vela_contract"]["inputs"]],
                [4, 3, 5],
            )
            self.assertEqual(
                [tensor["index"] for tensor in audit["vela_contract"]["outputs"]],
                [7, 6, 8],
            )
            self.assertTrue(audit["model_header_matches_vela"])

            golden_path = output / "golden" / "golden_sine_1khz.h"
            self.assertTrue(golden_path.is_file())
            golden = golden_path.read_text(encoding="utf-8")
            self.assertNotRegex(golden, r"(?<![.\w])\-?(?:0|1)f(?:[,;])")
            self.assertIn("0.0f", golden)
            self.assertIn("erb_gru_sine_1khz_pcm_output_board_int16[4000]", golden)

            dsp_path = output / "dsp_constants.h"
            self.assertTrue(dsp_path.is_file())
            dsp = dsp_path.read_text(encoding="utf-8")
            self.assertNotRegex(dsp, r"(?<![.\w])\-?(?:0|1)f(?:[,;])")
            assets = (output / "erb_gru_assets.h").read_text(encoding="utf-8")
            self.assertIn('"models/erb_gru/generated/dsp_constants.h"', assets)
            self.assertNotIn("vela_model_data", assets)
            model_data = (output / "erb_gru_model_data.h").read_text(encoding="utf-8")
            self.assertIn("erb_gru_vela_model_data.h", model_data)
            self.assertTrue(audit["board_pcm_reference"]["sine_1khz"]["derived_from_pcm16"])

            checked = subprocess.run(
                [*command, "--check"], capture_output=True, text=True, check=False
            )
            self.assertEqual(checked.returncode, 0, checked.stderr)


if __name__ == "__main__":
    unittest.main()
