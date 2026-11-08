# Điện năng thấp khi DeepFIR chạy liên tục — 2026-11-08

Tối ưu thêm trong frontend/feedback/reconstruction và probe timing đã được kiểm
chứng host/build: [audio_inference_optimization.md](audio_inference_optimization.md).
Các kết quả bên dưới thuộc đợt low-power trước; chưa có đo dòng/điện năng A/B cho
bản tối ưu mới, nên không suy ra mức tiết kiệm điện từ số thao tác hoặc RAM giảm.

Đã triển khai hướng 1 được duyệt: core220MHz, audio16kHz/hop16, model chạy mỗi hop.
Không đổi model/DSP, gain, pinout, điện áp, tần số hoặc lịch playback b+2. Chưa flash.
Evidence: `diagnostics/continuous_low_power_20261108_122920/`.

## Thay đổi và chủ sở hữu

- `app/main.c`: poll rồi gọi `audio_pipeline_wait_for_work()`; replay mailbox giữ
  luồng riêng và audio dừng như trước.
- Service chỉ chờ khi `g_running`. Driver transport mask IRQ, kiểm tra lại capture
  và cả bốn bộ đếm lỗi DMA, rồi board thực hiện shallow WFI; sau đó trả PRIMASK.
  IRQ đến giữa kiểm tra và WFI vẫn pending, đánh thức CPU rồi ISR chạy khi unmask.
  ISR/masked callers không được đưa vào wait. IRQ khác có thể đánh thức sớm; vòng
  lặp kiểm tra lại, không tạo queue/hop mới. NPU vẫn dùng semaphore WFE hiện có.
- `board/m55m1_power.[ch]`: chỉ gate các peripheral được liệt kê không dùng, gồm
  UART dư, ADC/DAC MCU, USB/Ethernet/CAN/camera, SPI/QSPI/I2S0/I2C1-3, các timer/PWM,
  crypto/CRC và PDMA1. Giữ audio, LPPDMA0, PDMA0, NPU/FMC/SRAM, GPIO/SWD, watchdog
  và oscillator. Không ghi đè toàn bộ clock register hoặc power domain.
- Release thông thường gate mọi UART/LPUART và disable/clear IRQ của chúng;
  console/debug UART code không còn trong linked image. Debug giữ UART2 PC4/PC5.
  Chỉ diagnostic opt-in capture/probe mới có ngoại lệ UART rõ ràng; script sản phẩm
  luôn tắt các option đó. Vendor debug bootstrap bị tắt bằng `NVT_DBG_UART_OFF=1`.
- `board/m55m1_timebase.[ch]`: SysTick external ACLK/2, ST0CKEN, không TICKINT.
  ACLK tiếp tục trong Idle; trả số đếm24bit nhân2 để giữ đơn vị core-cycle, độ phân
  giải2cycles, wrap2^25cycles (~152.52ms tại220MHz). Dùng elapsed helper, không tự
  trừ/mask24bit. Init giữ nguyên trạng thái khóa protected registers của caller.
- Codec và board I2C recovery dùng delay của timebase, chia chunk tối đa10ms cho
  depop800ms. Không để `CLK_SysTickDelay` ghi lại counter sau init/restart.
- Codec sau software reset ghi rõ PLL off P0R5=0x11, headset detect P0R67=0,
  volume ADC P0R116=0, Class-D P1R32=0x06, MICBIAS P1R46=0. Giữ PRB_P7, DOSR256,
  direct MCLK, stereo headphone, short protection, depop và gain ceiling−3dB.
  Các khối không dùng vốn đã off ở reset: chưa chứng minh tiết kiệm thêm từ DAC.

Không đổi chủ sở hữu analysis/context/h/c/taps/FIR history/DMA buffers. Không thêm
interpreter, allocation sau audio start, DSP hoặc logging blocking trong ISR.

## LPPDMA và căn cứ linh kiện

**Có dùng cho capture, hiện đã dùng:** DMIC RX -> LPPDMA0 ch0, scatter/gather16samples.
**Không dùng thay PDMA playback:** I2S không có trong request map LPPDMA, do đó I2S1
TX vẫn PDMA0 ch0. Đây là giới hạn phần cứng, không phải thiếu option compiler.
Auto-operation trong power-down không giải quyết đường audio này vì clock nguồn
APLL và I2S đang dùng phải hoạt động liên tục.

