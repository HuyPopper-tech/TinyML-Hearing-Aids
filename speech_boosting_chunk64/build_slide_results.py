import json
from pathlib import Path


ROOT = Path(r"D:\Tài liệu ĐATN\Tuần 8\speech_boosting_chunk64")
EVAL = ROOT / "evaluation"
REPORT = json.loads((EVAL / "report_summary.json").read_text(encoding="utf-8"))
HISTORY = json.loads((ROOT / "qat" / "qat_history.json").read_text(encoding="utf-8"))
FIRMWARE = json.loads((ROOT / "speech_boosting_m55m1_firmware_export" / "firmware_export_manifest.json").read_text(encoding="utf-8"))


def f(value, digits=3):
    return f"{value:.{digits}f}"


def table(rows, headers):
    return "| " + " | ".join(headers) + " |\n|" + "|".join(["---"] * len(headers)) + "|\n" + "\n".join("| " + " | ".join(row) + " |" for row in rows)


models = REPORT["models"]
fp32, ptq, qat = (models[key] for key in ("fp32_champion", "ptq_int8", "qat_int8"))
metrics = (("l1", "L1", 4), ("pesq_wb", "PESQ-WB", 3), ("stoi", "STOI", 3), ("si_sdr", "SI-SDR (dB)", 3), ("si_sdri", "SI-SDRi (dB)", 3), ("enhanced_to_clean_rms_ratio", "RMS ratio", 3))

rows = []
for key, label, digits in metrics:
    a, b, c = (item["overall"][key] for item in (fp32, ptq, qat))
    rows.append((label, f"{f(a['mean'], digits)} ± {f(a['ci95_half_width'], digits)}", f"{f(b['mean'], digits)}", f"{f(c['mean'], digits)}", f"{f(c['mean'] - a['mean'], digits)}"))

snr_rows = []
for snr in sorted(fp32["target_snr_db"], key=float):
    a, c = fp32["target_snr_db"][snr], qat["target_snr_db"][snr]
    snr_rows.append((snr, f(a['si_sdri']['mean']), f(c['si_sdri']['mean']), f(c['si_sdri']['mean']-a['si_sdri']['mean']), f(a['stoi']['mean']), f(c['stoi']['mean']), f(c['pesq_wb']['mean']-a['pesq_wb']['mean'])))

background_rows = []
for bg in fp32["background_profile"]:
    a, c = fp32["background_profile"][bg], qat["background_profile"][bg]
    background_rows.append((bg, f(a['si_sdri']['mean']), f(c['si_sdri']['mean']), f(c['si_sdri']['mean']-a['si_sdri']['mean']), f(a['stoi']['mean']), f(c['stoi']['mean'])))

validations = [row for row in HISTORY if row["qat_validation"]]
best = max(validations, key=lambda row: row["qat_validation"]["val_si_sdri"])
last = validations[-1]
parity = json.loads((EVAL / "qat_int8_parity_vs_fp32.json").read_text(encoding="utf-8"))

content = f"""# Bảng kết quả cho slide — Speech Boosting Chunk-64

## Phạm vi và định danh kết quả

- Test: {REPORT['evaluated_items']}/{REPORT['test_items_total']} clips, stratified sample, seed `{REPORT['selection_seed']}`.
- So sánh: FP32 champion, PTQ INT8, QAT INT8; PESQ/STOI/NaN errors đều bằng 0.
- Đây là **QAT-lite kernel-only** của run cũ (`qat/ckpt-8`), không phải kết quả full stream-QAT/QDQ mới.

## Bảng chính

{table(rows, ['Metric', 'FP32 mean ± 95% CI', 'PTQ INT8', 'QAT INT8', 'QAT − FP32'])}

Over-suppression: FP32 = {fp32['over_suppression']['count']}/100; PTQ = {ptq['over_suppression']['count']}/100; QAT = {qat['over_suppression']['count']}/100.

## Theo SNR

{table(snr_rows, ['SNR (dB)', 'FP32 SI-SDRi', 'QAT SI-SDRi', 'Δ SI-SDRi', 'FP32 STOI', 'QAT STOI', 'Δ PESQ'])}

## Theo background

{table(background_rows, ['Background', 'FP32 SI-SDRi', 'QAT SI-SDRi', 'Δ SI-SDRi', 'FP32 STOI', 'QAT STOI'])}

## QAT training / parity / deployment

| Chỉ số | Giá trị |
|---|---:|
| QAT total time | {f(HISTORY[-1]['total_minutes'], 2)} min |
| Champion checkpoint | epoch {best['qat_epoch']} (`ckpt-8`) |
| Best validation SI-SDRi | {f(best['qat_validation']['val_si_sdri'])} dB |
| Champion validation L1 | {f(best['qat_validation']['val_l1'], 4)} |
| Final validation SI-SDRi | {f(last['qat_validation']['val_si_sdri'])} dB |
| QAT parity mean abs. error | {f(parity['mean_abs_error_vs_fp32'], 6)} |
| QAT parity max abs. error | {f(parity['max_abs_error_vs_fp32'], 6)} |
| Raw INT8 / Vela CPU operators | {FIRMWARE['vela_cpu_operators']} |
| Stream ABI | {FIRMWARE['stream_inputs']} inputs / {FIRMWARE['stream_outputs']} outputs |
| State memory | {FIRMWARE['state_tensors']} tensors / {FIRMWARE['state_bytes']} bytes |
| Golden trace | {FIRMWARE['golden_chunks']} streaming chunks |

## Diễn giải cho slide kết luận

- QAT cải thiện so với PTQ: SI-SDRi tăng {f(qat['overall']['si_sdri']['mean'] - ptq['overall']['si_sdri']['mean'])} dB, PESQ tăng {f(qat['overall']['pesq_wb']['mean'] - ptq['overall']['pesq_wb']['mean'])}, STOI tăng {f(qat['overall']['stoi']['mean'] - ptq['overall']['stoi']['mean'])}.
- So với FP32, QAT còn khoảng cách {f(fp32['overall']['si_sdri']['mean'] - qat['overall']['si_sdri']['mean'])} dB SI-SDRi; đây là mục tiêu trực tiếp của full stream-QAT mới.
- Vela biên dịch được với 0 CPU operators và giữ ABI 64-sample / look-ahead 16-sample.
"""

(ROOT / "SLIDE_RESULTS_QAT_LITE.md").write_text(content, encoding="utf-8")
print("SLIDE_RESULTS_QAT_LITE=PASS")
