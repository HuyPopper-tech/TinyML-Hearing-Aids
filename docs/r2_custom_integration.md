# Tích hợp DeepFIR R2 trên custom M55M1 — 2026-11-08

Current input trial (2026-11-14): PCM24/16 kHz, DMIC +12 dB, software +21 dB
with shared pre-frontend/FIR compression (3/120 ms) and independent peak limiting.
Raw model replay stays unchanged. Frontend ring storage preserves the causal frame;
normal Release omits service stage timing while retaining deadline/SWD statistics.
See [current verification and board acceptance limits](verification/2026-11-14-pipeline-cleanup.md).

## Current status — 2026-11-09

Ordinary Debug and Release share the continuous custom-board pipeline and terminal
FIFO-aware playback handoff. Release Probe remains an opt-in of that same build
directory, stopping at60,000 hops and reporting after audio stops. Current guide,
new build hashes and acceptance: [continuous product finalization](verification/2026-11-09-continuous-product/README.md).

The user supplied a fault-free60,000-hop Probe run: max total863 us,
capture-to-progress885 us and capture-to-publication884 us, with no missed
deadline/overrun/underrun/DMA/model failures. Its flashed BIN identity was not
supplied. [Exact snapshot/receipt](verification/2026-11-08-board-probe-60000/README.md)
records that limitation. Earlier user Debug log reported arena use6976/65536B;
it is not a new measurement of the final cleanup image or a stack watermark.

Removed unreachable32-kHz/scatter/warmup paths, untagged submit, the old unused
telemetry module, RTOS build block and duplicate SRAM option. Package/libs/DSP
math, pins, clocks, gains, two silence slots, b+2 and1-ms fault guards are unchanged.
No agent flash/device operation; deployment_approved remains false. The initial
integration report below is a historical snapshot; its unmeasured timing/memory
statements are superseded only to the limited extent of the supplied logs above.

## Initial integration snapshot — 2026-11-08

Luồng sản phẩm hiện dùng **custom PCB**, với duy nhất Debug (`build-debug/`)
và Release (`build/`). Gói nguồn được giữ nguyên. Chưa flash; chưa phê duyệt
triển khai (`deployment_approved=false`). Không train/PTQ/Vela lại, không sửa `libs/`.

Evidence gốc tại máy: `diagnostics/r2_custom_integration_20261108_105000/` (gọi là `EVIDENCE`).
Receipt kiểm chứng được đưa lên Git: [source cleanup/build verification](verification/2026-11-08-source-cleanup/README.md). Các ứng dụng chẩn đoán độc lập đã được xóa; chỉ còn `app/main.c`.
Kế hoạch và tiêu chí đã ghi trước khi sửa tại
`docs/superpowers/plans/2026-11-08-r2-custom-integration.md` và spec cùng ngày.
Worktree ban đầu có thay đổi người dùng; baseline patch/status được lưu, không
commit/reset/clean. Route Light, thực hiện trực tiếp, không agent/explorer.

## Danh tính và import

Nguồn chuẩn: `deepfir_r2_scratch_ptq_20261107_151431_366715/` (`PACKAGE_ROOT`),
bundle `firmware_deepfir_r2_C_34c5bd074ae6_69c1c3d2/` (`FW_PACKAGE`).

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Checkpoint (provenance) | — | `34c5bd074ae63c57fd0df28c3a0a63e32dfd63be9be84fabc467823b70ccd0af` |
| Raw | 555600 | `9d11584db9ce1223eb26be1ab2b5f1493ff0c3c92ba3e2757a6063bd279d38ac` |
| Compiled, runtime | 516848 | `69c1c3d2f2491df24a66c2190f266091bf8c192fcbce6724ee430d5cf28b1c27` |
| Source firmware manifest | — | `caefa8239c579282a917c59292c41a2cff8fff1b76bf25ddc17bfb9baa12aeab` |

