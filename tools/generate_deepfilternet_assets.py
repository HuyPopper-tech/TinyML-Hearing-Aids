"""Verify and generate the DeepFilterNet U55 firmware asset package.

The source integration package is immutable.  This generator copies only the
two TFLite binaries and small release/Vela/quality metadata into the firmware
package, then renders the deploy contract and compact host fixtures.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import sys
from typing import Any


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SOURCE = (
    ROOT
    / "deepfilternet"
    / "deepfilternet_u55_full_seed20260805"
    / "integration_package"
)
DEFAULT_OUTPUT = ROOT / "models" / "deepfilternet" / "generated"
PACKAGE_VERSION = "deepfilternet_u55_full_seed20260805"

PRE_VELA_NAME = "deepfilternet_u55_int8.tflite"
VELA_NAME = "deepfilternet_u55_int8_vela.tflite"
VELA_BYTES = 1_197_856
VELA_SHA256 = "7ee81189598812499796635c3f88ae4c4bca1ff9be81c79ceb4d1fd6d723e475"
METADATA_FILES = (
    "export_metadata.json",
    "integration_manifest.json",
    "tensor_contract.json",
    "release_gates.json",
    "vela_selection.json",
    "package_audit.json",
    "performance_full.json",
    "metrics_smoke_summary.json",
    "parity_smoke_report.json",
    "vela_m55m1.ini",
    "vela_report.txt",
    "vela_report_performance.txt",
    "vela_report_size.txt",
)
FIXTURE_NAMES = ("silence", "impulse", "sine_440", "sine_3000", "noise")
MODEL_ARRAY_RE = re.compile(
    rb"deepfilternet_u55_vela_model\s*\[\s*\]\s*=\s*\{(?P<body>.*?)\};",
    re.DOTALL,
)
FLOAT_LITERAL_RE = re.compile(r"(?<![.\w])(-?)(0|1)f(?=[,;])")
DSP_ARRAY_RE = re.compile(
    r"alignas\(16\)\s+static\s+const\s+float\s+"
    r"(?P<name>dfn_[a-z0-9_]+)\[[^]]+\]\s*=\s*\{.*?\};\s*"
    r"static\s+const\s+size_t\s+\1_len\s*=\s*\d+;",
    re.DOTALL,
)
VELA_COMPLEXITY_RE = re.compile(
    r"CPU operators\s*=\s*(?P<cpu>\d+).*?"
    r"NPU operators\s*=\s*(?P<npu>\d+).*?"
    r"Neural network macs\s+(?P<macs>\d+)\s+MACs/batch",
    re.DOTALL,
)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    return sha256_bytes(path.read_bytes())


def require_file(path: Path) -> bytes:
    if not path.is_file():
        raise ValueError(f"required source file missing: {path}")
    return path.read_bytes()


def verify_identity(path: Path, expected_bytes: int, expected_sha256: str) -> bytes:
    data = require_file(path)
    if len(data) != expected_bytes:
        raise ValueError(f"size mismatch for {path.name}: {len(data)} != {expected_bytes}")
    digest = sha256_bytes(data)
    if digest != expected_sha256:
        raise ValueError(f"SHA-256 mismatch for {path.name}: {digest} != {expected_sha256}")
    return data


def _quantization(detail: dict[str, Any]) -> tuple[float, int]:
    parameters = detail["quantization_parameters"]
    scales = parameters["scales"]
    zero_points = parameters["zero_points"]
    if len(scales) != 1 or len(zero_points) != 1:
        raise ValueError(f"per-axis quantization is not supported: {detail['name']}")
    return float(scales[0]), int(zero_points[0])


def tensor_description(detail: dict[str, Any], ordinal: int) -> dict[str, Any]:
    import numpy as np

    shape = [int(value) for value in detail["shape"]]
    dtype = np.dtype(detail["dtype"])
    scale, zero_point = _quantization(detail)
    return {
        "ordinal": ordinal,
        "index": int(detail["index"]),
        "raw_index": int(detail["index"]),
        "name": str(detail["name"]),
        "shape": shape,
        "dtype": dtype.name,
        "bytes": int(np.prod(shape)) * int(dtype.itemsize),
        "scale": scale,
        "zero_point": zero_point,
    }


def inspect_tflite(path: Path) -> dict[str, Any]:
    try:
        import tensorflow as tf
    except ImportError as error:  # pragma: no cover - environment diagnostic
        raise ValueError("TensorFlow 2.20 is required to inspect TFLite contracts") from error
    if not str(tf.__version__).startswith("2.20."):
        raise ValueError(f"TensorFlow 2.20 is required, found {tf.__version__}")

    interpreter = tf.lite.Interpreter(model_path=str(path))
    return {
        "tensorflow_version": str(tf.__version__),
        "inputs": [
            tensor_description(detail, ordinal)
            for ordinal, detail in enumerate(interpreter.get_input_details())
        ],
        "outputs": [
            tensor_description(detail, ordinal)
            for ordinal, detail in enumerate(interpreter.get_output_details())
        ],
        "tensor_count": len(interpreter.get_tensor_details()),
        "operator_names": [
            str(operator["op_name"])
            for operator in interpreter._get_ops_details()  # pylint: disable=protected-access
        ],
    }


def _assert_tensor(actual: dict[str, Any], expected: dict[str, Any]) -> None:
    expected_bytes = int(math.prod(int(value) for value in expected["shape"]))
    expected = {"bytes": expected_bytes, **expected}
    for key in ("name", "shape", "dtype", "bytes", "zero_point"):
        if actual[key] != expected[key]:
            raise ValueError(f"tensor contract mismatch for {expected}: {key}")
    if actual["scale"] != float(expected["scale"]):
        raise ValueError(f"tensor contract mismatch for {expected}: scale")


def _match_contract(
    actual: dict[str, Any], expected: dict[str, dict[str, Any]], label: str
) -> dict[str, Any]:
    matches = [
        (logical_name, detail)
        for logical_name, detail in expected.items()
        if detail["name"] == actual["name"]
        and [int(value) for value in detail["shape"]] == actual["shape"]
    ]
    if len(matches) != 1:
        raise ValueError(f"cannot map {label} tensor ordinal {actual['ordinal']}: {actual}")
    logical_name, detail = matches[0]
    _assert_tensor(actual, {**detail, "dtype": "int8"})
    return {**actual, "logical_name": logical_name, "manifest_index": int(detail["index"])}


def verify_contracts(source: Path, pre_vela: dict[str, Any], vela: dict[str, Any]) -> dict[str, Any]:
    contract = json.loads((source / "tensor_contract.json").read_text(encoding="utf-8"))
    expected_inputs = contract["inputs"]
    expected_outputs = contract["outputs"]
    if len(vela["inputs"]) != 9 or len(vela["outputs"]) != 10:
        raise ValueError("Vela model must expose exactly nine inputs and ten outputs")
    if vela["tensor_count"] != 21:
        raise ValueError(f"Vela model tensor count is {vela['tensor_count']}, expected 21")
    if vela["operator_names"] != ["ethos-u"]:
        raise ValueError(f"Vela model must expose exactly one ethos-u operator: {vela}")
    if len(pre_vela["inputs"]) != 9 or len(pre_vela["outputs"]) != 10:
        raise ValueError("pre-Vela model must expose exactly nine inputs and ten outputs")

    mapped_pre_inputs = [_match_contract(item, expected_inputs, "pre-Vela input") for item in pre_vela["inputs"]]
    mapped_pre_outputs = [_match_contract(item, expected_outputs, "pre-Vela output") for item in pre_vela["outputs"]]
    mapped_inputs = [_match_contract(item, expected_inputs, "input") for item in vela["inputs"]]
    mapped_outputs = [_match_contract(item, expected_outputs, "output") for item in vela["outputs"]]
    # The pre-Vela manifest indices are intentionally retained as evidence, but
    # deploy ordinals and raw indices always come from the selected Vela model.
    return {
        "pre_vela": {
            "tensorflow_version": pre_vela["tensorflow_version"],
            "inputs": mapped_pre_inputs,
            "outputs": mapped_pre_outputs,
            "tensor_count": pre_vela["tensor_count"],
            "operator_names": pre_vela["operator_names"],
        },
        "vela": {
            "tensorflow_version": vela["tensorflow_version"],
            "inputs": mapped_inputs,
            "outputs": mapped_outputs,
            "tensor_count": vela["tensor_count"],
            "operator_names": vela["operator_names"],
        },
    }


def model_bytes_from_c(source: bytes) -> bytes:
    match = MODEL_ARRAY_RE.search(source)
    if match is None:
        raise ValueError("Vela model byte array not found")
    values = re.findall(rb"(?:0[xX][0-9a-fA-F]+|\d+)", match.group("body"))
    try:
        return bytes(int(value, 0) for value in values)
    except ValueError as error:
        raise ValueError("invalid model byte in C source") from error


def vela_complexity(source: Path) -> dict[str, int]:
    report = require_file(source / "vela_report.txt").decode("utf-8")
    match = VELA_COMPLEXITY_RE.search(report)
    if match is None:
        raise ValueError("Vela report is missing MAC/operator complexity")
    return {
        "macs_per_frame": int(match.group("macs")),
        "npu_operators": int(match.group("npu")),
        "cpu_operators": int(match.group("cpu")),
    }


def strip_dsp_constants(source: bytes) -> bytes:
    text = source.decode("utf-8")
    matches = {match.group("name"): match.group(0) for match in DSP_ARRAY_RE.finditer(text)}
    required = ("dfn_erb_analysis_257x32", "dfn_sqrt_hann_512")
    if any(name not in matches for name in required):
        raise ValueError("dsp constants are missing ERB analysis or sqrt-Hann")
    preamble = text[: text.index("alignas(16)")]
    stripped = preamble + "\n".join(matches[name] for name in required) + "\n"
    return FLOAT_LITERAL_RE.sub(r"\g<1>\g<2>.0f", stripped).encode("utf-8")


def _c_float(value: float) -> str:
    return f"{value:.12g}f"


def _macro_name(logical_name: str) -> str:
    return re.sub(r"[^A-Z0-9]+", "_", logical_name.upper()).strip("_")


def render_deploy_contract(
    version: str,
    model: bytes,
    contract: dict[str, Any],
    complexity: dict[str, int],
) -> bytes:
    digest = sha256_bytes(model)
    lines = [
        "/** Generated by tools/generate_deepfilternet_assets.py. Do not edit. */",
        "#pragma once",
        "",
        f'#define DFN_PACKAGE_VERSION "{version}"',
        f"#define DFN_MODEL_BYTES {len(model)}U",
        f'#define DFN_MODEL_SHA256 "{digest}"',
        "#define DFN_INPUT_COUNT 9U",
        "#define DFN_OUTPUT_COUNT 10U",
        "#define DFN_TENSOR_COUNT 21U",
        "#define DFN_OPERATOR_COUNT 1U",
        "#define DFN_HAS_ETHOS_U_CUSTOM_OP 1",
        f"#define DFN_MODEL_MACS_PER_FRAME {complexity['macs_per_frame']}U",
        f"#define DFN_MODEL_NPU_OPERATOR_COUNT {complexity['npu_operators']}U",
        f"#define DFN_MODEL_CPU_OPERATOR_COUNT {complexity['cpu_operators']}U",
        "",
    ]
    for direction in ("inputs", "outputs"):
        prefix = "DFN_INPUT" if direction == "inputs" else "DFN_OUTPUT"
        for tensor in contract[direction]:
            ordinal = tensor["ordinal"]
            name = _macro_name(tensor["logical_name"])
            lines.extend(
                [
                    f"#define {prefix}_{ordinal}_ORDINAL {ordinal}U",
                    f"#define {prefix}_{ordinal}_INDEX {tensor['raw_index']}U",
                    f'#define {prefix}_{ordinal}_NAME "{tensor["name"]}"',
                    f"#define {prefix}_{ordinal}_BYTES {tensor['bytes']}U",
                    f"#define {prefix}_{ordinal}_SCALE {_c_float(tensor['scale'])}",
                    f"#define {prefix}_{ordinal}_ZERO_POINT {tensor['zero_point']}",
                    f"#define {prefix}_{ordinal}_RANK {len(tensor['shape'])}U",
                ]
            )
            for axis, size in enumerate(tensor["shape"]):
                lines.append(f"#define {prefix}_{ordinal}_SHAPE_{axis} {size}U")
            lines.extend(
                [
                    f"#define {prefix}_{name}_ORDINAL {ordinal}U",
                    f"#define {prefix}_{name}_INDEX {tensor['raw_index']}U",
                    f"#define {prefix}_{name}_SCALE {_c_float(tensor['scale'])}",
                    f"#define {prefix}_{name}_ZERO_POINT {tensor['zero_point']}",
                    "",
                ]
            )
    lines.append(f"static const unsigned char dfn_model_sha256[32] = {{{', '.join(f'0x{byte:02x}' for byte in bytes.fromhex(digest))}}};")
    return ("\n".join(lines) + "\n").encode("utf-8")


def render_frontend_contract(contract: dict[str, Any]) -> bytes:
    wanted = (
        ("inputs", "erb_feature"),
        ("inputs", "complex_feature"),
        ("outputs", "gain_bins"),
        ("outputs", "df_coefficients"),
        ("outputs", "alpha"),
    )
    lines = [
        "/** Generated by tools/generate_deepfilternet_assets.py. Do not edit. */",
        "#pragma once",
        "",
    ]
    for direction, logical_name in wanted:
        values = contract[direction]
        detail = next(item for item in values if item["logical_name"] == logical_name)
        prefix = f"DFN_{direction.upper()}_{_macro_name(logical_name)}"
        lines.extend(
            [
                f"#define {prefix}_SCALE {_c_float(detail['scale'])}",
                f"#define {prefix}_ZERO_POINT {detail['zero_point']}",
                "",
            ]
        )
    return ("\n".join(lines)).encode("utf-8")


def render_assets_header() -> bytes:
    return b"""/** Generated by tools/generate_deepfilternet_assets.py. Do not edit. */
