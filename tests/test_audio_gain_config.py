"""CMake gain boundaries and configured production audio arithmetic."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AudioGainConfigTests(unittest.TestCase):
    def test_cmake_gain_defaults_boundaries_and_invalid_values(self):
        cmake = (ROOT/'CMakeLists.txt').read_text()
        block = cmake[cmake.index('set(M55M1_DMIC_GAIN_DB '):
                      cmake.index('if(CMAKE_BUILD_TYPE STREQUAL "Debug")')]
        definitions = cmake[cmake.index('set(M55M1_DEFS\n'):
                            cmake.index('set(M55M1_DEVICE_SOURCES')]
        settings = {'M55M1_DMIC_GAIN_DB': (-127, 36, 12),
                    'M55M1_SOFTWARE_GAIN_DB': (0, 36, 21),
                    'M55M1_DAC_GAIN_DB': (-63, 0, -9)}
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory)/'gains.cmake'
            script.write_text(block + definitions + '\nmessage(STATUS "DEFS=${M55M1_DEFS}")\n')

            def configure(name=None, value=None):
                return subprocess.run(['cmake', *([] if name is None else [f'-D{name}={value}']),
                                       '-P', str(script)], capture_output=True, text=True)

            result = configure()
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name, (minimum, maximum, default) in settings.items():
                self.assertIn(f'{name}={default}', result.stdout)
                for value in (minimum, maximum):
                    with self.subTest(name=name, valid=value):
                        accepted = configure(name, value)
                        self.assertEqual(accepted.returncode, 0, accepted.stderr)
                        self.assertIn(f'{name}={value}', accepted.stdout)
                for value in (minimum-1, maximum+1, '', '1.5', 'nan', 'OFF', '08',
                              '1;2', '1e1', '9999999999999999999999999999'):
                    with self.subTest(name=name, invalid=value):
                        rejected = configure(name, value)
                        self.assertNotEqual(rejected.returncode, 0, rejected.stdout)
                        self.assertIn(name, rejected.stderr)
                        self.assertIn(f'[{minimum}, {maximum}]', rejected.stderr)

    def test_software_gain_steps_and_streaming_numeric_oracle(self):
        # Every supported gain selects the expected coefficient; boundary/default
        # settings also exercise the production MVE branch against the double oracle.
        for vector in (False, True):
            gains = range(37) if not vector else (0, 12, 21, 36)
            for gain in gains:
                with self.subTest(gain=gain, vector=vector), tempfile.TemporaryDirectory() as directory:
                    binary = Path(directory)/'gain.exe'
                    result = subprocess.run([
                        'gcc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-ffp-contract=off', f'-DM55M1_SOFTWARE_GAIN_DB={gain}',
                        f'-DM55M1_DEEPFIR_MVE={int(vector)}',
                        *(['-D__ARM_FEATURE_MVE=2'] if vector else []),
                        '-I', str(ROOT), '-I', str(ROOT/'tests/mve_model'),
                        str(ROOT/'tests/audio_gain_harness.c'),
                        str(ROOT/'services/deepfir/deepfir_input_conditioner.c'),
                        '-lm', '-o', str(binary)], capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    run = subprocess.run([str(binary)], capture_output=True, text=True)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_board_gain_defaults_overrides_and_guards(self):
        board = (ROOT/'board/m55m1_audio.c').read_text()
        self.assertIn('.max_digital_gain_db = M55M1_AUDIO_SAFE_MAX_GAIN_DB', board)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)
            (path/'NuMicro.h').write_text('typedef struct I2C_T I2C_T;\n'
                                         'typedef struct I2S_T I2S_T;\n'
                                         'typedef struct PDMA_T PDMA_T;\n')
            for dmic, dac in ((None, None), (-127, -63), (36, 0), (-128, -9),
                              (37, -9), (12, -64), (12, 1)):
                valid = dmic is None or (-127 <= dmic <= 36 and -63 <= dac <= 0)
                with self.subTest(dmic=dmic, dac=dac):
                    source = path/'board.c'
                    source.write_text('#include "board/m55m1_audio.h"\n'
                                      f'_Static_assert(M55M1_DMIC_GAIN_DB == {12 if dmic is None else dmic}, "DMIC");\n'
                                      f'_Static_assert(M55M1_AUDIO_SAFE_MAX_GAIN_DB == {-9 if dac is None else dac}, "DAC");\n')
                    result = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                             *([] if dmic is None else [f'-DM55M1_DMIC_GAIN_DB={dmic}',
                                                                        f'-DM55M1_DAC_GAIN_DB={dac}']),
                                             '-I', str(path), '-I', str(ROOT), '-c', str(source),
                                             '-o', str(path/'board.o')], capture_output=True, text=True)
                    self.assertEqual(result.returncode == 0, valid, result.stderr)

            # Execute the unchanged BSP function against its real register layout.
            # The exposed range must encode the documented signed Q9.7 gain.
            vendor = (ROOT/'libs/StdDriver/src/dmic.c').read_text()
            start = vendor.index('void DMIC_SetDSPGainVolume(')
            function = vendor[start:vendor.index('\n/**', start)]
            source.write_text('#include <stdint.h>\n#include <assert.h>\n'
                              '#define __IO volatile\n#define __I volatile\n#define __O volatile\n'
                              '#define BIT15 (1U << 15)\n'
                              '#include "libs/Device/Nuvoton/M55M1/Include/dmic_reg.h"\n'
                              + function + '\nint main(void) {\n'
                              'for (int gain = -127; gain <= 36; ++gain) {\n'
                              'for (unsigned channel = 0; channel < 4; ++channel) {\n'
                              'DMIC_T mic = {0};\n'
                              'DMIC_SetDSPGainVolume(&mic, 1U << channel, (int16_t)gain);\n'
                              'uint32_t expected = (uint16_t)((-128 - gain) * 128);\n'
                              'expected <<= (channel % 2) * 16;\n'
                              'assert(mic.GAINCTL0 == (channel < 2 ? expected : 0));\n'
                              'assert(mic.GAINCTL1 == (channel >= 2 ? expected : 0));\n'
                              '}} return 0; }\n')
            binary = path/'dmic.exe'
            result = subprocess.run(['gcc', '-std=c11', '-O2', '-I', str(ROOT),
                                     str(source), '-o', str(binary)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
