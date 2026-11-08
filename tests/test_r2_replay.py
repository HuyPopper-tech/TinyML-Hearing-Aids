"""Replay comparator self-tests; synthetic runners never establish compiled parity."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('r2_replay', ROOT/'tools/replay_deepfir_r2.py')
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)
replay.PACKAGE = ROOT/'tests/fixtures/package_not_distributed'


class ReplayComparatorTests(unittest.TestCase):
    def test_replay_helper_rejects_modified_reference_before_execution(self):
        with tempfile.TemporaryDirectory() as directory:
            package = Path(directory)
            (package/'verify_raw_replay.py').write_text('raise RuntimeError("must not execute")')
            with patch.object(replay, 'PACKAGE', package):
                with self.assertRaisesRegex(ValueError, 'helper hash mismatch'):
                    replay.package_helpers()

    def test_mailbox_wire_offsets_feedback_and_failure_rejection(self):
        manifest = json.loads((ROOT/'models/deepfir/source_integration_manifest.json').read_text())
        helper = replay.package_helpers()
        details = {d: {t['semantic']: {'dtype': np.int16, 'quantization': (t['scale'],t['zero_point'])}
                       for t in manifest['tensor_binding'][d]} for d in ('inputs','outputs')}
        raw = np.concatenate([np.full(65, 17, np.int16), np.full(65, -9, np.int16),
                              np.full(200, 32767, np.int16), np.full(200, -32768, np.int16)])
        state = np.concatenate([helper['_r2_requantize_codes'](
            raw[130+200*i:330+200*i], details['outputs'][key+'_next'],
            details['inputs'][key]).ravel() for i,key in enumerate(('h','c'))])

        class Probe:
            # Synthetic byte memory only: verifies the host wire protocol, never model execution.
            def __init__(self):
                self.memory = bytearray(3428)
                self.memory[:4] = (0x52325250).to_bytes(4,'little')
                self.failed = False

            def read(self, address, bits, count):
                size = bits//8
                return [int.from_bytes(self.memory[address+i*size:address+(i+1)*size], 'little')
                        for i in range(count)]

            def write(self, address, bits, values):
                size = bits//8
                for i,value in enumerate(values):
                    self.memory[address+i*size:address+(i+1)*size] = (int(value)&((1<<bits)-1)).to_bytes(size,'little')
                if address == 4:
                    command = self.read(12,32,1)[0]
                    assert command in (1,2)
                    if command == 2:
                        self.memory[1568:] = np.concatenate([raw,state]).astype('<i2').tobytes()
                    self.memory[16:20] = int(self.failed).to_bytes(4,'little')
                    self.memory[8:12] = self.memory[4:8]

        probe = Probe()
        runner = replay.MailboxRunner(probe,0,helper,details['inputs'],details['outputs'])
        runner.reset()
        feature = np.arange(774,dtype=np.int16).reshape(1,2,129,3)
        zero = np.zeros((1,200),np.int16)
        result = runner(feature,zero,zero)
        self.assertEqual(probe.memory[20:1568],feature.astype('<i2').tobytes())
        np.testing.assert_array_equal(result['phase'],raw[65:130].reshape(1,65))
        np.testing.assert_array_equal(runner.state.ravel(),state)
        with self.assertRaisesRegex(ValueError,'diverged'):
            runner(feature,zero,zero)
        probe.failed = True
        with self.assertRaisesRegex(RuntimeError,'failed'):
            runner.reset()
        probe.memory[:4] = bytes(4)
        with self.assertRaisesRegex(ValueError,'not ready'):
            replay.MailboxRunner(probe,0,helper,details['inputs'],details['outputs'])
        connection = replay.OpenOcd.__new__(replay.OpenOcd)
        connection.command = lambda command: '65536'
        with self.assertRaisesRegex(ValueError,'Invalid'):
            connection.read(0,16,1)
        connection.command = lambda command: 'write failed'
        with self.assertRaisesRegex(ValueError,'write failed'):
            connection.write(0,16,[1])

    def test_package_helper_detects_each_code_mismatch_and_carries_state(self):
        manifest = json.loads((ROOT/'models/deepfir/source_integration_manifest.json').read_text())
        helper = replay.package_helpers()
        details = {d: {t['semantic']: {'dtype': np.int16, 'quantization': (t['scale'],t['zero_point'])}
                       for t in manifest['tensor_binding'][d]} for d in ('inputs','outputs')}
        raw = {'gain': np.full((1,65),17,np.int16), 'phase': np.full((1,65),-9,np.int16),
               'h_next': np.full((1,200),32767,np.int16), 'c_next': np.full((1,200),-32768,np.int16)}
        state = np.concatenate([helper['_r2_requantize_codes'](raw[k+'_next'],details['outputs'][k+'_next'],details['inputs'][k]).ravel() for k in ('h','c')])
        golden = {'feature_codes': np.zeros((1001,774),np.int16),
                  'gain_codes': np.tile(raw['gain'],(1001,1)), 'phase_codes': np.tile(raw['phase'],(1001,1)),
                  'states': np.tile(state,(1001,1))}
        count = 0
        def synthetic(**inputs):
            nonlocal count
            expected = np.zeros(400,np.int16) if count==0 else state
            np.testing.assert_array_equal(np.concatenate([inputs['h'].ravel(), inputs['c'].ravel()]),expected)
            count += 1
            return {k:v.copy() for k,v in raw.items()}
        result = helper['replay_codes'](synthetic,details['inputs'],details['outputs'],golden)
        self.assertTrue(result['bit_exact'] and result['full_trace'])
        self.assertEqual(result['hops'],1001)
        for key,index in [('gain_codes',7),('phase_codes',23),('states',301)]:
            damaged = {k:v.copy() for k,v in golden.items()}
            damaged[key][13,index] += 1
            count = 0
            result = helper['replay_codes'](synthetic,details['inputs'],details['outputs'],damaged)
            self.assertFalse(result['bit_exact'])
            self.assertEqual(sum(result['mismatching_codes'].values()),1)
            self.assertEqual(max(result['max_code_error'].values()),1)
            self.assertEqual(result['first_mismatch']['hop'],13)
            self.assertEqual(result['first_mismatch']['channel'],index)


if __name__=='__main__':
    unittest.main()
