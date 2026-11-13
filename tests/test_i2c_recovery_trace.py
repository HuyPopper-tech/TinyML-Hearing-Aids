"""Exercise actual board recovery code with modeled pins; no electrical claims."""

from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def function(source, name):
    position = source.index(name + "(")
    start = source.rfind("\n", 0, position) + 1
    brace = source.index("{", position)
    depth = 1
    end = brace + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


class I2cRecoveryTraceTests(unittest.TestCase):
    def test_bounded_slow_retry_and_controller_reset_in_both_profiles(self):
        source = (ROOT / "board/m55m1_audio.c").read_text()
        names = ("m55m1_audio_recover_i2c_attempt", "m55m1_audio_recover_i2c_bus")
        recovery = "\n".join(function(source, name) for name in names)
        for pin, bit in (("SDA", "BIT4"), ("SCL", "BIT5")):
            for op in ("==", "!="):
                recovery = recovery.replace(
                    f"(M55M1_AUDIO_I2C_{pin} {op} 0U)", f"((PA->PIN & {bit}) {op} 0U)")
        constants = "\n".join(re.findall(
            r"^#define M55M1_AUDIO_I2C_(?:RECOVERY_\w+|SLOW_RECOVERY_\w+)\s+[^\n]+",
            source, re.M))
        harness = r'''


        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <string.h>
        #include "common/i2c_recovery_trace.h"
        #define M55M1_ENABLE_DAC_I2S_OUTPUT 1
        #define BIT4 16U
        #define BIT5 32U
        #define GPIO_MODE_OPEN_DRAIN 2U
        #define SYS_I2C0RST 7U
        static struct {
            uint32_t PIN;
        } pa;
        #define PA (&pa)
        static uint32_t sda_drive, scl_drive, previous_scl;
        static unsigned fast_pulses, slow_pulses, fast_release, slow_release, resets, muxes;
        static unsigned slow_delays, total_us, stop_edges;
        static bool held_sda, held_scl, controller_enabled, slave_holds;
        static unsigned clock_fail;
        #define M55M1_AUDIO_I2C_SDA sda_drive
        #define M55M1_AUDIO_I2C_SCL scl_drive
        #define SET_GPIO_PA4() (++muxes)
        #define SET_GPIO_PA5() (++muxes)
        void SYS_ResetModule(unsigned module) {
            assert(module == SYS_I2C0RST);
            ++resets;
            controller_enabled = false;
        }
        static void GPIO_SetMode(void *port, unsigned mask, unsigned mode) {
            assert(port == PA && mask == (BIT4 | BIT5) && mode == GPIO_MODE_OPEN_DRAIN);
        }
        static void m55m1_timebase_delay_us(uint32_t us) {
            assert(us == 5U || us == 2000U);
            total_us += us;
            if (us == 2000U) {
                ++slow_delays;
            }
            bool rising = scl_drive && !previous_scl;
            if (rising && sda_drive) {
                if (us == 5U) {
                    ++fast_pulses;
                    if (fast_release && fast_pulses >= fast_release) {
                        slave_holds = false;
                    }
                } else {
                    ++slow_pulses;
                    if (slow_release && slow_pulses >= slow_release) {
                        slave_holds = false;
                    }
                }
            }
            if (rising && ((clock_fail == 1U && sda_drive) || (clock_fail == 2U && !sda_drive))) {
                held_scl = true;
            }
            if (rising && !sda_drive) {
                ++stop_edges;
            }
            previous_scl = scl_drive;
            pa.PIN = ((sda_drive && !held_sda && !slave_holds) ? BIT4 : 0U) |
                     ((scl_drive && !held_scl && !controller_enabled) ? BIT5 : 0U);
        }
        #if M55M1_RUNTIME_TELEMETRY
        static unsigned trace_events, trace_pulses, trace_releases;
        static bool trace_ok;
        static void m55m1_audio_trace_i2c_begin(void) {
            trace_events = trace_pulses = trace_releases = 0U;
        }
        static void m55m1_audio_trace_i2c_event(i2c_recovery_phase_t phase, uint8_t pulse) {
            ++trace_events;
            assert(trace_events <= I2C_RECOVERY_TRACE_CAPACITY);
            if (phase == I2C_RECOVERY_RELEASE) {
                ++trace_releases;
            }
            if (phase == I2C_RECOVERY_CLOCK_HIGH) {
                assert(pulse == ++trace_pulses);
            }
            if (phase == I2C_RECOVERY_STOP_SDA_LOW) {
                assert(!scl_drive);
            }
            if (phase == I2C_RECOVERY_STOP_SCL_RELEASE) {
                assert(scl_drive && !sda_drive);
            }
            if (phase == I2C_RECOVERY_STOP_SDA_RELEASE) {
                assert(scl_drive && sda_drive);
            }
        }
        static void m55m1_audio_trace_i2c_end(uint8_t pulses, bool released) {
            assert(pulses == trace_pulses);
            trace_ok = released;
        }
        #endif
        ''' + constants + "\n" + recovery + r'''


        static void run(unsigned fast, unsigned slow, bool sda_stuck, bool scl_stuck, unsigned fail_clock,
                        bool expected) {
            fast_release = fast;
            slow_release = slow;
            held_sda = sda_stuck;
            held_scl = scl_stuck;
            clock_fail = fail_clock;
            slave_holds = (fast != 0U || slow != 0U);
            controller_enabled = true;
            sda_drive = scl_drive = previous_scl = 1U;
            fast_pulses = slow_pulses = slow_delays = total_us = stop_edges = resets = muxes = 0U;
            pa.PIN = 0U;
            assert(m55m1_audio_recover_i2c_bus() == expected);
            assert(resets == 1U && muxes == 2U && !controller_enabled);
            assert(sda_drive && scl_drive);
            assert(fast_pulses <= 9U && slow_pulses <= 9U && total_us <= 47000U);
            if (scl_stuck || fail_clock) {
                assert(!slow_delays);
            }
        #if M55M1_RUNTIME_TELEMETRY
            assert(trace_ok == expected);
            assert(trace_releases == (slow_delays ? 2U : 1U));
        #endif
        }
        int main(void) {
            /* The manual recovery released SDA on slow pulse five after fast pulses failed. */
            run(0U, 5U, false, false, false, true);
            assert(fast_pulses == 9U && slow_pulses == 5U && stop_edges == 2U);
            run(0U, 0U, false, false, false, true);
            assert(!fast_pulses && !slow_delays && stop_edges == 1U);
            run(3U, 0U, false, false, false, true);
            assert(fast_pulses == 3U && !slow_delays);
            run(9U, 0U, false, false, false, true);
            assert(fast_pulses == 9U && !slow_delays);
            run(0U, 9U, false, false, false, true);
            assert(fast_pulses == 9U && slow_pulses == 9U);
            run(0U, 0U, true, false, false, false);
            assert(fast_pulses == 9U && slow_pulses == 9U);
            run(0U, 0U, false, true, false, false);
            assert(!fast_pulses && !slow_delays);
            run(0U, 0U, true, true, false, false);
            assert(!fast_pulses && !slow_delays);
            run(0U, 5U, false, false, true, false);
            assert(fast_pulses == 1U && !slow_delays);
            run(0U, 0U, false, false, 2U, false);
            assert(!fast_pulses && !slow_delays && stop_edges == 1U);
            return 0;
        }
        '''
        for telemetry in (0, 1):
            with self.subTest(telemetry=telemetry), tempfile.TemporaryDirectory() as directory:
                path = Path(directory) / "recovery.c"
                executable = Path(directory) / "recovery.exe"
                path.write_text(harness)
                build = subprocess.run([
                    "gcc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(ROOT),
                    f"-DM55M1_RUNTIME_TELEMETRY={telemetry}", str(path), "-o", str(executable)
                ], capture_output=True, text=True)
                self.assertEqual(build.returncode, 0, build.stderr)
                result = subprocess.run([str(executable)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_native_trace_covers_both_attempts_and_actual_lines(self):
        source = (ROOT / "board/m55m1_audio.c").read_text()
        helpers = "\n".join(function(source, name) for name in (
            "m55m1_audio_trace_i2c_gpio", "m55m1_audio_trace_i2c_begin", "m55m1_audio_trace_i2c_event",
            "m55m1_audio_trace_i2c_end", "m55m1_audio_recover_i2c_attempt",
            "m55m1_audio_recover_i2c_bus",
            "m55m1_audio_get_i2c_trace"))
        harness = r'''


        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <string.h>
        #include "common/i2c_recovery_trace.h"
        #define M55M1_RUNTIME_TELEMETRY 1
        #define M55M1_ENABLE_DAC_I2S_OUTPUT 1
        #define M55M1_AUDIO_I2C_RECOVERY_PULSES 9U
        #define M55M1_AUDIO_I2C_RECOVERY_DELAY_US 5U
        #define M55M1_AUDIO_I2C_SLOW_RECOVERY_DELAY_US 2000U
        #define SYS_I2C0RST 7U
        #define BIT4 (1U << 4)
        #define BIT5 (1U << 5)
        #define GPIO_MODE_OPEN_DRAIN 2U
        static struct {
            uint32_t MODE, DOUT, PIN, DINOFF, PUSEL, SMTEN;
        } pa;
        static struct {
            uint32_t GPA_MFP1;
        } sys;
        static struct {
            uint32_t CTL0, STATUS0, CLKDIV;
        } i2c;
        static struct {
            uint32_t GPIOCTL, I2CCTL, STSEL;
        } clk;
        static struct {
            uint32_t LOAD;
        } systick;
        #define PA (&pa)
        #define SYS (&sys)
        #define I2C0 (&i2c)
        #define CLK (&clk)
        #define SysTick (&systick)
        static uint32_t sda_drive, scl_drive, previous_scl, cycle, delays, release_at, high_phases;
        static const uint32_t SystemCoreClock = 220000000U;
        static bool stuck_sda, stuck_scl;
        static i2c_recovery_trace_t g_i2c_recovery_trace;
        #define M55M1_AUDIO_I2C_SDA sda_drive
        #define M55M1_AUDIO_I2C_SCL scl_drive
        #define SET_GPIO_PA4() (sys.GPA_MFP1 &= ~31U)
        #define SET_GPIO_PA5() (sys.GPA_MFP1 &= ~(31U << 8))
        static void GPIO_SetMode(void *port, uint32_t mask, uint32_t mode) {
            assert(port == PA && mask == (BIT4 | BIT5) && mode == GPIO_MODE_OPEN_DRAIN);
            pa.MODE = 0xA00U;
        }
        static void SYS_ResetModule(unsigned module) {
            assert(module == SYS_I2C0RST);
            i2c.CTL0 = 0U;
            i2c.STATUS0 = 0xF8U;
            i2c.CLKDIV = 0U;
        }
        static uint32_t m55m1_timebase_cycles_now(void) {
            return cycle;
        }
        static void m55m1_timebase_delay_us(uint32_t us) {
            assert(us == 5U || us == 2000U);
            ++delays;
            cycle += us * 220U;
            /* GPIO lvalues emulate output writes; PIN models external holds. */
            if (scl_drive && !previous_scl && sda_drive) {
                ++high_phases;
            }
            previous_scl = scl_drive;
            bool sda = sda_drive && !stuck_sda && (!release_at || high_phases >= release_at);
            bool scl = scl_drive && !stuck_scl;
            pa.PIN = (sda ? BIT4 : 0U) | (scl ? BIT5 : 0U);
            pa.DOUT = (sda_drive ? BIT4 : 0U) | (scl_drive ? BIT5 : 0U);
            systick.LOAD = 0xFFFFFFU;
            clk.STSEL = 7U;
        }
        '''
        # Production pin aliases read PIN on hardware. Model reads only, preserving writes.
        for pin, bit in (("SDA", "BIT4"), ("SCL", "BIT5")):
            for op in ("==", "!="):
                helpers = helpers.replace(
                    f"(M55M1_AUDIO_I2C_{pin} {op} 0U)", f"((PA->PIN & {bit}) {op} 0U)")
        harness += helpers + r'''


        static void run(unsigned release, bool hold_sda, bool hold_scl, unsigned pulses, bool ok) {
            memset(&g_i2c_recovery_trace, 0, sizeof(g_i2c_recovery_trace));
            g_i2c_recovery_trace.reset_flags = 0x20U;
            sys.GPA_MFP1 = 0x0909U;
            pa.MODE = 0x1234U;
            pa.DINOFF = 0x300000U;
            pa.PUSEL = 0x400U;
            pa.SMTEN = 0x30U;
            pa.DOUT = 0x30U;
            pa.PIN = 0x30U;
            i2c.CTL0 = 0x68U;
            i2c.STATUS0 = 0x28U;
            i2c.CLKDIV = 37U;
            clk.GPIOCTL = 1;
            clk.I2CCTL = 1;
            clk.STSEL = 0;
            systick.LOAD = 0;
            release_at = release;
            stuck_sda = hold_sda;
            stuck_scl = hold_scl;
            sda_drive = scl_drive = previous_scl = 1;
            cycle = delays = high_phases = 0;
            assert(m55m1_audio_get_i2c_trace() == 0);
            assert(m55m1_audio_recover_i2c_bus() == ok);
            const i2c_recovery_trace_t *t = m55m1_audio_get_i2c_trace();
            assert(t == &g_i2c_recovery_trace && t->attempted);
            assert(t->released == ok);
            assert(t->first_scl_low_event == (hold_scl ? 0 : I2C_RECOVERY_TRACE_NO_EVENT));
            assert(t->reset_flags == 0x20U && t->before.mfp == 0x0909U);
            assert(t->before.mode == 0x1234U && t->before.dinoff == 0x300000U);
            assert(t->i2c_control == 0x68U && t->i2c_status == 0x28U && t->i2c_divider == 37U);
            assert(t->core_hz == 220000000U);
            assert(t->after.mfp == 0 && t->after.mode == 0xA00U);
            assert(t->systick_load_before == 0 && t->systick_load_after == 0xFFFFFFU);
            unsigned attempts = (pulses > 9U) ? 2U : 1U;
            assert(t->pulses == pulses);
            assert(t->event_count == (hold_scl ? 1U : 4U * attempts + 2U * pulses));
            assert(delays == (hold_scl ? 1U : 5U * attempts + 2U * pulses));
            assert(t->event_count <= I2C_RECOVERY_TRACE_CAPACITY);
            assert(t->events[0].phase == I2C_RECOVERY_RELEASE);
            unsigned end = t->event_count;
            unsigned clocks = 0, releases = 0;
            for (unsigned n = 0; n < end; n++) {
                const i2c_recovery_event_t *event = &t->events[n];
                if (event->phase == I2C_RECOVERY_RELEASE) {
                    ++releases;
                }
                if (event->phase == I2C_RECOVERY_CLOCK_LOW) {
                    assert(event->pulse == clocks + 1U && !event->scl);
                    assert(n + 1U < end && t->events[n + 1U].phase == I2C_RECOVERY_CLOCK_HIGH);
                }
                if (event->phase == I2C_RECOVERY_CLOCK_HIGH) {
                    assert(event->pulse == ++clocks && event->scl);
                }
                if (event->phase == I2C_RECOVERY_STOP_SDA_LOW) {
                    assert(!event->scl);
                }
            }
            assert(clocks == pulses && releases == attempts);
            if (!hold_scl) {
                assert(t->events[end - 3].phase == I2C_RECOVERY_STOP_SDA_LOW);
                assert(t->events[end - 2].phase == I2C_RECOVERY_STOP_SCL_RELEASE);
                assert(t->events[end - 1].phase == I2C_RECOVERY_STOP_SDA_RELEASE);
            }
            assert(t->events[end - 1].sda == !hold_sda);
            assert(t->events[end - 1].scl == !hold_scl);
            for (unsigned n = 1; n < end; n++) {
                assert(t->events[n].cycle > t->events[n - 1].cycle);
            }
            /* Capacity guard cannot corrupt adjacent snapshot state. */
            for (unsigned n = 0; n <= I2C_RECOVERY_TRACE_CAPACITY; n++) {
                m55m1_audio_trace_i2c_event(I2C_RECOVERY_CLOCK_HIGH, 9);
            }
            assert(t->event_count == I2C_RECOVERY_TRACE_CAPACITY && t->reset_flags == 0x20U);
        }
        int main(void) {
            run(0, false, false, 0, true);
            run(3, false, false, 3, true);
            run(9, false, false, 9, true);
            run(0, true, false, 18, false);
            run(0, false, true, 0, false);
            run(0, true, true, 0, false);
            return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "trace.c"
            executable = Path(directory) / "trace.exe"
            path.write_text(harness)
            build = subprocess.run(['gcc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                                    '-I', str(ROOT), str(path), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_trace_is_cleared_before_init_failure_and_propagated_before_return(self):
        board = (ROOT / 'board/m55m1_audio.c').read_text()
        init = board[board.index('m55m1_audio_board_init_result_t m55m1_audio_board_init('):]
        self.assertLess(init.index('g_i2c_recovery_trace ='), init.index('CLK_SetSCLK('))
        self.assertLess(init.index('g_i2c_recovery_trace ='), init.index('return '))
        transport = (ROOT / 'drivers/audio_transport/audio_transport.c').read_text()
        self.assertLess(transport.index('report->i2c_trace ='),
                        transport.index('if (board_status !='))


if __name__ == '__main__':
    unittest.main()
