# SESSION HANDOFF — QAT / EVALUATION / EXPORT / FIRMWARE / SLIDES
**Ngày:** 2026-08-27
**Mục đích:** Bàn giao phiên làm việc để tiếp tục chạy full QAT, tích hợp firmware đo latency, và làm slide báo cáo Tuần 7–8.

## 1. Trạng thái end-to-end đã xác minh
Pipeline đã debug qua Kaggle: restore FP32 champion → full state-aware streaming QAT (manual fake-quant/QDQ) → export FP32/PTQ/QAT INT8 TFLite → eval ba model → Vela → firmware package.

Các mốc log đã quan sát:
- `FP32_STREAM_PARITY=PASS`; max abs error `2.6226043701171875e-06`, atol `1e-5`.
- `QAT_CALIBRATION=1/1 ranges=136`, `QAT_RESUME=FRESH`.
- Debug benchmark: `seconds_per_update=59.219150061000164`.
- Debug tạo champion `.../full_qat/champion/ckpt-2`.
- Raw INT8 validation debug: L1 `0.0099396`, SI-SDRi `4.30936` (1 item).
- `EXPORT_STAGE=INT8_MODELS_READY elapsed_min=1.27`.
- `EVALUATION_REPORT=PASS`, `VELA_ZERO_CPU=PASS`, `FIRMWARE_EXPORT=PASS`.
- Firmware ZIP Kaggle: `/kaggle/working/speech_boosting_chunk64/speech_boosting_m55m1_firmware_export.zip`.

## 2. File quan trọng
- Notebook gốc: `D:\Tài liệu ĐATN\Tuần 8\kaggle-train-speech-boosting-chunk64-v1.ipynb`
  - SHA-256 gốc: `7ac5d1a477f423a8246701a84099f583a6351684c46c717208973ba9dc261eea`.
- Notebook full QAT: `D:\Tài liệu ĐATN\Tuần 8\kaggle-train-speech-boosting-chunk64-full-stream-qat-v2.ipynb`.
- Builder: `D:\Tài liệu ĐATN\Tuần 8\build_full_stream_qat_notebook.py`.
- Test static: `D:\Tài liệu ĐATN\Tuần 8\tests\test_full_stream_qat_notebook.py`.
- Block cell: `D:\Tài liệu ĐATN\Tuần 8\full_stream_qat_cell_blocks\CELL_26_EVALUATION.py`, `CELL_28_FULL_STREAM_QAT.py`, `CELL_30_EXPORT.py`, `CELL_32_VELA_FIRMWARE.py`.
- Kết quả QAT-lite cũ: `D:\Tài liệu ĐATN\Tuần 8\speech_boosting_chunk64\SLIDE_RESULTS_QAT_LITE.md`.

## 3. Kiến trúc/pipeline giữ nguyên
- Streaming Wave-U-Net/LSTM; `CHUNK=64`, `LOOKAHEAD=16`.
- 18 cache state + LSTM h/c = 20 state tensors; TFLite có 21 inputs/21 outputs.
- State memory: 5,632 bytes.
- Target M55M1 + Ethos-U55; Vela audit đã cho CPU operators = 0.
- QAT thay phương pháp fine-tune fake-quant-aware, không đổi topology, I/O hay deploy pipeline của FP32.

## 4. Patch debug bắt buộc giữ
### 4.1 TensorShape Keras là tuple
Không dùng `tensor.shape.as_list()`.
```python
STREAM_STATE_SHAPES = tuple(
    tuple(int(dim) for dim in tensor.shape[1:])
    for tensor in stream_model.inputs[1:]
)
# initial state
tf.zeros((1, *shape), dtype=tf.float32)
```

### 4.2 State temporal shape trong `tf.while_loop`
```python
STREAM_STATE_INVARIANTS = tuple(
    tf.TensorShape([1, 1, None, shape[-1]])
    if len(shape) == 3 else tf.TensorShape([1, *shape])
    for shape in STREAM_STATE_SHAPES
)
loop_vars = (tf.constant(0), tuple(states), outputs)
_, next_states, outputs = tf.while_loop(
    cond, body, loop_vars,
    shape_invariants=(tf.TensorShape([]), STREAM_STATE_INVARIANTS, tf.TensorShape(None)),
    parallel_iterations=1,
)
```
Áp dụng cho cả `stream_fp32_compiled` và `stream_rollout`.

### 4.3 FP32 graph/eager parity
Sai khác `2.6226e-06` là numerical drift hợp lệ:
```python
FP32_STREAM_PARITY_ATOL = 1e-5
assert np.isfinite(_fp32_error) and _fp32_error <= FP32_STREAM_PARITY_ATOL
```

### 4.4 Frozen teacher snapshot
Không gọi `variable.read_value()`:
```python
def frozen_teacher_snapshot():
    return {
        variable.path: tf.Variable(
            initial_value=variable.numpy().copy(), trainable=False,
            dtype=variable.dtype, name=f"teacher_{index}",
        )
        for index, variable in enumerate(stream_model.trainable_variables)
    }
```

