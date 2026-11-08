"""Build existing Release opt-ins sequentially, then restore ordinary profiles."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
from datetime import datetime, timezone, timedelta

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
os.chdir(ROOT)
stamp = datetime.now(timezone(timedelta(hours=7))).strftime('%Y%m%d_%H%M%S')
env = dict(os.environ, PATH='C:/Program Files/ArmCompilerforEmbedded6.24/bin;'
           'C:/ST/STM32CubeCLT_1.20.0/Ninja/bin;' + os.environ['PATH'])
receipts = []


def run(name, command):
    log = HERE/(stamp+'_'+name+'.log')
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(['rtk', 'proxy', *command], env=env,
                                stdout=stream, stderr=subprocess.STDOUT)
    receipts.append(dict(name=name, command=['rtk', 'proxy', *command],
                         exit_code=result.returncode, log=log.name))
    (HERE/(stamp+'_variants_receipt.json')).write_text(json.dumps(receipts, indent=2))
    print(name, result.returncode, flush=True)
    if result.returncode:
        print(log.read_text(encoding='utf-8')[-4500:])
        raise RuntimeError(name)


try:
    for name, option in [('release_probe', 'M55M1_AUDIO_RELEASE_PROBE'),
                         ('compiled_replay', 'M55M1_DEEPFIR_COMPILED_REPLAY')]:
        run(name+'_config', ['cmake', '--preset', 'm55m1-release', '-D'+option+'=ON'])
        run(name+'_build', ['cmake', '--build', '--preset', 'm55m1-release'])
        snapshot = HERE/(stamp+'_'+name)
        snapshot.mkdir()
        images = []
        for suffix in ('.bin', '.axf', '.axf.map'):
            source = ROOT/'build'/('M55M1_GestureAI'+suffix)
            shutil.copy2(source, snapshot/source.name)
            images.append(dict(file=source.name, bytes=source.stat().st_size,
                               sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
        (snapshot/'images.json').write_text(json.dumps(images, indent=2))
finally:
    run('restore_release', ['cmd', '/c', 'build.bat', 'release', 'build'])
    run('restore_debug_editor', ['cmd', '/c', 'build.bat', 'debug', 'config'])
