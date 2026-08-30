# DeepFilterNet U55/M55M1 — ghi chú session và handoff firmware

**Cập nhật:** 05-08-2026
**Notebook nguồn:** `kaggle-train-deepfilternet-u55-v1-6.ipynb`
**Run đã phân tích:** `deepfilternet_u55_full_seed20260805`
**Trạng thái package:** `engineering_ready_smoke_passed` — đủ để bắt đầu tích hợp/đo board, chưa phải release cuối.

---

## 1. Mục tiêu và phạm vi model

Đây là DeepFilterNet đơn kênh cho speech denoising, bám sát ý tưởng hybrid DeepFilterNet/DeepFilterNet2 và biến thể triển khai MCU trong tài liệu Wögerbauer. Không có nhánh AEC/DDECc vì dataset hiện tại không có far-end reference.

- Audio đầu vào/đầu ra: PCM16, mono, **16 kHz**.
- Mô hình là causal: chỉ dùng frame hiện tại và lịch sử; không look-ahead.
- NPU U55 thực hiện neural network INT8.
- Cortex-M55 DSP thực hiện STFT/ISTFT, ERB feature, áp gain complex, DF 5-tap, alpha blend và WOLA.

Không được upsample giả lên 48 kHz: `speech_enhancement_v1` là 16 kHz.

## 2. Kiến trúc faithful-port

### Front-end

| Hạng mục | Giá trị |
|---|---:|
| Sample rate | 16.000 Hz |
| FFT/window | 512 samples, sqrt-Hann |
| Hop | 256 samples (16 ms/frame) |
| STFT bins | 257 bins, 31,25 Hz/bin |
| ERB | 32 bands, log-power feature |
| Complex feature | 144 bins × real/imag, đến 4,5 kHz |
| Deep filter | 5 taps × 144 bins × complex = 1.440 coefficient/frame |
| Mask | 32 ERB gains, nội suy hằng số trong graph thành 257 gain bins |
| Blend | 1 scalar `alpha`, sigmoid |

### Neural topology

```text
ERB log-power (32) ──────┐
                          ├─ ERB encoder ─────┐
Complex STFT (144 × 2) ──┘                    │
                                               ├─ fusion + 3 grouped-GRU equivalent
                                               │     (4 group, tổng 320 state)
                                               ├─ ERB decoder → 257 bin gains
                                               ├─ DF decoder (2 grouped-GRU, 8 group,
                                               │  tổng 320 state) → 5×144×complex taps
                                               └─ alpha decoder → scalar alpha
```

Grouped convolution/linear/GRU được biểu diễn bằng primitive tương thích Vela:

- Grouped convolution: depthwise + pointwise hoặc nhánh convolution độc lập.
- Grouped linear: các dense block-diagonal độc lập.
- Grouped GRU: các GRU group với state explicit, hạ về `FullyConnected`, `Add`, `Mul`, `Sigmoid`, `Tanh`; không dùng `tf.keras.GRU` nguyên khối trong graph export.
- BatchNorm không tồn tại runtime; nếu có phải fold vào weight/bias trước export.

### Công thức firmware

Với spectrum đầu vào `X[n,k]`, gain `G`, complex DF coefficient `C` và `alpha`:

```text
X_erb[n,k] = G_bin[n,k] × X[n,k]
X_df[n,k]  = Σ(i=0..4) C[n,i,k] × X_erb[n-i,k]      với k < 144
Y[n,k]     = (1-alpha[n]) × X_erb[n,k] + alpha[n] × X_df[n,k]
```

Với `k >= 144`, dùng trực tiếp `X_erb[n,k]`. Gain bin 257 phần tử đã được model xuất trực tiếp; firmware **không được tự triển khai lại ERB expansion 32 → 257**.

## 3. Pipeline train và QAT

### Dataset

`speech_enhancement_v1`, manifest Kaggle:

- Train: 17.500 pair.
- Validation: 3.750 pair.
- Test: 3.750 pair.
- Bắt buộc kiểm tra path tồn tại, sample rate 16 kHz, không leak audio giữa split.

Data loader hiện dùng `map(AUTOTUNE)`, `prefetch(AUTOTUNE)`, shuffle và random crop. Không dùng `cache()` audio vì vừa làm mất random crop vừa tốn RAM/disk.

### Schedule full của v1-6

| Stage | Batch | Crop | Step/epoch | Max epoch | Validation |
|---|---:|---:|---:|---:|---|
| FP32 warm-up | 8 | 32.000 mẫu (2 s) | 400 | 22 | 256 pair cố định, mỗi 2 epoch |
| QAT fine-tune | 8 | 8.192 mẫu (0,512 s) | 200 | 4 | 256 pair cố định, mỗi 2 epoch |

