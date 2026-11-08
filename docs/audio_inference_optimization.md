# Tối ưu audio/inference liên tục — 2026-11-08

Đã triển khai hướng 1 được duyệt trên luồng custom PCB, dùng chung cho Debug và
Release. Model R2, 16 kHz/hop 16, qparams, encoded state và lịch b+2 giữ đúng hợp
đồng. Chưa flash và chưa đo tốc độ hoặc điện năng trên bo.

Evidence và bảng nghiệm thu: [audio-optimization](verification/2026-11-08-audio-optimization/README.md).
Thiết kế/tiêu chí đã duyệt: [design](superpowers/specs/2026-11-08-audio-inference-optimization-design.md).

## Thay đổi trong luồng chính

- Adapter chuẩn hóa PCM16 một lần, cho frontend và FIR mượn cùng hop float bất biến.
  Frontend ghi thẳng nửa current của context; bỏ buffer feature riêng. API PCM16
  vẫn có cho self-test/caller trực tiếp. Adapter sở hữu context/h/c/FIR, frontend
  sở hữu analysis history; không thêm interpreter hoặc pipeline.
- Frontend bỏ memset phổ đã được ghi đầy đủ. Quantizer tính reciprocal một lần
  mỗi hop, dùng phép nhân và truncation ties-away; quay lại phép chia cũ trong dải
  sát half-code và trường hợp cực trị. Differential 11.570.644 trường hợp không
  đổi code. Feedback dùng dịch phải trên magnitude không âm, giữ int64, ties-away
  và saturation; 15.400.960 trường hợp không đổi kết quả.
- Reconstruction dùng `arm_sin_cos_f32` có sẵn, đổi radian sang degree rõ ràng;
  phase ngoài dải an toàn dùng libm. Không đổi endpoints, Hermitian, IFFT/roll/taper.
  Bỏ khởi tạo phổ thừa và một lần copy taps đầu hop MVE; vẫn giữ trạng thái cuối
  hop bằng nhau và kiểm tra MVE/scalar lúc boot Debug.
- Debug ghi tối đa 16 breadcrumb thô cho hop gần nhất. Không format/enqueue ở
  stage mark; lỗi dừng DMA rồi mới in `[HOP_STAGE]`, kèm cycle/status/overflow.
  Init log và telemetry Debug định kỳ sau publish vẫn có. Release thường tắt UART.
- Timing có thêm PCM; fault report có capture/process/publish cycle và playback
  slot. Probe Release có sẵn được nâng lên 60.000 hop, dừng ở lỗi đầu tiên, in tổng
  hợp và `[AUDIO_RELEASE_RESULT]` sau khi dừng. Luồng thường vẫn full-reset/restart
  đồng bộ; không nới deadline hoặc phát kết quả dở/stale để duy trì âm thanh.

## Kết quả số và lựa chọn giữ lại

127/127 host tests đạt. CMSIS scalar thực chạy FFT và sweep toàn bộ 65.536 phase
code ở gain lớn nhất; sai số taps tối đa `2.7351718e-7`, trong tolerance sẵn có
`atol=2e-5, rtol=2e-5`.

So sánh reconstruction/FIR cũ và mới qua cả 26.441 hop gain/phase canonical, với
PCM tổng hợp xác định và history liên tục: taps tối đa `1.1920929e-7`, output float
tối đa `2.9802322e-7`. Có 90 và 46 mẫu PCM khác, tối đa 1 code, trong tổng 423.056
mẫu. Tolerance float không được nới; quy tắc rounding/clipping PCM cho cùng float
vẫn chính xác. Đây là DSP replay với PCM tổng hợp, không chạy controller, không
thực thi MVE và không chứng minh chất lượng âm thanh hay compiled parity.

Real FFT256 thử bằng CMSIS scalar làm đổi 642.552/3.170.304 feature code, tối đa
197 code, vượt tiêu chí 1 code. Đã loại; complex FFT vẫn là active backend.

Release RW_RAM giảm từ 77.936 xuống 77.168 byte; adapter giảm 6.048 xuống 5.280
byte. Debug RW_RAM 78.456 xuống 77.920 byte, đã gồm trace 192 byte. Release BIN
tăng 2.592 byte (612.800 → 615.392), gồm chi phí kernel/bảng sin-cos và thay đổi mã.
Map là dữ liệu link, không phải phép đo high-water hoặc thời gian thực thi.

## DMA, cache và lifetime đã rà soát

DMIC vẫn LPPDMA0 channel 0, I2S1 vẫn PDMA0 channel 0; uncached DMA 320 byte,
masked ownership handoff, barriers và health/deadline checks được giữ nguyên.
Immutable model đăng ký/clean trước `AllocateTensors`. Bridge cung cấp `base_sizes`
từ tensor byte lengths; cache hook hỗ trợ range align và bỏ qua immutable range,
nhưng vẫn có fallback whole-cache nếu thiếu range. Chưa đo request thực trên bo.

