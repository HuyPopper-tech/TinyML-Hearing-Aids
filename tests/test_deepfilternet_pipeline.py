"""Production DeepFilterNet pipeline and 16 kHz transport contract."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepFilterNetPipelineTests(unittest.TestCase):
    def read(self, path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")

    def test_cmake_links_only_deepfilternet_production_sources(self):
        cmake = self.read("CMakeLists.txt")
        self.assertIn("option(M55M1_ENABLE_DEEPFILTERNET", cmake)
        self.assertIn("services/deepfilternet/deepfilternet_inference.cpp", cmake)
        self.assertIn("services/deepfilternet/deepfilternet_audio_enhancer.cpp", cmake)
        self.assertIn("models/deepfilternet/generated/deepfilternet_u55_vela_model_data.c", cmake)
        self.assertNotIn("services/erb_gru/", cmake)

    def test_stack_monitor_calls_are_diagnostics_only(self):
        transport = self.read("drivers/audio_transport/audio_transport.c")
        pipeline = self.read("services/audio_pipeline/audio_pipeline.c")
        self.assertIn(
            "#if M55M1_DEEPFILTERNET_DIAGNOSTICS\n"
            "uint32_t audio_transport_stack_configured_bytes", transport)
        self.assertIn(
            "#if M55M1_DEEPFILTERNET_DIAGNOSTICS\n"
            "static void audio_pipeline_board_progress", pipeline)

    def test_audio_block_contract_is_direct_256_samples(self):
        transport = self.read("drivers/audio_transport/audio_transport.h")
        board = self.read("board/m55m1_audio.c")
        codec = self.read("drivers/tlv320dac3100/tlv320dac3100.h")
        self.assertIn("AUDIO_TRANSPORT_SAMPLES_PER_BLOCK    256U", transport)
        self.assertIn("return 256U;", board)
        self.assertIn("TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK      256U", codec)
        self.assertIn("TLV320DAC3100_32KHZ_SAMPLES_PER_BLOCK      320U", codec)

    def test_pipeline_calls_dfn_once_and_attributes_one_hop_tag(self):
        pipeline = self.read("services/audio_pipeline/audio_pipeline.c")
        self.assertEqual(pipeline.count("deepfilternet_audio_enhancer_process("), 1)
        self.assertIn("deepfilternet_audio_enhancer_discontinuity();", pipeline)
        self.assertIn("g_pending_tag", pipeline)
        self.assertIn("audio_transport_submit(g_enhanced_block)", pipeline)
        self.assertIn("audio_transport_submit_tagged(g_enhanced_block, &g_pending_tag)", pipeline)
        self.assertIn("g_pending_tag = current_tag", pipeline)
        self.assertIn("audio_transport_submit_tagged(", pipeline)

    def test_deadline_gate_reports_cumulative_16_ms_misses(self):
        pipeline = self.read("services/audio_pipeline/audio_pipeline.c")
        enhancer = self.read(
            "services/deepfilternet/deepfilternet_audio_enhancer.cpp"
        )
        telemetry = self.read("services/audio_telemetry.c")
        self.assertIn(".deadline_miss = enhancer_diagnostics.deadline_miss_count", pipeline)
        self.assertIn("g_diagnostics.e2e_us > 16000U", enhancer)
        self.assertIn("++g_diagnostics.deadline_miss_count", enhancer)
        self.assertNotIn("e2e_40ms", telemetry)

    def test_fault_hooks_and_golden_selection_are_explicit(self):
        cmake = self.read("CMakeLists.txt")
        inference = self.read("services/deepfilternet/deepfilternet_inference.cpp")
        for scenario in ("silence", "impulse", "sine_440", "sine_3000", "noise"):
            self.assertIn(scenario, cmake)
        self.assertIn("MODEL_GOLDEN_TEST_DATA=1", cmake)
        self.assertIn("M55M1_DEEPFILTERNET_FAULT_INVOKE_FAIL", inference)
        self.assertIn("M55M1_DEEPFILTERNET_FAULT_INVOKE_SLOW", inference)
        self.assertIn("ethosu_u55_fault_delay_ms(12U)", inference)


if __name__ == "__main__":
    unittest.main()
