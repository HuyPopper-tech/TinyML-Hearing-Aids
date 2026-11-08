# Phân tích ngân sách pipeline dưới 1 ms — 2026-11-08

Phạm vi: phân tích firmware tại `6bf6b37`, đối chiếu evidence model cũ và kiểm
chứng số học cho phương án tiếp theo. **Chưa sửa runtime, DMA, model hoặc build
profile; chưa đo hay flash board trong đợt phân tích này.** Light route, không agent.

## Kết luận từ bằng chứng hiện có

Có cơ sở để tối ưu phần CPU trước khi thay model. Log R2 đã cung cấp có
`last_invoke_us=474`, nhưng toàn service mất 1079 µs và capture tới kiểm tra
deadline mất 1088 µs. Phần ngoài Invoke chiếm khoảng 605 µs. Invoke bao gồm TFLM,
driver và cache, không phải phép đo riêng NPU. Cần giảm ít nhất 89 µs để đạt
<1000 µs; mục tiêu ≤900 µs cần giảm ít nhất 188 µs từ lần chạy đó.

Tuy nhiên, log mới nhất có `frame=0`, `capture_last_seq=0`, `dac_underrun=1`,
tất cả `last_*_us=0`: chưa có hop được xử lý. Nó không đo hiệu quả của guarded
FP32 quantizer vừa thêm. `mve_test_us=6388` là self-test lúc boot trước stream,
không phải thời gian mỗi hop. Không quy lỗi cho breakpoint sau Continue: người
dùng xác nhận không dừng thêm. Các log người dùng chưa kèm hash BIN.

## Model cũ đã đạt gì?

- Báo cáo có sẵn trong repo
  `docs/DEEPFIR_M55M1_SESSION_SUMMARY_2026-09-24.md` lưu log 2000 hop:
  `total_mean_us=906`, `total_max_us=911`, `e2e_mean_us=916`, `e2e_max_us=927`,
  `late_1ms=0`, `dmic_overrun=0`, nhưng `dac_underrun=1`. Đây là evidence lịch sử
  người dùng cung cấp, chưa phải nghiệm thu stream liên tục không lỗi. Model
  trong báo cáo là compiled SHA bắt đầu `40dcd47bc`, không phải R2.
- Package ngay trước R2 được lưu trong
  `diagnostics/r2_custom_integration_20261108_105000/previous_active_package/`:
  compiled 622128 byte, SHA256
  `9e86a0b89bef324e9061e74646ceb012e14de76b3bdaf40c546cab6a3709e079`.
  [Tài liệu tích hợp cũ](COMPLEX3_CONV5_INTEGRATION_2026-10-23.md) xác nhận
  output trực tiếp 128 taps và hai state; h/c feedback có thể copy chính xác.

R2 trả gain65/phase65, nên thêm sin/cos, Hermitian, IDFT128, roll/taper; cả hai
state phải requantize, không được memcpy. Đây là khác biệt cấu trúc quan trọng.
Không suy ra mọi chênh lệch thời gian đều do model: profile, instrumentation,
startup, cold/warm cache và scheduler giữa các lần đo chưa được ghép cặp.
Không cộng các stage maxima của 2000 hop thành một hop giả định.

## Phân rã lần chạy R2 bị deadline

| Stage | Thời gian người dùng báo, µs | Nhận định |
|---|---:|---|
| Frontend | 317 | Cần split pre-FFT / FFT / post-FFT của image mới |
| TFLM Invoke | 474 | Chưa có last NPU để tách driver/cache |
| Feedback h/c | 101 | Ưu tiên cao: vòng generic INT64, hai lane không identity |
| Gain/phase → taps | 79 | Chi phí R2 mới; FFT inverse đã dùng CMSIS MVE |
| FIR/crossfade | 39 | Đã có kernel MVE; gần mức 40 µs lịch sử |
| PCM | 5 | Không phải ưu tiên đầu |
| Phần còn lại trong service | ≈64 | 1079 trừ các stage; cần bridge/other split |
| Ngoài service tới deadline | ≈9 | Chênh 1088−1079; không phải toàn bộ DMA jitter |

Source hiện hành bật CPU 220 MHz, NPU0 và D-cache; model compiled chỉ có một
Ethos-U subgraph, không CPU op, một interpreter. Clock init người dùng báo
`core_hz=220000000`. Chưa có phép đo riêng tần số/PMU NPU trên board.
Vela 0,726957 ms là ước tính controller tại 220 MHz, không bao gồm frontend,
feedback, reconstruction, FIR hoặc transport. Không có bằng chứng cần train,
PTQ hay compile Vela lại để giải quyết phần CPU này.

## Ưu tiên 0: xác minh phase FIFO / DMA lúc startup