## 5. Cấu hình FULL cần chạy
Chỉ đổi biến sẵn có:
```python
# Cell 2
PROFILE = "qat"
DEBUG = False

# Cell 26
EVALUATION_MODE = "stratified_report_sample"
EVALUATION_MAX_ITEMS = 300
EVALUATION_SEED = 20260827
EVALUATION_SAVE_EVERY = 1
EVALUATION_LOG_EVERY = 5
EVALUATION_MODELS = ("fp32_champion", "ptq_int8", "qat_int8")

# Cell 28
QAT_DEBUG_EXPORT_ONLY = False
QAT_RESUME = False
QAT_MAX_SECONDS = int(5.25 * 60 * 60)
QAT_MAX_UPDATES = 6000
QAT_BENCHMARK_STEPS = 5
QAT_LOG_EVERY = 10
QAT_VALIDATE_EVERY_UPDATES = 50
QAT_WARMUP_CHUNKS = 32
QAT_TRAIN_CHUNKS = 64
QAT_CALIBRATION_SEGMENTS = 24
QAT_CALIBRATION_CHUNKS = QAT_WARMUP_CHUNKS + QAT_TRAIN_CHUNKS
REPRESENTATIVE_CLIPS = 24
REPRESENTATIVE_CHUNKS = QAT_CALIBRATION_CHUNKS
QAT_RAW_VAL_ITEMS = 8
QAT_LR_INITIAL = 1e-5
QAT_LR_FINAL = 5e-6
QAT_LR_DECAY_UPDATE = 50
QAT_TEACHER_L1_WEIGHT = 0.10
QAT_RANGE_HEADROOM = 1.05
```
`QAT_LR_DECAY_UPDATE=50`, không phải 3600: code giảm LR bằng điều kiện `qat_updates == ...`; ngân sách 5.25 giờ sẽ dừng rất xa 3600 update. 50 tạo pha ổn định LR `5e-6` sau warm-up QAT ban đầu.

## 6. Thời gian và log cần theo dõi
- Full QAT: cap 5.25 giờ; update thực tế do `QAT_BENCHMARK` quyết định.
- Eval 300 clips, 3 model: ước tính 35–45 phút (debug 100 clips: FP32 ~5m, PTQ ~3m, QAT ~3m).
- Export 1–2 phút; Vela/package <2 phút.
- Tổng dự kiến 6–6.8 giờ, dưới 12 giờ.

Mốc log: `QAT_BENCHMARK`, `QAT_PLANNED_UPDATES`, `QAT_UPDATE`, `QAT_RAW_INT8_VALIDATION`, `EVAL_MODEL ... EVAL_CLIP=x/300 ... ETA_MIN`, `EVALUATION_REPORT=PASS`, `VELA_ZERO_CPU=PASS`, `FIRMWARE_EXPORT=PASS`.

Nếu kernel dừng: chỉ dùng `QAT_RESUME=True` khi checkpoint và QAT config trong `full_qat` còn nguyên. Session full mới/sạch dùng `False`.

## 7. Baseline cũ cho slide (QAT-lite, N=100/3750)
| Metric | FP32 | QAT-lite INT8 | Delta |
|---|---:|---:|---:|
| L1 | 0.005336 ± 0.000756 | 0.005900 | +0.000564 |
| PESQ-WB | 1.488 ± 0.081 | 1.182 | -0.306 |
| STOI | 0.761 ± 0.033 | 0.723 | -0.0375 |
| SI-SDR | 6.152 ± 1.195 dB | 4.543 dB | -1.609 dB |
| SI-SDRi | 5.439 ± 1.148 dB | 3.829 dB | -1.610 dB |
| RMS ratio | 1.010 ± 0.044 | 1.032 | +0.022 |
| PESQ/STOI errors | 0/0 | 0/0 | — |

PTQ cũ có SI-SDRi khoảng 2.87 dB, thấp hơn QAT-lite. Không dùng số QAT-lite để kết luận full QAT; full QAT phải dùng report N=300 cùng seed/scope trên cả 3 model.

## 8. Việc tiếp theo ưu tiên 1 — Firmware và latency
1. Từ Kaggle full run tải: QAT INT8 TFLite, Vela TFLite, firmware ZIP, evaluation CSV/JSON, golden vectors, quantization metadata và log Vela.
2. Tích hợp Vela TFLite vào project M55M1; giữ nguyên input quantization và feedback 20 state outputs → inputs.
3. Khởi tạo zero states; warm-up 20–50 chunks.
4. Đo tối thiểu 1,000 chunks bằng cycle counter/timer target.
5. Xuất CSV raw latency; tính min/median/mean/p95/max (ms), chunks/s, real-time factor.
6. Báo cáo model flash size, tensor arena, state memory, NPU delegation/không CPU fallback.
7. So output một số golden chunks board/host với tolerance quantization.

## 9. Việc tiếp theo ưu tiên 2 — Slide Tuần 7–8
Bố cục tối thiểu:
1. Mục tiêu và ràng buộc edge.
2. Speech enhancement/dataset.
3. Streaming architecture (Chunk 64, lookahead 16).
4. FP32 → PTQ → full state-aware QAT → TFLite → Vela → firmware.
5. Tuần 7: streaming architecture/pipeline/checkpoint/export contract.
6. Tuần 8: QAT, evaluation, Vela, firmware package.
7. Debug evidence: parity, shape invariants, raw INT8 validation.
8. Bảng FP32/PTQ/QAT quality từ full report N=300.
9. Deployment: 21 I/O, 20 states, 5632B state, Vela CPU ops=0.
10. Latency firmware, kết luận, kế hoạch tiếp.

## 10. Kiểm tra cục bộ
```powershell
rtk proxy python -m unittest 'D:\Tài liệu ĐATN\Tuần 8\tests\test_full_stream_qat_notebook.py' -v
```
Đã có static tests PASS; runtime Kaggle end-to-end đã xác minh ở debug log phía trên.

## 11. Thuật ngữ chính xác
Full QAT hiện tại là **manual full state-aware stream-QDQ QAT**, không phải graph rewrite TFMOT. Evaluation N=300 là sample stratified phục vụ báo cáo, giữ đủ PESQ-WB/STOI/SI-SDR/SI-SDRi/L1/RMS/error counts trong thời lượng dưới 12 giờ.
