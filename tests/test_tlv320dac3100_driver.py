"""Static contracts for the TLV320DAC3100 headphone playback feature."""

from __future__ import annotations

import unittest
from pathlib import Path
import re


REPO_ROOT = Path(__file__).resolve().parents[1]


class TLV320DAC3100DriverTests(unittest.TestCase):
    def test_public_driver_api_and_realtime_profile_are_declared(self):
        header = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.h").read_text(
            encoding="utf-8"
        )

        for required in (
            "TLV320DAC3100_I2C_ADDRESS",
            "TLV320DAC3100_SAMPLE_RATE_HZ",
            "TLV320DAC3100_MCLK_HZ",
            "TLV320DAC3100_MONO_SAMPLES_PER_BLOCK",
            "tlv320dac3100_config_t",
            "tlv320dac3100_init",
            "tlv320dac3100_start",
            "tlv320dac3100_stop",
            "tlv320dac3100_submit_mono_block",
            "tlv320dac3100_set_digital_gain_db",
            "tlv320dac3100_mute",
            "tlv320dac3100_get_status",
            "tlv320dac3100_recover",
            "tlv320dac3100_handle_pdma_irq",
            "NVT_NONCACHEABLE",
            "@brief",
        ):
            self.assertIn(required, header)

    def test_driver_declares_validated_16k_and_32k_ten_ms_profiles(self):
        """TinyML transport must select a complete profile before any DMA starts."""
        header = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.h").read_text(
            encoding="utf-8"
        )
        source = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.c").read_text(
            encoding="utf-8"
        )

        for required in (
            "TLV320DAC3100_AUDIO_PROFILE_16KHZ",
            "TLV320DAC3100_AUDIO_PROFILE_32KHZ",
            "TLV320DAC3100_16KHZ_MCLK_HZ",
            "TLV320DAC3100_32KHZ_MCLK_HZ",
            "TLV320DAC3100_16KHZ_SAMPLES_PER_BLOCK",
            "TLV320DAC3100_32KHZ_SAMPLES_PER_BLOCK",
            "TLV320DAC3100_BLOCK_DURATION_MS",
            "tlv320dac3100_audio_profile_t",
            "max_digital_gain_db",
            "tlv320dac3100_set_channel_digital_gain_db",
            "uint16_t sample_count",
        ):
            self.assertIn(required, header)

        for required in (
            "tlv320dac3100_get_profile_sample_rate",
            "tlv320dac3100_get_profile_mclk",
            "tlv320dac3100_get_profile_samples_per_block",
            "TLV320DAC3100_DMA_MAX_FRAMES_PER_BLOCK",
            "g_context.has_queued_audio",
        ):
            self.assertIn(required, source)

    def test_audio_transport_carries_timestamped_blocks_without_isr_logging(self):
        board_header = (REPO_ROOT / "board" / "m55m1_audio.h").read_text(
            encoding="utf-8"
        )
        board_source = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )
        timebase = (
            REPO_ROOT / "board" / "m55m1_timebase.c"
        ).read_text(encoding="utf-8")
        driver_header = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.h").read_text(
            encoding="utf-8"
        )
        driver_source = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.c").read_text(
            encoding="utf-8"
        )

        for required in (
            "m55m1_audio_dmic_block_timing_t",
            "m55m1_audio_dmic_take_block_timed",
            "g_dmic_state.last_completed_cycle",
        ):
            self.assertIn(required, board_header + board_source)
        for required in (
            "m55m1_timebase_cycles_to_us",
            "SysTick_CTRL_CLKSOURCE_Msk | SysTick_CTRL_ENABLE_Msk",
            "M55M1_TIMEBASE_SYSTICK_MASK",
        ):
            self.assertIn(required, timebase)
        self.assertNotIn("DWT->", board_source)
        for required in (
            "tlv320dac3100_audio_block_tag_t",
            "tlv320dac3100_audio_completion_t",
            "tlv320dac3100_submit_mono_block_tagged",
            "tlv320dac3100_take_audio_completion",
            "g_context.slot_tags[slot]",
        ):
            self.assertIn(required, driver_header + driver_source)

        lppdma_handler = board_source[board_source.index("void LPPDMA_IRQHandler") :]
        pdma_handler = driver_source[driver_source.index("void tlv320dac3100_handle_pdma_irq") :]
        self.assertNotIn("printf", lppdma_handler)
        self.assertNotIn("printf", pdma_handler)

    def test_board_and_normal_passthrough_select_one_matching_profile(self):
        """I2S and DMIC must use the same profile; a mixed rate audibly changes pitch."""
        board_header = (REPO_ROOT / "board" / "m55m1_audio.h").read_text(
            encoding="utf-8"
        )
        board_source = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )
        transport = (
            REPO_ROOT / "drivers" / "audio_transport" / "audio_transport.c"
        ).read_text(encoding="utf-8")
        pipeline = (
            REPO_ROOT / "services" / "audio_pipeline" / "audio_pipeline.c"
        ).read_text(encoding="utf-8")

        for required in (
            "m55m1_audio_profile_t profile",
            "M55M1_AUDIO_SAFE_MAX_GAIN_DB",
            "CLK_I2SDIV_I2S0DIV(58)",
            "CLK_I2SDIV_I2S0DIV(29)",
            "DMIC_DOWNSAMPLE_128",
            "DMIC_DOWNSAMPLE_64",
            "m55m1_audio_profile_samples_per_block",
        ):
            self.assertIn(required, board_header + board_source)

        for required in (
            "TLV320DAC3100_AUDIO_PROFILE_16KHZ",
            "M55M1_AUDIO_PROFILE_16KHZ",
            "AUDIO_TRANSPORT_SAMPLES_PER_BLOCK",
        ):
            self.assertIn(required, transport)
        for required in (
            "erb_gru_audio_enhancer_process(g_dmic_block, g_enhanced_block)",
            "audio_transport_submit_tagged(",
        ):
            self.assertIn(required, pipeline)

    def test_board_separates_220mhz_core_from_exact_audio_pll(self):
        board_header = (
            REPO_ROOT / "board" / "m55m1_audio.h"
        ).read_text(encoding="utf-8")
        board_source = (
            REPO_ROOT / "board" / "m55m1_audio.c"
        ).read_text(encoding="utf-8")
        pipeline = (
            REPO_ROOT / "services" / "audio_pipeline" / "audio_pipeline.c"
        ).read_text(encoding="utf-8")
        board = board_header + board_source

        for required in (
            "M55M1_AUDIO_CORE_HZ       220000000UL",
            "M55M1_AUDIO_AUDIO_APLL_HZ 475136006UL",
            "CLK_APLL1_SELECT",
            "CLK_STATUS_APLL1STB_Msk",
            "CLK_SET_SCLKDIV(1UL)",
            "CLK_I2SSEL_I2S0SEL_APLL1_DIV2",
            "M55M1_AUDIO_BOARD_INIT_APLL1_TIMEOUT",
            "M55M1_AUDIO_BOARD_INIT_CLOCK_MISMATCH",
            "M55M1_AUDIO_BOARD_STAGE_APLL1_WAIT",
            "M55M1_AUDIO_BOARD_STAGE_APLL1_READY",
            "m55m1_audio_clock_snapshot_t",
            "m55m1_audio_get_clock_snapshot",
        ):
            self.assertIn(required, board)
        self.assertNotIn("CLK_I2SSEL_I2S0SEL_APLL0_DIV2", board_source)
        dmic_start = board_source[
            board_source.index("bool m55m1_audio_dmic_start") :
            board_source.index("bool m55m1_audio_dmic_take_block")
        ]
        self.assertIn("if (!g_clock_snapshot_valid)", dmic_start)
        self.assertEqual(dmic_start.count("CLK_EnableAPLL"), 1)
        for field in (
            "core_hz",
            "apll0_hz",
            "apll1_hz",
            "i2s_src_hz",
            "mclk_hz",
        ):
            self.assertIn(field, pipeline)

    def test_driver_uses_direct_mclk_headphone_route_and_pdma_ping_pong(self):
        source = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.c").read_text(
            encoding="utf-8"
        )

        for required in (
            "TLV320DAC3100_REG_CODEC_INTERFACE_CTRL_1",
            "TLV320DAC3100_REG_NDAC",
            "TLV320DAC3100_REG_MDAC",
            "TLV320DAC3100_REG_DOSR_MSB",
            "TLV320DAC3100_REG_DOSR_LSB",
            "TLV320DAC3100_REG_DAC_PROCESSING_BLOCK",
            "TLV320DAC3100_REG_HEADPHONE_DRIVERS",
            "TLV320DAC3100_REG_HEADPHONE_DEPOP",
            "TLV320DAC3100_HEADPHONE_DRIVERS_POWERED",
            "0xC4U",
            "I2S_Open",
            "I2S_EnableMCLK",
            "I2S_ENABLE_TXDMA",
            "PDMA_OP_SCATTER",
            "PDMA_WIDTH_32",
            "TLV320DAC3100_DMA_MAX_FRAMES_PER_BLOCK",
            "uint32_t g_stereo_buffers",
            "PDMA_I2S0_TX",
            "PDMA_I2S1_TX",
            "NVT_NONCACHEABLE",
            "TLV320DAC3100_DAC_POWER_LEFT_RIGHT",
            "g_context.slot_available[0] = false",
            "g_context.slot_available[1] = false",
            "g_context.slot_contains_audio[0] = false",
            "g_context.slot_contains_audio[1] = false",
            "PDMA_RESET(g_context.config.pdma, g_context.config.pdma_channel)",
            "g_context.status.initialized = false",
        ):
            self.assertIn(required, source)

    def test_codec_i2c_write_uses_a_bounded_driver_owned_transaction(self):
        """A missing I2C SI flag must not block the foreground diagnostic indefinitely."""
        source = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.c").read_text(
            encoding="utf-8"
        )
        writer_start = source.index("static tlv320dac3100_status_t tlv320dac3100_write_register")
        writer_end = source.index("static uint32_t tlv320dac3100_get_pdma_request", writer_start)
        writer = source[writer_start:writer_end]

        self.assertIn("tlv320dac3100_write_one_reg_bounded", source)
        self.assertIn("TLV320DAC3100_I2C_WAIT_LIMIT", source)
        self.assertEqual(writer.count("tlv320dac3100_write_one_reg_bounded"), 2)
        self.assertNotIn("I2C_WriteByteOneReg", source)

    def test_resetless_codec_startup_waits_for_ack_and_preserves_i2c_failure_context(self):
        """A breakout without GPIO reset needs bounded power-up/ACK recovery."""
        header = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.h").read_text(
            encoding="utf-8"
        )
        source = (REPO_ROOT / "drivers" / "tlv320dac3100" / "tlv320dac3100.c").read_text(
            encoding="utf-8"
        )
        board = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )
        diagnostic = (
            REPO_ROOT / "app" / "test_dac_i2s_uart1_diagnostic_main.c"
        ).read_text(encoding="utf-8")

        for required in (
            "i2c_failures",
            "queued_blocks",
            "available_slot_mask",
            "audio_slot_mask",
            "next_completed_slot",
            "last_i2c_page",
            "last_i2c_register",
            "last_i2c_phase",
            "last_i2c_attempts",
            "last_i2c_status",
            "last_i2c_control",
            "TLV320DAC3100_CODEC_POWER_UP_SETTLE_US",
            "TLV320DAC3100_I2C_READY_ATTEMPTS",
            "TLV320DAC3100_I2C_WRITE_ATTEMPTS",
            "tlv320dac3100_wait_for_i2c_ready",
            "CLK_SysTickDelay(TLV320DAC3100_CODEC_POWER_UP_SETTLE_US)",
            "M55M1_AUDIO_I2C_HZ    400000UL",
            "I2C_Open(I2C1, M55M1_AUDIO_I2C_HZ)",
            "SYS_ResetModule(SYS_I2C1RST);",
            "m55m1_audio_recover_i2c_bus",
            "GPIO_MODE_OPEN_DRAIN",
            "M55M1_AUDIO_BOARD_INIT_I2C_STUCK",
            "I2C_STUCK",
            "I2C_LINES sda=%u scl=%u",
            "I2C_REGS c=%08lx s=%02lx",
            "p=%u r=%02x ph=%u a=%u n=%lu",
        ):
            self.assertIn(required, header + source + board + diagnostic)

    def test_uart1_diagnostic_uses_pa2_pa3_and_reports_every_stage(self):
        """The standalone DAC diagnostic must retain its independent UART1 observability."""
        diagnostic = REPO_ROOT / "app" / "test_dac_i2s_uart1_diagnostic_main.c"
        self.assertTrue(diagnostic.is_file(), "UART1 DAC diagnostic source is missing")
        source = diagnostic.read_text(encoding="utf-8")

        for required in (
            "SET_UART1_RXD_PA2();",
            "SET_UART1_TXD_PA3();",
            "CLK_EnableModuleClock(UART1_MODULE);",
            "CLK_SetModuleClock(UART1_MODULE, CLK_UARTSEL0_UART1SEL_HIRC",
            "UART_Open(UART1, 115200U);",
            "BOOT",
            "UART_READY",
            "BOARD_READY",
            "CODEC_INIT",
            "CODEC_CONTROL",
            "AUDIO_QUEUE",
            "PLAYING",
            "RUNNING",
            "tlv320dac3100_get_status",
            "completed_blocks",
            "underrun_blocks",
            "dma_errors",
            "I2S0->STATUS0",
            "PDMA0->TDSTS",
            "@brief",
            "static bool dac_diagnostic_take_block_due(void)",
            "__get_PRIMASK()",
            "__disable_irq()",
            "__set_PRIMASK(primask)",
            "if (dac_diagnostic_take_block_due())",
        ):
            self.assertIn(required, source)

        self.assertNotIn("UART5_MODULE", source)
        self.assertNotIn("m55m1_audio_dmic_start", source)
        self.assertNotIn("SET_UART1_RXD_PA8();", source)
        self.assertNotIn("SET_UART1_TXD_PA9();", source)
        self.assertEqual(source.count("g_tone_block_due = false;"), 1)

    def test_uart1_diagnostic_captures_reset_source_and_clock_boundaries(self):
        """A reset loop must identify its reset source and last board-clock boundary."""
        diagnostic = (
            REPO_ROOT / "app" / "test_dac_i2s_uart1_diagnostic_main.c"
        ).read_text(encoding="utf-8")
        board_header = (REPO_ROOT / "board" / "m55m1_audio.h").read_text(
            encoding="utf-8"
        )
        board_source = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )

        for required in (
            "SYS_GetResetSrc()",
            "SYS_ClearResetSrc",
            "RSTSTS=",
            "m55m1_tlv320dac3100_test_board_init_with_progress",
            "M55M1_AUDIO_BOARD_STAGE_HXT_WAIT",
            "M55M1_AUDIO_BOARD_STAGE_HXT_READY",
            "M55M1_AUDIO_BOARD_STAGE_APLL0_WAIT",
            "M55M1_AUDIO_BOARD_STAGE_APLL0_READY",
            "M55M1_AUDIO_BOARD_INIT_HXT_TIMEOUT",
            "M55M1_AUDIO_BOARD_INIT_APLL0_TIMEOUT",
            "BOARD_RESULT",
            "@brief",
        ):
            self.assertIn(required, diagnostic + board_header + board_source)

        self.assertLess(
            diagnostic.index("RSTSTS="),
            diagnostic.index("m55m1_tlv320dac3100_test_board_init_with_progress"),
        )

    def test_board_switches_sclk_to_hirc_before_reprogramming_apll0(self):
        """The audio PLL must never be disabled while it might clock the CPU."""
        board_source = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )
        diagnostic = (
            REPO_ROOT / "app" / "test_dac_i2s_uart1_diagnostic_main.c"
        ).read_text(encoding="utf-8")

        self.assertIn("CLK_SetSCLK(CLK_SCLKSEL_SCLKSEL_HIRC);", board_source)
        self.assertIn("M55M1_AUDIO_BOARD_STAGE_SCLK_HIRC_READY", board_source)
        self.assertIn("CLK->SCLKSEL", diagnostic)
        self.assertLess(
            board_source.index("CLK_SetSCLK(CLK_SCLKSEL_SCLKSEL_HIRC);"),
            board_source.index("CLK_EnableAPLL(CLK_APLLCTL_APLLSRC_HXT"),
        )

    def test_board_reports_post_apll_boundaries_before_i2c_recovery(self):
        """A boot stop after APLL lock must identify the next hardware boundary."""
        board_header = (REPO_ROOT / "board" / "m55m1_audio.h").read_text(
            encoding="utf-8"
        )
        board_source = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )

        for boundary in (
            "M55M1_AUDIO_BOARD_STAGE_SCLK_APLL0_READY",
            "M55M1_AUDIO_BOARD_STAGE_MODULE_CLOCKS_READY",
            "M55M1_AUDIO_BOARD_STAGE_AUDIO_PINS_READY",
            "M55M1_AUDIO_BOARD_STAGE_I2C_RECOVERY_READY",
            "M55M1_AUDIO_BOARD_STAGE_I2C_PINS_READY",
            "M55M1_AUDIO_BOARD_STAGE_I2C_RESET_READY",
            "M55M1_AUDIO_BOARD_STAGE_CYCLE_COUNTER_READY",
        ):
            self.assertIn(boundary, board_header)
            self.assertIn(boundary, board_source)

        self.assertLess(
            board_source.index("CLK_SetSCLK(CLK_SCLKSEL_SCLKSEL_APLL0);"),
            board_source.index("M55M1_AUDIO_BOARD_STAGE_SCLK_APLL0_READY"),
        )
        self.assertLess(
            board_source.index("M55M1_AUDIO_BOARD_STAGE_AUDIO_PINS_READY"),
            board_source.index("m55m1_audio_recover_i2c_bus()"),
        )

    def test_led_diagnostic_uses_pa7_and_covers_audio_faults(self):
        """The PA7 diagnostic must observe playback without reintroducing UART timing."""
        diagnostic = REPO_ROOT / "app" / "test_dac_i2s_led_diagnostic_main.c"
        self.assertTrue(diagnostic.is_file(), "PA7 DAC LED diagnostic source is missing")
        source = diagnostic.read_text(encoding="utf-8")

        for required in (
            "GPIO_SetMode(PA, BIT7, GPIO_MODE_OUTPUT);",
            "PA7 = 1U;",
            "PA7 = 0U;",
            "SysTick_Config(SystemCoreClock / 1000UL)",
            "void SysTick_Handler(void)",
            "++g_tick_ms;",
            "LED_STAGE_CODEC_INIT",
            "LED_STAGE_CODEC_CONTROL",
            "LED_STAGE_AUDIO_QUEUE",
            "LED_STAGE_PLAYING",
            "LED_STAGE_RUNTIME",
            "LED_CAUSE_INVALID_ARGUMENT",
            "LED_CAUSE_INVALID_STATE",
            "LED_CAUSE_BUSY",
            "LED_CAUSE_CLOCK_MISMATCH",
            "LED_CAUSE_I2C_ERROR",
            "LED_CAUSE_DMA_ERROR",
            "LED_CAUSE_UNDERRUN",
            "LED_CAUSE_STALL",
            "tlv320dac3100_get_status",
            "if (dac_led_take_block_due())",
            "dac_led_update",
            "@brief",
        ):
            self.assertIn(required, source)

        for forbidden in (
            "UART_Open(",
            "UART_Write(",
            "SET_UART",
            "UART1_MODULE",
            "UART5_MODULE",
            "DMIC",
            "CCAP",
            "SDHC",
        ):
            self.assertNotIn(forbidden, source)

        systick_start = source.index("void SysTick_Handler(void)")
        systick_end = source.index("/**", systick_start + 1)
        systick_handler = source[systick_start:systick_end]
        self.assertEqual(
            "".join(systick_handler.split()),
            "voidSysTick_Handler(void){++g_tick_ms;}",
        )

        expected_mappings = {
            "LED_STAGE_BOARD_CLOCK": 1,
            "LED_STAGE_CODEC_INIT": 2,
            "LED_STAGE_CODEC_CONTROL": 3,
            "LED_STAGE_AUDIO_QUEUE": 4,
            "LED_STAGE_PLAYING": 5,
            "LED_STAGE_RUNTIME": 6,
            "LED_CAUSE_INVALID_ARGUMENT": 1,
            "LED_CAUSE_INVALID_STATE": 2,
            "LED_CAUSE_BUSY": 3,
            "LED_CAUSE_CLOCK_MISMATCH": 4,
            "LED_CAUSE_I2C_ERROR": 5,
            "LED_CAUSE_DMA_ERROR": 6,
            "LED_CAUSE_UNDERRUN": 7,
            "LED_CAUSE_STALL": 8,
            "LED_CAUSE_HXT_TIMEOUT": 9,
            "LED_CAUSE_APLL_TIMEOUT": 10,
        }
        for name, value in expected_mappings.items():
            self.assertRegex(source, rf"#define\s+{name}\s+{value}U")

        main_start = source.index("int main(void)")
        main = source[main_start:]
        self.assertLess(
            main.index("if (dac_led_take_block_due())"),
            main.index("tlv320dac3100_get_status(&driver_status)"),
        )
        self.assertLess(
            main.index("tlv320dac3100_get_status(&driver_status)"),
            main.index("dac_led_update(now_ms)"),
        )
        self.assertNotIn("CLK_SysTickDelay", main)

    def test_led_diagnostic_reports_clock_setup_before_codec_initialisation(self):
        """Clock faults must be visible instead of hiding the first LED pulse."""
        diagnostic = REPO_ROOT / "app" / "test_dac_i2s_led_diagnostic_main.c"
        board_source = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )
        board_header = (REPO_ROOT / "board" / "m55m1_audio.h").read_text(
            encoding="utf-8"
        )
        source = diagnostic.read_text(encoding="utf-8")
        main = source[source.index("int main(void)") :]

        for required in (
            "LED_STAGE_BOARD_CLOCK",
            "LED_CAUSE_HXT_TIMEOUT",
            "LED_CAUSE_APLL_TIMEOUT",
            "m55m1_audio_board_init_result_t",
            "M55M1_AUDIO_BOARD_INIT_HXT_TIMEOUT",
            "M55M1_AUDIO_BOARD_INIT_APLL0_TIMEOUT",
            "CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk)",
            "CLK->STATUS & CLK_STATUS_APLL0STB_Msk",
            "@brief",
        ):
            self.assertIn(required, source + board_source + board_header)

        self.assertNotIn("(void)CLK_WaitClockReady(CLK_STATUS_HXTSTB_Msk)", board_source)
        self.assertLess(
            main.index("dac_led_emit_startup_pulses(1U);"),
            main.index("m55m1_tlv320dac3100_test_board_init"),
        )
        self.assertLess(
            main.index("m55m1_tlv320dac3100_test_board_init"),
            main.index("tlv320dac3100_init(&config)"),
        )

    def test_pa7_led_smoke_isolated_active_low_blink(self):
        """The isolated PA7 test must prove LED hardware without audio dependencies."""
        diagnostic = REPO_ROOT / "app" / "test_pa7_led_smoke_main.c"
        self.assertTrue(diagnostic.is_file(), "PA7 LED smoke-test source is missing")
        source = diagnostic.read_text(encoding="utf-8")

        for required in (
            "CLK_EnableModuleClock(GPIOA_MODULE);",
            "SET_GPIO_PA7();",
            "GPIO_SetMode(PA, BIT7, GPIO_MODE_OUTPUT);",
            "PA7 = 0U;",
            "PA7 = 1U;",
            "CLK_SysTickDelay(500000UL);",
            "@brief",
        ):
            self.assertIn(required, source)

        for forbidden in (
            "tlv320dac3100",
            "m55m1_tlv320dac3100",
            "I2S",
            "I2C",
            "PDMA",
            "UART",
            "DMIC",
            "CCAP",
            "SDHC",
        ):
            self.assertNotIn(forbidden, source)

    def test_board_binding_and_smoke_test_use_confirmed_pins(self):
        board = (REPO_ROOT / "board" / "m55m1_audio.c").read_text(
            encoding="utf-8"
        )
        dmic_app = (REPO_ROOT / "app" / "test_dmic_uart5_main.c").read_text(encoding="utf-8")
        passthrough_app = (
            REPO_ROOT / "app" / "test_dmic_i2s_passthrough_main.c"
        ).read_text(encoding="utf-8")
        diagnostic_app = (REPO_ROOT / "app" / "main.c").read_text(encoding="utf-8")

        for required in (
            "SET_I2S0_DO_PF0();",
            "SET_I2S0_LRCK_PF1();",
            "SET_I2S0_BCLK_PB5();",
            "SET_I2S0_MCLK_PB4();",
            "SET_I2C1_SCL_PB1();",
            "SET_I2C1_SDA_PB0();",
            "PDMA0",
            "PDMA_I2S0_TX",
            "m55m1_audio_profile_mclk_hz",
            "475136006UL",
            "CLK_I2SDIV_I2S0DIV(58)",
            "SET_DMIC1_CLK_PB2();",
            "SET_DMIC1_DAT_PB3();",
            "DMIC0_MODULE",
            "LPPDMA0_MODULE",
            "LPPDMA_DMIC0_RX",
            "DMIC_DOWNSAMPLE_128",
            "DMIC_DOWNSAMPLE_64",
            "DMIC_SET_LATCHEDGE_CH23(DMIC0, DMIC_LATCHDATA_CH23RF);",
            "m55m1_audio_dmic_start",
            "m55m1_audio_dmic_take_block",
        ):
            self.assertIn(required, board)

        self.assertIn("m55m1_tlv320dac3100_test_dmic_take_block", dmic_app)
        self.assertNotIn("g_tone_block", dmic_app)
        self.assertNotIn("UART0PF01_Init", dmic_app)

        for required in (
            "Standalone normal 16 kHz or 32 kHz DMIC-to-headphone passthrough test",
            "m55m1_tlv320dac3100_test_board_init",
            "tlv320dac3100_init(&config)",
            "tlv320dac3100_start()",
            "m55m1_tlv320dac3100_test_dmic_start",
            "m55m1_tlv320dac3100_test_dmic_take_block",
            "tlv320dac3100_submit_mono_block(g_dmic_block, sample_count)",
            "TLV320DAC3100_STATUS_BUSY",
            "@brief",
        ):
            self.assertIn(required, passthrough_app)

        self.assertLess(
            passthrough_app.index("tlv320dac3100_start()"),
            passthrough_app.index("m55m1_tlv320dac3100_test_dmic_start"),
        )
        self.assertNotIn("UART_Open(", passthrough_app)

        self.assertIn("Bare-metal boot composition", diagnostic_app)
        self.assertIn("services/audio_pipeline/audio_pipeline.h", diagnostic_app)
        self.assertNotIn("UART_Open(", diagnostic_app)
        self.assertNotIn("UART5_MODULE", diagnostic_app)

    def test_cmake_and_driver_documentation_are_integrated(self):
        cmake = (REPO_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        documentation = (REPO_ROOT / "docs" / "driver" / "tlv320dac3100.md").read_text(
            encoding="utf-8"
        )

        self.assertIn("drivers/tlv320dac3100/tlv320dac3100.c", cmake)
        self.assertIn("board/m55m1_audio.c", cmake)
        for required in (
            "TLV320DAC3100 I2S Headphone Transport",
            "PF0",
            "PF1",
            "PB5",
            "PB4",
            "PB1",
            "PB0",
            "4.096 MHz",
            "8.192 MHz",
            "16 kHz",
            "32 kHz",
            "PDMA",
            "SWD",
            "32-bit",
            "-3 dB",
            "400 kHz",
            "20 ms",
            "transport/reference",
        ):
            self.assertIn(required, documentation)

    def test_dac_and_dmic_notes_are_separate_and_capture_debug_history(self):
        """Future hearing-aid work needs concise hardware facts, not lost UART logs."""
        dac_notes = (REPO_ROOT / "docs" / "driver" / "tlv320dac3100.md").read_text(
            encoding="utf-8"
        )
        dmic_path = REPO_ROOT / "docs" / "driver" / "mp34dt05_dmic.md"
        self.assertTrue(dmic_path.is_file(), "DMIC implementation notes are missing")
        dmic_notes = dmic_path.read_text(encoding="utf-8")

        for required in (
            "16 kHz",
            "32 kHz",
            "8.192 MHz",
            "32-bit",
            "-3 dB",
            "underrun",
            "transport/reference",
        ):
            self.assertIn(required, dac_notes)
        for required in (
            "MP34DT05",
            "PB2",
            "PB3",
            "OSR=128",
            "OSR=64",
            "2.048 MHz",
            "rising edge",
            "1.024 MHz",
            "LPPDMA",
            "completion rate",
            "transport/reference",
        ):
            self.assertIn(required, dmic_notes)


if __name__ == "__main__":
    unittest.main()