#pragma once

#include \"deepfilternet_deploy_contract.h\"
#include \"deepfilternet_frontend_contract.h\"
#include \"dsp_constants.h\"
#include \"deepfilternet_u55_vela_model_data.h\"
"""


def render_model_data_header() -> bytes:
    return b"""/** Generated by tools/generate_deepfilternet_assets.py. Do not edit. */
#pragma once
#include <stddef.h>
#include <stdint.h>
extern const unsigned char deepfilternet_u55_vela_model[];
extern const size_t deepfilternet_u55_vela_model_len;
"""


def _fixture_samples(name: str) -> list[int]:
    if name == "silence":
        return [0] * 512
    if name == "impulse":
        return [32767] + [0] * 511
    if name.startswith("sine_"):
        frequency = int(name.removeprefix("sine_"))
        return [int(round(12000 * math.sin(2.0 * math.pi * frequency * n / 16000.0))) for n in range(512)]
    state = 0xD1F17E
    samples = []
    for _ in range(512):
        state = (1664525 * state + 1013904223) & 0xFFFFFFFF
        samples.append(((state >> 16) & 0xFFFF) - 32768)
    return samples


def render_fixture(name: str) -> tuple[bytes, bytes]:
    samples = _fixture_samples(name)
    values = ", ".join(str(value) for value in samples)
    lines = [
        "/** Generated by tools/generate_deepfilternet_assets.py. Do not edit. */",
        "#pragma once",
        "#include <stddef.h>",
        "#include <stdint.h>",
        "",
        f"#define DFN_FIXTURE_{_macro_name(name)}_SAMPLES 512U",
        f"static const int16_t dfn_fixture_{name}[512] = {{{values}}};",
        f"static const size_t dfn_fixture_{name}_len = 512U;",
        "",
    ]
    pcm = b"".join(int(value).to_bytes(2, "little", signed=True) for value in samples)
    return ("\n".join(lines)).encode("utf-8"), pcm


def generated_audit(
    source: Path,
    model: bytes,
    pre_contract: dict[str, Any],
    vela_contract: dict[str, Any],
    fixture_audit: dict[str, Any],
    complexity: dict[str, int],
) -> bytes:
    integration = json.loads((source / "integration_manifest.json").read_text(encoding="utf-8"))
    release = json.loads((source / "release_gates.json").read_text(encoding="utf-8"))
    selection = json.loads((source / "vela_selection.json").read_text(encoding="utf-8"))
    metrics = json.loads((source / "metrics_smoke_summary.json").read_text(encoding="utf-8"))
    audit = {
        "schema_version": 1,
        "package_version": PACKAGE_VERSION,
        "source_package": str(source.relative_to(ROOT).as_posix()) if source.is_relative_to(ROOT) else str(source),
        "deploy_model": {
            "path": VELA_NAME,
            "bytes": len(model),
            "sha256": sha256_bytes(model),
        },
        "complexity": complexity,
        "vela_contract": vela_contract,
        "pre_vela_contract": pre_contract,
        "frontend_contract": {
            "header": "deepfilternet_frontend_contract.h",
            "symbols": [
                "DFN_INPUTS_ERB_FEATURE_SCALE",
                "DFN_INPUTS_ERB_FEATURE_ZERO_POINT",
                "DFN_INPUTS_COMPLEX_FEATURE_SCALE",
                "DFN_INPUTS_COMPLEX_FEATURE_ZERO_POINT",
                "DFN_OUTPUTS_GAIN_BINS_SCALE",
                "DFN_OUTPUTS_GAIN_BINS_ZERO_POINT",
                "DFN_OUTPUTS_DF_COEFFICIENTS_SCALE",
                "DFN_OUTPUTS_DF_COEFFICIENTS_ZERO_POINT",
                "DFN_OUTPUTS_ALPHA_SCALE",
                "DFN_OUTPUTS_ALPHA_ZERO_POINT",
            ],
        },
        "dsp_constants": {
            "header": "dsp_constants.h",
            "symbols": ["dfn_erb_analysis_257x32", "dfn_sqrt_hann_512"],
        },
        "pcm_fixtures": fixture_audit,
        "release_status": integration.get("release_status", "engineering_ready_smoke_passed"),
        "quality_scope": "smoke_512_not_release_gate",
        "quality": {
            "source_scope": metrics.get("scope", "smoke_128_not_release_gate"),
            "scope": "smoke_512_not_release_gate",
            "passed": bool(metrics.get("passed", False)),
            "release_gate_enforced": False,
        },
        "release_gates": release,
        "vela_selection": selection,
        "model_translation_units": ["deepfilternet_u55_vela_model_data.c"],
        "passed": True,
    }
    return (json.dumps(audit, indent=2, sort_keys=True, allow_nan=False) + "\n").encode("utf-8")


def verify_source(source: Path) -> tuple[bytes, bytes, dict[str, Any], dict[str, Any], bytes]:
    manifest = json.loads((source / "integration_manifest.json").read_text(encoding="utf-8"))
    export = json.loads((source / "export_metadata.json").read_text(encoding="utf-8"))
    pre_meta = export["pre_vela"]
    pre_model = verify_identity(source / PRE_VELA_NAME, int(pre_meta["bytes"]), str(pre_meta["sha256"]))
    vela_model = verify_identity(source / VELA_NAME, VELA_BYTES, VELA_SHA256)
    vela_c = require_file(source / "deepfilternet_u55_vela_model_data.c")
    if model_bytes_from_c(vela_c) != vela_model:
        raise ValueError("Vela model-data C bytes do not match selected FlatBuffer")
    pre = inspect_tflite(source / PRE_VELA_NAME)
    vela = inspect_tflite(source / VELA_NAME)
    mapped = verify_contracts(source, pre, vela)
    if manifest.get("release_status") != "engineering_ready_smoke_passed":
        raise ValueError("source release status changed unexpectedly")
    return pre_model, vela_model, mapped["pre_vela"], mapped["vela"], vela_c


def build_outputs(source: Path) -> tuple[dict[str, bytes], dict[str, bytes]]:
    pre_model, vela_model, pre, vela, vela_c = verify_source(source)
    complexity = vela_complexity(source)
    fixtures: dict[str, bytes] = {}
    fixture_audit: dict[str, Any] = {}
    for name in FIXTURE_NAMES:
        header, pcm = render_fixture(name)
        fixtures[f"golden/golden_{name}.h"] = header
        fixture_audit[name] = {
            "path": f"golden/golden_{name}.h",
            "samples": 512,
            "pcm16_sha256": sha256_bytes(pcm),
            "generator": "deterministic_math_v1",
        }
    generated = {
        "deepfilternet_deploy_contract.h": render_deploy_contract(
            PACKAGE_VERSION, vela_model, vela, complexity
        ),
        "deepfilternet_frontend_contract.h": render_frontend_contract(vela),
        "deepfilternet_assets.h": render_assets_header(),
        "deepfilternet_model_data.h": render_model_data_header(),
        "deepfilternet_u55_vela_model_data.h": require_file(source / "deepfilternet_u55_vela_model_data.h"),
        "deepfilternet_u55_vela_model_data.c": vela_c,
        "dsp_constants.h": strip_dsp_constants(require_file(source / "dsp_constants.h")),
        **fixtures,
    }
    generated["golden/golden_manifest.json"] = (
        json.dumps(
            {
                "minimum_frames": 1,
                "scenarios": {
                    name: {
                        "header": f"golden_{name}.h",
                        "frames": 1,
                        "samples": 512,
                        "sha256": sha256_bytes(
                            fixtures[f"golden/golden_{name}.h"]
                        ),
                    }
                    for name in FIXTURE_NAMES
                },
            },
            indent=2,
            sort_keys=True,
        )
        + "\n"
    ).encode("utf-8")
    generated["deploy_audit.json"] = generated_audit(
        source, vela_model, pre, vela, fixture_audit, complexity
    )

    package = {
        PRE_VELA_NAME: pre_model,
        VELA_NAME: vela_model,
    }
    for name in METADATA_FILES:
        package[name] = require_file(source / name)
    return generated, package


def _check_model_tus(directory: Path) -> None:
    if not directory.is_dir():
        return
    model_tus = sorted(path for path in directory.rglob("*.c") if "model_data" in path.name)
    if len(model_tus) != 1:
        raise ValueError(f"expected exactly one model-data translation unit, found {model_tus}")


def write_or_check(
    directory: Path, files: dict[str, bytes], check: bool, *, model_tu: bool = False
) -> None:
    expected = {Path(name).as_posix() for name in files}
    if check:
        if model_tu:
            _check_model_tus(directory)
        if not directory.is_dir():
            raise ValueError(f"generated directory missing: {directory}")
        actual = {path.relative_to(directory).as_posix() for path in directory.rglob("*") if path.is_file()}
        if actual != expected:
            raise ValueError(f"generated asset set is stale: unexpected={sorted(actual - expected)} missing={sorted(expected - actual)}")
        for name, content in files.items():
            path = directory / name
            if path.read_bytes() != content:
                raise ValueError(f"generated asset is stale: {path}")
        return
    directory.mkdir(parents=True, exist_ok=True)
    for name, content in files.items():
        path = directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)
    if model_tu:
        _check_model_tus(directory)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", "--package", dest="source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--package-output", type=Path, default=None)
    parser.add_argument("--check", action="store_true")
    arguments = parser.parse_args()
    source = arguments.source.resolve()
    output = arguments.output.resolve()
    package_output = (
        arguments.package_output.resolve()
        if arguments.package_output is not None
        else output.parent / "packages" / PACKAGE_VERSION
    )
    try:
        generated, package = build_outputs(source)
        write_or_check(output, generated, arguments.check, model_tu=True)
        write_or_check(package_output, package, arguments.check)
    except (KeyError, OSError, ValueError, ImportError) as error:
        print(f"DeepFilterNet asset generation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
