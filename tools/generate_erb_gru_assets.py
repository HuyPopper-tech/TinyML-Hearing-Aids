"""Verify an ERB-GRU integration package and generate board deployment assets.

Tensor indices in the training manifest describe the pre-Vela graph. This tool
therefore extracts the deploy contract from the exact Vela FlatBuffer whose
bytes are embedded in firmware, while retaining the raw package unchanged.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import re
import sys
from typing import Any


MODEL_HEADER_PATTERN = re.compile(
    r"erb_gru_vela_model\s*\[\s*\]\s*=\s*\{(?P<body>.*?)\};", re.DOTALL
)
HEX_BYTE_PATTERN = re.compile(r"0x([0-9a-fA-F]{2})")
INVALID_C_FLOAT_PATTERN = re.compile(r"(?<![.\w])(-?)(0|1)f(?=[,;])")
VELA_CPU_OPERATORS_PATTERN = re.compile(r"^CPU operators\s*=\s*(\d+)", re.MULTILINE)
VELA_NPU_OPERATORS_PATTERN = re.compile(r"^NPU operators\s*=\s*(\d+)", re.MULTILINE)
VELA_MACS_PATTERN = re.compile(
    r"^Neural network macs\s+(\d+)\s+MACs/batch", re.MULTILINE
)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def verify_file(path: Path, expected_bytes: int | None, expected_sha256: str) -> None:
    if not path.is_file():
        raise ValueError(f"required package file missing: {path}")
    if (expected_bytes is not None) and (path.stat().st_size != expected_bytes):
        raise ValueError(
            f"size mismatch for {path.name}: {path.stat().st_size} != {expected_bytes}"
        )
    actual_sha256 = sha256_file(path)
    if actual_sha256 != expected_sha256:
        raise ValueError(
            f"SHA-256 mismatch for {path.name}: {actual_sha256} != {expected_sha256}"
        )


def model_bytes_from_header(path: Path) -> bytes:
    match = MODEL_HEADER_PATTERN.search(path.read_text(encoding="utf-8"))
    if match is None:
        raise ValueError(f"model byte array not found in {path}")
    return bytes(int(value, 16) for value in HEX_BYTE_PATTERN.findall(match.group("body")))


def tensor_description(detail: dict[str, Any]) -> dict[str, Any]:
    scale, zero_point = detail["quantization"]
    return {
        "index": int(detail["index"]),
        "name": str(detail["name"]),
        "shape": [int(value) for value in detail["shape"]],
        "dtype": detail["dtype"].__name__,
        "bytes": int(detail["shape"].prod()) * int(detail["dtype"]().nbytes),
        "scale": float(scale),
        "zero_point": int(zero_point),
    }


def inspect_tflite(path: Path) -> dict[str, Any]:
    try:
        import tensorflow as tf
    except ImportError as error:
        raise ValueError("TensorFlow 2.20 is required to inspect TFLite contracts") from error
    if not str(tf.__version__).startswith("2.20."):
        raise ValueError(f"TensorFlow 2.20 is required, found {tf.__version__}")

    interpreter = tf.lite.Interpreter(model_path=str(path))
    operators = [
        {
            "name": str(operator["op_name"]),
            "index": int(operator["index"]),
        }
        for operator in interpreter._get_ops_details()  # pylint: disable=protected-access
    ]
    return {
        "tensorflow_version": str(tf.__version__),
        "inputs": [tensor_description(detail) for detail in interpreter.get_input_details()],
        "outputs": [tensor_description(detail) for detail in interpreter.get_output_details()],
        "operators": operators,
    }


def verify_tensor(
    tensor: dict[str, Any],
    *,
    name_fragment: str,
    shape: list[int],
    scale: float,
    zero_point: int,
) -> None:
    if (
        name_fragment not in tensor["name"]
        or tensor["shape"] != shape
        or tensor["dtype"] != "int8"
        or tensor["scale"] != scale
        or tensor["zero_point"] != zero_point
    ):
        raise ValueError(f"tensor contract mismatch: {tensor}")


def verify_contracts(
    manifest: dict[str, Any],
    pre_vela: dict[str, Any],
    vela: dict[str, Any],
) -> None:
    logical_inputs = {item["logical_name"]: item for item in manifest["tensors"]["inputs"]}
    logical_outputs = {item["logical_name"]: item for item in manifest["tensors"]["outputs"]}

    expected_input_order = ("state1", "state2", "erb_feature")
    expected_output_order = ("next_state2", "next_state1", "gain")
    if len(pre_vela["inputs"]) != 3 or len(pre_vela["outputs"]) != 3:
        raise ValueError("pre-Vela model must expose exactly three inputs and outputs")
    if len(vela["inputs"]) != 3 or len(vela["outputs"]) != 3:
        raise ValueError("Vela model must expose exactly three inputs and outputs")
    if [operator["name"] for operator in vela["operators"]] != ["ethos-u"]:
        raise ValueError(f"Vela model is not one ethos-u partition: {vela['operators']}")

    for contract in (pre_vela, vela):
        for ordinal, logical_name in enumerate(expected_input_order):
            expected = logical_inputs[logical_name]
            verify_tensor(
                contract["inputs"][ordinal],
                name_fragment=logical_name,
                shape=expected["shape"],
                scale=float(expected["scale"]),
                zero_point=int(expected["zero_point"]),
            )
        for ordinal, logical_name in enumerate(expected_output_order):
            expected = logical_outputs[logical_name]
            verify_tensor(
                contract["outputs"][ordinal],
                name_fragment=expected["name"],
                shape=expected["shape"],
                scale=float(expected["scale"]),
                zero_point=int(expected["zero_point"]),
            )

    if [tensor["index"] for tensor in vela["inputs"]] != [4, 3, 5]:
        raise ValueError(f"unexpected Vela input indices: {vela['inputs']}")
    if [tensor["index"] for tensor in vela["outputs"]] != [7, 6, 8]:
        raise ValueError(f"unexpected Vela output indices: {vela['outputs']}")


def parse_vela_performance(report: str) -> dict[str, int]:
    cpu_match = VELA_CPU_OPERATORS_PATTERN.search(report)
    npu_match = VELA_NPU_OPERATORS_PATTERN.search(report)
    macs_match = VELA_MACS_PATTERN.search(report)
    if (cpu_match is None) or (npu_match is None) or (macs_match is None):
        raise ValueError("Vela report is missing operator or MAC metadata")

    performance = {
        "cpu_operator_count": int(cpu_match.group(1)),
        "npu_operator_count": int(npu_match.group(1)),
        "macs_per_inference": int(macs_match.group(1)),
    }
    if (
        performance["cpu_operator_count"] != 0
        or performance["npu_operator_count"] == 0
        or performance["macs_per_inference"] == 0
    ):
        raise ValueError(f"invalid Vela performance metadata: {performance}")
    return performance


def verify_package(
    package: Path,
) -> tuple[dict[str, Any], dict[str, Any], dict[str, Any], dict[str, int]]:
    manifest = json.loads((package / "integration_manifest.json").read_text(encoding="utf-8"))

    for entry in manifest["model_files"].values():
        verify_file(
            package / entry["path"],
            int(entry["bytes"]),
            str(entry["sha256"]),
        )
    verify_file(
        package / manifest["dsp_contract"]["header"],
        None,
        str(manifest["dsp_contract"]["header_sha256"]),
    )

    golden = manifest["dsp_contract"]["golden"]
    golden_directory = package / golden["directory"]
    golden_manifest_path = golden_directory / golden["manifest"]
    verify_file(golden_manifest_path, None, str(golden["manifest_sha256"]))
    golden_manifest = json.loads(golden_manifest_path.read_text(encoding="utf-8"))
    if int(golden_manifest["total_frames"]) != int(golden["total_frames"]):
        raise ValueError("golden frame count differs between manifests")
    for scenario_name, scenario in golden_manifest.items():
        if scenario_name == "total_frames":
            continue
        verify_file(
            golden_directory / scenario["header"],
            None,
            str(scenario["header_sha256"]),
        )

    report = (package / manifest["model_files"]["vela_report"]["path"]).read_text(
        encoding="utf-8"
    )
    if ("CPU operators = 0 (0.0%)" not in report) or (
        "Accelerator configuration               Ethos_U55_256" not in report
    ):
        raise ValueError("Vela report does not prove a CPU-free Ethos-U55-256 partition")
    performance = parse_vela_performance(report)

    pre_vela = inspect_tflite(package / manifest["model_files"]["pre_vela_tflite"]["path"])
    vela = inspect_tflite(package / manifest["model_files"]["vela_tflite"]["path"])
    verify_contracts(manifest, pre_vela, vela)
    return manifest, pre_vela, vela, performance


def c_float(value: float) -> str:
    return f"{value:.17g}f"


def numeric_array(source: str, symbol: str, dtype: Any) -> Any:
    import numpy as np

    match = re.search(
        rf"\b{re.escape(symbol)}\s*\[[^\]]+\]\s*=\s*\{{(?P<body>.*?)\}};",
        source,
        re.DOTALL,
    )
    if match is None:
        raise ValueError(f"array not found: {symbol}")
    values = [
        value.strip().removesuffix("f")
        for value in match.group("body").split(",")
        if value.strip()
    ]
    return np.asarray(values, dtype=dtype)


def append_board_pcm_reference(
    golden_source: str,
    dsp_source: str,
    scenario: str,
    gain_scale: float,
    gain_zero_point: int,
) -> tuple[str, dict[str, Any]]:
    import numpy as np

    prefix = f"erb_gru_{scenario}"
    pcm_input = numeric_array(golden_source, f"{prefix}_pcm_input_int16", np.int16)
    raw_expected = numeric_array(golden_source, f"{prefix}_pcm_output_int16", np.int16)
    gain_q = numeric_array(golden_source, f"{prefix}_gain_int8", np.int8).reshape(-1, 32)
    sqrt_hann = numeric_array(dsp_source, "sqrt_hann_320", np.float64)
    synthesis = numeric_array(dsp_source, "wola_synthesis_320", np.float64)
    normalization = numeric_array(dsp_source, "wola_normalization_320", np.float64)
    expansion = numeric_array(
        dsp_source, "erb_gain_expansion_32x161", np.float64
    ).reshape(32, 161)

    output = np.zeros(pcm_input.size, dtype=np.float64)
    overlap = np.zeros(160, dtype=np.float64)
    pcm_float = pcm_input.astype(np.float64) / 32768.0
    for frame_index in range(gain_q.shape[0]):
        offset = frame_index * 160
        frame = pcm_float[offset:offset + 320] * sqrt_hann
        spectrum = np.fft.rfft(frame, n=320)
        gains = (
            gain_q[frame_index].astype(np.float64) - gain_zero_point
        ) * gain_scale
        time_domain = np.fft.irfft(spectrum * (gains @ expansion), n=320)
        time_domain *= synthesis * normalization
        output[offset:offset + 160] = time_domain[:160] + overlap
        overlap = time_domain[160:]
    output[-160:] = overlap

    scaled = np.clip(output, -1.0, 0.999969482421875) * 32768.0
    rounded = np.where(scaled >= 0.0, np.floor(scaled + 0.5), np.ceil(scaled - 0.5))
    board_expected = np.clip(rounded, -32768, 32767).astype(np.int16)
    error = board_expected.astype(np.int64) - raw_expected.astype(np.int64)
    signal_energy = float(np.sum(raw_expected.astype(np.float64) ** 2))
    noise_energy = float(np.sum(error.astype(np.float64) ** 2))
    raw_snr = None if noise_energy == 0.0 else float(
        10.0 * np.log10(signal_energy / noise_energy)
    )

    rows = []
    for offset in range(0, board_expected.size, 16):
        rows.append(
            "    " + ", ".join(str(int(value)) for value in board_expected[offset:offset + 16]) + ","
        )
    declaration = (
        f"\nalignas(16) static const int16_t {prefix}_pcm_output_board_int16"
        f"[{board_expected.size}] = {{\n"
        + "\n".join(rows)
        + f"\n}};\nstatic const size_t {prefix}_pcm_output_board_int16_len = "
        f"{board_expected.size};\n"
    )
    if "#endif  // MODEL_GOLDEN_TEST_DATA" not in golden_source:
        raise ValueError(f"golden test guard missing for {scenario}")
    generated = golden_source.replace(
        "\n#endif  // MODEL_GOLDEN_TEST_DATA",
        declaration + "\n#endif  // MODEL_GOLDEN_TEST_DATA",
    )
    return generated, {
        "derived_from_pcm16": True,
        "frames": int(gain_q.shape[0]),
        "samples": int(board_expected.size),
        "raw_package_pcm_snr_db": raw_snr,
        "raw_package_pcm_rmse_lsb": float(np.sqrt(np.mean(error.astype(np.float64) ** 2))),
        "raw_package_pcm_max_error_lsb": int(np.max(np.abs(error))),
    }


def package_version(manifest: dict[str, Any]) -> str:
    experiment_id = str(manifest.get("experiment_id", ""))
    _, separator, version = experiment_id.partition(":")
    if (separator == "") or not re.fullmatch(r"[A-Za-z0-9_.-]+", version):
        raise ValueError("manifest experiment_id must contain a safe package version")
    return version


def render_contract(
    manifest: dict[str, Any],
    vela: dict[str, Any],
    version: str,
    performance: dict[str, int],
) -> str:
    model = manifest["model_files"]["vela_tflite"]
    digest = str(model["sha256"])
    inputs = vela["inputs"]
    outputs = vela["outputs"]
    digest_bytes = ", ".join(
        f"0x{digest[index:index + 2]}" for index in range(0, len(digest), 2)
    )
    return f"""/**
 * @file erb_gru_deploy_contract.h
 * @brief Generated contract for the deployed ERB-GRU Vela FlatBuffer.
 * @author Bui Minh Huy
 * @date 2026-07-29
 *
 * Generated by tools/generate_erb_gru_assets.py. Do not edit.
 */
