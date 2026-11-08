"""MVE checks must inspect each linked function, not arbitrary image bytes."""
import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location(
    'verify_mve', Path(__file__).resolve().parents[1] / 'tools/verify_mve_disassembly.py')
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class MveRangeTests(unittest.TestCase):
    def test_requires_vectors_in_every_kernel_and_fma_in_fir(self):
        mapping = '\n'.join(f' {name} 0x{0x1001 + i * 32:x} Thumb Code 16 object.o'
                            for i, name in enumerate(MODULE.REQUIRED_KERNELS))
        instructions = {'deepfir_feedback_q31': 'vqrdmulh.s32 q2,q3,q4',
                        'deepfir_quantize_feature_block': 'vcvt.s32.f32 q2,q3'}
        assembly = '\n'.join(f' {0x1000 + i * 32:x}: ee37 4e42 '+
                             instructions.get(name, 'vfma.f32 q2,q3,r2')
                             for i, name in enumerate(MODULE.REQUIRED_KERNELS))
        self.assertEqual(len(MODULE.verify_kernel_ranges(mapping, assembly)), 9)
        with self.assertRaisesRegex(ValueError, 'No MVE'):
            MODULE.verify_kernel_ranges(mapping, ' 9999: vfma.f32 q2,q3,r2')
        with self.assertRaisesRegex(ValueError, 'FMA'):
            MODULE.verify_kernel_ranges(mapping, assembly.replace('vfma.f32', 'vmul.f32'))
        with self.assertRaisesRegex(ValueError, 'Q31'):
            MODULE.verify_kernel_ranges(mapping, assembly.replace('vqrdmulh.s32', 'vmul.s32'))
        with self.assertRaisesRegex(ValueError, 'conversion'):
            MODULE.verify_kernel_ranges(mapping, assembly.replace('vcvt.s32.f32', 'vmul.f32'))