| Nguồn đã đọc | Quyết định |
|---|---|
| M55M1 DS Rev1.00 pp95-98,102 | Idle dừng CPU, giữ ACLK/peripheral/APLL; không deep power-down |
| M55M1 DS p171 và BSP lppdma.h request0-13 | DMIC được hỗ trợ; không có I2S |
| BSP clk_reg.h STSEL/STCTL | External SysTick ACLK/2 và ST0CKEN |
| TI SLAS671C pp21,46,66,76,79,81,85 | Giữ PRB_P7/headphone; tắt unused blocks, giữ reserved bits |
| MP34DT05 Rev1 pp6-7 | Clock normal1.2-3.25MHz; startup10ms; giữ PDM2.048MHz liên tục |

Nguồn chính thức: [Nuvoton datasheet](https://www.nuvoton.com/export/resource-files/en-us--DS_M55M1_Series_EN_Rev1.00.pdf),
[AN0082 power](https://www.nuvoton.com/export/resource-files/en-us--AN_0082_M55M1_M5531_Power_Management_EN_Rev1.00.pdf),
[TI DAC](https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf),
[ST microphone](https://www.st.com/resource/en/datasheet/mp34dt05.pdf),
[Arm WFI/PRIMASK](https://arm-software.github.io/CMSIS_6/v6.3.0/Core/group__intrinsic__CPU__gr.html).
Hash PDF và các trang trích đọc nằm trong `datasheet_identity.json` và
`*_power_excerpt.txt`. Không dùng số mW của cấu hình48kHz TI làm số đo16kHz hiện tại.

## Kiểm chứng thực sự đã chạy

136/136 unittest PASS, gồm C thật cho clock gate Debug/Release, SCR shallow WFI,
IRQ pending race, cả bốn lỗi transport, masked/ISR callers, service running/stopped,
counter wrap/long delay/protected-register ownership, codec register values và
fail-fast ở từng write. Các numeric/binding/feedback/FFT/FIR/PCM/schedule/recovery
tests vẫn đạt. Hardware được model trong host test; không phải đo công suất hay
thực thi compiled controller. Test ownership đã phát hiện và bắt lại lỗi init
counter làm khóa register của board caller; log red/green được giữ riêng.

GCC13.2 children gặp STATUS_DLL_NOT_FOUND trên máy, kể cả test cũ. Runner evidence
copy đúng bytes compiler/binutils/DLL vào thư mục runtime riêng; wrapper chỉ thêm
`-B` và `-static` cho host GCC. Không thay source/compiler hệ thống hoặc include
path Arm. Lần chạy thử sai môi trường và các lỗi fixture đã được giữ trong log;
kết quả cuối: `20261108_124949_regression.log` (136 tests, OK).

Debug/Release config/build đều exit0 với Arm Compiler6.24. Build cuối:
`20261108_124907_build_receipt.json`. Map/hash/binary cuối:
`verified_builds/`, kiểm tra bổ sung `power_link_audit.json`,
`linked_idle_thumb.txt` (DSB/WFI/ISB giải mã Thumb đúng).

| Thuộc tính | Debug | Release |
|---|---:|---:|
| BIN bytes | 627360 | 612736 |
| Tăng BIN so với baseline phiên này | 1248 | 1216 |
| RW_RAM bytes | 78168 | 77936 |
| Model+metadata SRAM bytes | 516852 | 516852 |
| Arena capacity bytes | 65536 | 65536 |
| Noncacheable DMA region bytes | 320 | 320 |
| Stack / heap bytes | 8192 / 2048 | 8192 / 2048 |

SHA256 Debug `7cc8260d35e3f227a65a84985939e29b39e058fe8ce8843f363fd14d36839fe6`;
Release `d405e68bb2a8b37e255bbf4cd9f9bdd8f0ecb5691aa69612ae73337e129b8bca`.
RW/arena/buffer capacities không tăng. Chưa đo arena actual use/stack high-water.
Mọi47 source artifact hash, destination hash và compiled C payload vẫn đúng;
compiled model chỉ xuất hiện một lần, không trùng runtime/core/interpreter.

| Mục nghiệm thu | Trạng thái | Evidence/giới hạn |
|---|---|---|
| Artifact integrity | PASS | verified_builds/product_audit.json |
| Architecture/source integration | PASS | Native tests, link audit, session_changes.diff |
| Host numeric correctness | PASS | 136/136; không thay expected numeric |
| Debug/Release build/link | PASS | Build receipts + maps/BIN |
| Raw replay rerun | NOT_RUN | Receipt Linux của package chỉ là lịch sử |
| Compiled replay | BLOCKED | Thiếu backend đang chạy/bo được phép chạy; runner trước đó sẵn có |
| Board initialization/actual memory | NOT_RUN | Không truy cập board |
| Full-hop deadline/b+2/wake jitter | NOT_RUN | Cần đo lại với Idle, timestamp capture hiện lấy trong ISR |
| Physical audio/latency | NOT_RUN | Chưa có phép đo đường vật lý |
| Current/energy saving | NOT_RUN | Không có số % hoặc mA suy đoán |

## Tái lập và phép đo A/B tiếp theo

Build vẫn chỉ hai directory/profile thông thường:

```powershell
rtk proxy cmd /c build.bat debug config
rtk proxy cmd /c build.bat debug build
rtk proxy cmd /c build.bat release config
rtk proxy cmd /c build.bat release build
rtk python -m unittest discover -s tests -v
rtk git diff --check
```

Nếu môi trường GCC còn lỗi DLL, dùng runner có log/env/hash:
`rtk proxy python -X utf8 diagnostics/continuous_low_power_20261108_122920/verify.py tests`.
Nó tạo lại runtime kiểm chứng tạm trong evidence; runtime này không vào firmware.

Sau khi được phép flash: target custom M55M1R2LJAE LQFP64; DMIC PA1CLK/PA2DATA,
I2S1 PA8DATA/PA10MCLK/PA11BCLK/PB0LRCK, I2C0 PA4SDA/PA5SCL. DMA capture LPPDMA0ch0,
TX PDMA0ch0. Giữ SWD PF0/PF1. CH549 không có đường nRESET vật lý; xác minh dây nguồn,
GND/VREF, dùng reset tay/power-cycle và hướng dẫn `M55M1_OPENOCD_DEBUG.md` nếu cần
recovery APROM. Không thay pin/clock/DMA wiring để thực hiện phép đo này.

1. Dùng baseline Release đã giữ tại `baseline/build_M55M1_GestureAI.bin`
   (SHA c3c333227c617347c7f684e7f2ee264fc7e6fdd138f7e166fcea3234cb8ae879)
   và Release mới. Baseline đã có NPU WFE; phần mới chủ yếu bỏ busy poll giữa hop.
2. Cùng điện áp cấp, tải headphone, volume, stimulus, nhiệt độ và thời gian warmup;
   đo cùng rail/điểm cấp nguồn, tránh USB/SWD back-power. Tách số đo toàn board và
   rail MCU/DAC nếu phần cứng cho phép. Dùng bandwidth lấy được chu kỳ1ms.
3. Capture V/I liên tục ít nhất60s sau startup. Tính Pavg=integral(V*I)/T và
   Ehop=integral(V*I)/Nhop thành công; dùng delta sequence khi không restart.
   Báo mean/max/current spikes, điều kiện và sai số thiết bị; không suy % từ idle %.
4. Kiểm tra LRCK16kHz, PDM2.048MHz, không drop/repeat mẫu; các fault/deadline/overrun/
   underrun/restart counters không tăng trong soak. Đo stage/total hop, wake jitter
   và b+2 bằng instrumentation hiện có/trace opt-in, không log blocking ở realtime.
   Timestamp capture trong ISR chưa bao gồm thời gian từ cạnh DMA đến khi CPU thức:
   phải đo phần đó bằng đường hardware/trace phù hợp trước khi chốt margin<1ms.
5. Kiểm tra startup hai silence slots, reset/fault recovery, DAC gain/clipping,
   continuity và latency vật lý. Đo không gắn debugger khi lấy dòng đại diện cuối.

Chưa có kết quả công suất để chọn chỉnh PLL/voltage/codec mode tiếp theo.
Giữ `deployment_approved=false`; phiên này không flash hoặc thay trạng thái bo.