#pragma once

#define ERB_GRU_PACKAGE_VERSION "{version}"
#define ERB_GRU_MODEL_BYTES {int(model["bytes"])}U
#define ERB_GRU_MODEL_SHA256 "{digest}"
static const unsigned char erb_gru_model_sha256[32] = {{{digest_bytes}}};
#define ERB_GRU_MACS_PER_INFERENCE {performance["macs_per_inference"]}U
#define ERB_GRU_NPU_OPERATOR_COUNT {performance["npu_operator_count"]}U
#define ERB_GRU_CPU_OPERATOR_COUNT {performance["cpu_operator_count"]}U

#define ERB_GRU_BATCH_SIZE 1
#define ERB_GRU_STATE_SIZE 64
#define ERB_GRU_FEATURE_SIZE 32
#define ERB_GRU_STATE_RANK 2
#define ERB_GRU_FEATURE_RANK 3

#define ERB_GRU_INPUT_STATE1_ORDINAL 0
#define ERB_GRU_INPUT_STATE2_ORDINAL 1
#define ERB_GRU_INPUT_FEATURE_ORDINAL 2
#define ERB_GRU_OUTPUT_STATE2_ORDINAL 0
#define ERB_GRU_OUTPUT_STATE1_ORDINAL 1
#define ERB_GRU_OUTPUT_GAIN_ORDINAL 2

