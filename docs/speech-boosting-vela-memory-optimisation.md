# Tối ưu Vela và bộ nhớ cho Speech Boosting Chunk64

## 1. Mục tiêu và phạm vi

Tài liệu này xác định cách tối ưu **deployment** của Speech Boosting Chunk64
trên M55M1/Ethos-U55 mà không thay đổi topology, chất lượng model, sample rate,
chunk ABI hoặc train lại model.

Mục tiêu theo thứ tự ưu tiên:

1. Giữ tất cả 197 operator trên Ethos-U55, không có CPU fallback.
2. Giảm `Ethos-U55 neural network invocation` và jitter của nó.
3. Giảm thời gian CPU quanh invoke, đặc biệt là copy/requantize 20 state tensor.
4. Bảo toàn realtime 16 kHz, chunk 64 mẫu, look-ahead 16 mẫu.
5. Dùng SRAM0/1 có kiểm soát; DMA và CPU-only data không được cạnh tranh vô ích
   với model/U55 arena.

Phạm vi này không bao gồm thay đổi model architecture, retraining, sample rate,
chunk size, state ABI, hoặc thiết kế paging model tự phát triển.

## 2. Contract deployment cố định

| Thuộc tính | Giá trị |
|---|---:|
| Sample rate | 16,000 Hz |
| Chunk hiện tại | 64 PCM16 samples = 4 ms |
| Look-ahead | 16 samples = 1 ms logic |
| Input/output tensors | 21 / 21 |
| Persistent state tensors | 20 |
| Persistent stream state | 5,632 bytes |
| Dtype stream | INT8 |
| Ethos-U55 operators | 197 |
| CPU operators | 0 |
| Raw INT8 regression | 100 chunks golden trace |

Một output chunk chỉ được phát hành sau khi 16 mẫu đầu của chunk kế tiếp đã có.
Vì DMIC hiện bàn giao block 64 mẫu, latency `capture_to_model_output_ready`
gồm gần một block 4 ms chờ look-ahead cộng với thời gian xử lý. Nó không đồng
nghĩa với riêng thời gian U55 invoke.

## 3. Số đo đã xác minh

### 3.1 `aprom_x100`

Model giữ trong APROM; tensor arena ở SRAM. Board đo được invocation khoảng
6.13 ms (maximum 6.153 ms), chỉ xử lý xấp xỉ 166 invoke/s và phát sinh capture
gaps/DMIC overruns liên tục. Profile này vượt ngân sách 4 ms của chunk.

### 3.2 `sram01_x100`

FlatBuffer Vela 959,920 bytes được đặt ở SRAM0/1, tensor arena 64 KiB ngay sau
model. Board đã đo 60 cửa sổ một giây:

| Chỉ số | Kết quả |
|---|---:|
| U55 invoke min / mean / max | 3.150 / 3.151–3.152 / 3.164 ms |
| Look-ahead + PCM quantization | 73–80 us |
| Input state tensor copy | 43–48 us |
| Output state tensor copy + requantize | 343–389 us |
| Output dequantization | 2–9 us |
| Capture gaps / DMIC overruns / invoke errors | 0 / 0 / 0 |
| Invocations | 14,999; một pre-roll chunk nên thấp hơn 15,000 capture đúng một lần |

Kết quả cho thấy bottleneck chính của `aprom_x100` là access constant/model từ
APROM. Copy state không phải bottleneck lớn nhất, nhưng output state copy chiếm
khoảng 0.35 ms và là phần CPU lớn nhất sau invoke.

## 4. Phân vùng vật lý của M55M1

| Vùng | Dung lượng | Mục đích trong firmware |
|---|---:|---|
| SRAM0 + SRAM1 | 1 MiB | Model Vela và TFLM tensor arena U55-visible |
| SRAM2 | 320 KiB | DMA descriptors/buffers non-cacheable; CPU buffer lớn khi không U55-visible |
| DTCM | 128 KiB | Stack và CPU-only foreground data có latency xác định |
| ITCM | 64 KiB | Code hot được chọn rõ ràng, không phải vùng model |
| APROM | 2 MiB | Firmware code, hằng số, load image model |

