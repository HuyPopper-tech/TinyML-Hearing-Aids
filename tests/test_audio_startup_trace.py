"""Exercise the actual bounded board recorder; no peripheral timing claim."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class AudioStartupTraceTests(unittest.TestCase):
    def test_recorder_is_bounded_preserves_irq_mask_and_freezes_on_stop(self):
        source = (ROOT/'board/m55m1_audio.c').read_text()
        self.assertIn('static void m55m1_audio_begin_startup_trace(', source)
        body = source[source.index('static void m55m1_audio_begin_startup_trace('):
                      source.index('// End startup trace implementation.')]
        harness = r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stddef.h>
        #include <stdint.h>
        #include "common/audio_startup_trace.h"
        static volatile audio_startup_trace_t g_startup_trace;
        static unsigned mask, cycle;
        static struct {uint32_t STATUS0,STATUS1,CTL1;} i2s={11,12,13}, *I2S1=&i2s;
        static struct {struct {uint32_t CTL;} DSCT[1]; uint32_t TDSTS;} tx={{{14}},15};
        static struct {struct {uint32_t CTL;} LPDSCT[1]; uint32_t TDSTS;} rx={{{16}},17};
        #define PDMA0 (&tx)
        #define LPPDMA (&rx)
        #define M55M1_AUDIO_PDMA_CH 0
        #define M55M1_AUDIO_LPPDMA_CH 0
        #define PDMA_GET_TD_STS(p) ((p)->TDSTS)
        #define LPPDMA_GET_TD_STS(p) ((p)->TDSTS)
        #define I2S_STATUS1_TXCNT_Msk 0x1f00u
        #define I2S_STATUS1_TXCNT_Pos 8
        #define I2S_CTL1_TXTH_Msk 0xf00u
        #define I2S_CTL1_TXTH_Pos 8
        #define M55M1_ENABLE_DAC_I2S_OUTPUT 1
        static struct {uint32_t ICSR;} scb={19};
        #define SCB (&scb)
        static struct {uint32_t completed_blocks;} g_dmic_state;
        #define __get_PRIMASK() mask
        #define __get_BASEPRI() 3U
        #define __get_IPSR() 24U
        #define __disable_irq() (mask=1U)
        #define __set_PRIMASK(x) (mask=(x))
        #define __DMB() ((void)0)
        ''' + body + r'''
        int main(void) {
            m55m1_audio_begin_startup_trace();
            m55m1_audio_record_startup_event(AUDIO_STARTUP_TX_ARM, ++cycle);
            assert(mask==0 && g_startup_trace.count==1);
            assert(g_startup_trace.events[0].primask==0 && g_startup_trace.events[0].basepri==3);
            mask=1; m55m1_audio_record_startup_event(AUDIO_STARTUP_TX_ENABLED, ++cycle);
            assert(mask==1); mask=0;
            for (unsigned i=0;i<100;i++) {
                g_dmic_state.completed_blocks=i+1;
                m55m1_audio_record_startup_event(AUDIO_STARTUP_TX_TD, ++cycle);
                m55m1_audio_record_startup_event(AUDIO_STARTUP_RX_TD, ++cycle);
                m55m1_audio_record_startup_event(AUDIO_STARTUP_CAPTURE_TAKE, ++cycle);
            }
            assert(g_startup_trace.count==9 && g_startup_trace.tx_events==4 && g_startup_trace.rx_events==2);
            m55m1_audio_record_startup_event(AUDIO_STARTUP_STOP, ++cycle);
            assert(g_startup_trace.count==10 && !g_startup_trace.active);
            const volatile audio_startup_trace_t *trace=m55m1_audio_get_startup_trace();
            assert(trace->events[0].i2s_status0==11 && trace->events[0].tx_dma_ctl==14);
            assert(trace->events[3].rx_sequence==1 && trace->events[0].ipsr==24);
            I2S1=NULL; // No MMIO reads may happen after stop.
            m55m1_audio_record_startup_event(AUDIO_STARTUP_TX_TD, ++cycle);
            assert(trace->count==10);
            m55m1_audio_begin_startup_trace();
            assert(trace->count==0 && trace->active && trace->tx_events==0 && trace->take_events==0);
            I2S1=&i2s;
            for (unsigned i=0;i<100;i++) { m55m1_audio_record_startup_event(AUDIO_STARTUP_TX_ARM, ++cycle); }
            assert(trace->count==AUDIO_STARTUP_TRACE_CAPACITY);
            m55m1_audio_record_startup_event(AUDIO_STARTUP_STOP, ++cycle);
            assert(!trace->active && trace->count==AUDIO_STARTUP_TRACE_CAPACITY);
            return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'trace.c'
            path.write_text(harness)
            executable=Path(directory)/'trace.exe'
            result=subprocess.run(['gcc','-std=c11','-Wall','-Wextra','-Werror','-I',str(ROOT),
                                   str(path),'-o',str(executable)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(executable)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
