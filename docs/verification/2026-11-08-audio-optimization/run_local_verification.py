"""Local execution receipts; reuses an existing isolated compiler read-only."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone, timedelta

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
os.chdir(ROOT)
runtime = ROOT / 'diagnostics/i2c_stuck_trace_20261108_162019/host_compiler_runtime'
env = dict(os.environ, PATH=str(runtime) + ';' + os.environ['PATH'], PYTHONUTF8='1')
mode = sys.argv[1]
stamp = datetime.now(timezone(timedelta(hours=7))).strftime('%Y%m%d_%H%M%S')
unittest = ['rtk', 'python', '-m', 'unittest', 'discover', '-s', 'tests']
builds = [(p + '_' + a, ['rtk', 'proxy', 'cmd', '/c', 'build.bat', p, a])
          for p in ('debug', 'release') for a in ('config', 'build')]
if mode == 'baseline':
    commands = [('baseline_tests', unittest + ['-v'])] + builds
elif mode == 'build':
    commands = builds
elif mode == 'config':
    commands = [('debug_config', ['rtk', 'proxy', 'cmd', '/c', 'build.bat', 'debug', 'config'])]
elif mode == 'tests':
    commands = [('tests', unittest + ['-v'])]
else:
    commands = [(p, unittest + ['-p', p, '-v']) for p in sys.argv[2:]]
receipts = []
for name, command in commands:
    log = HERE / (stamp + '_' + name.replace('*', 'all') + '.log')
    started = time.time()
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run(command, env=env, stdout=stream, stderr=subprocess.STDOUT)
    receipts.append(dict(command=command, exit_code=result.returncode,
                         elapsed_s=time.time()-started, log=log.name))
    (HERE / (stamp + '_' + mode + '_receipt.json')).write_text(json.dumps(receipts, indent=2))
    print(name, result.returncode, flush=True)
    if result.returncode:
        print(log.read_text(encoding='utf-8')[-6500:])
        sys.exit(result.returncode)
if mode in ('baseline', 'build'):
    snapshot = HERE / (stamp + '_' + mode)
    snapshot.mkdir()
    identities = []
    for directory in ('build-debug', 'build'):
        destination = snapshot / directory
        destination.mkdir()
        for suffix in ('.bin', '.axf', '.axf.map'):
            source = ROOT / directory / ('M55M1_GestureAI' + suffix)
            if source.exists():
                shutil.copy2(source, destination / source.name)
                identities.append(dict(path=str(source.relative_to(ROOT)), bytes=source.stat().st_size,
                                       sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
    (snapshot / 'images.json').write_text(json.dumps(identities, indent=2))