Candidate ABI: `deepfir_ablation_r2_ac_v1_gain_phase_split_range_hc_v2`.
47 artifact hashes trong firmware manifest đã khớp trước import và tại audit cuối.
Mảng C++ đích bằng compiled TFLite từng byte; chỉ đổi alignment của mảng lên 32.
Raw/checkpoint/golden không link vào production. Binary chứa đúng một compiled
image; link inputs có một model, một runtime và một Ethos-U core/device.

`tools/import_deepfir_r2.py` đã lưu gói active cũ vào
`EVIDENCE/previous_active_package/` trước import. Manifest nguồn được sao chép
nguyên byte thành `models/deepfir/source_integration_manifest.json`; trạng thái
firmware và hash đích nằm riêng trong `integration_manifest.json`.
Inventory/hash/build linkage nằm tại `artifact_integrity.json`, `import_receipt.json`
và `final_audit/product_audit.json`; receipt import là ảnh chụp ban đầu, audit là trạng thái cuối.

Vela 5.2.0 và FC NATURAL/legacy/bias policy giữ theo receipt của package.
Các đường dẫn Kaggle chỉ là provenance: thay prefix
`/kaggle/working/deepfir_r2_scratch_ptq_20261107_151431_366715/` bằng `PACKAGE_ROOT`;
nếu artifact đã được đóng bundle thì tìm basename trong `FW_PACKAGE` và kiểm hash.
`provenance_path_mapping.json` ghi 13 đường dẫn mở được và 7 đường dẫn lịch sử
không cần mở. Không truy cập thư mục nghiên cứu ngoài repo.

## ABI và chủ sở hữu

Tất cả tensor giao tiếp INT16, zero point 0; qparams lấy từ header package.
Khởi tạo kiểm tra port/index/name/dtype/rank/shape/bytes/scale/zero point và SRAM.

| Semantic | Runtime port | Shape | Scale |
| --- | ---: | --- | ---: |
| Input feature_context | 0 | [1,2,129,3] | 9.672559826867655e-05 |
| Input h | 2 | [1,200] | 3.0516976039507426e-05 |
| Input c | 1 | [1,200] | 2.9065247872495092e-05 |
| Output gain | 3 | [1,65] | 5.657631845679134e-05 |
| Output phase | 2 | [1,65] | 7.72049679653719e-05 |
| Output h_next | 0 | [1,200] | 3.051537714782171e-05 |
| Output c_next | 1 | [1,200] | 2.9116219593561254e-05 |

Semantic adapter order là feature/h/c và gain/phase/h_next/c_next. Đã thay
giả định ba output/taps128 trực tiếp trong adapter, binding, service, caller và fixture.
State giữ encoded model order: graph đã chứa permutation/radius transform,
firmware không biến đổi tọa độ lần nữa. Hai lane đều requantize, không direct copy:
h multiplier 2147371134 shift31, c multiplier1075624845 shift30, int64,
ties-away, saturation. Commit sau invoke và trước hop tiếp theo. Golden `states`
là next-input state sau feedback.

| Buffer/state | Chủ sở hữu duy nhất | Quy tắc |
| --- | --- | --- |
| Analysis history240, FFT256 scratch/window | `g_feature_state`, frontend service | zero startup, update một lần/hop |
| Previous/current context774, h/c200 | `g_adapter`, inference service | commit một lần; reset toàn bộ khi fault |
| Previous taps128, FIR history127/crossfade | `g_adapter.dsp` | cùng input hiện tại, không FIR trễ thêm hop |
| FFT256/128 instances | inference service | callback kiểm trạng thái |
| Interpreter/resolver/arena | inference service | một instance, placement/static storage |
| PCM working input/output | audio pipeline | tách biệt, publish khi xử lý thành công |
| Capture ping/pong | board/LPPDMA | producer DMA; foreground copy sau completion và gate request |
| Stereo ping/pong + silence | DAC driver/PDMA | chỉ sửa slot đã trả; gate DMA + bounded busy/TD check + barriers |

