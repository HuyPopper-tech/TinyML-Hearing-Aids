"""Contract tests for the model-neutral M55M1 Ethos-U runtime."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
BOARD_HEADER = ROOT / "board" / "m55m1_ethosu_board.h"
BOARD_SOURCE = ROOT / "board" / "m55m1_ethosu_board.c"
RUNTIME_HEADER = ROOT / "drivers" / "ethosu_runtime" / "ethosu_runtime.h"
RUNTIME_SOURCE = ROOT / "drivers" / "ethosu_runtime" / "ethosu_runtime.c"
TFLM_BRIDGE = ROOT / "drivers" / "ethosu_runtime" / "ethosu_tflm_bridge.cpp"
SMOKE_SOURCE = ROOT / "app" / "test_ethosu_runtime_smoke_main.c"


class EthosURuntimeContractTests(unittest.TestCase):
    def test_runtime_source_tree_and_public_api_exist(self):
        for path in (BOARD_HEADER, BOARD_SOURCE, RUNTIME_HEADER, RUNTIME_SOURCE):
            self.assertTrue(path.is_file(), f"missing runtime file: {path}")

        header = RUNTIME_HEADER.read_text(encoding="utf-8")
        required_symbols = (
            "ethosu_runtime_init",
            "ethosu_runtime_deinit",
            "ethosu_runtime_get_state",
            "ethosu_runtime_get_driver_version",
            "ethosu_runtime_get_hw_info",
            "ethosu_runtime_start",
            "ethosu_runtime_poll",
            "ethosu_runtime_wait",
            "ethosu_runtime_invoke",
            "ethosu_runtime_soft_reset",
            "ethosu_runtime_get_stats",
        )
        for symbol in required_symbols:
            self.assertIn(symbol, header)

    def test_runtime_is_model_neutral_and_hides_vendor_types(self):
        header = RUNTIME_HEADER.read_text(encoding="utf-8").lower()
        source = RUNTIME_SOURCE.read_text(encoding="utf-8").lower()
        for archived_name in (
            "deepfir",
            "speech_boosting",
            "neuralaid",
            "deepfilternet",
            "erb_gru",
        ):
            self.assertNotIn(archived_name, header)
            self.assertNotIn(archived_name, source)
        self.assertNotIn("struct ethosu_driver", header)
        self.assertNotIn("numicro.h", header)
        self.assertNotIn("malloc(", source)
        self.assertNotIn("free(", source)

    def test_runtime_owns_irq_cache_and_static_sync_hooks(self):
        source = RUNTIME_SOURCE.read_text(encoding="utf-8")
        for symbol in (
            "NPU_IRQHandler",
            "ethosu_flush_dcache",
            "ethosu_invalidate_dcache",
            "ethosu_semaphore_create",
            "ethosu_semaphore_give_from_ISR",
            "ethosu_mutex_create",
            "ethosu_inference_begin",
            "ethosu_inference_end",
        ):
            self.assertIn(symbol, source)
        self.assertIn("static", source)

    def test_tflm_bridge_is_project_owned_and_model_neutral(self):
        self.assertTrue(TFLM_BRIDGE.is_file(), f"missing TFLM bridge: {TFLM_BRIDGE}")
        source = TFLM_BRIDGE.read_text(encoding="utf-8").lower()
        for archived_name in (
            "deepfir",
            "speech_boosting",
            "neuralaid",
            "deepfilternet",
            "erb_gru",
        ):
            self.assertNotIn(archived_name, source)
        self.assertIn("ethosu_runtime_invoke", source)
        self.assertNotIn("ethosu_reserve_driver", source)
        self.assertNotIn("ethosu_invoke_v3", source)
        self.assertIn("tfliteevaltensorbytelength", source)
        self.assertIn("size_max / sizeof(uint64_t)", source)
        self.assertIn("size_max / sizeof(size_t)", source)
        self.assertNotIn("{`n        .custom_operator_payload =", source)

    def test_cmake_runtime_is_opt_in_and_tflm_requires_it(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("M55M1_ENABLE_ETHOSU_RUNTIME", cmake)
        self.assertIn("M55M1_BUILD_ETHOSU_SMOKE", cmake)
        self.assertIn("M55M1_ENABLE_TFLM", cmake)
        self.assertIn("M55M1_ENABLE_TFLM=ON requires M55M1_ENABLE_ETHOSU_RUNTIME=ON", cmake)
        self.assertIn("M55M1_ETHOSU_CORE_SOURCES", cmake)
        self.assertIn("list(REMOVE_ITEM M55M1_STDDRIVER_SOURCES ${M55M1_ETHOSU_SOURCES})", cmake)
        self.assertIn("drivers/ethosu_runtime/ethosu_runtime.c", cmake)
        self.assertIn("drivers/ethosu_runtime/ethosu_tflm_bridge.cpp", cmake)
        self.assertIn("M55M1_EthosUSmoke.axf", cmake)
        self.assertIn("M55M1_BUILD_ETHOSU_SMOKE=ON requires M55M1_ENABLE_ETHOSU_RUNTIME=ON", cmake)

        tflm_cmake = (ROOT / "cmake" / "tensorflow_lite_micro.cmake").read_text(
            encoding="utf-8"
        )
        self.assertIn("ethosu.cpp", tflm_cmake)

    def test_board_binding_and_smoke_are_model_free(self):
        board_header = BOARD_HEADER.read_text(encoding="utf-8")
        board_source = BOARD_SOURCE.read_text(encoding="utf-8")
        for symbol in (
            "m55m1_ethosu_board_init",
            "m55m1_ethosu_board_disable_irq",
            "m55m1_ethosu_board_get_base_address",
            "m55m1_ethosu_board_cycles_now",
            "m55m1_ethosu_board_cycles_elapsed",
        ):
            self.assertIn(symbol, board_header)
            self.assertIn(symbol, board_source)
        self.assertNotIn("ethosu_driver.h", board_source)
        self.assertNotIn("NPU_IRQHandler", board_source)
        self.assertTrue(SMOKE_SOURCE.is_file(), f"missing smoke source: {SMOKE_SOURCE}")
        smoke = SMOKE_SOURCE.read_text(encoding="utf-8").lower()
        for archived_name in ("deepfir", "speech_boosting", "neuralaid", "wave_unet"):
            self.assertNotIn(archived_name, smoke)
        self.assertIn("ethosu_runtime_init", smoke)
        self.assertIn("ethosu_runtime_get_driver_version", smoke)
        self.assertIn("ethosu_runtime_get_hw_info", smoke)
        self.assertIn("ethosu_runtime_soft_reset", smoke)
        self.assertIn("ethosu_runtime_deinit", smoke)
        self.assertIn("debug_uart_tx_try_enqueue", smoke)


if __name__ == "__main__":
    unittest.main()