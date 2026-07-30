"""Style contract for project-owned C/C++ lines introduced on this branch."""

from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROJECT_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}
EXCLUDED_PREFIXES = ("libs/", "models/erb_gru/generated/", "models/erb_gru/packages/")
PUBLIC_HEADER_ROOTS = ("app", "board", "common", "drivers", "services")


def branch_changed_source_hunks():
    """Return actual source and touched new-file line ranges for branch C/C++ hunks."""
    paths = subprocess.run(
        ["git", "diff", "--name-only", "main"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
    ).stdout.splitlines()
    for relative_path in paths:
        path = Path(relative_path)
        if path.suffix not in PROJECT_SOURCE_SUFFIXES:
            continue
        if relative_path.startswith(EXCLUDED_PREFIXES):
            continue
        if not (ROOT / path).is_file():
            continue
        diff = subprocess.run(
            ["git", "diff", "--unified=1", "main", "--", relative_path],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        ranges = []
        for header in re.finditer(r"^@@ -\d+(?:,\d+)? \+(\d+)(?:,(\d+))? @@", diff, re.MULTILINE):
            start = int(header.group(1))
            count = int(header.group(2) or "1")
            ranges.append(range(start, start + max(count, 1)))
        yield relative_path, (ROOT / path).read_text(encoding="utf-8").splitlines(), ranges


def project_public_headers():
    """Yield project-owned public headers in deterministic path order."""
    for root_name in PUBLIC_HEADER_ROOTS:
        yield from sorted((ROOT / root_name).rglob("*.h"))


class ErbGruStyleContractTests(unittest.TestCase):
    def test_public_doxygen_uses_multiline_blocks_with_one_tag_per_line(self):
        """Keep generated API documentation readable and diff-friendly."""
        violations = []
        tag_pattern = re.compile(r"@\w+")

        for path in project_public_headers():
            relative_path = path.relative_to(ROOT).as_posix()
            source = path.read_text(encoding="utf-8")
            for line_number, line in enumerate(source.splitlines(), start=1):
                if "/**" in line and "*/" in line:
                    violations.append(f"{relative_path}:{line_number}: single-line Doxygen")
                if len(tag_pattern.findall(line)) > 1:
                    violations.append(f"{relative_path}:{line_number}: multiple Doxygen tags")

        self.assertEqual(violations, [], "Doxygen formatting violations:\n" + "\n".join(violations))

    def test_branch_added_functions_and_control_blocks_use_same_line_braces(self):
        """Catch added Allman braces without inspecting comments, macros, or initializers."""
        violations = []
        control = re.compile(r"^\s*(?:if|for|while)\s*\(.*\)\s*$|^\s*(?:do|else)\s*$|^\s*else\s+if\s*\(.*\)\s*$")
        function_end = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_\s*]*\)\s*$")

        for relative_path, source_lines, ranges in branch_changed_source_hunks():
            touched = {line_number for changed_range in ranges for line_number in changed_range}
            for line_number, line in enumerate(source_lines[:-1], start=1):
                if line_number not in touched:
                    continue
                if line.lstrip().startswith(("//", "/*", "*", "#")):
                    continue
                if not (control.match(line) or function_end.match(line)):
                    continue
                next_line = source_lines[line_number]
                if next_line.strip() == "{":
                    violations.append(f"{relative_path}: {line.strip()}")

        self.assertEqual(violations, [], "Allman-style added code:\n" + "\n".join(violations))


if __name__ == "__main__":
    unittest.main()