- FP32 optimizer: Adam, LR đầu `3e-4`; giảm LR theo plateau.
- QAT optimizer: Adam, LR đầu `3e-5`, sau FP32 champion.
- Loss: level-normalized CCMSE (`c=0,6`) cùng STFT-consistency.
- QAT dùng manual fake-quant: kernel Conv/Dense per-channel; activation, ERB/complex feature và recurrent state per-tensor INT8.
- Fake-quant chỉ tồn tại lúc train. Model inference/export phải là primitive INT8 sạch, không FakeQuant/Flex/float fallback.

**Không so trực tiếp validation loss FP32 với QAT:** crop của hai stage khác nhau nên thang loss không tương đương.

### Kết quả run `seed20260805`

| Hạng mục | Kết quả |
|---|---:|
| FP32 champion | `checkpoint_fp32/ckpt-18` |
| FP32 best fixed validation loss | **2,6222679** |
| QAT champion | `checkpoint_qat/ckpt-2` |
| QAT best fixed validation loss | **3,6974673** |
| Thời gian FP32 stage | ~22.540 s (~6 giờ 16 phút) |
| Thời gian QAT + calibration | ~7.491 s (~2 giờ 05 phút) |
| Fake quantizer | 183 |
| QAT/FP32 cached-step ratio | ~4,60× |

FP32 không cải thiện sau epoch 18; QAT không cải thiện sau epoch 2. Việc chọn checkpoint là hợp lý.

### Diễn giải QAT saturation

`qat_smoke_report.json` ghi saturation cực đại khoảng 14,8%, nằm ở kernel quantizer Conv2D. Đây là cảnh báo range/weight cho QAT, **không phải** bằng chứng ERB/state/audio runtime bị clip. Chất lượng INT8 smoke hiện vẫn đạt, nhưng phải giữ QAT range/saturation report khi review bản final hoặc khi thay đổi schedule.

## 4. Kết quả quality smoke

Full run đã chạy smoke **512 utterance**. Đây không phải full frozen-test 3.750 sample.

| Metric trung bình | FP32 | INT8 | FP32 − INT8 |
|---|---:|---:|---:|
| SI-SDR | 5,7692 dB | 5,8668 dB | -0,0976 dB |
| PESQ-WB | 1,7319 | 1,7417 | -0,0098 |
| STOI | 0,8003 | 0,8025 | -0,0022 |

Các quality gate trung bình FP32–INT8 đều đạt:

- `ΔSI-SDR ≤ 0,5 dB`
- `ΔPESQ ≤ 0,05`
- `ΔSTOI ≤ 0,01`

INT8 không giảm trung bình trên smoke. Tuy nhiên vẫn có utterance riêng lẻ giảm, nên final evaluation cần giữ metrics per-sample và xem tail distribution.

**Giới hạn diễn giải:** output hiện không chứa metric baseline của mixture nhiễu. Không thể dùng riêng các giá trị trên để kết luận mức cải thiện tuyệt đối so với input noisy; chúng chủ yếu xác nhận retention FP32 → INT8.

## 5. Export Vela và tài nguyên M55M1

| Hạng mục | Kết quả |
|---|---:|
| Target | M55M1, Ethos-U55-256, 220 MHz |
| TFLite INT8 pre-Vela | 1.392.536 B |
| TFLite INT8 post-Vela | **1.197.856 B** |
| Vela CPU operators | **0** |
| Vela NPU operators | 426 (100%) |
| Vela NPU SRAM estimate | 15,12 KiB |
| Vela flash estimate | ~1.131,89 KiB |
| Package flash audit | 1.265.696 B / budget 1.887.436 B |
| Headroom audit | ~621.740 B (~607 KiB) |
| Neural workload | ~73,4 MMAC/s ở 16 kHz |

`deepfilternet_u55_vela_model_data.c` lớn khoảng 5,2 MiB vì là biểu diễn text C của byte array. Nó **không** là dung lượng APROM sau biên dịch; model binary post-Vela là ~1,14 MiB.

Flash audit chưa tính toàn bộ TFLM, application, DSP code và audio buffers. Link-map release là nguồn quyết định cuối.

## 6. Nội dung integration package và cách dùng

Thư mục `integration_package/` có:

- `deepfilternet_u55_int8_vela.tflite`: model deploy sau Vela.
- `deepfilternet_u55_vela_model_data.c/.h`: model data để compile vào firmware.
- `deepfilternet_u55_contract.h` và `tensor_contract.json`: shape, quantization, state contract.
- `dsp_constants.h`: ERB map, window và constant DSP.
- `deepfilternet_u55_postprocess.c/.h`: reference postprocess.
- `golden/`: 5 scenario (silence, noise, impulse, sine 440 Hz, sine 3 kHz).
- Vela report/config/selection, QAT ranges, smoke metrics, parity report, README và audit manifest.

### Quy tắc compile

