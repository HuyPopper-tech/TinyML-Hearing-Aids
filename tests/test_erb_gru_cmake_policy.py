"""Configure-level checks for ERB-GRU production and diagnostic build policy."""

from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN = ROOT / "cmake" / "armclang.cmake"


@unittest.skipUnless(shutil.which("cmake"), "cmake is required")
@unittest.skipUnless(shutil.which("ninja"), "ninja is required")
class ErbGruCMakePolicyTests(unittest.TestCase):
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
            "-DM55M1_ERB_GRU_DIAGNOSTICS=OFF",
            "-DM55M1_ERB_GRU_FAULT_INJECTION=INVOKE_FAIL",
        )
        with temporary:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "Production Release firmware rejects golden data and fault injection",
                result.stdout + result.stderr,
            )

    def test_diagnostic_release_accepts_fault_injection(self):
        temporary, _, result = self.configure(
            "-DM55M1_ERB_GRU_DIAGNOSTICS=ON",
            "-DM55M1_ERB_GRU_FAULT_INJECTION=INVOKE_FAIL",
        )
        with temporary:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_non_auto_probe_requires_diagnostics(self):
        temporary, _, result = self.configure(
            "-DM55M1_ERB_GRU_DIAGNOSTICS=OFF",
            "-DM55M1_ERB_GRU_DSP_PROBE=NO_VECTORIZE",
        )
        with temporary:
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(
                "Non-AUTO DSP probes require M55M1_ERB_GRU_DIAGNOSTICS=ON",
                result.stdout + result.stderr,
            )

    def test_clean_defaults_exclude_diagnostic_sources_and_probe_flags(self):
        temporary, build, result = self.configure()
        with temporary:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            cache = (build / "CMakeCache.txt").read_text(encoding="utf-8")
            commands = (build / "compile_commands.json").read_text(encoding="utf-8")
            self.assertIn("M55M1_ERB_GRU_DIAGNOSTICS:BOOL=OFF", cache)
            self.assertIn("M55M1_ERB_GRU_FAULT_INJECTION:STRING=OFF", cache)
            self.assertIn("M55M1_ERB_GRU_DSP_PROBE:STRING=AUTO", cache)
            self.assertNotIn("services/audio_telemetry.c", commands)
            self.assertNotIn("drivers/debug_uart_tx/debug_uart_tx.c", commands)
            enhancer_command = next(
                line
                for line in commands.splitlines()
                if '"command":' in line and "erb_gru_audio_enhancer.cpp" in line
            )
            self.assertNotIn("-fno-vectorize", enhancer_command)

    def test_probe_changes_only_the_enhancer_compile_options(self):
        for probe, expected in (
            ("NO_VECTORIZE", ("-fno-vectorize", "-fno-slp-vectorize")),
            ("O2", ("-O2",)),
        ):
            with self.subTest(probe=probe):
                temporary, build, result = self.configure(
                    "-DM55M1_ERB_GRU_DIAGNOSTICS=ON",
                    f"-DM55M1_ERB_GRU_DSP_PROBE={probe}",
                )
                with temporary:
                    self.assertEqual(
                        result.returncode, 0, result.stdout + result.stderr
                    )
                    commands = (build / "compile_commands.json").read_text(
                        encoding="utf-8"
                    )
                    enhancer_command = next(
                        line
                        for line in commands.splitlines()
                        if '"command":' in line
                        and "erb_gru_audio_enhancer.cpp" in line
                    )
                    inference_command = next(
                        line
                        for line in commands.splitlines()
                        if '"command":' in line and "erb_gru_inference.cpp" in line
                    )
                    for option in expected:
                        self.assertIn(option, enhancer_command)
                        self.assertNotIn(option, inference_command)
                    if probe == "O2":
                        self.assertGreater(
                            enhancer_command.rfind("-O2"),
                            enhancer_command.rfind("-O3"),
                        )


if __name__ == "__main__":
    unittest.main()
