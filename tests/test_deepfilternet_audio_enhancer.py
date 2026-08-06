from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
KISSFFT = ROOT / "libs" / "tensorflow" / "third_party" / "kissfft"
SOURCE = ROOT / "services" / "deepfilternet" / "deepfilternet_audio_enhancer.cpp"


class DeepFilterNetAudioEnhancerTests(unittest.TestCase):
    def test_scalar_oracle_and_generated_scenarios(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            main = Path(directory) / "oracle.cpp"
            main.write_text(
                f"""
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include "models/deepfilternet/generated/dsp_constants.h"
#include "models/deepfilternet/generated/deepfilternet_frontend_contract.h"
#include "models/deepfilternet/generated/golden/golden_silence.h"
#include "models/deepfilternet/generated/golden/golden_impulse.h"
#include "models/deepfilternet/generated/golden/golden_sine_440.h"
#include "models/deepfilternet/generated/golden/golden_sine_3000.h"
#include "models/deepfilternet/generated/golden/golden_noise.h"
#include "services/deepfilternet/deepfilternet_inference.h"
static bool g_fail = true;
extern "C" bool deepfilternet_inference_init(void) {{ return true; }}
extern "C" void deepfilternet_inference_reset(void) {{ }}
extern "C" bool deepfilternet_inference_invoke(const int8_t erb[32], const int8_t complex[288], int8_t gain[257], int8_t df[1440], int8_t *alpha) {{
    (void)erb; (void)complex; (void)gain; (void)df; (void)alpha;
    return !g_fail;
}}
#define MODEL_GOLDEN_TEST_DATA 1
#include "{SOURCE.as_posix()}"

static int quant(float value, float scale, int zero_point) {{
    long result = std::lrintf(value / scale) + zero_point;
    if (result < -128) {{ result = -128; }}
    if (result > 127) {{ result = 127; }}
    return static_cast<int>(result);
}}
static void require(bool condition, int code) {{ if (!condition) {{ std::exit(code); }} }}

static void run_fixture(const int16_t *fixture) {{
    int16_t output[DEEPFILTERNET_AUDIO_HOP_SAMPLES] = {{}};
    deepfilternet_audio_enhancer_reset();
    require(deepfilternet_audio_enhancer_process(fixture, output) == DEEPFILTERNET_AUDIO_RESULT_PREROLL_SILENCE, 10);
    require(deepfilternet_audio_enhancer_process(fixture + 256, output) == DEEPFILTERNET_AUDIO_RESULT_UNITY_FALLBACK, 11);
    require(deepfilternet_audio_enhancer_process(fixture + 256, output) == DEEPFILTERNET_AUDIO_RESULT_UNITY_FALLBACK, 12);
    for (unsigned i = 8; i + 8 < 256; ++i) {{
        require(std::abs(static_cast<int>(output[i]) - fixture[256 + i]) <= 1, 13);
    }}
    int16_t boundary[256] = {{}};
    require(deepfilternet_audio_enhancer_flush(boundary), 14);
    for (unsigned i = 0; i < 256; ++i) {{
        const double phase = 2.0 * 3.14159265358979323846 * (256.0 + i) / 512.0;
        const int expected = static_cast<int>(std::lrint(static_cast<double>(fixture[256 + i]) * 0.5 * (1.0 - std::cos(phase))));
        require(std::abs(static_cast<int>(boundary[i]) - expected) <= 1, 15);
    }}
    require(deepfilternet_audio_enhancer_flush(boundary), 16);
    for (int16_t sample : boundary) {{ require(sample == 0, 17); }}
}}

int main() {{
    require(deepfilternet_audio_enhancer_init(), 1);
    require(apply_input_gain(100) == 100 && apply_input_gain(-100) == -100, 30);
    require(quantize(0.5f, 1.0f, 0) == 0 && quantize(1.5f, 1.0f, 0) == 2 && quantize(2.5f, 1.0f, 0) == 2, 2);
    run_fixture(dfn_fixture_silence);
    run_fixture(dfn_fixture_impulse);
    run_fixture(dfn_fixture_sine_440);
    run_fixture(dfn_fixture_sine_3000);
    run_fixture(dfn_fixture_noise);

    deepfilternet_audio_enhancer_reset();
    g_hops_seen = 0; append_hop(dfn_fixture_sine_440); g_hops_seen = 1; append_hop(dfn_fixture_sine_440 + 256); analyze();
    for (unsigned bin = 0; bin < 144; ++bin) {{
        require(g_complex_q[2 * bin] == quant(g_spectrum[bin].r, DFN_INPUTS_COMPLEX_FEATURE_SCALE, DFN_INPUTS_COMPLEX_FEATURE_ZERO_POINT), 20);
        require(g_complex_q[2 * bin + 1] == quant(g_spectrum[bin].i, DFN_INPUTS_COMPLEX_FEATURE_SCALE, DFN_INPUTS_COMPLEX_FEATURE_ZERO_POINT), 21);
    }}
    float independent_magnitude[257] = {{}};
    for (unsigned bin = 0; bin < 257; ++bin) {{
        independent_magnitude[bin] = std::sqrt(g_spectrum[bin].r * g_spectrum[bin].r + g_spectrum[bin].i * g_spectrum[bin].i);
        require(std::fabs(g_magnitude[bin] - independent_magnitude[bin]) < 1.0e-5f, 22);
    }}
    for (unsigned band = 0; band < 32; ++band) {{
        float energy = 0.0f;
        for (unsigned bin = 0; bin < 257; ++bin) {{ energy += independent_magnitude[bin] * dfn_erb_analysis_257x32[bin * 32 + band]; }}
        const float feature = std::log((energy > 1.0e-6f) ? energy : 1.0e-6f);
        require(g_erb_q[band] == quant(feature, DFN_INPUTS_ERB_FEATURE_SCALE, DFN_INPUTS_ERB_FEATURE_ZERO_POINT), 23);
    }}

    clear_df_history();
    for (unsigned bin = 0; bin < 257; ++bin) {{
        g_spectrum[bin].r = (bin == 0 || bin == 200 || bin == 256) ? 10.0f : 0.0f;
        g_spectrum[bin].i = 0.0f; g_gain_q[bin] = 127;
    }}
    std::memset(g_df_q, 27, sizeof(g_df_q));
    g_alpha_q = -128; postprocess(true);
    const float gain = dequantize(127, DFN_OUTPUTS_GAIN_BINS_SCALE, DFN_OUTPUTS_GAIN_BINS_ZERO_POINT);
    require(std::fabs(g_filtered[256].r - 10.0f * gain) < 1.0e-5f, 23);
    require(std::fabs(g_filtered[200].r - 10.0f * gain) < 1.0e-5f, 24);
    require(std::fabs(g_filtered[0].r - 10.0f * gain) < 1.0e-5f, 25);

    clear_df_history();
    g_df_history[0][0].r = 7.0f;
    g_df_q[(1 * 144 + 0) * 2] = static_cast<int8_t>(quant(0.5f, DFN_OUTPUTS_DF_COEFFICIENTS_SCALE, DFN_OUTPUTS_DF_COEFFICIENTS_ZERO_POINT));
    g_alpha_q = 127; postprocess(true);
    const float alpha = dequantize(127, DFN_OUTPUTS_ALPHA_SCALE, DFN_OUTPUTS_ALPHA_ZERO_POINT);
    const float coeff = dequantize(g_df_q[(1 * 144 + 0) * 2], DFN_OUTPUTS_DF_COEFFICIENTS_SCALE, DFN_OUTPUTS_DF_COEFFICIENTS_ZERO_POINT);
    require(std::fabs(g_filtered[0].r - (alpha * coeff * 7.0f + (1.0f - alpha) * 10.0f * gain)) < 1.0e-4f, 26);
    require(std::fabs(g_filtered[200].r - 10.0f * gain) < 1.0e-5f, 27);
    require(std::fabs(g_filtered[256].r - 10.0f * gain) < 1.0e-5f, 28);
    g_alpha_q = -128; g_df_q[(1 * 144 + 0) * 2] = 127; postprocess(true);
    require(std::fabs(g_filtered[0].r - 10.0f * gain) < 1.0e-5f, 29);
    return 0;
}}
""",
                encoding="utf-8",
            )
            binary = Path(directory) / "oracle.exe"
            result = subprocess.run(
                [
                    "g++", "-std=c++11", "-O2", "-I", str(ROOT), "-I", str(KISSFFT),
                    str(main), str(KISSFFT / "kiss_fft.c"), str(KISSFFT / "tools" / "kiss_fftr.c"),
                    "-lm", "-o", str(binary),
                ],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
            self.assertEqual(run.returncode, 0, f"oracle exit {run.returncode}: {run.stderr}")

    def test_input_gain_contract_is_clamped_and_telemetry_ready(self) -> None:
        source = SOURCE.read_text(encoding="utf-8")
        header = (ROOT / "services/deepfilternet/deepfilternet_audio_enhancer.h").read_text(
            encoding="utf-8"
        )
        self.assertIn("M55M1_DEEPFILTERNET_INPUT_GAIN_DB", source)
        self.assertIn("apply_input_gain", source)
        self.assertIn("model_input_peak", header)
        self.assertIn("model_input_clip_count", header)


if __name__ == "__main__":
    unittest.main()