Source facts: `I2S_Open()` trong StdDriver đặt TX threshold 8 word; driver DAC
hiện hành không override. I2S có TX FIFO 16 mức theo
[datasheet Nuvoton, phần I2S](https://www.nuvoton.com/export/resource-files/en-us--DS_M55M1_Series_EN_Rev1.00.pdf).
Descriptor hiện chứa 16 word, mỗi word đóng gói một stereo frame PCM16. Transport
prepare DMIC, start DAC/I2S TX, rồi enable DMIC. Trong
`drivers/tlv320dac3100/tlv320dac3100.c`, TD tăng `completed_blocks`; từ TD thứ hai,
slot tiếp theo rỗng có thể làm stop vì underrun.

DMA TD báo dữ liệu đã chuyển vào FIFO, không chứng minh frame cuối đã ra dây
I2S. Vì vậy cần kiểm tra việc dùng TD làm epoch playback slot. FIFO prefetch có
thể làm hạn submit sớm hơn dự kiến, ngay cả khi compute dưới 1 ms. **Đây là giả
thuyết có căn cứ source, chưa phải root cause đã xác nhận.** Chưa xác minh đủ
quy tắc request PDMA theo TXTH và phase thực tế từ TRM/target.

[Mô hình minh họa](verification/2026-11-08-pipeline-budget/fifo_model.json)
giả sử FIFO rỗng, refill từng word khi occupancy ≤ threshold, không latency DMA,
IRQ hay shift register. Threshold 8 cho TD1/TD2 tại 437,5/1437,5 µs; threshold 0
cho 937,5/1937,5 µs. Với capture đầu tại 1000 µs, cửa sổ tới TD2 tương ứng chỉ
437,5 hoặc 937,5 µs. Các số này **không phải timing board**, chỉ minh họa tại sao
không thể coi TD là ranh giới serial playback mà không kiểm tra FIFO.

Đo tiếp bằng trace tĩnh opt-in, format sau stop: epoch enable TX/DMIC, RX TD đầu
và capture publish, bốn TX TD đầu, TXCNT/TXTH, descriptor/count, IRQ pending/mask,
cờ underrun I2S thực. Host test phải mô hình FIFO đi trước playback. Chỉ sau khi
xác nhận mới chỉnh threshold/start phase và ownership; không bỏ qua underrun,
thêm silence hop/queue hoặc nới deadline để làm log đạt.

## Ưu tiên 1: feedback MVE chính xác từng mã

`deepfir_model_adapter.c::carry_state()` hiện gọi hàm generic cho 400 phần tử;
Thumb disassembly có INT64 multiply, chưa có `VQRDMULH` trong commit state.
Chuyển kiểm tra qparams cố định ra ngoài vòng lặp, giữ hàm generic làm fallback.

Với `a=abs(int32(code))`, zero point đều 0, đặt
`Q(u,M)=floor((u*M + 2^30)/2^31)` trên lane không âm:

- h: `Q(a,2147371134)`, tương đương shift 31 của contract.
- c: `Q(2*a,1075624845)`, tương đương shift 30.
- Khôi phục dấu rồi saturate INT16. Không đổi model order/coordinate.

Đây là ứng viên `vqrdmulhq_s32` cho bốn code/lần, load widen INT16→INT32. Xử lý
magnitude trước giữ ties-away cho cả số âm; không dùng trực tiếp rounding trên
lane âm. `a≤32768`, `2*a≤65536`, không gặp saturation INT32 của phép nhân này.
[Arm MVE intrinsics](https://arm-software.github.io/acle/mve_intrinsics/mve.html)
định nghĩa intrinsic/instruction tương ứng; cần kiểm tra lại disassembly và chạy
exhaustive self-test trên target khi triển khai.

Đã compile/chạy C thật so công thức mô phỏng Q31 với hàm feedback hiện tại,
toàn miền INT16 của cả hai lane: **131072/131072 khớp, max code error 0**.
h có 46449 code khác identity, c có 64963: không được copy state như model cũ.
[Proof và lệnh](verification/2026-11-08-pipeline-budget/proof.json) lưu hash nguồn,
contract và kết quả. Đây là proof số học native, **không chạy instruction MVE và
không đo tốc độ**. Không áp fast path cho qparams khác điều kiện đã chứng minh.

## Ưu tiên 2–4: frontend, reconstruction, phần còn lại

1. Đo lại frontend sau guarded FP32 quantizer của `6bf6b37`. Chỉ vectorize phần
   magnitude/interleave/quantization thực sự còn chiếm thời gian. Giữ thứ tự
   tính, guard half-code và double fallback cho lane mơ hồ; không thay bằng
   float→Q15 khác rounding. FFT256 complex và log/exp đã có CMSIS MVE. Real-FFT
   thử trước đây đổi 642552 feature code, max 197 code, đã bị loại.
2. Trong `deepfir_reconstruction.c`, đo sin/cos tách khỏi IFFT; thử vector hóa
   dequant gain/phase, nội suy bảng CMSIS, Hermitian và roll/taper cố định. Giữ
   radian→degree đúng API, endpoint DC/Nyquist, IDFT128 normalization, roll+64.
   Không thay model head, minimum phase, gain hoặc nới tolerance. Không tạo lại
   bảng lớn/hàm DSP song song nếu helper hiện có đã đủ.
3. FIR 39 µs và PCM 5 µs có lợi ích tuyệt đối nhỏ, làm sau feedback/frontend.
   Nếu vẫn thiếu margin, thử đặt có chọn lọc hot CPU code trong ITCM và đo A/B;
   không suy từ kích thước SRAM/flash ra tốc độ. Chưa có bằng chứng cần bỏ cache
   hooks hay thay clock. Không dùng `-Ofast` để né kiểm tra NaN/rounding.

## Ngân sách thử nghiệm và tiêu chí nghiệm thu

Đây là **mục tiêu engineering**, chưa phải thời gian đo hay cam kết sẽ đạt:

| Stage | Mục tiêu µs |
|---|---:|
| Frontend | 250 |
| Invoke, giữ mốc cũ để lập ngân sách | 474 |
| Feedback | 25 |
| Reconstruction | 45 |
| FIR | 40 |
| PCM | 5 |
| Bridge/other/dispatch | 61 |
| Capture → publish tổng | **900** |

Nếu chỉ carry 101→25 µs, mốc 1088 vẫn khoảng 1012 µs; cần tối ưu thêm.
Các mục tiêu cần đo cùng image/hop, bao gồm cold-start, IRQ/cache/publish và jitter.
Schedule b+2 vẫn chỉ có budget một hop 1 ms; không thêm queue hoặc đổi hop16/16kHz.

Scope triển khai tiếp: trace/startup trong driver transport/DAC và probe tests;
feedback ở adapter và numeric/MVE tests; frontend/reconstruction ở service hiện
có. Adapter tiếp tục sở hữu h/c/context/FIR history/taps, feature state sở hữu
analysis history; pipeline sở hữu working buffers, driver sở hữu DMA descriptors.
Không thêm interpreter/history owner hoặc thay app→services→drivers→board→libs.

- Integer feedback/feature: exact với reference, toàn INT16 và các guard edge;
  target self-test MVE và disassembly scoped đều đạt trước timing claim.
- Float DSP: giữ tolerance và replay reference hiện có; không sửa expected/golden.
  Chạy lại 26441-hop DSP comparison; compiled controller parity phải zero code
  mismatch riêng, không thay bằng mock hay DSP-only replay.
- Host regression, Debug/Release/Probe build/link và map không trùng runtime/model.
- Board: cùng BIN/hash/flags, 60000 hop liên tục, sequence/slot b+2 đúng,
  overrun/underrun/deadline miss/restart đều 0; capture→publish max ≤900 µs là
  mục tiêu margin, mọi hop bắt buộc <1000 µs. Ghi stage/IRQ max, cold/warm starts.
- Không suy acoustic latency hoặc chất lượng tiếng Việt từ compute timing.

Trước thay DMA/flash phải nêu pin/recovery: custom DMIC PA1/PA2, I2S1
PA8/PA10/PA11/PB0, I2C PA4/PA5, UART PC4/PC5; SWD PF0/PF1. Probe không có
nRESET: không coi lệnh reset OpenOCD là power reset; cần đường manual reset/
power-cycle. Đợt này không thay các cấu hình đó và chưa thao tác board.

## Trạng thái kiểm chứng của đợt phân tích

| Mục | Trạng thái | Giới hạn |
|---|---|---|
| Đối chiếu source/model cũ và R2 | PASS | Identity và ABI khác nhau, không phải matched timing A/B |
| Công thức feedback Q31 native | PASS | 131072 code, không chạy MVE |
| Mô hình FIFO minh họa | PASS | Self-check giả định; không chứng minh request/phase thực |
| Host regression | PASS | 128 tests, không skip; [log](verification/2026-11-08-pipeline-budget/tests.log) |
| Runtime MVE feedback mới | NOT_RUN | Chưa triển khai |
| Debug/Release build mới | NOT_RUN | Không đổi runtime, dùng evidence build revision trước |
| Compiled parity | BLOCKED | Chưa có lần thực thi backend/board được xác nhận |
| Startup FIFO phase / full-hop ≤900 µs | NOT_RUN | Cần trace board mới |
| Physical audio/latency/power | NOT_RUN | Không đo, không flash |

Evidence đợt này: [README](verification/2026-11-08-pipeline-budget/README.md).
`deployment_approved` không thay đổi.
