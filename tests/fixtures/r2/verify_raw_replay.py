"""Independent raw integer replay; generated with the same Q31 definitions."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import sys

os.environ['CUDA_VISIBLE_DEVICES'] = '-1'
import numpy as np
import tensorflow as tf

def _r2_qparams(detail):
    scale, zero = detail['quantization']
    if not np.isfinite(scale) or scale <= 0 or detail['dtype'] != np.int16:
        raise ValueError('R2 strict16x8 ABI requires static int16 ports')
    return (float(scale), int(zero))

def _r2_quantize(value, detail):
    scale, zero = _r2_qparams(detail)
    scaled = np.asarray(value, dtype=np.float64) / scale
    rounded = np.sign(scaled) * np.floor(np.abs(scaled) + 0.5)
    return np.clip(rounded + zero, -32768, 32767).astype(np.int16)

def _r2_requantize_codes(codes, source_detail, target_detail):
    source_scale, source_zero = _r2_qparams(source_detail)
    target_scale, target_zero = _r2_qparams(target_detail)
    if (source_scale, source_zero) == (target_scale, target_zero):
        return codes.astype(np.int16, copy=True)
    mantissa, exponent = math.frexp(source_scale / target_scale)
    multiplier = int(math.floor(mantissa * (1 << 31) + 0.5))
    shift = 31 - exponent
    if multiplier == 1 << 31:
        multiplier >>= 1
        shift -= 1
    if not 0 <= shift <= 46 or not 0 < multiplier <= 1 << 31:
        raise ValueError('R2 state qparams exceed the firmware Q31 int64 range')
    values = codes.astype(np.int64) - source_zero
    numerator = values * multiplier
    if shift > 0:
        divisor = 1 << shift
        if np.any(np.abs(numerator) > np.iinfo(np.int64).max):
            raise OverflowError('R2 Q31 affine numerator exceeds int64')
        rounded = np.sign(numerator) * ((np.abs(numerator) + divisor // 2) // divisor)
        rounded += target_zero
    elif shift == 0:
        rounded = numerator + target_zero
    else:
        rounded = (numerator << -shift) + target_zero
    return np.clip(rounded, -32768, 32767).astype(np.int16)

def replay_codes(runner, inputs: dict, outputs: dict, golden: dict,
                 max_hops: int | None = None) -> dict:
    widths = dict(feature_codes=774, gain_codes=65, phase_codes=65, states=400)
    arrays = {key: np.asarray(golden[key]) for key in widths}
    n = len(arrays['feature_codes'])
    for key, width in widths.items():
        if arrays[key].dtype != np.int16 or arrays[key].shape != (n, width):
            raise ValueError(f'Invalid golden shape/dtype: {key}')
    hops = n if max_hops is None else min(n, max_hops)
    if hops < 1000:
        raise ValueError('Independent replay requires at least 1,000 hops per utterance')
    state = {key: _r2_quantize(np.zeros((1, 200)), inputs[key]) for key in ('h', 'c')}
    maxima = dict(gain=0, phase=0, state=0)
    counts = dict(maxima)
    first = None
    for hop in range(hops):
        result = runner(feature_context=arrays['feature_codes'][hop].reshape(1, 2, 129, 3),
                        **state)
        for key in ('gain', 'phase', 'h_next', 'c_next'):
            width = 65 if key in ('gain', 'phase') else 200
            if result[key].dtype != np.int16 or result[key].shape != (1, width):
                raise ValueError(f'Invalid runtime output: {key}')
        state = {key: _r2_requantize_codes(result[key + '_next'], outputs[key + '_next'],
                                          inputs[key]) for key in ('h', 'c')}
        actual = dict(gain=result['gain'].reshape(-1), phase=result['phase'].reshape(-1),
                      state=np.concatenate([state[key].reshape(-1) for key in ('h', 'c')]))
        for key, expected_key in (('gain', 'gain_codes'), ('phase', 'phase_codes'), ('state', 'states')):
            expected = arrays[expected_key][hop]
            errors = np.abs(actual[key].astype(np.int32) - expected.astype(np.int32))
            counts[key] += int(np.count_nonzero(errors))
            maxima[key] = max(maxima[key], int(np.max(errors)))
            if first is None and np.any(errors):
                channel = int(np.flatnonzero(errors)[0])
                first = dict(hop=hop, tensor=key, channel=channel,
                             expected=int(expected[channel]), actual=int(actual[key][channel]))
    return dict(bit_exact=first is None, hops=hops, full_trace=hops == n,
                max_code_error=maxima, mismatching_codes=counts, first_mismatch=first)



def verify_raw(raw_path, golden_dir):
    if tf.__version__ != '2.20.0':
        raise ValueError('Independent replay requires TensorFlow 2.20.0')
    raw_path, golden_dir = Path(raw_path).resolve(), Path(golden_dir).resolve()
    paths = sorted(golden_dir.glob('*.npz'))
    if not paths:
        raise ValueError('No full integer golden traces')
    rows = []
    shapes = dict(feature_context=(1, 2, 129, 3), h=(1, 200), c=(1, 200),
                  gain=(1, 65), phase=(1, 65), h_next=(1, 200), c_next=(1, 200))
    for path in paths:
        interpreter = tf.lite.Interpreter(model_content=raw_path.read_bytes(), num_threads=1,
            experimental_op_resolver_type=tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES)
        signatures = interpreter.get_signature_list()
        matches = [key for key, value in signatures.items() if set(value['inputs']) == {'feature_context', 'h', 'c'}
                   and set(value['outputs']) == {'gain', 'phase', 'h_next', 'c_next'}]
        if len(matches) != 1:
            raise ValueError('Missing or ambiguous raw model signature')
        runner = interpreter.get_signature_runner(matches[0])
        inputs, outputs = runner.get_input_details(), runner.get_output_details()
        for key, detail in {**inputs, **outputs}.items():
            scale, zero = detail['quantization']
            if tuple(detail['shape']) != shapes[key] or detail['dtype'] != np.int16 or scale <= 0 or zero != 0:
                raise ValueError(f'Invalid integer port: {key}')
        with np.load(path, allow_pickle=False) as saved:
            golden = {key: saved[key] for key in ('feature_codes', 'gain_codes', 'phase_codes', 'states')}
        result = replay_codes(runner, inputs, outputs, golden)
        rows.append(dict(file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(), **result))
        del runner, interpreter
    passed = all(row['bit_exact'] and row['full_trace'] for row in rows)
    library = Path(__import__('tensorflow.lite.python.interpreter_wrapper._pywrap_tensorflow_interpreter_wrapper',
                             fromlist=['__file__']).__file__)
    return dict(schema='deepfir.native-raw-replay-verification.v1',
        status='PASS_NATIVE_RAW_BITEXACT' if passed else 'FAIL_NATIVE_RAW_BITEXACT',
        release_eligible=passed and platform.system() == 'Linux',
        raw_sha256=hashlib.sha256(raw_path.read_bytes()).hexdigest(),
        runtime=dict(tensorflow=tf.__version__, git_version=tf.version.GIT_VERSION,
            platform=platform.platform(), python=sys.version, machine=platform.machine(),
            resolver='BUILTIN_WITHOUT_DEFAULT_DELEGATES', interpreter_threads=1,
            native_library_sha256=hashlib.sha256(library.read_bytes()).hexdigest()),
        replay=rows, total_hops=sum(row['hops'] for row in rows), full_trace=True,
        compiled_execution='NOT_RUN', deployment_approved=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--raw', type=Path, required=True)
    parser.add_argument('--goldens', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise FileExistsError('Choose a fresh verification receipt path')
    report = verify_raw(args.raw, args.goldens)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
    print(json.dumps(report, indent=2))
    return 0 if report['status'] == 'PASS_NATIVE_RAW_BITEXACT' else 1


if __name__ == '__main__':
    raise SystemExit(main())
