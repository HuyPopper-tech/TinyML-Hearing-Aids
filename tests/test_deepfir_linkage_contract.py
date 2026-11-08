"""Regression checks for DeepFIR C APIs used by the C++ inference service."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirLinkageContractTests(unittest.TestCase):
    def test_c_headers_give_public_functions_c_linkage(self):
        declarations = {
            "services/deepfir/deepfir_dsp.h": "deepfir_feature_reset",
            "services/deepfir/deepfir_model_adapter.h": "deepfir_model_adapter_reset",
            "services/deepfir/deepfir_cmsis_fft_adapter.h": "deepfir_cmsis_fft_init",
        }
        for relative_path, symbol in declarations.items():
            with self.subTest(header=relative_path):
                content = (ROOT / relative_path).read_text(encoding="utf-8")
                linkage_start = content.index('extern "C" {')
                linkage_end = content.rindex("#ifdef __cplusplus\n}")
                declaration = content.index(symbol, linkage_start)
                self.assertLess(declaration, linkage_end)

    def test_service_validates_tensors_against_current_package_qparams(self):
        service = (
            ROOT / "services/deepfir/deepfir_inference_service.cpp"
        ).read_text(encoding="utf-8")
        self.assertIn('g_inputs[index] = find_tensor(true, index)', service)
        self.assertIn('g_outputs[index] = find_tensor(false, index)', service)
        self.assertIn('deepfir_tensor_validate(input, semantic, &actual)', service)
        # Exact shape/port/qparam rejection is executed in test_r2_integration.
        self.assertNotIn("0.0073748258873820305f", service)
        self.assertNotIn("0.005003856960684061f", service)


if __name__ == "__main__":
    unittest.main()
