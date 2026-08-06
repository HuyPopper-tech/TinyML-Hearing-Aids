"""Contract and state-safety tests for the DeepFilterNet inference adapter."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
HEADER = ROOT / "services" / "deepfilternet" / "deepfilternet_inference.h"
SOURCE = ROOT / "services" / "deepfilternet" / "deepfilternet_inference.cpp"
CONTRACT = ROOT / "models" / "deepfilternet" / "generated" / "deepfilternet_deploy_contract.h"


class DeepFilterNetInferenceTests(unittest.TestCase):
    def read(self, path: Path) -> str:
        return path.read_text(encoding="utf-8")

    def test_public_api_is_the_approved_frame_adapter(self):
        header = self.read(HEADER)
        self.assertIn("bool deepfilternet_inference_init(void);", header)
        self.assertIn("void deepfilternet_inference_reset(void);", header)
        self.assertRegex(
            header,
            r"bool deepfilternet_inference_invoke\(\s*"
            r"const int8_t erb_feature\[32\],\s*"
            r"const int8_t complex_feature\[288\],\s*"
            r"int8_t gain_bins\[257\],\s*"
            r"int8_t df_coefficients\[1440\],\s*"
            r"int8_t \*alpha\);",
        )

    def test_adapter_validates_complete_generated_contract_before_allocate(self):
        source = self.read(SOURCE)
        contract = self.read(CONTRACT)
        self.assertIn("DFN_INPUT_COUNT 9U", contract)
        self.assertIn("DFN_OUTPUT_COUNT 10U", contract)
        self.assertIn("DFN_TENSOR_COUNT 21U", contract)
        self.assertIn("DFN_OPERATOR_COUNT 1U", contract)
        self.assertIn("model_contract_valid", source)
        self.assertIn("tensor_contract_valid", source)
        self.assertIn("model_contract_valid(model)", source)
        self.assertLess(source.index("model_contract_valid(model)"), source.index("AllocateTensors"))
        self.assertIn("DFN_INPUT_0_NAME", source)
        self.assertIn("DFN_OUTPUT_9_NAME", source)
        self.assertIn("DFN_MODEL_SHA256", source)
        self.assertIn("tensor->name", source)
        self.assertIn("tensor->bytes", source)
        self.assertIn("bytes *=", source)
        self.assertIn("tensor->params.scale", source)
        self.assertIn("tensor->params.zero_point", source)

    def test_c_header_compiles_as_c_and_cpp(self):
        if shutil.which("gcc") is None or shutil.which("g++") is None:
            self.skipTest("gcc and g++ are required")
        probe = '#include "services/deepfilternet/deepfilternet_inference.h"\nint main(void) { return 0; }\n'
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "probe.c"
            path.write_text(probe, encoding="utf-8")
            c_result = subprocess.run(
                [shutil.which("gcc"), "-std=c11", "-pedantic-errors", "-fsyntax-only", "-I", str(ROOT), str(path)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(c_result.returncode, 0, c_result.stderr)
            cpp_result = subprocess.run(
                [shutil.which("g++"), "-std=c++11", "-pedantic-errors", "-fsyntax-only", "-I", str(ROOT), str(path)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(cpp_result.returncode, 0, cpp_result.stderr)

    def test_adapter_validates_post_vela_op_layout_and_arena_residency(self):
        source = self.read(SOURCE)
        self.assertNotIn("kSharedArenaBasepSlots", source)
        self.assertNotIn("kU55BasepLimit", source)
        self.assertIn("kExpectedEthosUInputTensorIndices", source)
        self.assertIn("kExpectedEthosUOutputTensorIndices", source)
        self.assertIn("kExpectedTensorBuffers", source)
        self.assertIn("0, 1, 2, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11", source)
        self.assertIn("12, 13, 14, 15, 16, 17, 18, 19, 20", source)
        self.assertIn("1U, 2U, 0U", source)
        self.assertIn("operation->inputs()", source)
        self.assertIn("operation->outputs()", source)
        self.assertIn("tensor->buffer()", source)
        self.assertIn("tensor_storage_in_arena", source)
        self.assertIn("logical_io_uses_shared_arena", source)
        self.assertNotIn("shared_arena_base", source)

    def test_reset_values_and_df0_requantization_are_explicit(self):
        source = self.read(SOURCE)
        for value in (
            "DFN_INPUT_ERB_CONV_CACHE_ZERO_POINT",
            "DFN_INPUT_COMPLEX_CONV_CACHE_ZERO_POINT",
            "DFN_INPUT_ENC_STATE_0_ZERO_POINT",
            "DFN_INPUT_ENC_STATE_1_ZERO_POINT",
            "DFN_INPUT_ENC_STATE_2_ZERO_POINT",
            "DFN_INPUT_DF_STATE_0_ZERO_POINT",
            "DFN_INPUT_DF_STATE_1_ZERO_POINT",
        ):
            self.assertIn(value, source)
        self.assertIn("DFN_OUTPUT_NEXT_DF_STATE_0", source)
        self.assertIn("deterministic", source.lower())
        self.assertRegex(source, r"std::round|roundf|floorf|ceilf")
        self.assertIn("saturat", source.lower())

    def test_invoke_stages_outputs_and_resets_on_any_failure(self):
        source = self.read(SOURCE)
        self.assertIn("std::memcpy(g_staged_gain", source)
        self.assertIn("std::memcpy(gain_bins, g_staged_gain", source)
        self.assertIn("deepfilternet_inference_reset();", source)
        self.assertIn("return false;", source)
        self.assertRegex(source, r"if \([^\n]*Invoke\(\)[^\n]*\)")
        self.assertIn("output_tensor_valid", source)

    def test_all_io_ordinals_map_to_generated_raw_indices(self):
        source = self.read(SOURCE)
        for ordinal, index in ((0, 11), (1, 5), (2, 8), (3, 9), (4, 10), (5, 6), (6, 4), (7, 3), (8, 7)):
            self.assertIn(f"DFN_INPUT_{ordinal}_ORDINAL", source)
            self.assertIn(f"DFN_INPUT_{ordinal}_INDEX", source)
        for ordinal, index in ((0, 16), (1, 14), (2, 13), (3, 20), (4, 19), (5, 15), (6, 18), (7, 12), (8, 17), (9, 10)):
            self.assertIn(f"DFN_OUTPUT_{ordinal}_ORDINAL", source)
            self.assertIn(f"DFN_OUTPUT_{ordinal}_INDEX", source)

    def test_static_arena_and_single_ethos_u_registration(self):
        source = self.read(SOURCE)
        self.assertIn("constexpr size_t kTensorArenaBytes = 256U * 1024U;", source)
        self.assertIn("MicroMutableOpResolver<1>", source)
        self.assertIn("AddEthosU", source)
        self.assertNotRegex(source, r"malloc|calloc|new\s+tflite::")

    def test_transaction_staging_is_adapter_owned_not_stack_owned(self):
        source = self.read(SOURCE)
        invoke = source[source.index("extern \"C\" bool deepfilternet_inference_invoke") :]
        invoke = invoke[:invoke.index("extern \"C\" void deepfilternet_inference_reset")]
        for name in (
            "staged_gain",
            "staged_df",
            "staged_alpha",
            "staged_erb_cache",
            "staged_complex_cache",
            "staged_enc_state_0",
            "staged_enc_state_1",
            "staged_enc_state_2",
            "staged_df_state_0",
            "staged_df_state_1",
        ):
            self.assertNotRegex(invoke, rf"int8_t {name}\[")
        self.assertIn("g_staged_gain", source)
        self.assertIn("foreground-only", source.lower())


if __name__ == "__main__":
    unittest.main()