`deepfir_reconstruction.c` là DSP stateless ở services, không nằm trong models.
Production capture trial (2026-11-14): FIFO24/DMA32, signed PCM24 in `int32_t`,
normalized by 8388608 before the shared frontend/FIR. PCM16 replay retains
`pcm/32768`; `PCM24 = PCM16 * 256` gives identical normalized values.
The model tensor ABI and PCM16 playback remain unchanged. This capture change
has host/build validation only; earlier PCM16 board timing does not validate it.

Frontend uses normalized PCM, Hamming đối xứng mẫu số255, FFT256 forward không chuẩn
hóa, magnitude/real/imag theo contract; previous ban đầu zero. Reconstruction
giữ real DC/Nyquist, imaginary endpoints zero, Hermitian inverse128 positive/128,
roll+64 và taper. FIR cũ/mới cùng history/input, midpoint-Hann16; reset delta64.
PCM ties-away và saturate. Không thêm RMS/AGC, normalization, minimum phase,
tap clipping, OLA hoặc FIFO để bù latency.

## Custom PCB, schedule và recovery

| Chức năng | Binding |
| --- | --- |
| DMIC0 channel2 | PA1 clock, PA2 data, rising edge, OSR128, 2.048 MHz nominal |
| I2S1 -> DAC | PA8 DO, PA10 MCLK, PA11 BCLK, PB0 LRCK |
| I2C0 control | PA4 SDA, PA5 SCL, 400 kHz, DAC address0x18 |
| Debug UART2 | PC5 TX, PC4 RX, 115200 8N1 |
| DMA | capture LPPDMA0 ch0; playback PDMA0 ch0; diagnostic UART2 PDMA1 ch0 |
| Recovery | PF0 SWDIO / PF1 SWCLK giữ nguyên; custom J4 theo OpenOCD guide |

Core220MHz, HXT24MHz. RX/TX cùng nguồn APLL1/2: I2S MCLK divider58 /256
và DMIC /2 /58 /128 cho cùng nominal sample rate16k. Điều này loại nguồn
drift tần số độc lập trong cấu hình; phase/FIFO/start skew và rate thực tế vẫn
phải đo. RX descriptors chuẩn bị trước TX start, RX enable ngay sau TX enable.

Capture b, xử lý slot b+1, playback slot b+2; TX bắt đầu hai slot silence.
Sequence bắt đầu1 ứng với capture slot0; tagged submit chỉ nhận đúng sequence,
đúng released slot và đúng TX completion count. Không thêm queue hop.
DMA gate/busy/TD guard tránh sửa buffer đang dùng; DMA không bị chặn chỉ bằng
mask IRQ. ISR chỉ handoff/ack/signal, và gate TX khi underrun; không DSP/log/I2C.
Host register model kiểm chứng logic, chưa chứng minh timing của DMA thật.

`audio_pipeline_get_stats()` cung cấp cumulative fault/restart counters, sequence,
logical capture sample position, scheduled playback slot, cycle timestamps và
max capture-to-publish. `audio_transport_take_completion()` có DAC timestamp và
actual DMA playback slot để debugger/diagnostic đối chiếu sequence+1. Snapshot
completion giữ mới nhất, không phải trace lịch sử toàn bộ; đo liên tục cần lấy
mỗi hop qua debugger/instrumentation phù hợp. Stage timings gồm frontend,
invoke/NPU, carry, reconstruction, FIR và PCM/other; opt-in Release probe tổng hợp
sau 2000 hop và dừng audio trước báo cáo. Không log blocking trên realtime path.

FFT/invoke/NaN fault latch; sequence gap/duplicate, overrun/underrun, submit BUSY/error
hoặc >=1000us từ capture completion tới process/publish làm stop DMA/mute,
flush việc cũ, full frontend/context/state/FIR reset và restart hai silence slots.
Không retry context dở hoặc phát stale output. Restart fail giữ stopped, đếm lỗi.
Deadline guard không phải bằng chứng đã đạt deadline trên board.