Ethos-U55 deployment hiện dùng SRAM0/1 cho model và arena. Không chuyển
FlatBuffer, TFLM tensor arena, hoặc tensor mà U55 nhận trực tiếp sang DTCM hay
SRAM2. Các region Vela, linker, MPU/cache và driver phải khớp nhau; Vela chỉ
mô tả/schedule logical memory, không tự cấu hình phần cứng firmware.
[Tài liệu Arm CMSIS-Ethos-U](https://github.com/ARM-software/CMSIS-Ethos-U/blob/main/documentation/doxygen/vela/src/main.md)

### 4.1 Layout hiện tại của `sram01_x100`

```text
0x20100000  g_speech_boosting_u55_vela_model      959,920 B
0x201EA5B0  g_tensor_arena                         65,536 B
             generic RW + heap trong SRAM0/1
0x20200000  kết thúc SRAM0/1

0x2000xxxx  DTCM: stack, audio working set 6,048 B,
             UART TX ring 2,048 B, TFLM interpreter/resolver storage

0x20230000  SRAM2 non-cacheable: DMIC/I2S PDMA buffers và descriptors
```

`sram01_x100` giữ một load image trong APROM để startup nạp model vào SRAM; tại
runtime, TFLM và U55 dùng con trỏ model tại SRAM0/1. D-cache của model SRAM
được clean một lần trước `AllocateTensors()`/invoke đầu tiên.

### 4.2 State copy đúng vùng

`speech_boosting_stream_state_t` là CPU-owned state 5,632 bytes. Mỗi invoke:

1. CPU copy state vào input tensor trong TFLM arena SRAM0/1.
2. U55 invoke chỉ làm việc với model/arena U55-visible.
3. CPU đọc output state từ arena, requantize semantic mapping và ghi lại state.

Do U55 không nhận con trỏ tới `speech_boosting_stream_state_t`, state container,
capture/rolling/playback buffer và UART ring đặt DTCM là hợp lệ. Động tác này
giảm áp lực SRAM0/1 nhưng không loại được copy vào/ra TFLM arena; vì thế không
kỳ vọng làm U55 invocation ngắn đi đáng kể.

## 5. Vela 5.1: điều gì thực sự ảnh hưởng latency

Vela compile ahead-of-time TFLite cho Ethos-U. Ba field trong `[Memory_Mode]`
quyết định vai trò logical của bộ nhớ:

| Field | Nội dung |
|---|---|
| `const_mem_area` | command stream, encoded weights, scales, constants/model read-only |
| `arena_mem_area` | feature maps, IO, intermediate tensor, runtime scratch read-write |
| `cache_mem_area` | staging/fast scratch khi là vùng vật lý khác arena |

`arena_cache_size` là ngân sách schedule tính bằng **bytes** khi dùng
`--optimise Performance`. Nó không đồng nghĩa trực tiếp với
`SPEECH_BOOSTING_TENSOR_ARENA_BYTES`: firmware vẫn phải cấp phát đủ TFLM arena
và linker vẫn phải đặt đúng các vùng. `--optimise Size` bỏ qua cache budget để
giảm peak SRAM, đổi lại có thể tăng traffic và latency.
[Vela options và memory mapping](https://arm-software.github.io/CMSIS-Ethos-U/main/vela/index.html)

`Axi0`, `Axi1`, `Sram`, `OffChipFlash` trong Vela là tên logical. Trong project:

```ini
[System_Config.Ethos_U55_M55M1_FLASH_X0.300]
axi0_port=Sram
axi1_port=OffChipFlash

[Memory_Mode.Shared_1024K]
const_mem_area=Axi1
arena_mem_area=Axi0
cache_mem_area=Axi0

[Memory_Mode.M55M1_sram01_x100]
const_mem_area=Axi0
arena_mem_area=Axi0
cache_mem_area=Axi0
```

`OffChipFlash` là nhãn bandwidth của Vela, không đổi APROM vật lý thành flash
ngoài và không tăng clock APROM của MCU. M55M1 core/NPU chạy tối đa 220 MHz;
firmware đã cấu hình mức đó.

## 6. Chiến lược tối ưu theo tầng

### Tầng A — giữ model/ABI y nguyên, ưu tiên áp dụng

1. **Giữ `sram01_x100` làm baseline production.** Đây là profile duy nhất đã
   chứng minh max invoke dưới 3.5 ms và không có capture gap trong 60 giây.
2. **Giữ `--optimise Performance` và `tensor-allocator HillClimb`.** Không đổi
   sang `Size` khi mục tiêu đang là latency thấp nhất.
3. **Giữ model, arena và direct-U55 tensors trong SRAM0/1.** Không tách một
   phần FlatBuffer sang APROM: standard TFLM/Ethos-U delegate dùng FlatBuffer
   liên tục; split/paging cần runtime/driver riêng và tạo jitter DMA.
4. **Chuyển CPU-only RW sang DTCM.** Giữ DMA target ở SRAM2 non-cacheable.
5. **Cache discipline:** model data writable chỉ khi startup copy; clean D-cache
   một lần trước U55 dùng. DMA buffer dùng non-cacheable region, tránh cache
   clean trong realtime path.
6. **Không format UART trong ISR.** UART log chỉ foreground; ISR chỉ chuyển
   descriptor/event.

### Tầng B — tối ưu CPU state transaction, không đổi model output

Output-state stage hiện gồm 20 semantic mappings và requantization per byte.
Thứ tự thử nghiệm:

1. Tách đo `audio-output memcpy`, `state-output requantize`, `state-input
   memcpy` để biết phần thực sự chiếm 0.35 ms.
2. Giữ mapping theo manifest, không copy theo ordinal tensor.
3. Nếu float `round` là bottleneck, tạo fixed-point requantization tương đương
   theo từng cặp scale/zero-point. Mọi byte của 100 golden chunks, state cuối
   mỗi chunk và PCM16 output phải byte-identical với baseline trước khi dùng.
4. Chỉ sau parity pass mới thay thế loop float. Không gộp state output/input,
   không bỏ zero-point reset, và không thay đổi state quantization contract.

Tầng này có thể giảm CPU stage và jitter, nhưng U55 invocation ~3.15 ms sẽ chỉ
thay đổi gián tiếp.

### Tầng C — Vela compile experiment có kiểm soát

Mỗi experiment phải sinh package/profile riêng; không ghi đè baseline.

| Biến số | Giá trị thử | Ý nghĩa |
|---|---|---|
| `arena_cache_size` | 64, 128, 256, 512 KiB, 1 MiB | Trade-off on-chip reuse và memory traffic |
| `tensor-allocator` | HillClimb, Greedy, LinearAlloc | Layout intermediate tensor / peak arena |
| `max-block-dependency` | 0, 1, 2, 3 | Trade-off interrupt/dependency latency và throughput |
| `--optimise` | Performance trước; Size chỉ benchmark memory | Performance ưu tiên latency; Size ưu tiên SRAM |
| const placement | APROM vs SRAM0/1 | Đã xác minh SRAM0/1 là quyết định lớn nhất |

Không thay đổi nhiều biến trong một profile. Tên profile phải phản ánh đúng biến
được thay đổi, ví dụ `sram01_perf_cache256k_hillclimb`.

Không dùng `--debug-force-legacy-core`: raw model hiện tại sinh CPU operator
với lựa chọn đó, vi phạm contract U55-only.

## 7. Quy trình tạo và kiểm chứng profile

### 7.1 Baseline bất biến

Input là:

```text
models/speech_boosting/generated/speech_boosting_qat_raw_int8.tflite
```

`aprom_x030` phải tái tạo SHA-256:

```text
c0c2a641f2b317925003632b5eabcf1f4b1f4f9018f3ed5386e76f105be1f6c7
```

Lệnh generator hiện tại:

```powershell
python tools/generate_speech_boosting_vela_profiles.py sram01_x100
```

Nó phải lưu trong profile package: Vela TFLite, C/C++ model array, manifest,
Vela command/report, contracts, tensor map, quantization metadata và golden
trace.

### 7.2 Lệnh thử một profile mới

```powershell
python -m ethosu.vela `
  models/speech_boosting/generated/speech_boosting_qat_raw_int8.tflite `
  --config models/speech_boosting/integration/vela_m55m1_profiles.ini `
  --system-config M55M1_sram01_x100 `
  --memory-mode M55M1_sram01_x100 `
  --accelerator-config ethos-u55-256 `
  --optimise Performance `
  --tensor-allocator HillClimb `
  --arena-cache-size 262144 `
  --verbose-config --verbose-cycle-estimate --show-cpu-operations `
  --output-dir build/vela_experiments/sram01_perf_cache256k
```

Sau thử nghiệm, không chọn output thủ công. Cập nhật generator với profile mới,
generate package đầy đủ, rồi để CMake chọn profile qua
`M55M1_SPEECH_BOOSTING_MEMORY_PROFILE`.

### 7.3 Gates bắt buộc

Host:

1. SHA/manifest/ordinal/shape/INT8 quantization contract pass.
2. 20 mappings, zero-state byte values và 100 raw-INT8 golden chunks pass.
3. Vela report: 197 NPU operators, 0 CPU operators.
4. `python -m unittest discover -s tests -v` và `git diff --check` pass.

Firmware/map:

1. `U55_MODEL_RAM` và `U55_TENSOR_ARENA` nằm hoàn toàn trong
   `0x20100000–0x201FFFFF`.
2. Model/arena/RW/heap không vượt linker assert SRAM0/1.
3. DTCM chỉ chứa CPU-only data; DMA region vẫn SRAM2 non-cacheable.
4. U55 model SRAM được clean D-cache trước first allocation/invoke.

Board, sau warm-up 50 chunks và tối thiểu 60 giây:

1. maximum U55 invoke <= 3.5 ms ở **mọi** cửa sổ một giây;
2. 0 CPU operators, 0 invoke error;
3. 0 capture gap, 0 DMIC overrun, 0 DMA error;
4. log memory range, stage min/mean/max và end-to-end khi DAC/I2S được bật;
5. invocation count/interrupt count tăng đồng nhất; PMU cycle/MAC counters có
   dữ liệu hợp lệ nếu đánh giá utilization.

## 8. Cách đọc kết quả và quyết định

Ưu tiên theo dữ liệu board, không theo cycle estimate duy nhất của Vela.

- Nếu một profile giảm model SRAM nhưng max invoke vượt 3.5 ms: không chọn.
- Nếu `--optimise Size` giảm arena nhưng tăng invoke/gap: chỉ lưu báo cáo,
  không thay production profile.
- Nếu cache budget nhỏ hơn giữ max invoke <= 3.5 ms và map headroom lớn hơn:
  chọn profile nhỏ nhất đạt toàn bộ gate.
- Nếu state fixed-point giữ golden parity và giảm output-state stage: chọn nó
  độc lập với profile Vela vì nó là CPU transaction optimization.
- Nếu không có profile nào cải thiện `sram01_x100`, giữ `sram01_x100`; tối ưu
  tiếp theo phải là model redesign/retraining, nằm ngoài tài liệu này.

## 9. Checklist trước khi đổi production profile

- [ ] Profile package độc lập, command/reports/manifest đầy đủ.
- [ ] Vela SHA và raw model provenance được ghi lại.
- [ ] 197 U55 / 0 CPU operators.
- [ ] Contract 21 input/21 output, 20 state mapping pass.
- [ ] Raw-INT8 100 chunks pass.
- [ ] Map/linker/cache validation pass.
- [ ] Board warm-up + >=60 s đo pass.
- [ ] UART log không bị truncate; memory fields được tách dòng nếu cần.
- [ ] DAC/I2S end-to-end được đo lại khi phần cứng khả dụng.
