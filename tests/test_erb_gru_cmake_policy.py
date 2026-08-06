"""Configure-level checks for DeepFilterNet production and diagnostic policy."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = ROOT / "cmake" / "armclang.cmake"


@unittest.skipUnless(shutil.which("cmake"), "cmake is required")
@unittest.skipUnless(shutil.which("ninja"), "ninja is required")
class DeepFilterNetCMakePolicyTests(unittest.TestCase):
    def configure(self, *definitions: str):
        temporary = tempfile.TemporaryDirectory()
        build = Path(temporary.name) / "build"
        command = [
            "cmake",
            "-S",
            str(ROOT),
            "-B",
            str(build),
            "-G",
            "Ninja",
            f"-DCMAKE_TOOLCHAIN_FILE={TOOLCHAIN}",
            "-DCMAKE_BUILD_TYPE=Release",
            *definitions,
        ]
        result = subprocess.run(
            command, capture_output=True, text=True, check=False
        )
        return temporary, build, result

    def test_production_release_rejects_fault_injection(self):
        temporary, _, result = self.configure(
            "-DM55M1_DEEPFILTERNET_DIAGNOSTICS=OFF",
            "-DM55M1_DEEPFILTERNET_FAULT_INJECTION=INVOKE_FAIL",
        )
        with temporary:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "Production Release firmware rejects golden data and fault injection",
                result.stdout + result.stderr,
            )

    def test_diagnostic_release_accepts_fault_injection(self):
        temporary, _, result = self.configure(
            "-DM55M1_DEEPFILTERNET_DIAGNOSTICS=ON",
            "-DM55M1_DEEPFILTERNET_FAULT_INJECTION=INVOKE_FAIL",
        )
        with temporary:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_clean_defaults_exclude_diagnostic_sources_and_probe_controls(self):
        temporary, build, result = self.configure()
        with temporary:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
            commands = (build / "compile_commands.json").read_text(encoding="utf-8")
            self.assertIn("M55M1_DEEPFILTERNET_DIAGNOSTICS:BOOL=OFF", cache)
            self.assertIn("M55M1_DEEPFILTERNET_FAULT_INJECTION:STRING=OFF", cache)
            self.assertIn("M55M1_DEEPFILTERNET_DSP_MODE:STRING=MVE", cache)
            self.assertNotIn("services/audio_telemetry.c", commands)
            self.assertNotIn("drivers/debug_uart_tx/debug_uart_tx.c", commands)
            enhancer_command = next(
                line
                for line in commands.splitlines()
                if '"command":' in line and "deepfilternet_audio_enhancer.cpp" in line
            )
            self.assertNotIn("-fno-vectorize", enhancer_command)

if __name__ == "__main__":
    unittest.main()