CH549 không có nRESET vật lý: không giả định connect-under-reset. Nếu halt/Secure
debug lỗi, dừng server, ngắt nguồn và SWDIO/SWCLK để tránh back-power, cấp nguồn
lại rồi nối GND/SWD; kiểm stable Secure debug, halt và PDID trước lập trình.
Không thay CONFIG/security hoặc retry erase để né lỗi. Xem
`docs/M55M1_OPENOCD_DEBUG.md`. Phiên này không mở probe/flash.

## Host verification và giới hạn số học

Baseline ngoài sandbox:117 tests PASS. Sandbox từ chối tạo file C trong temp;
các native tests cuối chạy ngoài sandbox, log giữ nguyên cả lần lỗi môi trường.
Suite cuối132 tests PASS ghi trong `final_regression.log`; focused R214 tests trong `positions_green.log`.

Chạy C thật: exact Q31 toàn bộ65536 mã/lane, binding rejection, carry/reset/port
order, synthetic frontend22 hop (startup/history/context), 14 gain/phase vectors
so independent NumPy IDFT, FIR/crossfade/PCM/clipping, fault output canary,
native pipeline sequence/deadline/restart và actual driver start/submit/IRQ qua
register-request model. Cùng numeric vectors chạy thêm với vendored CMSIS scalar
FFT thực tế. MVE target vẫn cần boot self-test và board numeric verification.

Tolerance định trước trong spec: float FFT/taps atol2e-5 + rtol2e-5;
FIR atol2e-5 + rtol2e-6; frontend tối đa1 quantized code do float32/libm so với
double reference. Integer binding/feedback/PCM yêu cầu exact. Không thay golden
hoặc nới tolerance để đạt. Các traces controller không chứa PCM: không suy diễn
frontend end-to-end từ chúng; frontend dùng vector tổng hợp riêng.

## Replay có thể tái lập

Hai NPZ đã kiểm schema/hash:11324 +15117 =26441 hop. Source receipt Linux
TensorFlow2.20.0/Python3.13.18, một thread,
`BUILTIN_WITHOUT_DEFAULT_DELEGATES` báo raw bit-exact. Đây là receipt lịch sử,
không phải rerun phiên này, cũng không chứng minh Vela runtime. Máy Windows
không có WSL distro Linux (`wsl_inventory_unsandboxed.log`); không gọi Windows
raw replay là canonical Linux rerun.

`tools/replay_deepfir_r2.py` dùng trực tiếp portable verifier/Q31 helper từ package,
carry state trên firmware, so gain/phase/next-input h/c đủ hai traces; ghi mismatch
count/max code error/first mismatch và runtime identity. Comparator có synthetic
one-code mutation test, không phải compiled parity. Khi thiếu backend, runner
đã tạo `compiled_replay/compiled_replay_receipt.json`: **BLOCKED**, runtime null.

Opt-in mailbox firmware đã compile/link Debug; bản đó lưu tại
`EVIDENCE/replay_build/`. Production Debug/Release hiện mailbox OFF. Để build
lại chẩn đoán trong cùng thư mục (không thêm profile):

```powershell
rtk proxy cmd /c build.bat debug config
rtk cmake -S . -B build-debug -DM55M1_DEEPFIR_COMPILED_REPLAY=ON
rtk cmake --build build-debug
```

Sau khi được phép flash riêng, chạy đúng replay AXF và OpenOCD Tcl server;
lấy `g_deepfir_replay_mailbox` và `g_deepfir_model` address từ map của chính AXF đó.
Runner không tự flash/reset/resume. Ví dụ với map replay đã lưu:

