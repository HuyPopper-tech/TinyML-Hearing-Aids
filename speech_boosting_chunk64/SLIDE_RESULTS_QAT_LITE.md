# Bảng kết quả cho slide — Speech Boosting Chunk-64

## Phạm vi và định danh kết quả

- Test: 100/3750 clips, stratified sample, seed `20260827`.
- So sánh: FP32 champion, PTQ INT8, QAT INT8; PESQ/STOI/NaN errors đều bằng 0.
- Đây là **QAT-lite kernel-only** của run cũ (`qat/ckpt-8`), không phải kết quả full stream-QAT/QDQ mới.

## Bảng chính

| Metric | FP32 mean ± 95% CI | PTQ INT8 | QAT INT8 | QAT − FP32 |
|---|---|---|---|---|
| L1 | 0.0053 ± 0.0008 | 0.0065 | 0.0059 | 0.0006 |
| PESQ-WB | 1.488 ± 0.081 | 1.143 | 1.182 | -0.306 |
| STOI | 0.761 ± 0.033 | 0.705 | 0.723 | -0.038 |
| SI-SDR (dB) | 6.152 ± 1.195 | 3.584 | 4.543 | -1.610 |
| SI-SDRi (dB) | 5.439 ± 1.148 | 2.870 | 3.829 | -1.610 |
| RMS ratio | 1.010 ± 0.044 | 1.050 | 1.032 | 0.022 |

Over-suppression: FP32 = 0/100; PTQ = 0/100; QAT = 0/100.

## Theo SNR

| SNR (dB) | FP32 SI-SDRi | QAT SI-SDRi | Δ SI-SDRi | FP32 STOI | QAT STOI | Δ PESQ |
|---|---|---|---|---|---|---|
| -5 | 5.899 | 5.202 | -0.697 | 0.565 | 0.542 | -0.029 |
| 0 | 5.335 | 4.927 | -0.408 | 0.701 | 0.682 | -0.075 |
| 5 | 9.304 | 7.338 | -1.967 | 0.784 | 0.741 | -0.357 |
| 10 | 5.534 | 2.967 | -2.567 | 0.834 | 0.781 | -0.427 |
| 15 | 1.123 | -1.286 | -2.409 | 0.920 | 0.872 | -0.641 |

## Theo background

| Background | FP32 SI-SDRi | QAT SI-SDRi | Δ SI-SDRi | FP32 STOI | QAT STOI |
|---|---|---|---|---|---|
| babble_public | 4.663 | 3.516 | -1.147 | 0.729 | 0.702 |
| quiet_indoor | 6.699 | 4.634 | -2.065 | 0.740 | 0.696 |
| urban_safety | 2.915 | 1.571 | -1.344 | 0.725 | 0.698 |
| vehicle_lowfreq | 7.434 | 5.396 | -2.038 | 0.803 | 0.754 |
| wind | 5.484 | 4.030 | -1.454 | 0.808 | 0.768 |

## QAT training / parity / deployment

| Chỉ số | Giá trị |
|---|---:|
| QAT total time | 80.55 min |
| Champion checkpoint | epoch 8 (`ckpt-8`) |
| Best validation SI-SDRi | 5.708 dB |
| Champion validation L1 | 0.0050 |
| Final validation SI-SDRi | 5.696 dB |
| QAT parity mean abs. error | 0.002087 |
| QAT parity max abs. error | 0.012464 |
| Raw INT8 / Vela CPU operators | 0 |
| Stream ABI | 21 inputs / 21 outputs |
| State memory | 20 tensors / 5632 bytes |
| Golden trace | 100 streaming chunks |

## Diễn giải cho slide kết luận

- QAT cải thiện so với PTQ: SI-SDRi tăng 0.959 dB, PESQ tăng 0.040, STOI tăng 0.018.
- So với FP32, QAT còn khoảng cách 1.610 dB SI-SDRi; đây là mục tiêu trực tiếp của full stream-QAT mới.
- Vela biên dịch được với 0 CPU operators và giữ ABI 64-sample / look-ahead 16-sample.
