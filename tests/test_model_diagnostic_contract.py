"""Contract tests for the checked-in DeepFIR production build."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "models" / "deepfir"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class DeepfirProductContractTests(unittest.TestCase):
    def test_package_provenance_and_local_hashes_match(self):
        integration = json.loads((PACKAGE / "integration_manifest.json").read_text())
        upstream = json.loads((PACKAGE / "source_integration_manifest.json").read_text())
        self.assertFalse(integration["deployment_approved"])
        self.assertFalse(upstream["deployment_approved"])
        for name, digest in integration["files"].items():
            self.assertEqual(digest, sha256(PACKAGE / name), name)

    def test_three_input_four_output_int16_contract(self):
        binding = json.loads((PACKAGE / "compiled_tensor_binding.json").read_text())
        self.assertEqual(["feature_context", "c", "h"], [t["semantic"] for t in binding["inputs"]])
        self.assertEqual(["h_next", "c_next", "phase", "gain"], [t["semantic"] for t in binding["outputs"]])
        self.assertEqual([1, 2, 129, 3], binding["inputs"][0]["shape"])
        for tensor in binding["inputs"] + binding["outputs"]:
            self.assertEqual("int16", tensor["dtype"])
            self.assertEqual(0, tensor["zero_point"])
            self.assertGreater(tensor["scale"], 0)

    def test_product_build_has_sram_model_and_no_diagnostic_entrypoint(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        scatter = (ROOT / "cmake" / "m55m1_audio.scf").read_text()
        self.assertIn("M55M1_MODEL_IN_SRAM", cmake)
        self.assertIn("U55_MODEL_RAM", scatter)
        self.assertIn("MODEL_IMAGE_SRAM_SIZE 0x000C0000", scatter)
        self.assertNotIn("M55M1_MODEL_DIAGNOSTIC", cmake)
        self.assertNotIn("model_diagnostic_main.cpp", cmake)
        service = (ROOT / "services" / "deepfir" / "deepfir_inference_service.cpp").read_text()
        self.assertIn("immutable_memory", service)
        self.assertIn("DEEPFIR_TENSOR_ARENA_BYTES = 64U * 1024U", service)
        self.assertIn("deepfir_model_adapter_process_pcm16", service)

    def test_debug_only_latency_and_release_silent_policy(self):
        cmake = (ROOT / "CMakeLists.txt").read_text()
        pipeline = (ROOT / "services" / "audio_pipeline" / "audio_pipeline.c").read_text()
        self.assertIn("M55M1_RUNTIME_TELEMETRY_VALUE", cmake)
        self.assertIn("#if M55M1_RUNTIME_TELEMETRY", pipeline)
        self.assertIn("feature_us", pipeline)
        self.assertNotIn("MODEL_DIAGNOSTIC", pipeline)
        for wrapper in ("build.bat", "build.sh"):
            text = (ROOT / wrapper).read_text(encoding="utf-8")
            self.assertNotIn("deepfir_ptq_20260921_033946", text)
            self.assertNotIn("DEEPFIR_DIAGNOSTIC", text)
            self.assertIn("M55M1_ENABLE_TFLM=ON", text)
            self.assertIn("M55M1_ENABLE_ETHOSU_RUNTIME=ON", text)
            self.assertIn("M55M1_ENABLE_DCACHE=ON", text)
            self.assertIn("-DM55M1_MODEL_IN_SRAM=ON", text)
            self.assertNotIn("-DM55M1_DEEPFIR_MODEL_IN_SRAM=ON", text)

    def test_deepfir_sources_follow_project_ownership(self):
        model_files = {
            "deepfir_model_data.cc",
            "deepfir_model_data.h",
            "deepfir_model_qparams.h",
            "m55m1_model_package.cmake",
            "integration_manifest.json",
        }
        service_files = {
            "deepfir_model_adapter.c",
            "deepfir_model_adapter.h",
            "deepfir_model_package.cc",
            "deepfir_model_package.h",
            "deepfir_inference_service.cpp",
            "deepfir_inference_service.h",
            "deepfir_dsp.c",
            "deepfir_dsp.h",
            "deepfir_cmsis_fft_adapter.c",
            "deepfir_cmsis_fft_adapter.h",
        }
        self.assertTrue(all((ROOT / "models" / "deepfir" / name).is_file() for name in model_files))
        self.assertTrue(all((ROOT / "services" / "deepfir" / name).is_file() for name in service_files))


if __name__ == "__main__":
    unittest.main()
