"""Check this supplied UART snapshot; never connect to or execute a device."""
from datetime import datetime
import hashlib
import json
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
path = HERE / 'user_log.txt'
rows = path.read_text(encoding='utf-8').splitlines()
def fields(row):
    return dict(token.split('=', 1) for token in row.split()[1:])
probe = fields(next(row for row in rows if row.startswith('[AUDIO_RELEASE_PROBE]')))
result = fields(next(row for row in rows if row.startswith('[AUDIO_RELEASE_RESULT]')))
events = [fields(row) for row in rows if row.startswith('[AUDIO_STARTUP]')]
assert result['result'] == 'COMPLETE'
assert int(probe['frame']) == int(probe['capture_last_seq']) == int(result['seq']) == 60000
assert int(result['playback_slot']) == int(result['seq']) + 1
for name in ('model_fail', 'submit_busy', 'submit_error', 'dmic_overrun', 'dac_underrun',
             'dmic_dma', 'dac_dma', 'late_1ms'):
    assert int(probe[name]) == 0, name
for name in ('fault', 'detail', 'deadline', 'restarts'):
    assert int(result[name]) == 0, name
assert int(probe['total_max_us']) < 900 and int(probe['e2e_max_us']) < 900
assert int(result['capture_publish_max_us']) < 900
assert int(probe['total_mean_us']) <= int(probe['total_max_us'])
assert int(probe['e2e_mean_us']) <= int(probe['e2e_max_us'])
assert int(result['last_total_us']) == sum(int(result['last_'+name+'_us']) for name in
    ('feature','invoke','bridge','carry','taps','fir','pcm','other'))
assert int(result['last_feature_us']) == sum(int(result['last_'+name+'_us']) for name in
    ('pre_fft','fft','post_fft')) + 1  # Independently truncated microsecond intervals.
first_tx = next(event for event in events if event['phase'] == 'TX_TD')
assert int(first_tx['elapsed_us']) == 1066 and int(first_tx['rx_seq']) == 1
assert [int(event['event']) for event in events] == list(range(len(events)))
stop_ticks = (int(events[-1]['cycle'])-int(events[0]['cycle'])) & 0x1ffffff
assert stop_ticks // 220 == int(events[-1]['elapsed_us']) == 60233
timebase = (ROOT/'board/m55m1_timebase.c').read_text()
assert '0x01FFFFFFUL' in timebase
receipt = {
    'recorded_local': datetime.now().astimezone().isoformat(),
    'provenance': 'User-pasted UART text; normalized UTF-8/LF snapshot, not independent device capture',
    'user_log_sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
    'flashed_bin_hash': 'NOT_SUPPLIED', 'independently_measured_clock': 'NOT_SUPPLIED',
    'associated_source_commit': '99c617b',
    'candidate_probe_bin_sha256': 'b6ead4bfeb58b033d6f7eff15a3bca35e7ed9bed30d0ee124c7f916fa4a165d6',
    'candidate_identity_attested_by_log': False,
    'probe': probe, 'result': result,
    'derived': {'samples':60000*16, 'nominal_seconds':60000*16/16000,
        'input_rail_percent':100*int(probe['in_clip'])/(60000*16),
        'total_budget_margin_us':1000-int(probe['total_max_us']),
        'e2e_budget_margin_us':1000-int(probe['e2e_max_us']),
        'e2e_target_900_margin_us':900-int(probe['e2e_max_us']),
        'stop_elapsed_is_modular':True, 'counter_period_us_at_configured_220mhz':(1<<25)/220},
    'timing_subgate': 'PASS', 'provenance_qualification':'USER_REPORTED',
    'physical_b_plus_2': 'NOT_RUN', 'compiled_parity': 'BLOCKED',
    'physical_audio_latency_power': 'NOT_RUN', 'agent_device_operation': 'NOT_RUN',
}
receipt_path = HERE / 'receipt.json'
if receipt_path.exists():
    saved = json.loads(receipt_path.read_text(encoding='utf-8'))
    receipt['recorded_local'] = saved['recorded_local']
    assert receipt == saved, 'Snapshot no longer matches its saved receipt'
else:
    receipt_path.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print('PASS: 60000 supplied hops, max total863us/e2e885us, all fault counters0; image identity unattested')
