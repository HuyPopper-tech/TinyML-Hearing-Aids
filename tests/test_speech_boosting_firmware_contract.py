"""Speech Boosting Chunk64 production firmware contract."""

import hashlib
import json
import math
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
GENERATED = ROOT / "models/speech_boosting/generated"


def quantize_state(value, output_scale, output_zero_point, input_scale, input_zero_point):
    quantized = round(
        ((value - output_zero_point) * output_scale / input_scale) + input_zero_point
    )
    return max(-128, min(127, quantized))


def round_away_from_zero(value):
    return math.floor(value + 0.5) if value >= 0 else math.ceil(value - 0.5)


def quantize_pcm16(sample, scale, zero_point):
    return max(-128, min(127, round_away_from_zero((sample / 32768.0) / scale + zero_point)))


class SpeechBoostingFirmwareContractTests(unittest.TestCase):
    def setUp(self):
        self.contract = json.loads(
            (GENERATED / "stream_io_contract.json").read_text(encoding="utf-8")
        )

    def test_speech_boosting_is_only_production_model_owner(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("option(M55M1_ENABLE_SPEECH_BOOSTING", cmake)
        self.assertIn("M55M1_ENABLE_SPEECH_BOOSTING=1", cmake)
        self.assertIn("services/speech_boosting/speech_boosting_inference.cpp", cmake)
        self.assertIn("services/speech_boosting/speech_boosting_audio_enhancer.cpp", cmake)
        self.assertNotIn("models/deepfilternet/generated/deepfilternet_u55_vela_model_data.c", cmake)
        self.assertNotIn("services/deepfilternet/deepfilternet_inference.cpp", cmake)
        self.assertNotIn("M55M1_ENABLE_NEURALAID=1", cmake)

    def test_export_hash_and_contract_match_package(self):
        manifest = json.loads((ROOT / "models/speech_boosting/firmware_export_manifest.json").read_text(encoding="utf-8"))
        model = GENERATED / "speech_boosting_qat_vela.tflite"
        self.assertEqual(hashlib.sha256(model.read_bytes()).hexdigest(), manifest["vela_sha256"])
        self.assertEqual(manifest["vela_cpu_operators"], 0)
        self.assertEqual(manifest["golden_chunks"], 100)
        self.assertEqual(self.contract["inputs"]["audio_shift_channels"]["shape"], [1, 1, 64, 2])
        self.assertEqual(self.contract["outputs"]["audio_out"]["shape"], [1, 1, 64, 1])
        self.assertEqual(len(self.contract["state_input_names"]), 20)
        self.assertEqual(len(self.contract["state_output_names"]), 20)

    def test_generated_deploy_contract_preserves_semantic_state_mapping(self):
        header = (GENERATED / "speech_boosting_deploy_contract.h").read_text(encoding="utf-8")
        self.assertIn("SPEECH_BOOSTING_STATE_OUTPUT_SCALE", header)
        self.assertIn("SPEECH_BOOSTING_STATE_OUTPUT_ZERO_POINT", header)
        self.assertIn("SPEECH_BOOSTING_STATE_TENSOR_SHAPES", header)
        self.assertIn("SPEECH_BOOSTING_VELA_MODEL_SHA256", header)
        output = self.contract["outputs"][self.contract["state_output_names"][18]]
        input_ = self.contract["inputs"][self.contract["state_input_names"][18]]
        self.assertNotEqual((output["scale"], output["zero_point"]), (input_["scale"], input_["zero_point"]))
        self.assertNotEqual(quantize_state(0, output["scale"], output["zero_point"], input_["scale"], input_["zero_point"]), 0)

    def test_all_ordinals_and_zero_state_contracts_are_complete(self):
        input_ordinals = [self.contract["inputs"]["audio_shift_channels"]["ordinal"]]
        output_ordinals = [self.contract["outputs"]["audio_out"]["ordinal"]]
        state_input_bytes = 0

        for input_name, output_name in zip(
            self.contract["state_input_names"], self.contract["state_output_names"]
        ):
            input_ = self.contract["inputs"][input_name]
            output = self.contract["outputs"][output_name]
            input_ordinals.append(input_["ordinal"])
            output_ordinals.append(output["ordinal"])
            self.assertEqual(input_["dtype"], "<class 'numpy.int8'>")
            self.assertEqual(output["dtype"], "<class 'numpy.int8'>")
            self.assertEqual(input_["shape"], output["shape"])
            self.assertGreater(input_["scale"], 0.0)
            self.assertGreater(output["scale"], 0.0)
            self.assertTrue(-128 <= input_["zero_point"] <= 127)
            self.assertTrue(-128 <= output["zero_point"] <= 127)
            state_input_bytes += math.prod(input_["shape"])

        self.assertEqual(sorted(input_ordinals), list(range(21)))
        self.assertEqual(sorted(output_ordinals), list(range(21)))
        self.assertEqual(state_input_bytes, 5632)
        inference = (ROOT / "services/speech_boosting/speech_boosting_inference.cpp").read_text(encoding="utf-8")
        self.assertIn("SPEECH_BOOSTING_STATE_INPUT_QUANTIZATION_ZERO_POINT", inference)
        self.assertIn("ordinal_mapping_valid", inference)
        self.assertIn("speech_boosting_inference_reset", inference)

    def test_pcm_lookahead_packing_and_output_dequantization_contract(self):
        audio_input = self.contract["inputs"]["audio_shift_channels"]
        audio_output = self.contract["outputs"]["audio_out"]
        capture = [(-32000 + index * 800) for index in range(80)]
        packed = [
            (
                quantize_pcm16(capture[index], audio_input["scale"], audio_input["zero_point"]),
                quantize_pcm16(capture[index + 16], audio_input["scale"], audio_input["zero_point"]),
            )
            for index in range(64)
        ]
        dequantized = [
            round_away_from_zero(
                (value - audio_output["zero_point"]) * audio_output["scale"] * 32768.0
            )
            for value in (-128, -1, 127)
        ]

        self.assertEqual(packed[0][0], quantize_pcm16(capture[0], audio_input["scale"], audio_input["zero_point"]))
        self.assertEqual(packed[0][1], quantize_pcm16(capture[16], audio_input["scale"], audio_input["zero_point"]))
        self.assertEqual(packed[-1][1], quantize_pcm16(capture[79], audio_input["scale"], audio_input["zero_point"]))
        self.assertTrue(all(-32768 <= value <= 32767 for value in dequantized))
        enhancer = (ROOT / "services/speech_boosting/speech_boosting_audio_enhancer.cpp").read_text(encoding="utf-8")
        self.assertIn("dequantize_pcm16", enhancer)
        self.assertIn("output_saturation_count", enhancer)

    def test_64_sample_transport_and_hardware_gain_precede_dma(self):
        transport = (ROOT / "drivers/audio_transport/audio_transport.h").read_text(encoding="utf-8")
        board = (ROOT / "board/m55m1_audio.c").read_text(encoding="utf-8")
        self.assertIn("AUDIO_TRANSPORT_SAMPLES_PER_BLOCK    64U", transport)
        self.assertIn("AUDIO_TRANSPORT_MICROPHONE_DSP_GAIN_DB 30", transport)
        self.assertIn("DMIC_SetDSPGainVolume(DMIC0, DMIC_CTL_CHEN2_Msk, M55M1_SPEECH_BOOSTING_DMIC_GAIN_DB)", board)
        self.assertLess(board.index("DMIC_SetDSPGainVolume"), board.index("DMIC_ENABLE_LPPDMA"))

    def test_inference_contract_and_pipeline_telemetry_are_complete(self):
        inference = (ROOT / "services/speech_boosting/speech_boosting_inference.cpp").read_text(encoding="utf-8")
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(encoding="utf-8")
        for text in ("sha256_compute", "VerifyModelBuffer", "arena_used_bytes", "requantize_state", "model_contract_valid"):
            self.assertIn(text, inference)
        for text in ("minimum_microseconds=", "mean_microseconds=", "maximum_microseconds=", "sample_count=", "audio_transport_take_completion", "microphone_peak_pulse_code_modulation_16", "microphone_root_mean_square_pulse_code_modulation_16", "dropped_submissions"):
            self.assertIn(text, pipeline)

    def test_uart_formatting_stays_outside_interrupt_handler(self):
        pipeline = (ROOT / "services/audio_pipeline/audio_pipeline.c").read_text(encoding="utf-8")
        uart_driver = (ROOT / "drivers/debug_uart_tx/debug_uart_tx.c").read_text(encoding="utf-8")
        interrupt_handler = uart_driver.split("void UART1_IRQHandler(void)", 1)[1]

        self.assertIn("vsnprintf", pipeline)
        self.assertNotIn("vsnprintf", interrupt_handler)
        self.assertNotIn("debug_uart_tx_try_enqueue", interrupt_handler)

    def test_golden_trace_has_100_raw_int8_chunks(self):
        try:
            import numpy as np
        except ImportError as error:
            self.skipTest(str(error))
        with np.load(GENERATED / "golden_raw_int8_100_chunks.npz", allow_pickle=True) as golden:
            input_chunks = golden["inputs"]
            output_chunks = golden["outputs"]
        self.assertEqual(len(input_chunks), 100)
        self.assertEqual(len(output_chunks), 100)
        self.assertEqual(len(input_chunks[0]), 21)
        self.assertEqual(len(output_chunks[0]), 21)
        self.assertEqual(input_chunks[0][8].dtype.name, "int8")
        self.assertEqual(output_chunks[0][17].dtype.name, "int8")

    def test_golden_trace_state_feedback_obeys_semantic_mapping(self):
        try:
            import numpy as np
        except ImportError as error:
            self.skipTest(str(error))
        with np.load(GENERATED / "golden_raw_int8_100_chunks.npz", allow_pickle=True) as golden:
            for chunk in range(99):
                next_inputs = dict(zip(golden["input_names"], golden["inputs"][chunk + 1]))
                current_outputs = dict(zip(golden["output_names"], golden["outputs"][chunk]))
                for input_name, output_name in zip(
                    self.contract["state_input_names"], self.contract["state_output_names"]
                ):
                    input_ = self.contract["inputs"][input_name]
                    output = self.contract["outputs"][output_name]
                    feedback = np.rint(
                        (current_outputs[output_name].astype(np.float64) - output["zero_point"])
                        * output["scale"]
                        / input_["scale"]
                        + input_["zero_point"]
                    )
                    feedback = np.clip(feedback, -128, 127).astype(np.int8)
                    self.assertTrue(
                        np.array_equal(next_inputs[input_name], feedback),
                        f"chunk={chunk} input={input_name} output={output_name}",
                    )

    def test_raw_int8_golden_trace_replays_all_100_chunks(self):
        try:
            import numpy as np
            import tensorflow as tf
        except ImportError as error:
            self.skipTest(str(error))

        raw_model = GENERATED / "speech_boosting_qat_raw_int8.tflite"
        with np.load(GENERATED / "golden_raw_int8_100_chunks.npz", allow_pickle=True) as golden:
            interpreter = tf.lite.Interpreter(model_path=str(raw_model))
            interpreter.allocate_tensors()
            input_details = {detail["name"]: detail for detail in interpreter.get_input_details()}
            output_details = {detail["name"]: detail for detail in interpreter.get_output_details()}
            input_contract = {
                value["name"]: name for name, value in self.contract["inputs"].items()
            }
            output_contract = {
                value["name"]: name for name, value in self.contract["outputs"].items()
            }
            for chunk in range(100):
                golden_inputs = dict(zip(golden["input_names"], golden["inputs"][chunk]))
                for raw_name, semantic_name in input_contract.items():
                    interpreter.set_tensor(input_details[raw_name]["index"], golden_inputs[semantic_name])
                interpreter.invoke()
                golden_outputs = dict(zip(golden["output_names"], golden["outputs"][chunk]))
                for raw_name, semantic_name in output_contract.items():
                    self.assertTrue(
                        np.array_equal(
                            interpreter.get_tensor(output_details[raw_name]["index"]),
                            golden_outputs[semantic_name],
                        ),
                        f"chunk={chunk} tensor={semantic_name}",
                    )


if __name__ == "__main__":
    unittest.main()
