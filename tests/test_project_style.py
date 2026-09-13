"""Focused style gates for project-owned C and C++ sources."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = ("app", "board", "common", "drivers", "services")
ARCHIVED_FAMILIES = (
    "deepfir",
    "speech_boosting",
    "neuralaid",
    "deepfilternet",
    "erb_gru",
)


def project_files():
    for root_name in SOURCE_ROOTS:
        root = ROOT / root_name
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if path.is_file() and path.suffix.lower() in {".c", ".h", ".cc", ".cpp"}:
                if "generated" not in path.parts:
                    yield path


class ProjectStyleTests(unittest.TestCase):
    def test_project_sources_have_no_tabs_or_trailing_whitespace(self):
        violations = []
        for path in project_files():
            for line_number, line in enumerate(
                path.read_text(encoding="utf-8").splitlines(), start=1
            ):
                if "\t" in line or line.rstrip() != line:
                    violations.append(f"{path.relative_to(ROOT)}:{line_number}")
        self.assertEqual([], violations)

    def test_project_sources_do_not_use_esp_idf_identifiers(self):
        violations = []
        for path in project_files():
            text = path.read_text(encoding="utf-8")
            if any(token in text for token in ("ESP_OK", "ESP_LOGE", "idf_component_register")):
                violations.append(str(path.relative_to(ROOT)))
        self.assertEqual([], violations)

    def test_project_headers_have_c_compatibility_guards(self):
        violations = []
        for path in project_files():
            if path.suffix.lower() != ".h":
                continue
            text = path.read_text(encoding="utf-8")
            if "#pragma once" not in text or "extern \"C\"" not in text:
                violations.append(str(path.relative_to(ROOT)))
        self.assertEqual([], violations)

    def test_project_sources_do_not_include_archived_model_families(self):
        violations = []
        for path in project_files():
            text = path.read_text(encoding="utf-8").lower()
            if any(family in text for family in ARCHIVED_FAMILIES):
                violations.append(str(path.relative_to(ROOT)))
        self.assertEqual([], violations)


if __name__ == "__main__":
    unittest.main()