Giữ memcpy vào ba input TFLM: storage/lifetime và lợi ích của direct binding chưa
được chứng minh để đổi coupling. Không thay hooks, DMA, pin hoặc clock chỉ từ
suy đoán. `npu_us` là khoảng wall time giữa callbacks runtime, có cả chờ IRQ;
không phải số cycle phần cứng NPU thuần. Cache clean trước begin và invalidate sau
end nằm trong Invoke; không cộng Invoke và NPU như hai khoảng độc lập.

## Build và thực hiện tiếp trên bo

Hai đầu ra thường đã được khôi phục, replay/capture/probe đều OFF; clangd dùng
Debug. Binary hiện có ở `build-debug/M55M1_GestureAI.bin` và
`build/M55M1_GestureAI.bin`. Hash và map summary nằm trong evidence.

```bat
rtk proxy cmd /c build.bat debug config
rtk proxy cmd /c build.bat debug build
rtk proxy cmd /c build.bat release config
rtk proxy cmd /c build.bat release build
rtk python -m unittest discover -s tests -v
rtk git diff --check
```

Máy hiện tại cần host GCC runtime tách biệt do DLL/toolchain Windows; recipe và
command receipts được lưu cùng evidence, không sửa compiler hoặc PATH toàn máy.

Trước lần chạy bo cần xác nhận đúng image và quyền flash riêng. Pins custom:
DMIC CLK PA1/DAT PA2; I2S1 DO PA8, MCLK PA10, BCLK PA11, LRCK PB0; I2C0 SDA PA4/SCL
PA5; SWD PF0/PF1, Debug UART2 PC4/PC5. CH549 không có chân nRESET: giữ đường reset
vật lý/cấp nguồn và SWD sẵn dùng; không coi reset request OpenOCD là reset đã xảy ra.

1. Giải quyết init nếu còn `board_status=5` theo [I2C trace](i2c_stuck_trace.md).
   Thu cả cold-start và nạp/debug lại; không chọn riêng các lần boot thành công.
2. Chạy compiled replay bằng runner/backend hiện có, đủ 26.441 hop, carry state
   đúng firmware. Yêu cầu zero code mismatch gain/phase/next-input state; mock
   Invoke hoặc DSP replay ở trên không thay được bước này. Runner hiện báo BLOCKED
   vì chưa có backend M55M1 được cho phép chạy.
3. Build probe bằng các lệnh dưới. Đây vẫn là thư mục `build`, không tạo
   profile/thư mục thứ ba.
   Lưu hash trước khi nạp. Các image probe/replay vừa build cũng được lưu riêng
   dưới diagnostics theo timestamp để không bị build thường ghi đè.

   ```bat
   rtk proxy cmake --preset m55m1-release -DM55M1_AUDIO_RELEASE_PROBE=ON
   rtk proxy cmake --build --preset m55m1-release
   ```

4. Đo baseline/candidate cùng compiler/clock/cache/instrumentation. Probe chạy
   tối đa 60.000 hop và kết thúc ngay khi lỗi. `COMPLETE` chỉ là đủ thời gian quan
   sát, chưa đồng nghĩa đạt timing. Fault IDs: 0 transport, 1 sequence, 2 inference,
   3 process deadline, 4 submit, 5 publish deadline, 6 restart. Giữ toàn bộ log,
   không bỏ lần FAIL đầu tiên. Lỗi init trước audio cần xem init log/Debugger;
   không giả định probe tổng hợp đã được phát.
5. Kiểm tra không deadline/overrun/underrun/sequence/restart, capture-to-publish
   luôn <1.000 us; mục tiêu margin đề xuất là tối đa 900 us. Tách stage max và
   IRQ/dispatch jitter; không cộng các max khác hop rồi coi là tổng đã đo.
   Đo actual arena, stack high-water, b+2 trong slot/sample và startup/recovery.
6. Đo continuity, clipping/packing và latency âm học bằng đường vật lý. Đo dòng
   với cùng nguồn/tín hiệu/tải/âm lượng/thời gian; ít thao tác CPU hơn chưa chứng
   minh điện năng thấp hơn. Debug không bắt buộc đạt 1 ms, nhưng vẫn dừng khi lỗi.
7. Khôi phục bằng `build.bat release build`, rồi `build.bat debug config`; kiểm
   tra cache flags OFF và hash image thường trước lần nạp tiếp theo.

Không đổi `deployment_approved=false`. Đợt này không train/PTQ/Vela lại, không
sửa package/model/golden, không flash hoặc thao tác trên bo.
