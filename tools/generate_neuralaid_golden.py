"""Generate firmware-owned NeuralAid raw-TFLite streaming golden traces."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

import numpy as np
import tensorflow as tf


ROOT = Path(__file__).resolve().parents[1]
EXPORT = ROOT / "neuralaid_u55_equivalent_seed20260823" / "integration_package"
DESTINATION = ROOT / "models" / "neuralaid" / "generated" / "golden"
RAW_MODEL = EXPORT / "neuralaid_int8_raw.tflite"
TENSOR_CONTRACT = ROOT / "models" / "neuralaid" / "generated" / "tensor_contract.json"
STATE_CONTRACT = ROOT / "models" / "neuralaid" / "generated" / "state_quant_contract.json"

SAMPLE_RATE = 16000
CHUNK_SAMPLES = 96
LOOKBACK_SAMPLES = 96
LOOKAHEAD_SAMPLES = 64
FFT_LENGTH = 256
STREAMING_GOLDEN_CHUNKS = 100
PRE_EMPHASIS = 0.97
PCM_FULL_SCALE = 32768.0
SYNTHESIS_WINDOW = np.concatenate(
    [
        np.full(LOOKAHEAD_SAMPLES, 1.0 / 3.0, dtype=np.float32),
        np.ones(CHUNK_SAMPLES - LOOKAHEAD_SAMPLES, dtype=np.float32),
        np.full(LOOKAHEAD_SAMPLES, 1.0 / 3.0, dtype=np.float32),
    ]
)
OUTPUT_FOR_INPUT = (1, 2, 9, 10, 11, 12, 13, 14, 3, 4, 5, 6, 7, 8)


def quantize(value: np.ndarray, scale: float, zero_point: int) -> np.ndarray:
    return np.clip(np.rint(value / scale + zero_point), -128, 127).astype(np.int8)


def dequantize(value: np.ndarray, scale: float, zero_point: int) -> np.ndarray:
    return (value.astype(np.float32) - zero_point) * scale


def pre_emphasis_sequence(waveform: np.ndarray) -> np.ndarray:
    previous = np.concatenate([np.zeros(1, dtype=np.float32), waveform[:-1]])
    return waveform.astype(np.float32) - PRE_EMPHASIS * previous


def make_stream_contexts(waveform: np.ndarray) -> np.ndarray:
    preemphasized = pre_emphasis_sequence(waveform)
    chunks = int(np.ceil(preemphasized.size / CHUNK_SAMPLES))
    target = chunks * CHUNK_SAMPLES
    padded = np.pad(preemphasized, (LOOKBACK_SAMPLES, target - preemphasized.size + LOOKAHEAD_SAMPLES))
    return np.stack(
        [padded[index * CHUNK_SAMPLES:index * CHUNK_SAMPLES + FFT_LENGTH]
         for index in range(chunks)]
    ).astype(np.float32)


def rfft_to_tf(spectrum: np.ndarray) -> np.ndarray:
    return np.stack([spectrum.real, spectrum.imag], axis=-1).astype(np.float32)


def tf_to_packed(spectrum: np.ndarray) -> np.ndarray:
    packed = np.empty(FFT_LENGTH, dtype=np.float32)
    packed[0] = spectrum[0, 0]
    packed[1] = spectrum[-1, 0]
    packed[2::2] = spectrum[1:-1, 0]
    packed[3::2] = spectrum[1:-1, 1]
    return packed


def packed_to_tf(packed: np.ndarray) -> np.ndarray:
    result = np.zeros((129, 2), dtype=np.float32)
    result[0, 0] = packed[0]
    result[-1, 0] = packed[1]
    result[1:-1, 0] = packed[2::2]
    result[1:-1, 1] = packed[3::2]
    return result


def synthesis_stream(short_frame: np.ndarray, ola_tail: np.ndarray,
                     ola_weight_tail: np.ndarray) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    head_weight = SYNTHESIS_WINDOW[:LOOKAHEAD_SAMPLES] + ola_weight_tail
    head = (short_frame[:LOOKAHEAD_SAMPLES] + ola_tail) / head_weight
    body = short_frame[LOOKAHEAD_SAMPLES:CHUNK_SAMPLES] / SYNTHESIS_WINDOW[
        LOOKAHEAD_SAMPLES:CHUNK_SAMPLES
    ]
    return (np.concatenate([head, body]), short_frame[CHUNK_SAMPLES:],
            SYNTHESIS_WINDOW[CHUNK_SAMPLES:].copy())


def state_layout(contract: dict) -> tuple[list[int], list[tuple[float, int]], list[tuple[float, int]]]:
    pairs = contract["state_pairs"]
    sizes = [int(np.prod(pair["input_shape"])) for pair in pairs]
    input_quant = [(pair["input_scale"], pair["input_zero_point"]) for pair in pairs]
    output_quant = [(pair["output_scale"], pair["output_zero_point"]) for pair in pairs]
    return sizes, input_quant, output_quant


def run_signal(name: str, waveform: np.ndarray, tensor_contract: dict,
               state_contract: dict) -> dict:
    interpreter = tf.lite.Interpreter(model_path=str(RAW_MODEL))
    interpreter.allocate_tensors()
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    input_by_index = {detail["index"]: detail for detail in input_details}
    output_by_index = {detail["index"]: detail for detail in output_details}
    sizes, input_quant, output_quant = state_layout(state_contract)
    offsets = np.cumsum([0, *sizes])
    contexts = make_stream_contexts(waveform)
    state = np.concatenate(
        [np.full(size, zero, dtype=np.int8) for size, (_, zero) in zip(sizes, input_quant)]
    )
    traces: dict[str, list[np.ndarray]] = {key: [] for key in (
        "context", "preemphasis", "packed_rfft", "tf_input_int8", "tf_output_int8",
        "state_in_int8", "state_out_int8", "ifft", "synthesis", "pcm"
    )}
    ola_tail = np.zeros(LOOKAHEAD_SAMPLES, dtype=np.float32)
    ola_weight_tail = np.zeros(LOOKAHEAD_SAMPLES, dtype=np.float32)
    for context in contexts:
        spectrum = np.fft.rfft(context, n=FFT_LENGTH).astype(np.complex64)
        tf_frame = rfft_to_tf(spectrum)
        tf_input = quantize(tf_frame, *tensor_contract["tflite_inputs"][0]["quantization"])
        interpreter.set_tensor(input_by_index[0]["index"], tf_input[None, ...])
        for ordinal, pair in enumerate(state_contract["state_pairs"]):
            begin, end = offsets[ordinal], offsets[ordinal + 1]
            interpreter.set_tensor(pair["input_index"], state[begin:end].reshape(pair["input_shape"]))
        interpreter.invoke()
        model_output = interpreter.get_tensor(output_details[0]["index"])[0].copy()
        updated = np.empty_like(state)
        for ordinal, pair in enumerate(state_contract["state_pairs"]):
            begin, end = offsets[ordinal], offsets[ordinal + 1]
            encoded = interpreter.get_tensor(pair["output_index"]).reshape(-1)
            if pair["raw_copy_ok"]:
                updated[begin:end] = encoded
            else:
                real = dequantize(encoded, *output_quant[ordinal])
                updated[begin:end] = quantize(real, *input_quant[ordinal])
        output_float = dequantize(model_output, *tensor_contract["tflite_outputs"][0]["quantization"])
        enhanced = output_float[:, 0] + 1j * output_float[:, 1]
        ifft = np.fft.irfft(enhanced, n=FFT_LENGTH).astype(np.float32)
        short = ifft[LOOKBACK_SAMPLES:] * SYNTHESIS_WINDOW
        emitted, ola_tail, ola_weight_tail = synthesis_stream(short, ola_tail, ola_weight_tail)
        traces["context"].append(context)
        traces["preemphasis"].append(pre_emphasis_sequence(waveform))
        traces["packed_rfft"].append(tf_to_packed(tf_frame))
        traces["tf_input_int8"].append(tf_input)
        traces["tf_output_int8"].append(model_output)
        traces["state_in_int8"].append(state)
        traces["state_out_int8"].append(updated)
        traces["ifft"].append(ifft)
        traces["synthesis"].append(short)
        traces["pcm"].append(np.clip(np.rint(emitted * PCM_FULL_SCALE), -32768, 32767).astype(np.int16))
        state = updated
    return {key: np.stack(value) for key, value in traces.items()}


def main() -> None:
    if not RAW_MODEL.is_file():
        raise FileNotFoundError(RAW_MODEL)
    tensor_contract = json.loads(TENSOR_CONTRACT.read_text(encoding="utf-8"))
    state_contract = json.loads(STATE_CONTRACT.read_text(encoding="utf-8"))
    DESTINATION.mkdir(parents=True, exist_ok=True)
    samples = np.arange(CHUNK_SAMPLES * STREAMING_GOLDEN_CHUNKS, dtype=np.float32)
    signals = {
        "silence": np.zeros_like(samples),
        "impulse": np.r_[np.array([1.0], dtype=np.float32), np.zeros(samples.size - 1, dtype=np.float32)],
        "sine_440": np.sin(2.0 * np.pi * 440.0 * samples / SAMPLE_RATE).astype(np.float32),
        "sine_3000": np.sin(2.0 * np.pi * 3000.0 * samples / SAMPLE_RATE).astype(np.float32),
        "noise": np.random.default_rng(20260823).normal(size=samples.size).astype(np.float32),
    }
    manifest = {"raw_model_sha256": hashlib.sha256(RAW_MODEL.read_bytes()).hexdigest(),
                "chunks": STREAMING_GOLDEN_CHUNKS, "signals": sorted(signals),
                "contract": {"lookback": LOOKBACK_SAMPLES, "chunk": CHUNK_SAMPLES,
                             "lookahead": LOOKAHEAD_SAMPLES, "fft": FFT_LENGTH}}
    for name, signal in signals.items():
        np.savez_compressed(DESTINATION / f"{name}.npz", input_pcm=(signal * 32767).astype(np.int16),
                            **run_signal(name, signal, tensor_contract, state_contract))
    (DESTINATION / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    print(f"NeuralAid golden: {DESTINATION}")


if __name__ == "__main__":
    main()
