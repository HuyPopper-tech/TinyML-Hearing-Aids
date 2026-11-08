"""R2 compiled-mailbox replay/comparator; author: Bui Minh Huy; date: 2026-11-08.

No probe is opened without --openocd. This tool never flashes, resets or resumes
a target. A separately authorized replay firmware must already be running.
"""
import argparse
import ast
from datetime import datetime, timezone
import hashlib
import json
import math
from pathlib import Path
import socket
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT/'deepfir_r2_scratch_ptq_20261107_151431_366715'/'firmware_deepfir_r2_C_34c5bd074ae6_69c1c3d2'
COMPILED_SHA = '69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27'
TRACES = {'176f4e875a6dd550': 11324, 'c07ec5e74f1bd3dc': 15117}


def package_helpers():
    """Execute only the four portable functions from the immutable verifier.

    Its TensorFlow import is intentionally excluded: compiled replay uses the
    target's existing TFLM/Ethos-U interpreter, never a host raw-model invoke.
    """
    reference = PACKAGE/'verify_raw_replay.py'
    if not reference.is_file():
        reference = ROOT/'tests/fixtures/r2/verify_raw_replay.py'
    manifest = json.loads((ROOT/'models/deepfir/source_integration_manifest.json').read_text())
    if hashlib.sha256(reference.read_bytes()).hexdigest() != manifest['artifacts']['verify_raw_replay.py']:
        raise ValueError('Replay reference helper hash mismatch')
    tree = ast.parse(reference.read_text())
    names = {'_r2_qparams', '_r2_quantize', '_r2_requantize_codes', 'replay_codes'}
    selected = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name in names]
    if len(selected) != len(names):
        raise ValueError('Package verifier API changed')
    scope = {'np': np, 'math': math}
    exec(compile(ast.Module(body=selected, type_ignores=[]), str(reference), 'exec'), scope)
    return scope


def validate_package():
    manifest = json.loads((PACKAGE/'firmware_manifest.json').read_text())
    for name, digest in manifest['artifacts'].items():
        if hashlib.sha256((PACKAGE/name).read_bytes()).hexdigest() != digest:
            raise ValueError('Source artifact hash mismatch: '+name)
    for name, count in TRACES.items():
        with np.load(PACKAGE/'integer_replay'/(name+'.npz'), allow_pickle=False) as golden:
            widths = {'feature_codes': 774, 'gain_codes': 65, 'phase_codes': 65, 'states': 400}
            if set(golden.files) != set(widths):
                raise ValueError('Trace schema mismatch: '+name)
            for key, width in widths.items():
                if golden[key].dtype != np.int16 or golden[key].shape != (count, width):
                    raise ValueError('Trace shape/dtype mismatch: '+name+'/'+key)
    if hashlib.sha256((PACKAGE/'controller_16x8_vela.tflite').read_bytes()).hexdigest() != COMPILED_SHA:
        raise ValueError('Compiled identity mismatch')
    return manifest


class OpenOcd:
    def __init__(self, endpoint):
        host, port = endpoint.rsplit(':', 1)
        self.socket = socket.create_connection((host, int(port)), timeout=15)

    def command(self, command):
        self.socket.sendall(command.encode('ascii')+b'\x1a')
        response = bytearray()
        while not response.endswith(b'\x1a'):
            chunk = self.socket.recv(65536)
            if not chunk:
                raise ConnectionError('OpenOCD disconnected')
            response.extend(chunk)
            if len(response) > 4*1024*1024:
                raise ValueError('Oversized OpenOCD response')
        return response[:-1].decode('ascii').strip()

    def read(self, address, bits, count):
        result = self.command(f'read_memory {address:#x} {bits} {count}')
        values = [int(v, 0) for v in result.split()]
        if len(values) != count or any(v < 0 or v >= 1 << bits for v in values):
            raise ValueError('Invalid OpenOCD memory response')
        return values

    def write(self, address, bits, values):
        values = ' '.join(str(int(v) & ((1 << bits)-1)) for v in values)
        result = self.command(f'write_memory {address:#x} {bits} {{{values}}}')
        if result:
            raise ValueError('OpenOCD write failed: '+result)

    def close(self):
        self.socket.close()


