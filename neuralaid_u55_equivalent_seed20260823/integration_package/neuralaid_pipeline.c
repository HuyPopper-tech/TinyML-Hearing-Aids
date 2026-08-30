#include "neuralaid_pipeline.h"
#include "neuralaid_contract.h"
#include <limits.h>
static int16_t audio_ring[NEURALAID_FFT_LENGTH], output_queue[4][NEURALAID_CHUNK_SAMPLES]; static unsigned write_index, pending, output_read, output_count; static int16_t previous_sample;
static int16_t sat16(int32_t x){if(x>INT16_MAX)return INT16_MAX;if(x<INT16_MIN)return INT16_MIN;return (int16_t)x;}
static void process_one_chunk(void){int16_t*out=output_queue[(output_read+output_count)%4];/* TODO production modules: context->CMSIS RFFT->pack/quantize->TFLM/Ethos-U->dequantize->iRFFT->window/OLA. */for(unsigned i=0;i<NEURALAID_CHUNK_SAMPLES;++i)out[i]=0;if(output_count<4)++output_count;}
void neuralaid_reset_stream(void){for(unsigned i=0;i<NEURALAID_FFT_LENGTH;++i)audio_ring[i]=0;write_index=pending=output_read=output_count=0;previous_sample=0;}
void neuralaid_init(void*a,size_t n){(void)a;(void)n;neuralaid_reset_stream();}
void neuralaid_push_audio(const int16_t*in,size_t count){for(size_t i=0;i<count;++i){int32_t y=(int32_t)in[i]-((NEURALAID_PREEMPHASIS_Q15*(int32_t)previous_sample)>>15);previous_sample=in[i];audio_ring[write_index]=sat16(y);write_index=(write_index+1u)%NEURALAID_FFT_LENGTH;++pending;while(pending>=NEURALAID_CHUNK_SAMPLES+NEURALAID_LOOKAHEAD_SAMPLES&&output_count<4){pending-=NEURALAID_CHUNK_SAMPLES;process_one_chunk();}}}
bool neuralaid_pop_output(int16_t out96[96]){if(!output_count)return false;for(unsigned i=0;i<NEURALAID_CHUNK_SAMPLES;++i)out96[i]=output_queue[output_read][i];output_read=(output_read+1u)%4;--output_count;return true;}
