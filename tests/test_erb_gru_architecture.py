"""Static dependency contracts for the production ERB-GRU firmware."""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ErbGruArchitectureTests(unittest.TestCase):
    def read(self, path: str) -> str:
        return (ROOT / path).read_text(encoding="utf-8")

    def test_app_depends_only_on_the_audio_pipeline_service(self):
        app = self.read("app/main.c")
        project_includes = re.findall(r'#include "([^"]+)"', app)

        self.assertEqual(
            project_includes,
            ["services/audio_pipeline/audio_pipeline.h"],
        )
        for forbidden in (
            "m55m1_",
            "tlv320dac3100_",
            "erb_gru_",
            "audio_telemetry_",
            "NuMicro.h",
        ):
            self.assertNotIn(forbidden, app)

    def test_board_headers_never_depend_on_driver_headers(self):
        violations = {}
        for header in sorted((ROOT / "board").glob("*.h")):
            driver_includes = re.findall(
                r'#include "(drivers/[^"]+)"',
                header.read_text(encoding="utf-8"),
            )
            if driver_includes:
                violations[header.name] = driver_includes

        self.assertEqual(violations, {})

    def test_production_target_uses_the_production_audio_board_module(self):
        cmake = self.read("CMakeLists.txt")

        self.assertIn("${PROJ_ROOT}/board/m55m1_audio.c", cmake)
        self.assertNotIn(
            "${PROJ_ROOT}/board/m55m1_tlv320dac3100_test.c",
            cmake,
        )

    def test_ethosu_timing_uses_the_production_timebase(self):
        board = self.read("board/m55m1_ethosu_u55.c")

        self.assertIn('#include "board/m55m1_timebase.h"', board)
        self.assertIn("m55m1_timebase_cycles_now()", board)
        self.assertIn("m55m1_timebase_cycles_elapsed(", board)
        self.assertNotIn("m55m1_tlv320dac3100_test", board)

    def test_enhancer_uses_the_service_internal_frontend_assets(self):
        enhancer = self.read("services/erb_gru/erb_gru_audio_enhancer.cpp")

        self.assertIn(
            '#include "services/erb_gru/erb_gru_frontend_assets.h"',
            enhancer,
        )
        self.assertNotIn("models/erb_gru/generated/erb_gru_assets.h", enhancer)

    def test_model_bytes_and_deploy_contract_enter_only_inference(self):
        source_roots = ("app", "board", "common", "drivers", "services")
        model_data_users = []
        umbrella_users = []
        for source_root in source_roots:
            for source in (ROOT / source_root).rglob("*"):
                if source.suffix not in {".c", ".cc", ".cpp", ".h", ".hpp"}:
                    continue
                text = source.read_text(encoding="utf-8")
                relative = source.relative_to(ROOT).as_posix()
                if "erb_gru_model_data.h" in text:
                    model_data_users.append(relative)
                if "models/erb_gru/generated/erb_gru_assets.h" in text:
                    umbrella_users.append(relative)

        self.assertEqual(
            model_data_users,
            ["services/erb_gru/erb_gru_inference.cpp"],
        )
        self.assertEqual(umbrella_users, [])
        inference = self.read("services/erb_gru/erb_gru_inference.cpp")
        self.assertIn(
            '#include "models/erb_gru/generated/erb_gru_deploy_contract.h"',
            inference,
        )

    def test_audio_pipeline_exposes_exactly_the_four_lifecycle_functions(self):
        header = ROOT / "services" / "audio_pipeline" / "audio_pipeline.h"

        self.assertTrue(header.exists(), "audio pipeline public header is missing")
        declarations = re.findall(
            r"^(?:bool|void)\s+(audio_pipeline_[a-z_]+)\s*\([^;]*\);",
            header.read_text(encoding="utf-8"),
            flags=re.MULTILINE,
        )
        self.assertEqual(
            declarations,
            [
                "audio_pipeline_init",
                "audio_pipeline_start",
                "audio_pipeline_poll",
                "audio_pipeline_stop_safe",
            ],
        )


if __name__ == "__main__":
    unittest.main()