class MailboxRunner:
    def __init__(self, probe, address, helper, inputs, outputs):
        self.probe, self.address = probe, address
        self.helper, self.inputs, self.outputs = helper, inputs, outputs
        self.request = probe.read(address+4, 32, 1)[0]
        if probe.read(address, 32, 1)[0] != 0x52325250:
            raise ValueError('Replay mailbox is not ready; no audio firmware may be used')
        self.records = []

    def transact(self, command):
        self.request = (self.request+1) & 0xffffffff
        self.probe.write(self.address+12, 32, [command])
        self.probe.write(self.address+4, 32, [self.request])
        end = time.monotonic()+5
        while time.monotonic() < end:
            if self.probe.read(self.address+8, 32, 1)[0] == self.request:
                status = self.probe.read(self.address+16, 32, 1)[0]
                if status:
                    raise RuntimeError(f'Firmware invoke/reset failed: {status:#x}')
                return
            time.sleep(0.001)
        raise TimeoutError('Firmware replay response timeout; context must be reset')

    def reset(self):
        self.transact(1)
        self.state = np.zeros((2, 200), dtype=np.int16)
        self.records = []

    def __call__(self, feature_context, h, c):
        if not np.array_equal(h.reshape(-1), self.state[0]) or not np.array_equal(c.reshape(-1), self.state[1]):
            raise ValueError('Host and firmware feedback state diverged')
        self.probe.write(self.address+20, 16, feature_context.reshape(-1))
        self.transact(2)
        codes = np.asarray(self.probe.read(self.address+20+1548, 16, 930), dtype=np.uint16).view(np.int16)
        result = {'gain': codes[:65].reshape(1,65), 'phase': codes[65:130].reshape(1,65),
                  'h_next': codes[130:330].reshape(1,200), 'c_next': codes[330:530].reshape(1,200)}
        self.state = codes[530:].reshape(2,200).copy()
        expected = np.concatenate([self.helper['_r2_requantize_codes'](
            result[key+'_next'], self.outputs[key+'_next'], self.inputs[key]).reshape(-1) for key in ('h','c')])
        if not np.array_equal(self.state.reshape(-1), expected):
            raise ValueError('Firmware feedback differs from package Q31 helper')
        self.records.append(codes.copy())
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True, help='New evidence directory; never overwritten')
    parser.add_argument('--openocd', help='Already running OpenOCD Tcl endpoint, e.g. 127.0.0.1:6666')
    parser.add_argument('--mailbox', type=lambda s: int(s,0))
    parser.add_argument('--model-address', type=lambda s: int(s,0))
    parser.add_argument('--firmware', type=Path, help='Exact already-programmed ELF for runtime identity')
    args = parser.parse_args()
    manifest = validate_package()
    args.output.mkdir(parents=True, exist_ok=False)
    receipt = {'created_utc': datetime.now(timezone.utc).isoformat(), 'status': 'BLOCKED',
               'compiled_sha256': COMPILED_SHA, 'source_manifest_sha256': hashlib.sha256((PACKAGE/'firmware_manifest.json').read_bytes()).hexdigest(),
               'trace_hops': TRACES, 'deployment_approved': False,
               'runtime_identity': None, 'traces': {}, 'blocker': 'No running, authorized M55M1 compiled replay backend supplied'}
    probe = None
    try:
        if args.openocd:
            if args.mailbox is None or args.model_address is None or args.firmware is None:
                raise ValueError('--openocd requires --mailbox, --model-address and --firmware')
            probe = OpenOcd(args.openocd)
            actual_model = bytearray()
            for offset in range(0,516848,4096):
                size = min(4096,516848-offset)
                actual_model.extend(np.array(probe.read(args.model_address+offset,32,size//4),dtype='<u4').tobytes())
            if hashlib.sha256(actual_model).hexdigest() != COMPILED_SHA:
                raise ValueError('Target SRAM compiled model identity mismatch')
            receipt['runtime_identity'] = {'backend': 'M55M1 TFLM/Ethos-U55', 'openocd': probe.command('version'),
                'elf_path': str(args.firmware.resolve()), 'elf_sha256': hashlib.sha256(args.firmware.read_bytes()).hexdigest(),
                'target_sram_model_sha256': COMPILED_SHA, 'mailbox_address': hex(args.mailbox)}
            helper = package_helpers()
            details = {direction: {t['semantic']: {'dtype': np.int16, 'quantization': (t['scale'],t['zero_point'])}
                                  for t in manifest['tensor_binding'][direction]} for direction in ('inputs','outputs')}
            runner = MailboxRunner(probe,args.mailbox,helper,details['inputs'],details['outputs'])
            for name in TRACES:
                runner.reset()
                with np.load(PACKAGE/'integer_replay'/(name+'.npz'), allow_pickle=False) as golden:
                    receipt['traces'][name] = helper['replay_codes'](runner,details['inputs'],details['outputs'],golden)
                np.savez_compressed(args.output/(name+'_compiled.npz'),codes=np.stack(runner.records))
            receipt['status'] = 'PASS' if all(r['bit_exact'] and r['full_trace'] for r in receipt['traces'].values()) else 'FAIL'
            receipt['blocker'] = None
    except Exception as error:
        receipt['status'] = 'FAIL'
        receipt['error'] = str(error)
    finally:
        if probe:
            probe.close()
        (args.output/'compiled_replay_receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
    print(json.dumps(receipt,indent=2))
    return 0 if receipt['status']=='PASS' else 2


if __name__=='__main__':
    raise SystemExit(main())
