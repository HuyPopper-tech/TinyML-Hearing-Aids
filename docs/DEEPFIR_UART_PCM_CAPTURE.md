# DeepFIR paired PCM capture over UART

This opt-in Release image captures one second of synchronized DMIC input and
DeepFIR/FIR output. It is a diagnostic image: after sending the capture to the
host, audio stays stopped until the board is reset.

## Build

From the firmware repository root:

```powershell
rtk proxy cmd /c build.bat release config
rtk cmake -S . -B build -DM55M1_AUDIO_UART_PCM_CAPTURE=ON
rtk cmake --build build
```

Use `build/M55M1_GestureAI.bin` and its matching AXF, after separate flash
authorization. This opt-in replaces the Release image in the existing directory;
it does not add a profile. Restore the product with `build.bat release build`,
which reconfigures capture OFF. See [R2 integration](r2_custom_integration.md).

## Connect and capture

UART2 uses PC4 RX and PC5 TX at 115200 baud, 8N1. Use the board's existing
UART2 log connection, or a 3.3 V USB-UART adapter: adapter TX to PC4, adapter
RX to PC5, and common GND. Do not connect adapter VCC. Keep PF0/PF1 available
for the board's SWD recovery path.

Install pyserial if needed, then start the receiver before resetting the board:

```powershell
rtk proxy python -m pip install pyserial
rtk proxy python tools/receive_deepfir_audio.py COM5 --out-dir D:\DeepFIR-captures\quiet
```

Replace `COM5` with the UART2 COM port. The receiver waits for `READY`; reset
the board when prompted. The audio pipeline then runs normally until you press
Enter. At that prompt, leave the room quiet or speak for the next one second.
The firmware sends `CAPTURE_ARMED`, records 16,000 aligned input/output sample
pairs, stops audio, and transfers the packet. At 115200 baud the 64,000-byte
PCM payload takes about 5.6 seconds to send.

The receiver validates the packet header and CRC before writing:

- `input.wav`: DMIC PCM16 before model processing.
- `fir_output.wav`: PCM16 after DeepFIR/FIR and before DAC playback.
- `report.json`: gain, clipping and sequence-gap counts, RMS levels, output to
  input RMS ratio, correlation, and sample differences.

The pair locates whether noise is already present at the digitized DMIC input
or changes in the DeepFIR output before DAC playback. If both WAVs are clean
but headphone playback is noisy, inspect the DAC/analog path; this capture does
not record the analog output.

Repeat the process after resetting the board to capture another segment. For a
quiet-room sample, keep silent after pressing Enter; for speech, say a short
phrase during the capture window. A nonzero sequence-gap count means the pair
has missing audio blocks and should not be used for a detailed comparison.

## Packet and interpretation

The packet starts with `DFIRPCM1`, version 1, a 48-byte little-endian header,
then interleaved `(input_pcm16, fir_output_pcm16)` pairs and a CRC-32 stored in
the header. If inference fails, the board sends a status-only error packet
instead of presenting partial data as a valid WAV pair.

The paired files reveal whether the live filter changes the signal and how
much it changes the recorded segment. RMS reduction alone does not prove
speech enhancement: an aligned clean reference is needed to measure SNR
improvement.
