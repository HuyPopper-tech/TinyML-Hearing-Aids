"""Offline checks for Nuvoton configuration and the APROM programming boundary."""

from __future__ import annotations

import hashlib
import json
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SUPPORT = REPO_ROOT / "tools/openocd"
OPENOCD = SUPPORT / ".runtime/bin/openocd.exe"


class NuvotonOpenOcdConfigurationTests(unittest.TestCase):
    def test_uses_vendor_target_and_preserves_the_no_reset_probe_configuration(self):
        target = (SUPPORT / "m55m1-ch549.cfg").read_text(encoding="utf-8")
        adapter = (SUPPORT / "interface/ch549-cmsis-dap.cfg").read_text(encoding="utf-8")
        self.assertIn("set CPUDAPID 0x4c013477", target)
        self.assertIn("source [find target/numicroM55.cfg]", target)
        self.assertLess(target.index("target/numicroM55.cfg"), target.index("adapter speed 400"))
        self.assertIn("reset_config none", target)
        self.assertIn("adapter serial 0001A0000001", adapter)
        self.assertIn("cmsis_dap_backend hid", adapter)
        self.assertIn("transport select swd", adapter)
        self.assertNotIn("srst_only", adapter)
        self.assertFalse((REPO_ROOT / "tools/pyocd").exists())

    @unittest.skipUnless(shutil.which("powershell.exe"), "Windows installer check")
    def test_installer_rejects_an_untrusted_archive_before_extraction(self):
        with tempfile.TemporaryDirectory() as temporary:
            archive = Path(temporary) / "wrong.tar.gz"
            archive.write_bytes(b"not the official distribution")
            completed = subprocess.run([
                "powershell.exe", "-NoProfile", "-ExecutionPolicy", "Bypass",
                "-File", str(SUPPORT / "install.ps1"), "-ArchivePath", str(archive),
            ], capture_output=True, text=True, timeout=15)
        self.assertNotEqual(completed.returncode, 0)
        self.assertIn("Archive SHA-256 mismatch", completed.stdout + completed.stderr)

    @unittest.skipUnless(OPENOCD.is_file(), "Install the pinned OpenOCD runtime for interpreter checks")
    def test_installed_executable_and_vendor_target_match_the_pinned_distribution(self):
        manifest = json.loads((SUPPORT / "distribution.json").read_text(encoding="utf-8"))
        for filename, digest in manifest["files"].items():
            self.assertEqual(hashlib.sha256((SUPPORT / ".runtime" / filename).read_bytes()).hexdigest(), digest)
        completed = subprocess.run([
            str(OPENOCD), "-s", str(SUPPORT), "-s", str(SUPPORT / ".runtime/share/openocd/scripts"),
            "-f", str(SUPPORT / "m55m1-ch549.cfg"), "-c", "echo [target names]", "-c", "shutdown",
        ], capture_output=True, text=True, timeout=10)
        self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        self.assertIn("cortex_m.cpu", completed.stdout + completed.stderr)


