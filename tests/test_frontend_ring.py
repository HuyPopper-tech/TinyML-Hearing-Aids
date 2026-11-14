"""The optimized history must produce the exact shift-oracle FFT input."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class FrontendRingTests(unittest.TestCase):
    def test_exact_causal_frame_across_wrap_and_reset(self):
        for vector in (False, True):
            with self.subTest(vector=vector), tempfile.TemporaryDirectory() as directory:
                executable = Path(directory)/'frames.exe'
                command = ['gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                           '-ffp-contract=off', f'-DM55M1_DEEPFIR_MVE={int(vector)}',
                           *(['-D__ARM_FEATURE_MVE=2'] if vector else []),
                           '-I', str(ROOT), '-I', str(ROOT/'tests/mve_model'),
                           str(ROOT/'tests/frontend_ring_harness.c'),
                           str(ROOT/'services/deepfir/deepfir_features.c'),
                           '-lm', '-o', str(executable)]
                built = subprocess.run(command, capture_output=True, text=True)
                self.assertEqual(built.returncode, 0, built.stderr)
                run = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
                print(f'vector={vector}: {run.stdout.strip()}')
