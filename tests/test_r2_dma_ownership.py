"""Run actual driver start/submit/IRQ code with register-request models, not audio parity."""
from pathlib import Path
import subprocess
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DmaOwnershipTests(unittest.TestCase):
    def test_two_silence_slots_sequence_packing_pending_boundary_and_underrun(self):
        source = (ROOT/'drivers/tlv320dac3100/tlv320dac3100.c').read_text()
        start = source[source.index('tlv320dac3100_status_t tlv320dac3100_start(void)'):source.index('tlv320dac3100_status_t tlv320dac3100_mute(')]
        submit = source[re.search(r'tlv320dac3100_status_t\s+tlv320dac3100_submit_mono_block_tagged\(', source).start():source.index('bool tlv320dac3100_take_audio_completion(')]
        irq = source[source.index('void tlv320dac3100_handle_pdma_irq('):]
        harness = r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <stddef.h>
        #include <string.h>
        typedef int tlv320dac3100_status_t;
        enum {TLV320DAC3100_STATUS_OK, TLV320DAC3100_STATUS_INVALID_STATE,
              TLV320DAC3100_STATUS_INVALID_ARGUMENT, TLV320DAC3100_STATUS_BUSY};
        typedef struct {uint32_t sequence, capture_completed_cycle;} tlv320dac3100_audio_block_tag_t;
        typedef struct {uint32_t sequence, capture_completed_cycle, dac_completed_cycle, playback_slot;} tlv320dac3100_audio_completion_t;
        static bool requests, tx, pending, busy;
        static unsigned barriers;
        #define __get_PRIMASK() 0U
        #define __disable_irq() ((void)0)
        #define __set_PRIMASK(x) ((void)(x))
        #define __DMB() (++barriers)
        #define __DSB() (++barriers)
        #define I2S_ENABLE_TXDMA(x) ((void)(x), requests=true)
        #define I2S_DISABLE_TXDMA(x) ((void)(x), requests=false)
        #define I2S_ENABLE_TX(x) ((void)(x), tx=true)
        #define I2S_DISABLE_TX(x) ((void)(x), tx=false)
        #define PDMA_IS_CH_BUSY(p,c) ((void)(p),(void)(c),busy)
        #define PDMA_GET_TD_STS(p) ((void)(p),pending)
        #define PDMA_GET_ABORT_STS(p) ((void)(p),0U)
        #define PDMA_CLR_TD_FLAG(p,m) ((void)(p),(void)(m),pending=false)
        #define PDMA_CLR_ABORT_FLAG(p,m) ((void)(p),(void)(m))
        static struct {uint16_t samples_per_block;} profile={16};
        static struct {
            struct {bool initialized,running; uint32_t queued_blocks,completed_blocks,underrun_blocks,dma_errors;} status;
            struct {void *i2s,*pdma; uint8_t pdma_channel; void (*block_callback)(void *); void *callback_context;} config;
            const void *unused;
            typeof(profile) *profile;
            bool slot_available[2],slot_contains_audio[2],slot_has_tag[2];
            tlv320dac3100_audio_block_tag_t slot_tags[2];
            tlv320dac3100_audio_completion_t latest_completion;
            bool completion_ready,has_queued_audio;
            uint8_t completed_slot;
        } g_context;
        static uint32_t g_stereo_buffers[2][16],g_silence_buffer[16];
        static struct {uint32_t SA;} g_pdma_descriptors[2];
        ''' + start + submit + irq + r'''
        static void complete(unsigned slot) {
            assert(g_context.completed_slot == slot); pending=true;
            tlv320dac3100_handle_pdma_irq(g_context.status.completed_blocks+1);
        }
        static void reset(void) {
            memset(&g_context,0,sizeof(g_context));
            memset(g_stereo_buffers,0,sizeof(g_stereo_buffers));
            g_context.profile=&profile; g_context.status.initialized=true;
            pending=busy=false; assert(tlv320dac3100_start()==0);
            assert(!g_context.slot_available[0] && !g_context.slot_available[1]);
        }
        int main(void) {
            int16_t pcm[16]; for(unsigned i=0;i<16;i++) pcm[i]=(i&1)?32767:-32768;
            reset(); complete(0); assert(!g_context.status.underrun_blocks);
            tlv320dac3100_audio_block_tag_t tag={1,77};
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==0);
            assert(g_stereo_buffers[0][0]==0x80008000U && g_stereo_buffers[0][1]==0x7fff7fffU);
            complete(1); assert(!g_context.status.underrun_blocks && tx);
            tag.sequence=2; assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==0);
            complete(0); assert(g_context.latest_completion.sequence==1);
            assert(g_context.latest_completion.capture_completed_cycle==77);
            assert(g_context.latest_completion.dac_completed_cycle==3);
            assert(g_context.latest_completion.playback_slot==2);
            tag.sequence=1; unsigned queued=g_context.status.queued_blocks;
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==TLV320DAC3100_STATUS_BUSY);
            assert(g_context.status.queued_blocks==queued);
            reset(); complete(0); tag.sequence=1; pending=true;
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==TLV320DAC3100_STATUS_BUSY);
            assert(!requests && !g_context.status.queued_blocks && !g_stereo_buffers[0][0]);
            reset(); complete(0); busy=true;
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==TLV320DAC3100_STATUS_BUSY);
            assert(!requests && !g_context.status.queued_blocks);
            reset(); complete(0); complete(1);
            assert(g_context.status.underrun_blocks==1 && !requests && !tx);
            assert(barriers>0); return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'ownership.c'; binary=Path(directory)/'ownership.exe'
            path.write_text(harness)
            build=subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