#define ERB_GRU_INPUT_STATE1_INDEX {inputs[0]["index"]}
#define ERB_GRU_INPUT_STATE2_INDEX {inputs[1]["index"]}
#define ERB_GRU_INPUT_FEATURE_INDEX {inputs[2]["index"]}
#define ERB_GRU_OUTPUT_STATE2_INDEX {outputs[0]["index"]}
#define ERB_GRU_OUTPUT_STATE1_INDEX {outputs[1]["index"]}
#define ERB_GRU_OUTPUT_GAIN_INDEX {outputs[2]["index"]}

#define ERB_GRU_STATE1_SCALE {c_float(inputs[0]["scale"])}
#define ERB_GRU_STATE1_ZERO_POINT {inputs[0]["zero_point"]}
#define ERB_GRU_STATE2_SCALE {c_float(inputs[1]["scale"])}
#define ERB_GRU_STATE2_ZERO_POINT {inputs[1]["zero_point"]}
#define ERB_GRU_FEATURE_SCALE {c_float(inputs[2]["scale"])}
#define ERB_GRU_FEATURE_ZERO_POINT {inputs[2]["zero_point"]}
#define ERB_GRU_GAIN_SCALE {c_float(outputs[2]["scale"])}
#define ERB_GRU_GAIN_ZERO_POINT {outputs[2]["zero_point"]}
"""


def render_assets_header() -> str:
    return f"""/**
 * @file erb_gru_assets.h
 * @brief Stable includes for the verified ERB-GRU deployment package.
 * @author Bui Minh Huy
 * @date 2026-07-29
 *
 * Generated by tools/generate_erb_gru_assets.py. Do not edit.
 */
