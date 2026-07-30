"""Style contract for project-owned C/C++ lines introduced on this branch."""

from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROJECT_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp"}
EXCLUDED_PREFIXES = ("libs/", "models/erb_gru/generated/", "models/erb_gru/packages/")


def branch_changed_source_lines():
    """Return branch-added project source lines, excluding generated/vendor artifacts."""
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
            ["git", "diff", "--unified=0", "main", "--", relative_path],
            cwd=ROOT,
            capture_output=True,
            text=True,
            check=True,
        ).stdout
        yield relative_path, [line[1:] for line in diff.splitlines() if line.startswith("+") and not line.startswith("+++")]


class ErbGruStyleContractTests(unittest.TestCase):
    def test_branch_added_functions_and_control_blocks_use_same_line_braces(self):
        """Catch added Allman braces without inspecting comments, macros, or initializers."""
        violations = []
        control = re.compile(r"^\s*(?:if|for|while)\s*\(.*\)\s*$|^\s*do\s*$")
        function_end = re.compile(r"^\s*[A-Za-z_][A-Za-z0-9_\s*]*\)\s*$")

        for relative_path, added_lines in branch_changed_source_lines():
            for index, line in enumerate(added_lines[:-1]):
                if line.lstrip().startswith(("//", "/*", "*", "#")):
                    continue
                if not (control.match(line) or function_end.match(line)):
                    continue
                next_line = added_lines[index + 1]
                if next_line.strip() == "{":
                    violations.append(f"{relative_path}: {line.strip()}")

        self.assertEqual(violations, [], "Allman-style added code:\n" + "\n".join(violations))


if __name__ == "__main__":
    unittest.main()
