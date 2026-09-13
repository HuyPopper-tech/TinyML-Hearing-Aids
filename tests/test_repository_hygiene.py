"""Regression gates for the model-neutral repository tree."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class RepositoryHygieneTests(unittest.TestCase):
    def test_generated_directories_are_ignored(self):
        text = (ROOT / ".gitignore").read_text(encoding="utf-8")
        for rule in ("/build*/", "/out/", "/tmp/", "/compile_commands.json"):
            self.assertIn(rule, text)

    def test_no_model_binary_is_checked_into_clean_tree(self):
        forbidden = {".tflite", ".npy", ".npz", ".axf", ".bin", ".map"}
        found = [
            path.relative_to(ROOT).as_posix()
            for path in (ROOT / "models").rglob("*")
            if path.is_file() and path.suffix.lower() in forbidden
        ]
        self.assertEqual([], found)

    def test_only_documented_future_model_roots_remain(self):
        directories = {
            path.name for path in (ROOT / "models").iterdir() if path.is_dir()
        }
        self.assertEqual({"deepfir", "wave_unet"}, directories)


if __name__ == "__main__":
    unittest.main()
