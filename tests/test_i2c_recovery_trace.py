"""Exercise actual board recovery code with modeled pins; no electrical claims."""

from pathlib import Path
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
    def test_native_recovery_records_actual_lines_without_changing_policy(self):
        source = (ROOT / "board/m55m1_audio.c").read_text()
        helpers = "\n".join(function(source, name) for name in (
            "m55m1_audio_trace_i2c_gpio", "m55m1_audio_trace_i2c_begin", "m55m1_audio_trace_i2c_event",
            "m55m1_audio_trace_i2c_end", "m55m1_audio_recover_i2c_bus",
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
        #define BIT4 (1U << 4)
        #define BIT5 (1U << 5)
        #define GPIO_MODE_OPEN_DRAIN 2U
        static struct {uint32_t MODE,DOUT,PIN,DINOFF,PUSEL,SMTEN;} pa;
        static struct {uint32_t GPA_MFP1;} sys;
        static struct {uint32_t CTL0,STATUS0,CLKDIV;} i2c;
        static struct {uint32_t GPIOCTL,I2CCTL,STSEL;} clk;
        static struct {uint32_t LOAD;} systick;
        #define PA (&pa)
        #define SYS (&sys)
        #define I2C0 (&i2c)
        #define CLK (&clk)
        #define SysTick (&systick)
        static uint32_t sda_drive,scl_drive,cycle,delays,release_at,high_phases;
        static const uint32_t SystemCoreClock=220000000U;
        static bool stuck_sda,stuck_scl;
        static i2c_recovery_trace_t g_i2c_recovery_trace;
        #define M55M1_AUDIO_I2C_SDA sda_drive
        #define M55M1_AUDIO_I2C_SCL scl_drive
        #define SET_GPIO_PA4() (sys.GPA_MFP1 &= ~31U)
        #define SET_GPIO_PA5() (sys.GPA_MFP1 &= ~(31U << 8))
        static void GPIO_SetMode(void *port,uint32_t mask,uint32_t mode) {
            assert(port==PA && mask==(BIT4|BIT5) && mode==GPIO_MODE_OPEN_DRAIN);
            pa.MODE=0xA00U;
        }
        static uint32_t m55m1_timebase_cycles_now(void) {return cycle;}
        static void m55m1_timebase_delay_us(uint32_t us) {
            assert(us==5U); ++delays; cycle+=us*220U;
            /* GPIO lvalues emulate output writes; PIN models external holds. */
            if (delays>=3U && scl_drive && (delays&1U)) ++high_phases;
            bool sda=sda_drive && !stuck_sda && (!release_at || high_phases>=release_at);
            bool scl=scl_drive && !stuck_scl;
            pa.PIN=(sda?BIT4:0U)|(scl?BIT5:0U);
            pa.DOUT=(sda_drive?BIT4:0U)|(scl_drive?BIT5:0U);
            systick.LOAD=0xFFFFFFU; clk.STSEL=7U;
            /* Model the hardware input read aliases separately after writes. */
        }
        '''
        # Production pin aliases read PIN on hardware. Model reads only, preserving writes.
        recovery = helpers[helpers.index('static bool m55m1_audio_recover_i2c_bus('):]
        recovery = recovery.replace('(M55M1_AUDIO_I2C_SDA == 0U)', '((PA->PIN & BIT4) == 0U)')
        recovery = recovery.replace('(M55M1_AUDIO_I2C_SDA != 0U)', '((PA->PIN & BIT4) != 0U)')
        recovery = recovery.replace('(M55M1_AUDIO_I2C_SCL != 0U)', '((PA->PIN & BIT5) != 0U)')
        helpers = helpers[:helpers.index('static bool m55m1_audio_recover_i2c_bus(')] + recovery
        harness += helpers + r'''
        static void run(unsigned release,bool hold_sda,bool hold_scl,unsigned pulses,bool ok) {
            memset(&g_i2c_recovery_trace,0,sizeof(g_i2c_recovery_trace));
            g_i2c_recovery_trace.reset_flags=0x20U;
            sys.GPA_MFP1=0x0909U; pa.MODE=0x1234U; pa.DINOFF=0x300000U;
            pa.PUSEL=0x400U; pa.SMTEN=0x30U; pa.DOUT=0x30U; pa.PIN=0x30U;
            i2c.CTL0=0x68U; i2c.STATUS0=0x28U; i2c.CLKDIV=37U;
            clk.GPIOCTL=1;clk.I2CCTL=1;clk.STSEL=0;systick.LOAD=0;
            release_at=release;stuck_sda=hold_sda;stuck_scl=hold_scl;
            sda_drive=scl_drive=1;cycle=delays=high_phases=0;
            assert(m55m1_audio_get_i2c_trace()==0);
            assert(m55m1_audio_recover_i2c_bus()==ok);
            const i2c_recovery_trace_t *t=m55m1_audio_get_i2c_trace();
            assert(t==&g_i2c_recovery_trace && t->attempted);
            assert(t->released==ok);
            assert(t->first_scl_low_event==(hold_scl?0:I2C_RECOVERY_TRACE_NO_EVENT));
            assert(t->reset_flags==0x20U && t->before.mfp==0x0909U);
            assert(t->before.mode==0x1234U && t->before.dinoff==0x300000U);
            assert(t->i2c_control==0x68U && t->i2c_status==0x28U && t->i2c_divider==37U);
            assert(t->core_hz==220000000U);
            assert(t->after.mfp==0 && t->after.mode==0xA00U);
            assert(t->systick_load_before==0 && t->systick_load_after==0xFFFFFFU);
            assert(t->pulses==pulses && t->event_count==4+2*pulses);
            assert(delays==4+2*pulses && t->event_count<=I2C_RECOVERY_TRACE_CAPACITY);
            assert(t->events[0].phase==I2C_RECOVERY_RELEASE);
            for(unsigned n=1;n<=pulses;n++) {
                assert(t->events[2*n-1].phase==I2C_RECOVERY_CLOCK_LOW);
                assert(t->events[2*n].phase==I2C_RECOVERY_CLOCK_HIGH);
                assert(t->events[2*n].pulse==n);
                assert(t->events[2*n].scl==!hold_scl);
            }
            unsigned end=t->event_count;
            assert(t->events[end-3].phase==I2C_RECOVERY_STOP_SDA_LOW);
            assert(t->events[end-2].phase==I2C_RECOVERY_STOP_SCL_RELEASE);
            assert(t->events[end-1].phase==I2C_RECOVERY_STOP_SDA_RELEASE);
            assert(t->events[end-1].sda==!hold_sda);
            assert(t->events[end-1].scl==!hold_scl);
            for(unsigned n=1;n<end;n++) assert(t->events[n].cycle>t->events[n-1].cycle);
            /* Capacity guard cannot corrupt adjacent snapshot state. */
            for(unsigned n=0;n<30;n++) m55m1_audio_trace_i2c_event(I2C_RECOVERY_CLOCK_HIGH,9);
            assert(t->event_count==I2C_RECOVERY_TRACE_CAPACITY && t->reset_flags==0x20U);
        }
        int main(void) {
            run(0,false,false,0,true); run(3,false,false,3,true);
            run(9,false,false,9,true); run(0,true,false,9,false);
            run(0,false,true,0,false); run(0,true,true,9,false);
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
