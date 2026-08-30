"""NeuralAid U55 firmware integration contract."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class NeuralAidFirmwareContractTests(unittest.TestCase):
    def test_neuralaid_is_an_export_package_not_the_production_pipeline(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn('option(M55M1_ENABLE_NEURALAID', cmake)
        self.assertNotIn('services/neuralaid/neuralaid_inference.cpp', cmake)
        self.assertNotIn('services/neuralaid/neuralaid_audio_enhancer.cpp', cmake)
        self.assertNotIn('models/neuralaid/generated/neuralaid_u55_vela_model_data.cc', cmake)
        self.assertNotIn('neuralaid_audio_enhancer_process', pipeline)
        self.assertIn('speech_boosting_audio_enhancer_process', pipeline)

    def test_firmware_package_keeps_export_contract_and_state_rules(self):
        generated = ROOT / 'models/neuralaid/generated'
        contract = (generated / 'neuralaid_deploy_contract.h').read_text(encoding='utf-8')
        inference = (ROOT / 'services/neuralaid/neuralaid_inference.cpp').read_text(encoding='utf-8')

        self.assertTrue((generated / 'neuralaid_u55_vela_model_data.cc').is_file())
        self.assertIn('NEURALAID_MODEL_INPUT_COUNT 15U', contract)
        self.assertIn('NEURALAID_MODEL_OUTPUT_COUNT 15U', contract)
        self.assertIn('NEURALAID_MODEL_SHA256', contract)
        for name in ('conv_history', 'dual_path_history', 'lstm_h_3', 'lstm_c_0'):
            self.assertIn(name, inference)
        self.assertIn('requantize_state', inference)

    def test_neuralaid_export_does_not_own_production_uart_diagnostics(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(
            encoding="utf-8"
        )
        self.assertNotIn("M55M1_NEURALAID_DIAGNOSTICS", cmake)
        self.assertIn("M55M1_SPEECH_BOOSTING_DIAGNOSTICS", cmake)
        self.assertIn("debug_uart_tx_try_enqueue", pipeline)
        self.assertIn("audio_transport_cycles_elapsed", pipeline)
        self.assertNotIn("neuralaid_audio_enhancer_get_diagnostics", pipeline)
        self.assertIn("speech_boosting_audio_enhancer_get_diagnostics", pipeline)

    def test_neuralaid_diagnostics_prove_runtime_ethosu_execution(self):
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(encoding="utf-8")
        inference = (ROOT / "services/neuralaid/neuralaid_inference.cpp").read_text(encoding="utf-8")
        driver = (ROOT / "drivers/ethosu_u55/ethosu_u55.c").read_text(encoding="utf-8")

        self.assertIn("ethosu_u55_get_diagnostics", pipeline)
        self.assertIn("cpu_operator_count", inference)
        self.assertIn("ethosu_operator_count", inference)
        self.assertIn("ETHOSU_PMU_NPU_ACTIVE", driver)
        self.assertIn("ethosu_inference_begin", driver)
        self.assertIn("ethosu_inference_end", driver)

    def test_neuralaid_requantization_uses_init_time_lookup_table(self):
        inference = (ROOT / "services/neuralaid/neuralaid_inference.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("g_requant_lut", inference)
        self.assertIn("build_requant_lut", inference)
        self.assertIn("g_requant_lut[i][", inference)

    def test_neuralaid_inference_reports_cpu_and_invoke_stages(self):
        inference = (ROOT / "services/neuralaid/neuralaid_inference.cpp").read_text(
            encoding="utf-8"
        )
        header = (ROOT / "services/neuralaid/neuralaid_inference.h").read_text(
            encoding="utf-8"
        )

        for name in ("input_state_copy_us", "invoke_only_us", "output_state_us"):
            self.assertIn(name, inference)
            self.assertIn(name, header)

    def test_neuralaid_dsp_contract_remains_self_contained_in_its_export_package(self):
        enhancer = (ROOT / "services/neuralaid/neuralaid_audio_enhancer.cpp").read_text(
            encoding="utf-8"
        )
        header = (ROOT / "services/neuralaid/neuralaid_audio_enhancer.h").read_text(
            encoding="utf-8"
        )
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("neuralaid_contract.h", enhancer)
        self.assertIn("kLookbackSamples", enhancer)
        self.assertIn("kLookaheadSamples = NEURALAID_LOOKAHEAD_SAMPLES", enhancer)
        self.assertIn("cmsis_rfft_packed_to_tf", enhancer)
        self.assertIn("tf_to_cmsis_rfft_packed", enhancer)
        self.assertIn("g_neuralaid_synthesis_window", enhancer)
        self.assertIn("ola_weight", enhancer)
        self.assertIn("logical_frame", header)
        self.assertNotIn("neuralaid_window_coeffs.c", cmake)

    def test_neuralaid_uses_distinct_model_input_and_output_quantization(self):
        enhancer = (ROOT / "services/neuralaid/neuralaid_audio_enhancer.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("kModelInputTfScale", enhancer)
        self.assertIn("kModelOutputTfScale", enhancer)
        self.assertIn("kModelOutputTfZeroPoint", enhancer)

    def test_neuralaid_export_does_not_own_live_dac_diagnostics(self):
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(
            encoding="utf-8"
        )

        self.assertNotIn("neuralaid_audio_enhancer_process", pipeline)
        self.assertIn("speech_boosting_audio_enhancer_process", pipeline)
        self.assertIn("audio_transport_get_health", pipeline)

    def test_neuralaid_golden_generator_uses_raw_tflite_and_notebook_dsp_contract(self):
        generator = (ROOT / "tools/generate_neuralaid_golden.py").read_text(
            encoding="utf-8"
        )

        self.assertIn("neuralaid_int8_raw.tflite", generator)
        self.assertIn("STREAMING_GOLDEN_CHUNKS = 100", generator)
        self.assertIn("LOOKBACK_SAMPLES = 96", generator)
        self.assertIn("LOOKAHEAD_SAMPLES = 64", generator)
        self.assertIn("synthesis_stream", generator)
        self.assertIn("tf_input_int8", generator)

    def test_neuralaid_inference_rejects_wrong_tf_tensor_quantization_contract(self):
        inference = (ROOT / "services/neuralaid/neuralaid_inference.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("verify_tensor_contract", inference)
        self.assertIn("kTfInputScale", inference)
        self.assertIn("kTfOutputScale", inference)


if __name__ == '__main__':
    unittest.main()
