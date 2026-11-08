"""Run actual driver start/submit/IRQ code with register-request models, not audio parity."""
from pathlib import Path
import subprocess
import re
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class DmaOwnershipTests(unittest.TestCase):
    def test_product_terminal_mode_and_idle_underflow_irq_binding(self):
        driver=(ROOT/'drivers/tlv320dac3100/tlv320dac3100.c').read_text()
        descriptor=driver[driver.index('static void tlv320dac3100_configure_descriptor('):driver.index('static uint8_t tlv320dac3100_gain_to_register(')]
        self.assertRegex(descriptor,r'(?s)#if M55M1_PRODUCT_AUDIO.*?operation = PDMA_OP_BASIC;')
        self.assertIn('g_pdma_descriptors[slot].NEXT = 0U',descriptor)
        board=(ROOT/'board/m55m1_audio.c').read_text()
        handler=board[board.index('void I2S1_IRQHandler(void)'):board.index('void LPPDMA_IRQHandler(void)')]
        self.assertIn('g_playback_irq_callback(',handler)
        self.assertIn('NVIC_EnableIRQ(I2S1_IRQn)',board)
        self.assertLess(board.index('I2S_DisableInt(I2S1'),board.index('NVIC_EnableIRQ(I2S1_IRQn)'))
        status=driver[driver.index('void tlv320dac3100_get_status('):driver.index('tlv320dac3100_status_t tlv320dac3100_recover(')]
        self.assertIn('tlv320dac3100_tx_faulted()',status)

    def test_product_fifo_prefetch_preserves_two_silences_and_full_processing_slot(self):
        source = (ROOT/'drivers/tlv320dac3100/tlv320dac3100.c').read_text()
        begin = source.index('tlv320dac3100_status_t tlv320dac3100_start(void)')
        helpers = source.find('// Product FIFO handoff helpers.')
        start = source[helpers if helpers >= 0 else begin:source.index('tlv320dac3100_status_t tlv320dac3100_mute(')]
        submit = source[re.search(r'tlv320dac3100_status_t\s+tlv320dac3100_submit_mono_block_tagged\(', source).start():source.index('bool tlv320dac3100_take_audio_completion(')]
        irq = source[source.index('void tlv320dac3100_handle_pdma_irq('):]
        harness = r'''
        #include <assert.h>
        #include <stdbool.h>
        #include <stdint.h>
        #include <stddef.h>
        #include <string.h>
        #define M55M1_PRODUCT_AUDIO 1
        #define PDMA_OP_SCATTER 2U
        #define I2S_STATUS0_TXUDIF_Msk (1U<<16)
        #define I2S_IEN_TXUDFIEN_Msk 256U
        typedef int tlv320dac3100_status_t;
        enum {TLV320DAC3100_STATUS_OK,TLV320DAC3100_STATUS_INVALID_STATE,
              TLV320DAC3100_STATUS_INVALID_ARGUMENT,TLV320DAC3100_STATUS_BUSY,
              TLV320DAC3100_STATUS_DMA_ERROR};
        typedef struct {uint32_t sequence,capture_completed_cycle;} tlv320dac3100_audio_block_tag_t;
        typedef struct {uint32_t sequence,capture_completed_cycle,dac_completed_cycle,playback_slot;} tlv320dac3100_audio_completion_t;
        static struct {uint32_t STATUS0;} i2s;
        static bool requests,tx,pending,busy,abort_pending,underflow_irq,prefill=true,inject_underflow;
        static unsigned arms,fifo,mask;
        static uint32_t latched_source;
        #define __get_PRIMASK() mask
        #define __disable_irq() (mask=1)
        #define __set_PRIMASK(x) (mask=(x))
        #define __DMB() ((void)0)
        #define __DSB() do {if(inject_underflow) {i2s.STATUS0=I2S_STATUS0_TXUDIF_Msk;inject_underflow=false;}} while(0)
        #define I2S_ENABLE_TXDMA(x) ((void)(x),requests=true)
        #define I2S_DISABLE_TXDMA(x) ((void)(x),requests=false)
        #define I2S_ENABLE_TX(x) ((void)(x),tx=true)
        #define I2S_DISABLE_TX(x) ((void)(x),tx=false)
        #define I2S_GET_TX_FIFO_LEVEL(x) ((void)(x),fifo)
        #define I2S_WRITE_TX_FIFO(x,value) do {(void)(x);assert((value)==0);if(prefill) {++fifo;}} while(0)
        #define I2S_CLR_INT_FLAG(x,m) ((x)->STATUS0 &= ~(m))
        #define I2S_EnableInt(x,m) ((void)(x),(void)(m),underflow_irq=true)
        #define I2S_DisableInt(x,m) ((void)(x),(void)(m),underflow_irq=false)
        #define PDMA_IS_CH_BUSY(p,c) ((void)(p),(void)(c),busy)
        #define PDMA_GET_TD_STS(p) ((void)(p),pending)
        #define PDMA_GET_ABORT_STS(p) ((void)(p),abort_pending)
        #define PDMA_CLR_TD_FLAG(p,m) ((void)(p),(void)(m),pending=false)
        #define PDMA_CLR_ABORT_FLAG(p,m) ((void)(p),(void)(m),abort_pending=false)
        #define PDMA_STOP(p,c) ((void)(p),(void)(c),busy=false)
        #define PDMA_RESET(p,c) ((void)(p),(void)(c))
        #define PDMA_INT_TRANS_DONE 1U
        #define PDMA_DisableInt(p,c,m) ((void)(p),(void)(c),(void)(m))
        static struct {uint16_t samples_per_block;} profile={16};
        static struct {
            struct {bool initialized,running; uint32_t queued_blocks,completed_blocks,underrun_blocks,dma_errors;} status;
            struct {typeof(i2s) *i2s; void *pdma; uint8_t pdma_channel; void (*block_callback)(void *); void *callback_context;} config;
            typeof(profile) *profile;
            bool slot_available[2],slot_contains_audio[2],slot_has_tag[2];
            tlv320dac3100_audio_block_tag_t slot_tags[2];
            tlv320dac3100_audio_completion_t latest_completion;
            bool completion_ready,has_queued_audio,dma_active;
            uint8_t completed_slot;
        } g_context;
        static uint32_t g_stereo_buffers[2][16],g_silence_buffer[16];
        static struct {uint32_t SA;} g_pdma_descriptors[2];
        #define tlv320dac3100_get_pdma_request(x) ((void)(x),0U)
        #define PDMA_SetTransferMode(p,c,r,s,a) do { \
            (void)(p);(void)(c);(void)(r);assert((s)==PDMA_OP_SCATTER); \
            unsigned model_slot=(a)==(uint32_t)(uintptr_t)&g_pdma_descriptors[0]?0:1; \
            latched_source=g_pdma_descriptors[model_slot].SA; ++arms; \
        } while(0)
        ''' + start + submit + irq + r'''
        static void complete(unsigned slot,uint32_t cycle) {
            assert(g_context.completed_slot==slot);pending=true;fifo=16;
            tlv320dac3100_handle_pdma_irq(cycle);
        }
        static void reset(void) {
            memset(&g_context,0,sizeof(g_context));memset(g_stereo_buffers,0,sizeof(g_stereo_buffers));
            i2s.STATUS0=I2S_STATUS0_TXUDIF_Msk;fifo=arms=mask=0;
            requests=tx=pending=busy=abort_pending=underflow_irq=inject_underflow=false;prefill=true;
            g_context.config.i2s=&i2s;g_context.profile=&profile;g_context.status.initialized=true;
            for(unsigned s=0;s<2;s++) g_pdma_descriptors[s].SA=(uint32_t)(uintptr_t)g_silence_buffer;
            assert(tlv320dac3100_start()==0 && mask==0 && tx && fifo==16);
        }
        int main(void) {
            int16_t pcm[16];for(unsigned i=0;i<16;i++) pcm[i]=(i&1)?32767:-32768;
            reset();complete(1,1021);
            // Silence0 was prefilled; no instantaneous DMA TD consumes a logical slot.
            // RX1 has not arrived, but silence1 is still in FIFO after this first TD.
            assert(!g_context.status.underrun_blocks && tx);
            assert(arms==1 && !requests && !g_context.dma_active);
            assert(latched_source==(uint32_t)(uintptr_t)g_silence_buffer);
            tlv320dac3100_audio_block_tag_t tag={1,1050};
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==0);
            assert(arms==2 && requests && g_context.dma_active);
            assert(latched_source==(uint32_t)(uintptr_t)g_stereo_buffers[0]);
            assert(g_stereo_buffers[0][0]==0x80008000U && g_stereo_buffers[0][1]==0x7fff7fffU);
            complete(0,2021);
            assert(g_context.latest_completion.sequence==1 && g_context.latest_completion.playback_slot==2);
            assert(g_context.latest_completion.capture_completed_cycle==1050);
            for(unsigned seq=2;seq<=60000;seq++) {
                tag.sequence=seq;tag.capture_completed_cycle=seq*1000;
                assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==0);
                assert(arms==seq+1);
                complete((seq-1)&1,seq*1000+1021);
                assert(!g_context.status.underrun_blocks && g_context.latest_completion.playback_slot==seq+1);
            }
            assert(g_context.status.queued_blocks==60000 && underflow_irq && mask==0);
            // No refill: real FIFO underflow must stop even without another DMA TD.
            i2s.STATUS0=I2S_STATUS0_TXUDIF_Msk;pending=false;
            tlv320dac3100_handle_pdma_irq(60002021);
            assert(g_context.status.underrun_blocks==1 && !tx && !requests && !underflow_irq);
            tlv320dac3100_handle_pdma_irq(60002022);assert(g_context.status.underrun_blocks==1);
            reset();complete(1,1021);tag.sequence=1;pending=true;
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==TLV320DAC3100_STATUS_BUSY);
            assert(!requests && g_context.status.queued_blocks==0 && arms==1);
            reset();complete(1,1021);busy=true;
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==TLV320DAC3100_STATUS_BUSY);
            assert(!requests && g_context.status.queued_blocks==0);
            reset();complete(1,1021);tag.sequence=2;
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==TLV320DAC3100_STATUS_BUSY);
            reset();complete(1,1021);tag.sequence=1;
            i2s.STATUS0=I2S_STATUS0_TXUDIF_Msk;
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==TLV320DAC3100_STATUS_BUSY);
            assert(!tx && !requests && !g_context.status.queued_blocks);
            reset();complete(1,1021);tag.sequence=1;inject_underflow=true;
            assert(tlv320dac3100_submit_mono_block_tagged(pcm,16,&tag)==TLV320DAC3100_STATUS_BUSY);
            assert(!tx && !requests && !g_context.status.queued_blocks && arms==1);
            reset();abort_pending=true;pending=false;tlv320dac3100_handle_pdma_irq(5);
            assert(g_context.status.dma_errors==1 && !tx && !requests);
            reset();g_context.status.running=false;tx=false;fifo=0;prefill=false;
            assert(tlv320dac3100_start()==TLV320DAC3100_STATUS_DMA_ERROR);
            assert(!tx && !requests && mask==0);return 0;
        }
        '''
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'fifo.c'; binary=Path(directory)/'fifo.exe'
            path.write_text(harness)
            build=subprocess.run(['gcc','-std=gnu11','-Wall','-Wextra','-Werror',str(path),'-o',str(binary)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

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
