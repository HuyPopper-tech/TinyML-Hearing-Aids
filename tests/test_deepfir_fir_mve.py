"""Normal DeepFIR builds use MVE while host builds keep the scalar fallback."""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]


class DeepfirFirMveTests(unittest.TestCase):
    def test_host_keeps_scalar_fallback_without_mve(self):
        gcc = shutil.which("gcc")
        if gcc is None:
            self.skipTest("Host C compiler is unavailable")
        harness = r"""
            #include <stdbool.h>
            #include <stdint.h>
            #include "services/deepfir/deepfir_dsp.h"

            int main(void) {
                deepfir_dsp_state_t state;
                float samples[DEEPFIR_DSP_HOP] = {0};
                float taps[DEEPFIR_DSP_TAPS] = {0};
                float output[DEEPFIR_DSP_HOP] = {0};
                samples[0] = 0.25f;
                taps[0] = 1.0f;
                deepfir_dsp_reset(&state, taps);
                if (deepfir_dsp_prepare_mve()) return 1;
                if (deepfir_dsp_process(&state, samples, DEEPFIR_DSP_HOP,
                                        taps, output) != 0) return 2;
                return output[0] == 0.25f && state.sample_count == DEEPFIR_DSP_HOP
                       ? 0 : 3;
            }
        """
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "fir_fallback.c"
            executable = Path(directory) / "fir_fallback.exe"
            source.write_text(harness, encoding="utf-8")
            compile_result = subprocess.run(
                [
                    gcc, "-std=c11", "-O2", "-DM55M1_DEEPFIR_MVE=1",
                    "-I", str(ROOT), str(source),
                    str(ROOT / "services/deepfir/deepfir_dsp.c"),
                    "-lm", "-o", str(executable),
                ],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(compile_result.returncode, 0, compile_result.stderr)
            run = subprocess.run([str(executable)], capture_output=True)
            self.assertEqual(run.returncode, 0, run.stderr)

    def test_release_fir_uses_mve_vector_fma(self):
        armclang = shutil.which("armclang")
        if armclang is None:
            self.skipTest("Arm Compiler is unavailable")
        with tempfile.TemporaryDirectory() as directory:
            assembly = Path(directory) / "deepfir_dsp.s"
            result = subprocess.run(
                [
                    armclang, "--target=arm-arm-none-eabi", "-mthumb",
                    "-mcpu=cortex-m55", "-mfloat-abi=hard", "-O3", "-DNDEBUG",
                    "-DM55M1_DEEPFIR_MVE=1", "-DARM_MATH_MVEF=1",
                    "-I", str(ROOT / "libs/CMSIS/DSP/Include"),
                    "-I", str(ROOT / "libs/CMSIS/DSP/PrivateInclude"),
                    "-I", str(ROOT / "libs/CMSIS/Core/Include"),
                    "-I", str(ROOT / "libs/Device/Nuvoton/M55M1/Include"),
                    "-std=c11", "-S", "-o", str(assembly),
                    str(ROOT / "services/deepfir/deepfir_dsp.c"),
                ],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            source = assembly.read_text(encoding="utf-8")
            match = re.search(
                r"(?ms)^deepfir_dsp_process_mve:.*?(?=^\s*\.size\s+deepfir_dsp_process_mve\b)",
                source,
            )
            self.assertIsNotNone(match, "Release has no MVE FIR kernel")
            self.assertRegex(match.group(), r"(?i)\bvfma\.f32\s+q\d")
            tap_loops = [loop for loop in re.findall(
                r"(?ms)^\.LBB\d+_\d+:[^\n]*Inner Loop Header(?:(?!^\.LBB).)*?^\s+le\s+[^\n]+",
                match.group()) if 'vfma.f32' in loop]
            self.assertTrue(tap_loops, 'No low-overhead FIR tap loop')
            for loop in tap_loops:
                # Two output groups share each pair of scalar tap loads.
                self.assertEqual(len(re.findall(r'\bvfma\.f32\b', loop)), 4, loop)
                self.assertEqual(len(re.findall(r'(?m)^\s+ldr(?:\.w)?\s', loop)), 2, loop)
                self.assertNotRegex(loop, r'\[sp\b|Spill|Reload')


if __name__ == "__main__":
    unittest.main()
