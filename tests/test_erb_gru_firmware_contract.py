"""Regression checks for the deployed ERB-GRU Ethos-U firmware contract."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ErbGruFirmwareContractTests(unittest.TestCase):
    def read(self, path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")

    def test_cmake_exposes_safe_build_and_fault_options(self):
        cmake = self.read("CMakeLists.txt")
        for required in (
            'set(M55M1_STACK_SIZE "0x2000"',
            "option(M55M1_ENABLE_ERB_GRU",
            "option(M55M1_ENABLE_NPU",
            'set(M55M1_ERB_GRU_INPUT_GAIN_DB "0"',
            'set(M55M1_ERB_GRU_GOLDEN_SCENARIO ""',
            'set(M55M1_ERB_GRU_FAULT_INJECTION "OFF"',
            "Production Release firmware rejects golden data and fault injection",
            "drivers/ethosu_u55/ethosu_u55.c",
            "services/erb_gru/erb_gru_inference.cpp",
            "services/erb_gru/erb_gru_audio_enhancer.cpp",
        ):
            self.assertIn(required, cmake)
        self.assertNotIn("M55M1_ERB_GRU_DSP_PROBE", cmake)
        self.assertNotIn("drivers/ethosu_tflm", cmake)

    def test_release_gain_workaround_is_scoped_to_the_reduction_loop(self):
        source = self.read("services/erb_gru/erb_gru_audio_enhancer.cpp")
        app = self.read("app/main.c")

        reduction = source[
            source.index("if (inference_ok)\n        {", source.index("void expand_gains")) :
            source.index("g_bin_gains[bin] = bin_gain;", source.index("void expand_gains"))
        ]
        self.assertIn("#pragma clang loop vectorize(disable) interleave(disable)", reduction)
        self.assertIn("Armclang 6.24", reduction)
        self.assertIn("approximately 0.5", reduction)
        self.assertIn("NO_VECTORIZE", reduction)
        self.assertIn("host", reduction)
        self.assertIn("board", reduction)
        self.assertNotIn("M55M1_ERB_GRU_DSP_PROBE", app)

    def test_diagnostics_flag_gates_all_runtime_logging_sources(self):
        cmake = self.read("CMakeLists.txt")
        app = self.read("app/main.c")

        self.assertIn(
            'option(M55M1_ERB_GRU_DIAGNOSTICS "Build 1 Hz ERB-GRU diagnostics" OFF)',
            cmake,
        )
        self.assertIn("if(M55M1_ERB_GRU_DIAGNOSTICS)", cmake)
        self.assertIn(
            "M55M1_ERB_GRU_DIAGNOSTICS=${M55M1_ERB_GRU_DIAGNOSTICS_VALUE}",
            cmake,
        )
        self.assertIn("Golden scenarios require M55M1_ERB_GRU_DIAGNOSTICS=ON", cmake)
        self.assertIn("#if M55M1_ERB_GRU_DIAGNOSTICS", app)
        self.assertIn('"[BUILD] type=%s diagnostics=1 gain_db=%d "', app)
        self.assertIn("M55M1_ERB_GRU_GIT_REV", cmake)
        self.assertIn("M55M1_ERB_GRU_GIT_DIRTY", cmake)

    def test_fault_injection_is_not_reported_as_a_real_npu_error(self):
        inference = self.read("services/erb_gru/erb_gru_inference.cpp")
        header = self.read("services/erb_gru/erb_gru_inference.h")
        app = self.read("app/main.c")
        telemetry = self.read("services/audio_telemetry.c")

        fault_branch = inference[
            inference.index("#if M55M1_ERB_GRU_FAULT_INVOKE_FAIL") :
            inference.index("#endif", inference.index("#if M55M1_ERB_GRU_FAULT_INVOKE_FAIL"))
        ]
        self.assertIn("g_fault_injected_count", fault_branch)
        self.assertNotIn("g_invoke_error_count", fault_branch)
        self.assertIn("fault_injected_count", header)
        self.assertIn("g_telemetry.fault_injected", app)
        self.assertIn("fault_injected=%lu", telemetry)

    def test_generated_contract_uses_vela_ordinals_and_raw_indices(self):
        contract = self.read("models/erb_gru/generated/erb_gru_deploy_contract.h")
        for required in (
            "ERB_GRU_INPUT_STATE1_ORDINAL 0",
            "ERB_GRU_INPUT_STATE2_ORDINAL 1",
            "ERB_GRU_INPUT_FEATURE_ORDINAL 2",
            "ERB_GRU_INPUT_STATE1_INDEX 4",
            "ERB_GRU_INPUT_STATE2_INDEX 3",
            "ERB_GRU_INPUT_FEATURE_INDEX 5",
            "ERB_GRU_OUTPUT_STATE2_ORDINAL 0",
            "ERB_GRU_OUTPUT_STATE1_ORDINAL 1",
            "ERB_GRU_OUTPUT_GAIN_ORDINAL 2",
            "ERB_GRU_OUTPUT_STATE2_INDEX 7",
            "ERB_GRU_OUTPUT_STATE1_INDEX 6",
            "ERB_GRU_OUTPUT_GAIN_INDEX 8",
            "ERB_GRU_STATE1_ZERO_POINT -1",
            "ERB_GRU_STATE2_ZERO_POINT 0",
            "ERB_GRU_MODEL_BYTES 59232U",
            "45f59d59598d74d36daebbb0d61b4d05020e164e4f78a62fcc31b4bb6b7d8fe8",
        ):
            self.assertIn(required, contract)

    def test_model_bytes_only_enter_the_inference_translation_unit(self):
        inference = self.read("services/erb_gru/erb_gru_inference.cpp")
        enhancer = self.read("services/erb_gru/erb_gru_audio_enhancer.cpp")
        assets = self.read("models/erb_gru/generated/erb_gru_assets.h")
        self.assertIn("erb_gru_model_data.h", inference)
        self.assertNotIn("vela_model_data", enhancer)
        self.assertNotIn("vela_model_data", assets)

    def test_board_driver_and_service_own_separate_ethosu_responsibilities(self):
        board = self.read("board/m55m1_ethosu_u55.c")
        driver = self.read("drivers/ethosu_u55/ethosu_u55.c")
        inference = self.read("services/erb_gru/erb_gru_inference.cpp")
        self.assertIn("NVIC_SetPriority(NPU_IRQn, 2U)", board)
        self.assertNotIn("ethosu_driver", board)
        self.assertIn("NPU_IRQHandler", driver)
        self.assertIn("ethosu_irq_handler", driver)
        self.assertIn("DCACHE_ALIGN_LINE_SIZE", driver)
        for required in (
            "AddEthosU",
            "model_contract_valid",
            "model_hash_valid",
            "tensors_valid",
            "arena_used_bytes",
            "alignas(16)",
        ):
            self.assertIn(required, inference)

    def test_audio_service_matches_reset_framing_and_failure_contract(self):
        header = self.read("services/erb_gru/erb_gru_audio_enhancer.h")
        source = self.read("services/erb_gru/erb_gru_audio_enhancer.cpp")
        for required in (
            "ERB_GRU_AUDIO_RESULT_ENHANCED",
            "ERB_GRU_AUDIO_RESULT_PREROLL_SILENCE",
            "ERB_GRU_AUDIO_RESULT_UNITY_FALLBACK",
            "ERB_GRU_AUDIO_RESULT_INVALID",
            "erb_gru_audio_enhancer_flush",
        ):
            self.assertIn(required, header)
        for required in (
            "ERB_GRU_STATE1_ZERO_POINT",
            "ERB_GRU_STATE2_ZERO_POINT",
            "g_have_processed_frame",
            "inference_ok ? 0.0f : 1.0f",
            "std::memcpy(g_state1, g_next_state1",
            "std::memcpy(g_state2, g_next_state2",
        ):
            self.assertIn(required, source)
        self.assertNotIn("new ", source)
        self.assertNotIn("malloc", source)

    def test_streaming_loop_has_no_polling_printf(self):
        app = self.read("app/main.c")
        streaming_loop = app[app.rindex("    for (;;) {") :]
        self.assertNotIn("printf(", streaming_loop)
        self.assertNotIn("printf(", app[app.index("tlv320dac3100_start()") :])
        self.assertIn("audio_telemetry_poll(", streaming_loop)
        self.assertLess(
            app.index("erb_gru_audio_enhancer_init()"),
            app.index("tlv320dac3100_start()"),
        )

    def test_streaming_loop_tags_wola_output_with_previous_source_hop(self):
        app = self.read("app/main.c")

        self.assertIn("erb_gru_stream_timing_advance(", app)
        self.assertIn(".sequence = source_tag.sequence", app)
        self.assertRegex(
            app,
            r"\.capture_completed_cycle\s*=\s*source_tag\.capture_completed_cycle",
        )
        self.assertIn("tlv320dac3100_take_audio_completion(", app)
        self.assertIn("audio_telemetry_record_e2e_us(", app)

    def test_telemetry_is_rate_limited_and_uart_is_irq_driven(self):
        telemetry = self.read("services/audio_telemetry.c")
        uart = self.read("drivers/debug_uart_tx/debug_uart_tx.c")
        board = self.read("board/m55m1_debug_uart.c")
        for required in (
            "AUDIO_TELEMETRY_INTERVAL_FRAMES 100U",
            "dmic_block_pending",
            "snprintf",
            "debug_uart_tx_try_enqueue",
            "[MODEL]",
            "[AUDIO]",
            "[DSP]",
        ):
            self.assertIn(required, telemetry)
        for required in (
            "DEBUG_UART_TX_RING_BYTES 1024U",
            "UART1_IRQHandler",
            "UART_IS_TX_FULL",
            "UART_INTEN_THREIEN_Msk",
            "++g_drop_count",
        ):
            self.assertIn(required, uart)
        self.assertNotIn("snprintf", uart[uart.index("void UART1_IRQHandler"):])
        self.assertIn("NVIC_SetPriority(UART1_IRQn, 3U)", board)

    def test_board_owns_uart1_pinmux_without_vendored_change(self):
        board = self.read("board/m55m1_debug_uart.c")
        vendor = self.read("libs/Device/Nuvoton/M55M1/Source/system_M55M1.c")
        cmake = self.read("CMakeLists.txt")
        self.assertRegex(board, r"void SetDebugUartMFP\(void\)")
        self.assertIn("SET_UART1_RXD_PA2()", board)
        self.assertIn("SET_UART1_TXD_PA3()", board)
        self.assertRegex(board, r"void InitDebugUart\(void\)")
        self.assertIn("UART_Open(UART1, 115200U)", board)
        self.assertIn("PROPERTIES COMPILE_DEFINITIONS DEBUG_ENABLE_SEMIHOST=0", cmake)
        self.assertNotIn("DEBUG_PORT_UART_IDX  == 1", vendor)

    def test_inference_resolver_has_no_process_exit_destructor(self):
        inference = self.read("services/erb_gru/erb_gru_inference.cpp")

        self.assertIn("g_resolver_storage[sizeof(tflite::MicroMutableOpResolver<1>)]", inference)
        self.assertIn("tflite::MicroMutableOpResolver<1> *g_resolver", inference)
        self.assertIn("new (g_resolver_storage) tflite::MicroMutableOpResolver<1>()", inference)
        self.assertNotIn("tflite::MicroMutableOpResolver<1> g_resolver;", inference)

    def test_float_constants_are_valid_c_literals(self):
        for path in (
            "models/erb_gru/generated/dsp_constants.h",
            "models/erb_gru/generated/golden/golden_silence.h",
        ):
            text = self.read(path)
            self.assertNotRegex(text, r"(?<![.\w])\-?(?:0|1)f(?:[,;])")
            self.assertTrue(re.search(r"(?:0|1)\.0f", text))

    def test_golden_build_compiles_exactly_one_selected_corpus(self):
        cmake = self.read("CMakeLists.txt")
        runner = self.read("services/erb_gru/erb_gru_golden_runner.cpp")
        self.assertIn("MODEL_GOLDEN_TEST_DATA=1", cmake)
        self.assertIn("erb_gru_golden_runner.cpp", cmake)
        for scenario in (
            "silence",
            "sine_1khz",
            "clean_speech",
            "speech_noise",
            "near_full_scale",
        ):
            self.assertIn(f"golden_{scenario}.h", runner)
        self.assertIn("erb_gru_audio_enhancer_flush", runner)
        self.assertIn(
            "local.noise_energy <= (local.signal_energy / 1000000U)", runner
        )


if __name__ == "__main__":
    unittest.main()