#pragma once

#include "models/erb_gru/generated/erb_gru_deploy_contract.h"
#include "models/erb_gru/generated/dsp_constants.h"
"""


def render_model_data_header(version: str) -> str:
    package_root = f"models/erb_gru/packages/{version}"
    return f"""/**
 * @file erb_gru_model_data.h
 * @brief Stable include for the verified ERB-GRU Vela model bytes.
 * @author Bui Minh Huy
 * @date 2026-07-29
 *
 * Generated by tools/generate_erb_gru_assets.py. Do not edit.
 */
#pragma once

#include "{package_root}/erb_gru_vela_model_data.h"
"""


def generated_outputs(
    package: Path,
    manifest: dict[str, Any],
    pre_vela: dict[str, Any],
    vela: dict[str, Any],
    performance: dict[str, int],
) -> dict[str, bytes]:
    vela_path = package / manifest["model_files"]["vela_tflite"]["path"]
    header_path = package / manifest["model_files"]["model_header"]["path"]
    header_matches = model_bytes_from_header(header_path) == vela_path.read_bytes()
    if not header_matches:
        raise ValueError("model header bytes do not match the Vela FlatBuffer")

    version = package_version(manifest)
    if package.name != version:
        raise ValueError(f"package directory {package.name!r} does not match {version!r}")
    expected_tensorflow = str(manifest["dependency_versions"]["tensorflow"])
    if pre_vela["tensorflow_version"] != expected_tensorflow:
        raise ValueError(
            f"TensorFlow {expected_tensorflow} required by package, "
            f"found {pre_vela['tensorflow_version']}"
        )
    outputs = {
        "erb_gru_deploy_contract.h": render_contract(
            manifest, vela, version, performance
        ).encode("utf-8"),
        "erb_gru_assets.h": render_assets_header().encode("utf-8"),
        "erb_gru_model_data.h": render_model_data_header(version).encode("utf-8"),
    }
    dsp_source = package / manifest["dsp_contract"]["header"]
    sanitized_dsp = INVALID_C_FLOAT_PATTERN.sub(
        r"\g<1>\g<2>.0f", dsp_source.read_text(encoding="utf-8")
    )
    outputs["dsp_constants.h"] = sanitized_dsp.encode("utf-8")
    golden_directory = package / manifest["dsp_contract"]["golden"]["directory"]
    board_pcm_reference = {}
    for source in golden_directory.glob("*.h"):
        sanitized = INVALID_C_FLOAT_PATTERN.sub(r"\g<1>\g<2>.0f", source.read_text(encoding="utf-8"))
        scenario = source.stem.removeprefix("golden_")
        generated_golden, reference_audit = append_board_pcm_reference(
            sanitized,
            sanitized_dsp,
            scenario,
            float(vela["outputs"][2]["scale"]),
            int(vela["outputs"][2]["zero_point"]),
        )
        outputs[f"golden/{source.name}"] = generated_golden.encode("utf-8")
        board_pcm_reference[scenario] = reference_audit

    audit = {
        "schema_version": 1,
        "package_version": version,
        "source_manifest_experiment_id": manifest["experiment_id"],
        "deploy_model": {
            "path": manifest["model_files"]["vela_tflite"]["path"],
            "bytes": vela_path.stat().st_size,
            "sha256": sha256_file(vela_path),
        },
        "pre_vela_contract": pre_vela,
        "vela_contract": vela,
        "raw_manifest_indices_are_pre_vela": True,
        "model_header_matches_vela": header_matches,
        "vela_performance": performance,
        "tooling_environment": {
            "tensorflow": pre_vela["tensorflow_version"],
            "manifest_tensorflow": manifest["dependency_versions"]["tensorflow"],
        },
        "board_pcm_reference": board_pcm_reference,
        "passed": True,
    }
    outputs["deploy_audit.json"] = (
        json.dumps(audit, indent=2, sort_keys=True, allow_nan=False) + "\n"
    ).encode("utf-8")
    return outputs


def write_or_check(output: Path, files: dict[str, bytes], check: bool) -> None:
    if check:
        for name, expected in files.items():
            path = output / name
            if not path.is_file() or path.read_bytes() != expected:
                raise ValueError(f"generated asset is stale: {path}")
        return

    output.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        path = output / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()

    try:
        manifest, pre_vela, vela, performance = verify_package(arguments.package)
        outputs = generated_outputs(
            arguments.package, manifest, pre_vela, vela, performance
        )
        write_or_check(arguments.output, outputs, arguments.check)
    except (KeyError, OSError, ValueError) as error:
        print(f"ERB-GRU asset generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