1. Chỉ compile **một** translation unit model: `deepfilternet_u55_vela_model_data.c`.
2. Không compile `deepfilternet_u55_pre_vela_model_data.c`.
3. Không include/compile golden headers trong firmware release; chúng dành cho test và rất lớn.
4. Dùng model post-Vela, không dùng pre-Vela hoặc SavedModel trong firmware.
5. Phân vùng tensor arena, audio ring buffer và DSP scratch theo linker map/buffer thực tế, không suy ra chỉ từ Vela SRAM estimate.

## 7. Tensor và recurrent-state contract

Tất cả input/output neural là INT8.

| Tensor output | Shape | Quantization |
|---|---:|---|
| `gain_bins` | `[1, 257]` | scale `0,0038486531`, zero-point `-128` |
| `df_coefficients` | `[1, 5, 144, 2]` | scale `0,0051296786`, zero-point `27` |
| `alpha` | `[1, 1]` | scale `0,00390625`, zero-point `-128` |

State/cache:

- Raw INT8 copy được: `enc_state_0`, `enc_state_1`, `enc_state_2`, `df_state_1`, `erb_conv_cache`, `complex_conv_cache`.
- `df_state_0` phải **requantize** theo scale/zero-point contract, không raw-copy.
- Reset toàn bộ state khi boot, đổi stream, mất frame hoặc discontinuity.

## 8. DSP, Helium/MVE và board gate

Các phần CPU chủ yếu là DSP và phải ưu tiên CMSIS-DSP/Helium MVE:

- FFT/IFFT 512.
- sqrt-Hann và overlap-add/WOLA.
- Tính ERB log-power feature.
- Complex gain multiplication.
- Causal complex 5-tap DF MAC.
- Alpha blend và quantize/dequantize boundary.

Các gate bắt buộc trên board:

1. Scalar DSP và Helium/MVE golden output chênh không quá **1 LSB Q15**.
2. Log riêng: `fft_us`, `npu_us`, `df_us`, `wola_us`, `e2e_us`.
3. `e2e_us ≤ 16.000` cho mỗi hop 256 sample.
4. Dùng linker map thật để xác nhận `.text + .rodata + model + DSP constants ≤ 1,8 MiB` và SRAM trong 1,5 MiB.
5. Xác nhận model post-Vela có `CPU operators = 0`, không Flex và không float fallback.

Host parity hiện chỉ xác minh sequence model/frame core trong 10 frame đầu. Chênh audio FP32–INT8 host không phải phép thử 1-LSB MVE; board scalar-vs-MVE parity vẫn cần chạy.

## 9. Evaluation: smoke, shard và release

### Full profile hiện tại

- Train FP32 → QAT → export/Vela/golden.
- Chạy smoke 512 utterance.
- Có package firmware đầy đủ.
- Không được coi là final test dù metrics trung bình đã pass.

Smoke tốn ~10.178 s (~2 giờ 50 phút). Bottleneck là inference FP32 sequence (~19–20 s/sample), không phải TFLite (~0,25–0,29 s/sample), PESQ/STOI hay data loader. Vì vậy không nên ghép full test 3.750 sample vào cùng Kaggle session train.

### Final profile đánh giá resumable

1. Freeze checkpoint QAT champion và ghi SHA-256 model/checkpoint manifest.
2. Chia 3.750 test pair thành 15 shard × 250.
3. Mỗi shard ghi checkpoint partial mỗi 16 pair và resume không tạo duplicate.
4. Sau đủ 15 shard, `merge` kiểm tra index `0..3749`, không overlap, cùng hash model/checkpoint.
5. Chỉ sau merge mới tạo release metrics:
   - `metrics_per_sample.csv`
   - `metrics_summary.json`
   - `parity_report.json`
   - `package_audit.json`
   - `integration_manifest.json`

## 10. Điểm cần sửa/lưu ý trước run sau

- `metrics_smoke_summary.json` đang ghi scope `smoke_128_not_release_gate` nhưng run thực tế có 512 item. Đây là lỗi metadata/traceability, không làm đổi metric; cần đổi label cho đúng.
- Có `evaluation_shards/metrics_shard_000.partial.csv` sinh từ smoke. Nó không phải shard final và chưa có manifest completed; không được đưa vào merge/report final.
- Cảnh báo QAT speed ratio vượt ngưỡng là cảnh báo thời gian train, không phải lý do tắt QAT hay lỗi export.
- Warnings XNNPACK, LiteRT deprecation và exporter “ignored output_format/drop_control_dependency” là thông báo host/exporter; Vela report `CPU operators = 0` mới là tiêu chí deployment.
- Nếu firmware vượt flash/link-map hoặc `e2e_us` vượt 16 ms, mới mở compact fallback. Không thay đổi frontend 16 kHz, 32 ERB, DF 5×144 hoặc alpha trước khi có bằng chứng board gate fail.

## 11. Quyết định handoff

Có thể dùng package hiện tại để bắt đầu tích hợp firmware và đo U55/M55M1. Không được gọi model là release cuối cho đến khi hoàn thành full frozen-test 3.750 sample, board timing, scalar/MVE parity và linker-map thực tế.
