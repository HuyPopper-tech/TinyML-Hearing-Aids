"""Generate reproducible Vela 5.1 memory-profile firmware packages."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MODEL_ROOT = ROOT / "models" / "speech_boosting"
GENERATED = MODEL_ROOT / "generated"
PROFILES_ROOT = MODEL_ROOT / "profiles"
CONFIGURATION = MODEL_ROOT / "integration" / "vela_m55m1_profiles.ini"
RAW_MODEL = GENERATED / "speech_boosting_qat_raw_int8.tflite"
BASELINE_SHA256 = "c0c2a641f2b317925003632b5eabcf1f4b1f4f9018f3ed5386e76f105be1f6c7"
PROFILE_NAMES = ("aprom_x030", "aprom_x050", "aprom_x100", "sram01_x100")
VELA_CONFIGURATION_NAMES = {
    "aprom_x030": ("Ethos_U55_M55M1_FLASH_X0.300", "Shared_1024K"),
    "aprom_x050": ("M55M1_aprom_x050", "M55M1_aprom_x050"),
    "aprom_x100": ("M55M1_aprom_x100", "M55M1_aprom_x100"),
    "sram01_x100": ("M55M1_sram01_x100", "M55M1_sram01_x100"),
}


def run_vela(profile: str, output_directory: Path) -> tuple[Path, str, str]:
    system_configuration, memory_mode = VELA_CONFIGURATION_NAMES[profile]
    command = [
        sys.executable,
        "-m",
        "ethosu.vela",
        str(RAW_MODEL),
        "--config",
        str(CONFIGURATION),
        "--system-config",
        system_configuration,
        "--memory-mode",
        memory_mode,
        "--accelerator-config",
        "ethos-u55-256",
        "--optimise",
        "Performance",
        "--verbose-cycle-estimate",
        "--output-dir",
        str(output_directory),
    ]
    completed = subprocess.run(command, capture_output=True, text=True, check=False)
    report = completed.stdout + completed.stderr
    if completed.returncode != 0:
        raise RuntimeError(f"Vela failed for {profile}:\n{report}")
    model = output_directory / f"{RAW_MODEL.stem}_vela.tflite"
    if not model.is_file():
        raise RuntimeError(f"Vela did not produce {model}")
    if "CPU operators = 0 (0.0%)" not in report or "NPU operators = 197 (100.0%)" not in report:
        raise RuntimeError(f"{profile} violates the Ethos-U operator contract:\n{report}")
    return model, " ".join(command), report


def write_model_data(model: bytes, destination: Path, mutable: bool) -> None:
    qualifier = "uint8_t" if mutable else "const uint8_t"
    section = '__attribute__((section(".data.U55_MODEL_RAM"))) ' if mutable else ""
    rows = []
    for offset in range(0, len(model), 12):
        rows.append("    " + ", ".join(f"0x{value:02x}" for value in model[offset : offset + 12]) + ",")
    destination.write_text(
        '#include "speech_boosting_u55_vela_model_data.h"\n\n'
        f"alignas(32) {section}{qualifier} g_speech_boosting_u55_vela_model[] = {{\n"
        + "\n".join(rows)
        + "\n};\n\n"
        "const size_t g_speech_boosting_u55_vela_model_len = "
        "sizeof(g_speech_boosting_u55_vela_model);\n",
        encoding="utf-8",
    )


def write_model_header(destination: Path, mutable: bool) -> None:
    qualifier = "uint8_t" if mutable else "const uint8_t"
    destination.write_text(
        "#pragma once\n#include <stddef.h>\n#include <stdint.h>\n\n"
        f"extern {qualifier} g_speech_boosting_u55_vela_model[];\n"
        "extern const size_t g_speech_boosting_u55_vela_model_len;\n",
        encoding="utf-8",
    )


def write_contracts(profile: str, model: bytes, destination: Path) -> None:
    deployment = (GENERATED / "speech_boosting_deploy_contract.h").read_text(encoding="utf-8")
    model_hash = hashlib.sha256(model).hexdigest()
    hash_bytes = ", ".join(f"0x{byte:02x}" for byte in bytes.fromhex(model_hash))
    deployment = re.sub(
        r"#define SPEECH_BOOSTING_VELA_MODEL_BYTES \d+U",
        f"#define SPEECH_BOOSTING_VELA_MODEL_BYTES {len(model)}U",
        deployment,
    )
    deployment = re.sub(
        r"static const uint8_t SPEECH_BOOSTING_VELA_MODEL_SHA256\[32\] = \{[^}]+\};",
        f"static const uint8_t SPEECH_BOOSTING_VELA_MODEL_SHA256[32] = {{{hash_bytes}}};",
        deployment,
        flags=re.DOTALL,
    )
    (destination / "speech_boosting_deploy_contract.h").write_text(deployment, encoding="utf-8")

    generated_contract = (GENERATED / "speech_boosting_generated_contract.h").read_text(encoding="utf-8")
    if profile == "sram01_x100":
        generated_contract = generated_contract.replace("(128U * 1024U)", "(64U * 1024U)")
    (destination / "speech_boosting_generated_contract.h").write_text(generated_contract, encoding="utf-8")

    model_contract = (MODEL_ROOT / "speech_boosting_model_contract.h").read_text(encoding="utf-8")
    model_contract = re.sub(
        r'#define SPEECH_BOOSTING_VELA_SHA256 "[0-9a-f]+"',
        f'#define SPEECH_BOOSTING_VELA_SHA256 "{model_hash}"',
        model_contract,
    )
    model_contract += (
        f'\n#define SPEECH_BOOSTING_MEMORY_PROFILE_NAME "{profile}"\n'
        f'#define SPEECH_BOOSTING_MODEL_MEMORY_REGION_NAME "{"sram01" if profile == "sram01_x100" else "aprom"}"\n'
    )
    (destination / "speech_boosting_model_contract.h").write_text(model_contract, encoding="utf-8")


def generate_profile(profile: str) -> None:
    package = PROFILES_ROOT / profile
    generated = package / "generated"
    with tempfile.TemporaryDirectory(prefix=f"vela_{profile}_") as temporary:
        vela_output = Path(temporary) / "vela"
        vela_output.mkdir()
        vela_model, command, report = run_vela(profile, vela_output)
        model = vela_model.read_bytes()

    if profile == "aprom_x030" and hashlib.sha256(model).hexdigest() != BASELINE_SHA256:
        raise RuntimeError("aprom_x030 is not byte-identical to the approved production artifact")
    if package.exists():
        shutil.rmtree(package)
    generated.mkdir(parents=True)
    for name in (
        "speech_boosting_qat_raw_int8.tflite",
        "golden_raw_int8_100_chunks.npz",
        "quantization.json",
        "stream_io_contract.json",
        "tensor_map.json",
    ):
        shutil.copy2(GENERATED / name, generated / name)
    (generated / "speech_boosting_qat_vela.tflite").write_bytes(model)
    write_model_data(model, generated / "speech_boosting_u55_vela_model_data.cc", profile == "sram01_x100")
    write_model_header(generated / "speech_boosting_u55_vela_model_data.h", profile == "sram01_x100")
    write_contracts(profile, model, generated)
    (package / "vela_command.txt").write_text(command + "\n", encoding="utf-8")
    (package / "vela_report.txt").write_text(report, encoding="utf-8")
    manifest = {
        "memory_profile": profile,
        "vela_version": "5.1.0",
        "raw_int8_sha256": hashlib.sha256(RAW_MODEL.read_bytes()).hexdigest(),
        "vela_sha256": hashlib.sha256(model).hexdigest(),
        "vela_cpu_operators": 0,
        "vela_ethosu_operators": 197,
        "stream_inputs": 21,
        "stream_outputs": 21,
        "state_tensors": 20,
        "state_bytes": 5632,
        "model_memory_region": "sram01" if profile == "sram01_x100" else "aprom",
    }
    (package / "firmware_export_manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8"
    )


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("profile", choices=(*PROFILE_NAMES, "all"), default="all", nargs="?")
    arguments = parser.parse_args()
    profiles = PROFILE_NAMES if arguments.profile == "all" else (arguments.profile,)
    for profile in profiles:
        generate_profile(profile)
        print(f"GENERATED_PROFILE={profile}")


if __name__ == "__main__":
    main()
