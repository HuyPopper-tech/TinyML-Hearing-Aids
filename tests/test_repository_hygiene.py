"""Regression gates for the model-neutral repository tree."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RepositoryHygieneTests(unittest.TestCase):
    def test_generated_directories_are_ignored(self):
        text = (ROOT / ".gitignore").read_text(encoding="utf-8")
        for rule in ("/build*/", "/out/", "/tmp/", "/compile_commands.json"):
            self.assertIn(rule, text)
        for package in ("deepfir_r2_scratch_ptq_20261107_151431_366715",
                        "deepfir_ptq_20260921_033946",
                        "firmware_complex3_conv5_906b1d163f534778ba737eed737cab41"):
            self.assertIn(f"/{package}/", text)

    def test_no_model_binary_is_checked_into_clean_tree(self):
        forbidden = {".tflite", ".npy", ".npz", ".axf", ".bin", ".map"}
        found = [
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "models").rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden
        ]
        self.assertEqual([], found)

    def test_immutable_imports_preserve_bytes_on_windows_checkout(self):
        attributes = (ROOT / ".gitattributes").read_text(encoding="utf-8")
        self.assertIn("models/deepfir/** -text", attributes)
        self.assertIn("tests/fixtures/r2/verify_raw_replay.py -text", attributes)

    def test_only_the_active_model_root_remains(self):
        directories = {
            path.name for path in (ROOT / "models").iterdir() if path.is_dir()
        }
        self.assertEqual({"deepfir"}, directories)


if __name__ == "__main__":
    unittest.main()
