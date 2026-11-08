"""Host protocol tests for paired DeepFIR UART PCM capture."""

from pathlib import Path
import io
import json
import struct
import sys
import unittest
import wave
import zlib


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from tools.receive_deepfir_audio import (  # noqa: E402
    CaptureProtocolError,
    HEADER,
    MAGIC,
    read_capture_packet,
    write_capture_files,
)


def make_packet(input_pcm: bytes, output_pcm: bytes) -> bytes:
    sample_count = len(input_pcm) // 2
    payload = bytearray()
    for offset in range(0, len(input_pcm), 2):
        payload.extend(input_pcm[offset:offset + 2])
        payload.extend(output_pcm[offset:offset + 2])
    header = HEADER.pack(
        MAGIC, 1, HEADER.size, 0, 16000, sample_count, 16, 0,
        30, 0, 0, 0, 0, zlib.crc32(payload) & 0xFFFFFFFF,
    )
    return header + payload


class DeepfirUartCaptureHostTests(unittest.TestCase):
    def test_reads_framed_interleaved_capture_after_text_preamble(self):
        input_pcm = struct.pack("<4h", 100, -100, 200, -200)
        output_pcm = struct.pack("<4h", 50, -50, 100, -100)
        packet = read_capture_packet(
            io.BytesIO(b"boot log\r\n" + make_packet(input_pcm, output_pcm)),
            timeout_seconds=1,
        )

        self.assertEqual(packet.input_pcm, input_pcm)
        self.assertEqual(packet.output_pcm, output_pcm)
        self.assertEqual(packet.sample_rate, 16000)
        self.assertEqual(packet.sample_count, 4)
        self.assertEqual(packet.dmic_gain_db, 30)

    def test_rejects_corrupt_payload_before_writing_wavs(self):
        input_pcm = struct.pack("<2h", 100, -100)
        output_pcm = struct.pack("<2h", 50, -50)
        corrupt = bytearray(make_packet(input_pcm, output_pcm))
        corrupt[-1] ^= 0x80

        with self.assertRaisesRegex(CaptureProtocolError, "CRC"):
            read_capture_packet(io.BytesIO(corrupt), timeout_seconds=1)

    def test_writes_paired_wavs_and_reports_measured_level_change(self):
        input_pcm = struct.pack("<4h", 100, -100, 200, -200)
        output_pcm = struct.pack("<4h", 50, -50, 100, -100)
        packet = read_capture_packet(
            io.BytesIO(make_packet(input_pcm, output_pcm)), timeout_seconds=1
        )
        output_directory = ROOT / "build" / "test_deepfir_uart_capture"
        output_directory.mkdir(parents=True, exist_ok=True)
        paths = write_capture_files(packet, output_directory)
        self.addCleanup(lambda: output_directory.rmdir())
        self.addCleanup(lambda: [path.unlink(missing_ok=True) for path in paths.values()])
        self.assertEqual(set(paths), {"input", "output", "report"})
        with wave.open(str(paths["input"]), "rb") as wav_file:
            self.assertEqual(wav_file.getparams()[:4], (1, 2, 16000, 4))
            self.assertEqual(wav_file.readframes(4), input_pcm)
        with wave.open(str(paths["output"]), "rb") as wav_file:
            self.assertEqual(wav_file.readframes(4), output_pcm)
        report = json.loads(paths["report"].read_text(encoding="utf-8"))
        self.assertEqual(report["samples"], 4)
        self.assertAlmostEqual(report["output_to_input_rms_db"], -6.0206, places=3)
        self.assertEqual(report["different_sample_count"], 4)

    def test_rejects_device_error_packet(self):
        error_header = HEADER.pack(
            MAGIC, 1, HEADER.size, 1, 16000, 0, 16, 0,
            30, 0, 0, 0, 0, 0,
        )
        with self.assertRaisesRegex(CaptureProtocolError, "Device capture failed with status 1"):
            read_capture_packet(io.BytesIO(error_header), timeout_seconds=1)

    def test_rejects_sample_count_above_one_second_before_reading_payload(self):
        header = HEADER.pack(
            MAGIC, 1, HEADER.size, 0, 16000, 16001, 16, 0,
            30, 0, 0, 0, 0, 0,
        )
        with self.assertRaisesRegex(CaptureProtocolError, "sample count"):
            read_capture_packet(io.BytesIO(header), timeout_seconds=1)


if __name__ == "__main__":
    unittest.main()
