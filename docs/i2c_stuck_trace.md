# Debug board_status=5 trên mạch custom

`board_status=5` là `I2C_STUCK` trong board initialization, trước khi driver
TLV320DAC3100 chạy script thanh ghi. `codec_status=0` trong trường hợp này là
giá trị mặc định của report, không chứng minh codec đã khởi tạo thành công.

Người dùng đã gặp lỗi sau nạp/debug OpenOCD và thấy một lần power-cycle không
lỗi. Chưa đủ lần lặp để xác lập quan hệ nhân quả. Probe CH549 không nối nRESET;
`reset halt`/`sysresetreq` trong cấu hình chỉ là yêu cầu reset phần mềm, không
chứng minh bo hoặc codec đã nhận reset. Không đổi cấu hình reset trong bản trace.

## Cách lấy log

1. Dùng profile **Debug**, binary `build-debug/M55M1_GestureAI.bin`; đối chiếu
   SHA-256 với receipt của lần build này trong thư mục verification bên dưới.
   UART2 TX PC5 / RX PC4 dùng cấu hình terminal đang đọc được log INIT.
2. Mở thu UART trước khi chạy lại đúng quy trình nạp/debug đã từng lỗi. Chép
   toàn bộ log từ `[INIT] stage=transport` tới `[INIT_TRANSPORT]` và dòng FAIL/OK.
   Không chỉ chép dòng kết luận vì snapshot trước recovery là bằng chứng quan trọng.
3. Giữ riêng log mỗi lần, đánh dấu `OpenOCD` hoặc `power-cycle`, số lần thử và
   có nhấn reset/nạp lại hay không. Đề xuất 20 lần mỗi nhóm; không bỏ lần thành công.
4. Nếu UART không ra được, debugger đọc `g_i2c_recovery_trace` hoặc gọi getter
   `m55m1_audio_get_i2c_trace()` khi foreground đã dừng sau board initialization.
   Getter trả NULL nếu recovery chưa được thực hiện. Không đọc khi init đang ghi.

Agent không tự flash, nhấn reset hay thay nguồn. PA4 SDA / PA5 SCL và SWD PF0/PF1
giữ nguyên. Khi phiên debug không phục hồi được, dùng cách reset vật lý/power-cycle
đang có trên bo; không giả định CH549 có thể điều khiển nRESET hay RESET của codec.

## Đọc các nhóm log

| Nhóm | Ý nghĩa |
| --- | --- |
| `I2C_RECOVERY` | Kết quả return, số xung, số quan sát, reset flags thô, chỉ số quan sát đầu thấy SCL thấp khi firmware đang yêu cầu nhả SCL. `255` nghĩa là không ghi nhận trường hợp đó. |
| `I2C_GPIO stage=before/after` | Thanh ghi PA trước chuyển GPIO và sau STOP, trước mux trở lại I2C. Đọc PA4/PA5 trong MFP1, MODE, DOUT, PIN, DINOFF, PUSEL và SMTEN. |
| `I2C_REG` | CTL0/STATUS0/CLKDIV trước recovery và clock gate GPIO/I2C. Có thể phản ánh trạng thái còn lại từ debugger/lần chạy trước. |
| `I2C_TIMEBASE` | Nguồn SysTick/LOAD trước và sau delay đầu, cùng SystemCoreClock tại recovery. `before` chưa cấu hình là có thể xảy ra: delay tự init timebase. |
| `I2C_EDGE` | Mẫu logic sau release, mỗi pha clock-low/high và ba pha STOP; có pulse, raw cycle và elapsed_us tính từ mẫu release đầu tiên. |

Các kết quả: `BUS_FREE` khi recovery return true; nếu false thì phân loại theo
mẫu cuối thành `SDA_LOW`, `SCL_LOW`, `BOTH_LOW`, `READBACK_CHANGED` hoặc
`NO_FINAL_SAMPLE`. `released` là giá trị return thực, tách khỏi mẫu GPIO để không
che trường hợp đường bus đổi mức giữa các lần đọc.

- SDA thấp sau đủ chín xung nhưng SCL cao ở các pha clock.high: kiểm tra SDA
  có được nhả trong DOUT/MODE/MFP không, digital input có bị disable không, rồi
  mới phân biệt trạng thái slave giữ bus với vấn đề nguồn/pull-up/dây nối.
- SCL thấp ngay `release` hoặc tại `clock.high`: xung đã ghi vào GPIO chưa chắc
  xuất hiện trên bus. Không kết luận codec lỗi chỉ từ số xung đã yêu cầu.
- Hai đường cao cuối recovery nhưng return false: giữ cả snapshot và raw cycles
  để điều tra thay đổi mức giữa các lần đọc; không tự bỏ qua điều kiện lỗi.

Không diễn giải `clock.low scl=0` là lỗi: đó là mức firmware chủ động tạo.
`rststs` có thể giữ nhiều cờ sticky; không clear cờ trong trace và không coi một
cờ là bằng chứng duy nhất xác nhận kiểu reset. Giá trị điện áp, pull-up thực,
rise time, RESET codec và nguồn không đo được bằng snapshot này.

## Giới hạn và nghiệm thu

Trace có tối đa 22 mẫu, dùng storage tĩnh do board sở hữu. Transport chuyển
con trỏ chỉ đọc, còn service in sau recovery, trước khi audio/DMA bắt đầu.
Không UART/snprint/heap giữa các xung. Việc ghi RAM vẫn thêm overhead Debug;
`I2C_EDGE` là mẫu sau delay, không phải thời điểm cạnh vật lý. Release loại bỏ
trace buffer và log; chính sách chín xung/5 us/STOP chưa thay đổi.

Host test mô hình hóa pin và chạy C recovery thật để kiểm tra luồng phần mềm;
không chứng minh bus vật lý. Chỉ sửa recovery khi trace chỉ ra sai hỏng có thể
tái hiện; sau sửa phải test lại cả nạp/debug lẫn power-cycle. Không coi một lần
boot thành công là đã hết lỗi chập chờn. Lỗi deadline/underrun runtime là việc riêng.

## Codec failure after successful bus clear — 2026-11-09

When `board_status=0` and `codec_status=5`, recovery completed but a later codec
transaction failed. Use the additional Debug `[CODEC_I2C]` record to distinguish
byte NACK from SI/STOP timeout; do not infer that the earlier bus-clear failed.
See [codec transaction verification](verification/2026-11-09-codec-i2c-init/).

Evidence của bản triển khai: [verification](verification/2026-11-08-i2c-stuck-trace/).
Thiết kế: [spec](superpowers/specs/2026-11-08-i2c-stuck-trace-design.md).

Nguồn đối chiếu: [NXP UM10204, mục 3.1.16](https://www.nxp.com/docs/en/user-guide/UM10204.pdf)
và [TI TLV320DAC3100 SLAS671C, mục 6.3.1–6.3.3](https://www.ti.com/lit/ds/symlink/tlv320dac3100.pdf).
Khi SCL bị giữ thấp hoặc SDA không nhả sau bus-clear, chuẩn I2C chỉ dẫn reset
thiết bị/cấp lại nguồn; không giả định phần mềm luôn giải phóng được bus.