@unittest.skipUnless(OPENOCD.is_file(), "Install the pinned OpenOCD runtime for interpreter checks")
class ApromProgrammingTests(unittest.TestCase):
    def run_program(self, image: Path, *, failure: str = "", pdid: int = 0xA2000120,
                    dhcsr: int = 0x00130003, reset_dhcsr: int = 0x00130003,
                    cwd: Path = REPO_ROOT):
        with tempfile.TemporaryDirectory(prefix="openocd harness ") as temporary:
            harness = Path(temporary) / "check.tcl"
            harness.write_text(
                f"set image {{{image.as_posix()}}}\nset failure {{{failure}}}\nset pdid {pdid}\n"
                f"set dhcsr {dhcsr}\nset reset_dhcsr {reset_dhcsr}\n" + r'''
set calls {}
proc cortex_m.dap {args} {
    lappend ::calls [linsert $args 0 dap]
}
proc halt {timeout} {
    lappend ::calls [list halt $timeout]
    if {$::failure eq "halt"} {error "injected halt failure"}
}
proc reset {mode} {
    lappend ::calls [list reset $mode]
    if {$::failure eq "reset"} {error "injected reset failure"}
    set ::dhcsr $::reset_dhcsr
}
proc read_memory {address width count} {
    lappend ::calls [list read $address $width $count]
    if {$address == 0x40000000} {return [list $::pdid]}
    if {$address == 0xE000EDF0} {return [list $::dhcsr]}
    error "unexpected register read"
}
proc flash {args} {
    lappend ::calls [linsert $args 0 flash]
    if {$::failure eq [lindex $args 0]} {error "injected flash failure"}
}
proc verify_image {args} {
    lappend ::calls [linsert $args 0 verify]
    if {$::failure eq "verify"} {error "injected verify failure"}
}
set status [catch {m55m1_program $image} message]
echo "RESULT:${status}:$message"
foreach call $calls {echo "CALL:[join $call |]"}
''', encoding="utf-8")
            completed = subprocess.run([
                str(OPENOCD), "-f", str(SUPPORT / "m55m1-flash.tcl"),
                "-f", str(harness), "-c", "shutdown",
            ], capture_output=True, text=True, timeout=10, cwd=cwd)
        output = completed.stdout + completed.stderr
        self.assertEqual(completed.returncode, 0, output)
        calls = [line[5:] for line in output.splitlines() if line.startswith("CALL:")]
        return output, calls

    def test_cortex_monitor_image_resolves_without_windows_path_escaping(self):
        launch = json.loads((REPO_ROOT / ".vscode/launch.json").read_text(encoding="utf-8"))
        configs = [c for c in launch["configurations"] if c["request"] == "launch"]
        self.assertEqual(len(configs), 3)
        for config in configs:
            with self.subTest(profile=config["name"]):
                self.assertEqual(config["cwd"], "${workspaceFolder}")
                command = config["overrideLaunchCommands"][0]
                match = re.fullmatch(r"monitor m55m1_program \{(.+)\}", command)
                self.assertIsNotNone(match, command)
                image_name = match.group(1)
                self.assertNotIn("${", image_name, "workspace expansion introduces Windows backslashes into GDB MI")
                self.assertNotIn("\\", image_name)
                self.assertFalse(Path(image_name).is_absolute())
                with tempfile.TemporaryDirectory(prefix="OpenOCD workspace with spaces ") as temporary:
                    working = Path(temporary)
                    image = working / image_name
                    image.parent.mkdir(parents=True)
                    image.write_bytes(b"\x00" * 8)
                    output, calls = self.run_program(Path(image_name), cwd=working)
                self.assertIn("RESULT:0:", output)
                self.assertEqual(calls[-1], f"verify|{image_name}|0x00100000|bin")

    def test_writes_only_aprom_and_verifies_before_reporting_success(self):
        with tempfile.TemporaryDirectory(prefix="firmware with spaces ") as temporary:
            image = Path(temporary) / "application image.bin"
            image.write_bytes(b"\x00" * 8)
            output, calls = self.run_program(image)
        self.assertIn("RESULT:0:", output)
        self.assertEqual(calls, [
            "dap|apcsw|0|0x40000000", "halt|1000",
            "read|0xE000EDF0|32|1", "read|0xE000EDF0|32|1", "read|0x40000000|32|1",
            "reset|halt",
            "dap|apcsw|0|0x40000000", "halt|1000",
            "read|0xE000EDF0|32|1", "read|0xE000EDF0|32|1", "read|0x40000000|32|1",
            "flash|probe|0",
            f"flash|write_image|erase|{image.as_posix()}|0x00100000|bin",
            f"verify|{image.as_posix()}|0x00100000|bin",
        ])

    def test_rejects_blocked_or_unstable_debug_before_any_flash_operation(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "firmware.bin"
            image.write_bytes(b"\x00" * 8)
            for state in [0x02810001, 0x00030003, 0x00110001, 0x001B0003]:
                with self.subTest(state=hex(state)):
                    for kwargs in [{"dhcsr": state}, {"reset_dhcsr": state}]:
                        output, calls = self.run_program(image, **kwargs)
                        self.assertIn("RESULT:1:", output)
                        self.assertFalse(any(c.startswith("flash|") for c in calls), calls)
                        self.assertNotIn("verification completed", output)

    def test_rejects_invalid_files_before_accessing_the_target(self):
        with tempfile.TemporaryDirectory() as temporary:
            for filename, size in [("empty.bin", 0), ("short.bin", 7), ("too-large.bin", 0x200001), ("wrong.axf", 8)]:
                with self.subTest(filename=filename):
                    image = Path(temporary) / filename
                    image.write_bytes(b"\x00" * size)
                    output, calls = self.run_program(image)
                    self.assertIn("RESULT:1:", output)
                    self.assertEqual(calls, [])
            output, calls = self.run_program(Path(temporary) / "missing.bin")
            self.assertIn("RESULT:1:", output)
            self.assertEqual(calls, [])

    def test_wrong_device_and_operation_failures_stop_the_programming_sequence(self):
        with tempfile.TemporaryDirectory() as temporary:
            image = Path(temporary) / "firmware.bin"
            image.write_bytes(b"\x00" * 8)
            output, calls = self.run_program(image, pdid=0)
            self.assertIn("RESULT:1:Expected M55M1R2LJAE", output)
            self.assertEqual(len(calls), 5)
            for failure, count in [("halt", 2), ("reset", 6), ("probe", 12), ("write_image", 13), ("verify", 14)]:
                with self.subTest(failure=failure):
                    output, calls = self.run_program(image, failure=failure)
                    self.assertIn("RESULT:1:", output)
                    self.assertEqual(len(calls), count)
                    self.assertNotIn("verification completed", output)


if __name__ == "__main__":
    unittest.main()
