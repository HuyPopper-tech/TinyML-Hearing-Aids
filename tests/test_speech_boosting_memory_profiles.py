"""Vela 5.1 memory-profile and SRAM01 placement contracts."""

import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROFILES = ("aprom_x030", "aprom_x050", "aprom_x100", "sram01_x100")
BASELINE_SHA256 = "c0c2a641f2b317925003632b5eabcf1f4b1f4f9018f3ed5386e76f105be1f6c7"


class SpeechBoostingMemoryProfileTests(unittest.TestCase):
    def test_vela_5_1_profiles_use_logical_off_chip_flash(self):
        configuration = (
            ROOT / "models/speech_boosting/integration/vela_m55m1_profiles.ini"
        ).read_text(encoding="utf-8")

        self.assertNotIn("OnChipFlash", configuration)
        self.assertIn("axi1_port=OffChipFlash", configuration)
        self.assertIn("System_Config.Ethos_U55_M55M1_FLASH_X0.300", configuration)
        self.assertIn("Memory_Mode.Shared_1024K", configuration)
        for profile in PROFILES[1:]:
            self.assertIn(f"System_Config.M55M1_{profile}", configuration)
            self.assertIn(f"Memory_Mode.M55M1_{profile}", configuration)

    def test_each_profile_has_a_complete_reproducible_package(self):
        raw_sha256 = hashlib.sha256(
            (ROOT / "models/speech_boosting/generated/speech_boosting_qat_raw_int8.tflite").read_bytes()
        ).hexdigest()
        for profile in PROFILES:
            package = ROOT / "models/speech_boosting/profiles" / profile
            manifest = json.loads((package / "firmware_export_manifest.json").read_text(encoding="utf-8"))
            vela_model = package / "generated/speech_boosting_qat_vela.tflite"

            self.assertEqual(manifest["memory_profile"], profile)
            self.assertEqual(manifest["vela_version"], "5.1.0")
            self.assertEqual(manifest["raw_int8_sha256"], raw_sha256)
            self.assertEqual(hashlib.sha256(vela_model.read_bytes()).hexdigest(), manifest["vela_sha256"])
            self.assertEqual(manifest["vela_cpu_operators"], 0)
            self.assertEqual(manifest["vela_ethosu_operators"], 197)
            self.assertTrue((package / "generated/speech_boosting_u55_vela_model_data.cc").is_file())
            self.assertTrue((package / "generated/stream_io_contract.json").is_file())
            self.assertTrue((package / "generated/golden_raw_int8_100_chunks.npz").is_file())

            contract = json.loads(
                (package / "generated/stream_io_contract.json").read_text(encoding="utf-8")
            )
            self.assertEqual(contract["inputs"][contract["audio_input_name"]]["dtype"], "<class 'numpy.int8'>")
            self.assertEqual(contract["outputs"][contract["audio_output_name"]]["dtype"], "<class 'numpy.int8'>")
            self.assertEqual(len(contract["state_input_names"]), 20)
            self.assertEqual(len(contract["state_output_names"]), 20)
            self.assertEqual(
                sorted(
                    [contract["inputs"][contract["audio_input_name"]]["ordinal"]]
                    + [contract["inputs"][name]["ordinal"] for name in contract["state_input_names"]]
                ),
                list(range(21)),
            )
            self.assertEqual(
                sorted(
                    [contract["outputs"][contract["audio_output_name"]]["ordinal"]]
                    + [contract["outputs"][name]["ordinal"] for name in contract["state_output_names"]]
                ),
                list(range(21)),
            )

    def test_aprom_baseline_remains_byte_identical_to_production_model(self):
        model = ROOT / "models/speech_boosting/profiles/aprom_x030/generated/speech_boosting_qat_vela.tflite"
        self.assertEqual(hashlib.sha256(model.read_bytes()).hexdigest(), BASELINE_SHA256)

    def test_vela_5_1_recompiles_aprom_baseline_byte_identically(self):
        raw_model = ROOT / "models/speech_boosting/generated/speech_boosting_qat_raw_int8.tflite"
        configuration = ROOT / "models/speech_boosting/integration/vela_m55m1_profiles.ini"
        with tempfile.TemporaryDirectory(prefix="vela_aprom_x030_") as temporary:
            output = Path(temporary) / "output"
            output.mkdir()
            command = [
                sys.executable,
                "-m",
                "ethosu.vela",
                str(raw_model),
                "--config",
                str(configuration),
                "--system-config",
                "Ethos_U55_M55M1_FLASH_X0.300",
                "--memory-mode",
                "Shared_1024K",
                "--accelerator-config",
                "ethos-u55-256",
                "--output-dir",
                str(output),
            ]
            completed = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
            model = output / "speech_boosting_qat_raw_int8_vela.tflite"
            self.assertEqual(hashlib.sha256(model.read_bytes()).hexdigest(), BASELINE_SHA256)
            self.assertIn("CPU operators = 0 (0.0%)", completed.stdout)
            self.assertIn("NPU operators = 197 (100.0%)", completed.stdout)

    def test_firmware_can_select_flash_or_sram01_profile(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("M55M1_SPEECH_BOOSTING_MEMORY_PROFILE", cmake)
        self.assertIn("aprom_x030", cmake)
        self.assertIn("sram01_x100", cmake)
        self.assertIn("cmake/m55m1_speech_boosting.scf", cmake)

    def test_sram01_scatter_keeps_model_and_arena_inside_sram01(self):
        scatter = (ROOT / "cmake/m55m1_speech_boosting.scf").read_text(encoding="utf-8")
        sram_model = (
            ROOT
            / "models/speech_boosting/profiles/sram01_x100/generated/speech_boosting_qat_vela.tflite"
        )
        self.assertIn("U55_MODEL_RAM", scatter)
        self.assertIn("U55_TENSOR_ARENA", scatter)
        self.assertIn("0x20100000", scatter)
        self.assertIn("0x00100000", scatter)
        self.assertNotIn("0x20200000", scatter)
        self.assertIn("ScatterAssert", scatter)
        self.assertLess(sram_model.stat().st_size + (64 * 1024), 0x00100000)

    def test_sram01_model_cache_is_cleaned_once_before_tensor_allocation(self):
        inference = (ROOT / "services/speech_boosting/speech_boosting_inference.cpp").read_text(encoding="utf-8")
        driver_header = (ROOT / "drivers/ethosu_u55/ethosu_u55.h").read_text(encoding="utf-8")
        self.assertIn("M55M1_SPEECH_BOOSTING_MODEL_IN_SRAM", inference)
        self.assertIn("ethosu_flush_dcache", inference)
        self.assertLess(inference.index("ethosu_flush_dcache"), inference.index("AllocateTensors"))
        self.assertIn("void ethosu_flush_dcache", driver_header)
        generated_contract = (
            ROOT / "models/speech_boosting/profiles/sram01_x100/generated/speech_boosting_generated_contract.h"
        ).read_text(encoding="utf-8")
        self.assertIn("64U * 1024U", generated_contract)

    def test_boot_log_reports_profile_and_runtime_memory_ranges(self):
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(encoding="utf-8")
        for field in (
            "vela_memory_profile=",
            "model_memory_region=",
            "model_start_address=",
            "tensor_arena_memory_region=",
            "tensor_arena_start_address=",
        ):
            self.assertIn(field, pipeline)
        self.assertIn("neural_processing_unit_active_cycles=", pipeline)
        self.assertIn("multiply_accumulate_active_cycles=", pipeline)

    def test_sram01_profile_moves_cpu_only_working_data_to_dtcm(self):
        scatter = (ROOT / "cmake/m55m1_speech_boosting.scf").read_text(encoding="utf-8")
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(encoding="utf-8")
        inference = (ROOT / "services/speech_boosting/speech_boosting_inference.cpp").read_text(
            encoding="utf-8"
        )
        uart = (ROOT / "drivers/debug_uart_tx/debug_uart_tx.c").read_text(encoding="utf-8")
        board_audio = (ROOT / "board/m55m1_audio.c").read_text(encoding="utf-8")

        self.assertLess(scatter.index("DTCM_VTOR"), scatter.index("RW_RAM AlignExpr"))
        self.assertIn(".bss.DTCM.ZeroInit", scatter)
        self.assertIn("g_audio_working_set", pipeline)
        self.assertIn(".bss.DTCM.ZeroInit", pipeline)
        self.assertIn("speech_boosting_foreground_audio_working_set memory_region=dtcm", pipeline)
        self.assertIn("speech_boosting_memory_ranges", pipeline)
        self.assertIn("g_interpreter_storage", inference)
        self.assertIn(".bss.DTCM.ZeroInit", inference)
        self.assertIn("g_tx_storage", uart)
        self.assertIn(".bss.DTCM.ZeroInit", uart)
        self.assertIn("NVT_NONCACHEABLE", board_audio)
        self.assertIn(".bss.U55_TENSOR_ARENA", inference)


if __name__ == "__main__":
    unittest.main()
