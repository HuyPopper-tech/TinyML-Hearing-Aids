"""Validate the immutable NeuralAid export copied into firmware assets."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "neuralaid_u55_equivalent_seed20260823" / "integration_package"
DESTINATION = ROOT / "models" / "neuralaid" / "generated"
MODEL = "neuralaid_int8_vela.tflite"
SHA256 = "074d441e0b56c37695a45a2aaeb5e87c057fce4777c47c5d822f8cab023d49ff"


def check() -> None:
    for name in (MODEL, "tensor_contract.json", "state_quant_contract.json", "release_gates.json"):
        assert (SOURCE / name).is_file(), f"missing export asset: {name}"
        assert (DESTINATION / name).is_file(), f"missing firmware asset: {name}"
    assert hashlib.sha256((DESTINATION / MODEL).read_bytes()).hexdigest() == SHA256
    contract = json.loads((DESTINATION / "tensor_contract.json").read_text())
    assert len(contract["tflite_inputs"]) == 15
    assert len(contract["tflite_outputs"]) == 15


if __name__ == "__main__":
    check()
    print("NeuralAid assets: OK")
