"""Verify that a Release AXF contains real Cortex-M55 Helium instructions."""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
from pathlib import Path


VECTOR_REGISTER = re.compile(r"\bq[0-7]\b", re.IGNORECASE)
REQUIRED_KERNELS = (
    "deepfir_feature_pcm16", "deepfir_dsp_process_mve", "arm_cfft_f32",
    "arm_vlog_f32", "arm_vexp_f32", "arm_q15_to_float",
)

REQUIRED_MAP_REFERENCES = (
    "deepfir_features.o(.text.deepfir_feature_pcm16) refers to arm_q15_to_float.o",
    "deepfir_features.o(.text.deepfir_feature_pcm16) refers to arm_vlog_f32.o",
    "deepfir_features.o(.text.deepfir_feature_pcm16) refers to arm_vexp_f32.o",
    "deepfir_features.o(.text.deepfir_feature_pcm16) refers to arm_scale_f32.o",
    "deepfir_dsp_process_mve",
    "deepfir_model_adapter.o(.text.deepfir_model_adapter_process_hop) refers to arm_q15_to_float.o",
    "deepfir_model_adapter.o(.text.deepfir_model_adapter_process_pcm16) refers to arm_q15_to_float.o",
)


def verify_kernel_ranges(map_text: str, disassembly: str) -> dict[str, int]:
    """Reject vectors elsewhere in the image and ARM-mode decode false positives."""
    instructions = []
    for line in disassembly.splitlines():
        match = re.match(r"\s*([0-9a-fA-F]+):", line)
        if match and VECTOR_REGISTER.search(line):
            instructions.append((int(match.group(1), 16), line))
    counts = {}
    for name in REQUIRED_KERNELS:
        symbol = re.search(r"^\s*" + re.escape(name) +
                           r"\s+(0x[0-9a-fA-F]+)\s+Thumb Code\s+(\d+)",
                           map_text, re.MULTILINE)
        if symbol is None:
            raise ValueError(f"Missing Thumb function range: {name}")
        start = int(symbol.group(1), 16) & ~1
        end = start + int(symbol.group(2))
        lines = [line for address, line in instructions if start <= address < end]
        if not lines:
            raise ValueError(f"No MVE vector registers in {name}")
        if name == "deepfir_dsp_process_mve" and not any(
                re.search(r"\bvfma\.f32\s+q[0-7]", line, re.IGNORECASE) for line in lines):
            raise ValueError("FIR has no vector floating-point FMA")
        counts[name] = len(lines)
    return counts


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("axf", type=Path)
    parser.add_argument("--fromelf", help="Legacy argument; explicit Thumb decoding uses --objdump")
    parser.add_argument("--objdump", default=shutil.which("arm-none-eabi-objdump"))
    args = parser.parse_args()
    if not args.axf.is_file():
        raise SystemExit(f"AXF is missing: {args.axf}")
    map_path = args.axf.with_suffix(args.axf.suffix + ".map")
    if not map_path.is_file():
        raise SystemExit(f"Map is missing: {map_path}")
    map_text = map_path.read_text(encoding="utf-8", errors="replace")
    missing = [item for item in REQUIRED_MAP_REFERENCES if item not in map_text]
    if missing:
        raise SystemExit("Missing DeepFIR MVE linkage: " + ", ".join(missing))
    if not args.objdump:
        raise SystemExit("arm-none-eabi-objdump is required for explicit Thumb/MVE decoding")
    # Release --no_debug removes the mapping symbols fromelf needs for Thumb.
    result = subprocess.run(
        [args.objdump, "-d", "-m", "armv8.1-m.main", "-M", "force-thumb", str(args.axf)],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        raise SystemExit(result.stderr.strip() or "objdump failed")
    try:
        counts = verify_kernel_ranges(map_text, result.stdout)
    except ValueError as error:
        raise SystemExit(str(error)) from error
    print("DeepFIR feature, adapter, and FIR MVE linkage present")
    for name, count in counts.items():
        print(f"  {name}: {count} vector instructions in its linked Thumb range")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
