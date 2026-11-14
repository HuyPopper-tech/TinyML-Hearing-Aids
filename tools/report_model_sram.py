"""Report DeepFIR's linked SRAM footprint from an Arm linker map.

Author: Bui Minh Huy
Date: 2026-09-25
"""

import argparse
import re
from pathlib import Path


REGION = re.compile(
    r"^\s*Execution Region (\S+) \(Exec base: (0x[0-9a-fA-F]+), "
    r"Load base: .*?, Size: (0x[0-9a-fA-F]+),"
)
DETAILS = (
    ("tensor arena", "g_tensor_arena"),
    ("recurrent/FIR adapter", "g_adapterE"),
    ("feature and FFT state", "g_feature_state"),
    ("interpreter storage", "g_interpreter_storage"),
)


def report(map_path: Path) -> None:
    regions = {}
    sections = []
    region_name = None
    for line in map_path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = REGION.match(line)
        if match:
            region_name = match.group(1)
            regions[region_name] = (int(match.group(2), 16), int(match.group(3), 16))
            continue
        fields = line.split()
        if (region_name is None or len(fields) < 8 or
                not fields[0].startswith("0x") or
                not fields[2].startswith("0x")):
            continue
        sections.append((region_name, fields[-2], fields[-1], int(fields[2], 16)))

    if "U55_MODEL_RAM" not in regions or "RW_NOINIT" not in regions:
        raise ValueError("model SRAM regions are missing from the linker map")
    model_base, linked = regions["U55_MODEL_RAM"]
    reserved = regions["RW_NOINIT"][0] - model_base
    image = sum(size for region, _, obj, size in sections
                if region == "U55_MODEL_RAM" and obj == "deepfir_model_data.o")
    if not (0 < image <= linked <= reserved):
        raise ValueError("invalid DeepFIR image size or SRAM region boundary")

    static_sections = [(name, size) for region, name, obj, size in sections
                       if region == "RW_RAM" and obj.startswith("deepfir_")]
    static_total = sum(size for _, size in static_sections)
    print(f"[MODEL SRAM] {map_path}")
    print(f"  model image and metadata : {image:>9,} B")
    print(f"  region alignment/padding : {linked - image:>9,} B")
    print(f"  U55_MODEL_RAM linked      : {linked:>9,} B / {reserved:,} B reserved "
          f"({linked / reserved:.1%}); free {reserved - linked:,} B")
    detailed = 0
    for label, marker in DETAILS:
        size = sum(size for name, size in static_sections if marker in name)
        detailed += size
        print(f"  {label:<24}: {size:>9,} B")
    print(f"  other DeepFIR statics    : {static_total - detailed:>9,} B")
    print(f"  model-owned SRAM linked  : {linked + static_total:>9,} B")
    dtcm_total = sum(size for region, _, obj, size in sections
                     if region == "DTCM" and obj.startswith("deepfir_"))
    print(f"  CPU-only DeepFIR DTCM    : {dtcm_total:>9,} B (excluded from SRAM above)")
    print(f"  RW_RAM all firmware      : {regions['RW_RAM'][1]:>9,} B")
    print("  Tensor arena is allocated capacity; runtime peak use is not in the map.")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("map_file", type=Path)
    args = parser.parse_args()
    try:
        report(args.map_file)
    except (OSError, ValueError) as exc:
        parser.exit(1, f"model SRAM report: {exc}\n")
