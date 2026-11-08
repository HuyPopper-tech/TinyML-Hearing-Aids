"""Arm a one-second board capture and save paired PCM input/FIR WAV files."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from datetime import datetime
import json
import math
from pathlib import Path
import struct
import sys
import time
import wave
import zlib


MAGIC = b"DFIRPCM1"
VERSION = 1
HEADER = struct.Struct("<8sHHIIIHHhhIIII")
READY_LINE = b"READY"
ARMED_LINE = b"CAPTURE_ARMED"
CAPTURE_COMMAND = b"C"
MAX_SAMPLE_COUNT = 16000


class CaptureProtocolError(ValueError):
    """The board packet is incomplete, inconsistent, or failed verification."""


@dataclass(frozen=True)
class CapturePacket:
    sample_rate: int
    sample_count: int
    dmic_gain_db: int
    input_clip_count: int
    output_clip_count: int
    sequence_gap_count: int
    input_pcm: bytes
    output_pcm: bytes


def _read_exact(stream, count: int, deadline: float) -> bytes:
    result = bytearray()
    while len(result) < count:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"Timed out after {len(result)} of {count} bytes")
        part = stream.read(count - len(result))
        if part:
            result.extend(part)
    return bytes(result)


def wait_for_control_line(stream, expected: bytes, timeout_seconds: float) -> None:
    deadline = time.monotonic() + timeout_seconds
    line = bytearray()
    while time.monotonic() < deadline:
        value = stream.read(1)
        if not value:
            continue
        if value in (b"\r", b"\n"):
            if bytes(line).strip() == expected:
                return
            line.clear()
        elif len(line) < 128:
            line.extend(value)
        else:
            line.clear()
    raise TimeoutError(f"Timed out waiting for device line {expected.decode('ascii')!r}")


def read_capture_packet(stream, timeout_seconds: float = 30.0) -> CapturePacket:
    deadline = time.monotonic() + timeout_seconds
    window = bytearray()
    while bytes(window) != MAGIC:
        window.extend(_read_exact(stream, 1, deadline))
        if len(window) > len(MAGIC):
            del window[0]

    header_bytes = bytes(window) + _read_exact(stream, HEADER.size - len(MAGIC), deadline)
    (magic, version, header_size, status, sample_rate, sample_count, hop_samples,
     _reserved, dmic_gain_db, _reserved_gain, input_clips, output_clips,
     sequence_gaps, payload_crc) = HEADER.unpack(header_bytes)
    if magic != MAGIC or version != VERSION or header_size != HEADER.size:
        raise CaptureProtocolError("Unsupported or malformed capture header")
    if status != 0:
        raise CaptureProtocolError(f"Device capture failed with status {status}")
    if (sample_rate != 16000 or hop_samples != 16 or sample_count == 0 or
            sample_count > MAX_SAMPLE_COUNT):
        raise CaptureProtocolError("Capture sample rate, hop, or sample count is invalid")

    payload = _read_exact(stream, sample_count * 4, deadline)
    if (zlib.crc32(payload) & 0xFFFFFFFF) != payload_crc:
        raise CaptureProtocolError("Capture payload CRC mismatch")

    input_pcm = bytearray(sample_count * 2)
    output_pcm = bytearray(sample_count * 2)
    for sample in range(sample_count):
        payload_offset = sample * 4
        pcm_offset = sample * 2
        input_pcm[pcm_offset:pcm_offset + 2] = payload[payload_offset:payload_offset + 2]
        output_pcm[pcm_offset:pcm_offset + 2] = payload[payload_offset + 2:payload_offset + 4]
    return CapturePacket(
        sample_rate, sample_count, dmic_gain_db, input_clips, output_clips,
        sequence_gaps, bytes(input_pcm), bytes(output_pcm),
    )


def _analyze_pair(input_pcm: bytes, output_pcm: bytes) -> dict[str, int | float | None]:
    input_square_sum = 0
    output_square_sum = 0
    difference_square_sum = 0
    input_sum = 0
    output_sum = 0
    product_sum = 0
    different_samples = 0
    max_difference = 0
    input_peak = 0
    output_peak = 0
    sample_count = len(input_pcm) // 2

    for input_record, output_record in zip(
        struct.iter_unpack("<h", input_pcm),
        struct.iter_unpack("<h", output_pcm),
    ):
        source = input_record[0]
        result = output_record[0]
        difference = result - source
        input_sum += source
        output_sum += result
        product_sum += source * result
        input_square_sum += source * source
        output_square_sum += result * result
        difference_square_sum += difference * difference
        input_peak = max(input_peak, abs(source))
        output_peak = max(output_peak, abs(result))
        max_difference = max(max_difference, abs(difference))
        different_samples += source != result

    input_rms = math.sqrt(input_square_sum / sample_count)
    output_rms = math.sqrt(output_square_sum / sample_count)
    difference_rms = math.sqrt(difference_square_sum / sample_count)
    rms_db = (20.0 * math.log10(output_rms / input_rms)
              if input_rms > 0.0 and output_rms > 0.0 else None)
    covariance = product_sum - input_sum * output_sum / sample_count
    input_variance = input_square_sum - input_sum * input_sum / sample_count
    output_variance = output_square_sum - output_sum * output_sum / sample_count
    denominator = math.sqrt(max(0.0, input_variance) * max(0.0, output_variance))
    correlation = covariance / denominator if denominator > 0.0 else None

    return {
        "input_rms": input_rms,
        "output_rms": output_rms,
        "output_to_input_rms_db": rms_db,
        "difference_rms": difference_rms,
        "correlation": correlation,
        "input_peak": input_peak,
        "output_peak": output_peak,
        "max_absolute_sample_difference": max_difference,
        "different_sample_count": different_samples,
    }


def _write_wav(path: Path, pcm: bytes, sample_rate: int) -> None:
    with wave.open(str(path), "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm)


def write_capture_files(packet: CapturePacket, output_directory: Path) -> dict[str, Path]:
    output_directory.mkdir(parents=True, exist_ok=True)
    input_path = output_directory / "input.wav"
    output_path = output_directory / "fir_output.wav"
    report_path = output_directory / "report.json"
    analysis = _analyze_pair(packet.input_pcm, packet.output_pcm)
    _write_wav(input_path, packet.input_pcm, packet.sample_rate)
    _write_wav(output_path, packet.output_pcm, packet.sample_rate)
    report = {
        "sample_rate_hz": packet.sample_rate,
        "samples": packet.sample_count,
        "duration_seconds": packet.sample_count / packet.sample_rate,
        "dmic_gain_db": packet.dmic_gain_db,
        "input_clip_count": packet.input_clip_count,
        "output_clip_count": packet.output_clip_count,
        "sequence_gap_count": packet.sequence_gap_count,
        **analysis,
        "interpretation_limit": (
            "Input/output level change shows FIR effect, not speech SNR improvement; "
            "that requires a clean reference."
        ),
    }
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return {"input": input_path, "output": output_path, "report": report_path}


def _default_output_directory() -> Path:
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return Path(f"deepfir_capture_{timestamp}")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("port", help="UART COM port, for example COM5")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=60.0,
                        help="seconds to wait for firmware responses")
    parser.add_argument("--out-dir", type=Path, default=None)
    args = parser.parse_args(argv)

    try:
        import serial
    except ImportError:
        parser.error("pyserial is required; install it with: python -m pip install pyserial")

    output_directory = args.out_dir or _default_output_directory()
    try:
        with serial.Serial(args.port, args.baud, timeout=0.1,
                           rtscts=False, xonxoff=False) as port:
            print(f"Waiting for firmware READY on {args.port}; reset the board now.")
            wait_for_control_line(port, READY_LINE, args.timeout)
            input("Firmware ready. Press Enter when ready to speak or capture room noise: ")
            port.write(CAPTURE_COMMAND)
            port.flush()
            wait_for_control_line(port, ARMED_LINE, args.timeout)
            print("Capturing one second. Speak or keep the room quiet now.")
            packet = read_capture_packet(port, args.timeout)
        paths = write_capture_files(packet, output_directory)
    except (OSError, TimeoutError, CaptureProtocolError) as error:
        print(f"Capture failed: {error}", file=sys.stderr)
        return 1

    print(f"Input WAV:  {paths['input'].resolve()}")
    print(f"FIR WAV:    {paths['output'].resolve()}")
    print(f"Report:     {paths['report'].resolve()}")
    print(f"FIR output/input RMS: {json.loads(paths['report'].read_text(encoding='utf-8'))['output_to_input_rms_db']} dB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