```powershell
rtk python tools/replay_deepfir_r2.py --output diagnostics/r2_compiled_NEW_TIMESTAMP --openocd 127.0.0.1:6666 --mailbox 0x202300e0 --model-address 0x20100000 --firmware diagnostics/r2_custom_integration_20261108_105000/replay_build/M55M1_GestureAI.axf
```

Runner kiểm hash model SRAM thật trước invoke. ELF hash ghi file người chạy cung
cấp, không tự chứng thực toàn bộ target code; phải đảm bảo image đã nạp đúng AXF.
PASS chỉ khi **zero code mismatch** đủ26441 hop; nếu khác, điều tra, không sửa
golden/nới tolerance. Khôi phục sản phẩm bằng `build.bat debug build` (script
reconfigure mailbox OFF). Raw Linux rerun dùng đúng Python3.13.18/TensorFlow2.20.0
và native runtime trong source receipt, rồi chạy từ repo root Linux:

```sh
FW_PACKAGE=deepfir_r2_scratch_ptq_20261107_151431_366715/firmware_deepfir_r2_C_34c5bd074ae6_69c1c3d2
rtk python "$FW_PACKAGE/verify_raw_replay.py" --raw "$FW_PACKAGE/controller_16x8.tflite" --goldens "$FW_PACKAGE/integer_replay" --output diagnostics/r2_raw_NEW_TIMESTAMP.json
```

Lệnh này chưa chạy trong phiên; output phải mới. Đối chiếu platform/resolver/thread
và native-library identity trước khi gọi đó là canonical raw rerun.

## Build và bộ nhớ tĩnh

`product_build_receipt.json` và lần kiểm cuối `profile_restore_build_receipt.json` ghi bốn command config/build Debug/Release exit0;
`tool_versions.json`, logs, binary/hash/map/cache/ninja đã lưu riêng mỗi profile.
Không có compiler license blocker. Các thư mục build thừa đã inventory và lưu
binary/map/log/tệp probe cần giữ trong `obsolete_build_evidence.zip` trước xóa;
15 thư mục và4 wrappers cũ được bỏ. Chỉ còn `build/`, `build-debug/` ở root.

| Thành phần | Debug bytes | Release bytes |
| --- | ---: | ---: |
| Model image + length metadata tại0x20100000 | 516852 | 516852 |
| Model reservation SRAM | 786432 | 786432 |
| Tensor arena, align32 | 65536 | 65536 |
| Adapter persistent | 6056 | 6048 |
| Frontend persistent | 2072 | 2072 |
| CMSIS FFT context | 80 | 80 |
| RW_RAM region, toàn firmware | 78168 | 77936 |
| Noncacheable DMA region | 320 | 320 |
| Stack DTCM reservation | 8192 | 8192 |
| Heap reservation hiện hữu | 2048 | 2048 |
| Tổng linked RAM execution regions, gồm ITCM/DTCM/model/stack/heap/DMA | 608776 | 606380 |

DMA payload: RX2x16x2=64B, TX2x16x4=128B, silence16x4=64B; descriptors/padding
nằm trong320B. Không dùng Vela5.95KiB feature-map làm tổng RAM. Map tổng ROM
Debug626048B, Release611496B; BIN bao gồm padding nên kích thước khác ROM totals.
Tổng footprint phải cộng model SRAM và các execution regions không chồng nhau,
không chỉ đọc ELF RW total (model được phân loại RO nhưng thực thi từ SRAM).
`memory_totals.json` ghi tổng các region không chồng nhau; khoảng SRAM reservation
chưa sử dụng không tính vào linked bytes, và không tự trở thành RAM reclaimable.

`deepfir_inference_get_memory()` có arena capacity/actual used khi init thành công;
Debug log `[MODEL_MEMORY]` cũng ghi actual allocation. **Chưa chạy AllocateTensors
trên board**, nên chưa chứng minh64KiB đủ. Stack reservation không phải watermark;
reconstruction cần scratch stack, đo stack headroom sau replay và chạy audio.
Immutable model cache initialization đứng trước AllocateTensors; DMA uncached,
descriptor ownership và barriers được audit trong source, coherency thật cần board.

