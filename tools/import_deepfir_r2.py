"""Verify the immutable R2 export, archive the active package, then import runtime artifacts."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'deepfir_r2_scratch_ptq_20261107_151431_366715' / 'firmware_deepfir_r2_C_34c5bd074ae6_69c1c3d2'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def import_package(evidence):
    manifest = json.loads((SOURCE / 'firmware_manifest.json').read_text())
    for name, expected in manifest['artifacts'].items():
        path = (SOURCE / name).resolve()
        if not path.is_relative_to(SOURCE.resolve()) or digest(path) != expected:
            raise ValueError('Source artifact mismatch: ' + name)
    raw, compiled = SOURCE / 'controller_16x8.tflite', SOURCE / 'controller_16x8_vela.tflite'
    if digest(raw) != '9d11584db9ce1223eb26be1ab2b5f1493ff0c3c92ba3e2757a6063bd279d38ac' or raw.stat().st_size != 555600:
        raise ValueError('Unexpected raw model')
    if digest(compiled) != '69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27' or compiled.stat().st_size != 516848:
        raise ValueError('Unexpected compiled model')
    if manifest['candidate_abi'] != 'deepfir_ablation_r2_ac_v1_gain_phase_split_range_hc_v2':
        raise ValueError('Unexpected candidate ABI')
    model_source = (SOURCE / 'deepfir_model_data.cc').read_text()
    payload = bytes(int(x, 16) for x in re.findall(r'0x([0-9a-fA-F]{2})\b', model_source))
    if payload != compiled.read_bytes():
        raise ValueError('C array differs from compiled model')
    destination = ROOT / 'models/deepfir'
    archive = evidence / 'previous_active_package'
    shutil.copytree(destination, archive)  # Refuses to overwrite earlier evidence.
    # Keep the destination build recipe and README; old provenance lives in the archive.
    for path in destination.iterdir():
        if path.is_file() and path.name not in ('m55m1_model_package.cmake', 'README.md'):
            path.unlink()
    copied = ['deepfir_model_data.h', 'deepfir_contract.h', 'deepfir_r2_contract.h',
              'deepfir_r2_state_coordinates.h', 'state_coordinates.json', 'dsp_contract.json',
              'DSP_CONTRACT.md', 'ptq_quality.json', 'ptq_selection_lock.json',
              'integer_dtype_audit.json', 'vela_manifest.json', 'vela_patch_manifest.json',
              'native_raw_replay_receipt.json']
    for name in copied:
        shutil.copyfile(SOURCE / name, destination / name)
    # Cache-line alignment affects placement, never FlatBuffer bytes.
    (destination / 'deepfir_model_data.cc').write_text(model_source.replace('alignas(16)', 'alignas(32)'), encoding='utf-8')
    shutil.copyfile(SOURCE / 'firmware_manifest.json', destination / 'source_integration_manifest.json')
    (destination / 'compiled_tensor_binding.json').write_text(json.dumps(manifest['tensor_binding'], indent=2)+'\n')
    ins = sorted(manifest['tensor_binding']['inputs'], key=lambda t: ['feature_context', 'h', 'c'].index(t['semantic']))
    outs = sorted(manifest['tensor_binding']['outputs'], key=lambda t: ['gain', 'phase', 'h_next', 'c_next'].index(t['semantic']))
    header = ['/* R2 immutable binding; author: Bui Minh Huy; date: 2026-11-08. */', '#pragma once',
              '#include "deepfir_r2_contract.h"', '#ifdef __cplusplus', 'extern "C" {', '#endif',
              f'static const char deepfir_compiled_sha256[] = "{digest(compiled)}";']
    for direction, tensors in [('input', ins), ('output', outs)]:
        header += [f'static const float deepfir_{direction}_scale[{len(tensors)}] = {{'+','.join(str(t['scale'])+'f' for t in tensors)+'};',
                   f'static const int32_t deepfir_{direction}_zero[{len(tensors)}] = {{'+','.join(str(t['zero_point']) for t in tensors)+'};']
    for key in ['INPUT_PORT', 'OUTPUT_PORT', 'INPUT_ZERO', 'OUTPUT_ZERO', 'FEEDBACK_MULTIPLIER', 'FEEDBACK_SHIFT', 'DIRECT_COPY']:
        header.append(f'#define DF_{key} DF_R2_{key}')
    header += ['#ifdef __cplusplus', '}', '#endif', '']
    (destination / 'deepfir_model_qparams.h').write_text('\n'.join(header))
    receipt = dict(schema='m55m1.r2.integration.v1', source_package=SOURCE.relative_to(ROOT).as_posix(),
                   source_manifest_sha256=digest(SOURCE / 'firmware_manifest.json'),
                   checkpoint_sha256=manifest['checkpoint_sha256'], candidate_abi=manifest['candidate_abi'],
                   raw_sha256=digest(raw), compiled_sha256=digest(compiled), compiled_bytes=compiled.stat().st_size,
                   deployment_approved=False, changes=['Runtime remains in services/deepfir', 'C++ array alignment 32'],
                   files={p.name: digest(p) for p in destination.iterdir() if p.is_file()})
    (destination / 'integration_manifest.json').write_text(json.dumps(receipt, indent=2)+'\n')
    (evidence / 'import_receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')
    print('Imported', len(payload), 'model bytes;', len(manifest['artifacts']), 'source hashes verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, required=True)
    import_package(parser.parse_args().evidence)