## Chất lượng PTQ giữ nguyên

Đối chiếu `ptq/ptq_result.json`, selection lock và hai paired metrics, lưu trích
xuất `quality_review.json`. Bốn ngưỡng validation đạt; không dùng test chọn/tune lại.

| Test VoiceBank+DEMAND,824 câu | Giá trị | Delta FP32 |
| --- | ---: | ---: |
| PESQ-WB | 2.797777 | -0.008629 |
| STOI | 0.943140 | -0.000108 |
| ESTOI | 0.856613 | -0.000183 |
| SI-SDR dB | 18.038428 | +0.006607 |

Giữ chẩn đoán p244_155/SSN0dB validation STOI delta -0.03007958;
p257_326/bus7.5dB test PESQ delta -0.29606056. Test16/824 câu giảm STOI hơn0.01
so noisy mixture, cả PTQ lẫn FP32. Validation drift median ratio2.811069 >2.0;
`eligible=true` chỉ đủ coverage, không chứng minh drift dưới ngưỡng. Kết quả này
chưa chứng minh tiếng Việt hoặc hiệu quả trên người đeo máy trợ thính.

## Nghiệm thu và bước tiếp

| Gate | Trạng thái | Evidence/giới hạn |
| --- | --- | --- |
| 1 Artifact integrity | PASS |47 hashes, destination hashes, C array và BIN compiled bytes; final_audit/product_audit.json |
| 2 Architecture/source integration | PASS | native/architecture suite, single link inputs, custom-only profiles; no libs edits |
| 3 Host numeric correctness | PASS | final_regression.log, focused R2/CMSIS vectors; float tolerance như trên |
| 4 Debug/Release build/link | PASS | profile_restore_build_receipt.json, final_audit/product_debug/, final_audit/product_release/ |
| 5 Raw replay rerun | NOT_RUN | receipt Linux package PASS lịch sử; chưa có Linux distro/runtime canonical |
| 6 Compiled replay | BLOCKED | runner + replay image sẵn; thiếu authorized running M55M1/backend; chưa có mismatch số đo |
| 7 Board initialization/memory | BLOCKED | map/static capacity PASS; actual arena/stack/boot chưa chạy |
| 8 Full-hop deadline/schedule | BLOCKED | host logic PASS; IRQ/FIFO/skew, margin<1ms và long-soak chưa đo |
| 9 Physical audio/latency | NOT_RUN | chưa flash/chạy board, chưa đo acoustic latency/load/power |

Tiếp theo: xác nhận wiring custom và recovery, xin phép flash image replay cụ thể,
đo allocation/stack rồi chạy đủ hai trace. Sau zero mismatch, nạp product Release
được cho phép riêng, đo từng stage/tổng hop liên tục, deadline/overrun/underrun,
slot/sample alignment và RX/TX synchronization. Vela riêng controller ước tính
0.726957ms tại220MHz, chỉ còn0.273043ms danh nghĩa cho phần còn lại: rủi ro timing
chưa đóng. Nếu quá budget, profile stage lớn và tối ưu kèm parity checks.
Đo continuity/packing/gain/clipping, startup/fault reset và microphone-to-output
qua đường vật lý.64 mẫu là target alignment, không phải lệnh thêmFIFO hay
bằng chứng latency4ms;128 taps không chứng minh8ms. Không chuyển deployment_approved
thành true từ host/build/receipt lịch sử.

Receipt nghiệm thu máy đọc: `EVIDENCE/acceptance.json`. Bản binary/map/cache cuối
nằm trong `EVIDENCE/final_audit/product_debug/` và `product_release/`; các lần
chạy trước được giữ riêng. Test mailbox dùng byte-memory giả để kiểm wire offsets,
state feedback và failure rejection, không được tính là compiled execution.
