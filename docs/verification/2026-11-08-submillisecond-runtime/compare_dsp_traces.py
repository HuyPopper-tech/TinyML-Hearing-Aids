"""Canonical gain/phase DSP replay with synthetic PCM; never controller parity."""
from datetime import datetime
import ctypes as ct
import hashlib
import json
import os
from pathlib import Path
import subprocess
import numpy as np

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
WORK = ROOT/'diagnostics'/('subms_dsp_'+datetime.now().strftime('%Y%m%d_%H%M%S'))
WORK.mkdir(exist_ok=False)
runtime = ROOT/'diagnostics/i2c_stuck_trace_20261108_162019/host_compiler_runtime'
env = dict(os.environ, PATH=str(runtime)+os.pathsep+os.environ['PATH'])
source = WORK/'compare.c'
source.write_text(r'''
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include "services/deepfir/deepfir_cmsis_fft_adapter.h"
#include "services/deepfir/deepfir_reconstruction.h"
int baseline_reconstruct_taps(const int16_t *,const int16_t *,const float *,
    const int32_t *,deepfir_fft_callback,void *,float *);
typedef struct {uint32_t hops,pcm_mismatches,max_pcm_error,first_hop,first_sample;
    float max_tap_error,max_output_error;} result_t;
int compare(const int16_t *gain,const int16_t *phase,uint32_t hops,result_t *result) {
    deepfir_cmsis_fft_context_t fft;
    deepfir_dsp_state_t old_state,new_state;
    if(deepfir_cmsis_fft_init(&fft)) return 1;
    deepfir_dsp_reset(&old_state,0);deepfir_dsp_reset(&new_state,0);
    *result=(result_t){.hops=hops,.first_hop=UINT32_MAX,.first_sample=UINT32_MAX};
    const float scales[2]={5.657631845679134e-05f,7.72049679653719e-05f};
    const int32_t zeros[2]={0};
    uint32_t random=0x68712319U;
    for(uint32_t hop=0;hop<hops;++hop) {
        float old_taps[128],new_taps[128],pcm[16],old_out[16],new_out[16];
        if(baseline_reconstruct_taps(gain+hop*65,phase+hop*65,scales,zeros,
            deepfir_cmsis_fft_callback,&fft,old_taps) ||
            deepfir_reconstruct_taps(gain+hop*65,phase+hop*65,scales,zeros,
            deepfir_cmsis_fft_callback,&fft,new_taps)) return 2;
        for(unsigned i=0;i<128;++i) {
            float error=fabsf(old_taps[i]-new_taps[i]);
            if(error>result->max_tap_error) result->max_tap_error=error;
            if(error>2e-5f+2e-5f*fabsf(old_taps[i])) return 3;
        }
        for(unsigned i=0;i<16;++i) {
            random^=random<<13;random^=random>>17;random^=random<<5;
            pcm[i]=(float)(int16_t)random/32768.0f;
            if(hop%512==0) pcm[i]=0;
            if(hop%512==1) pcm[i]=(i==0)?32767.0f/32768.0f:0;
        }
        if(deepfir_dsp_process(&old_state,pcm,16,old_taps,old_out) ||
           deepfir_dsp_process(&new_state,pcm,16,new_taps,new_out)) return 4;
        for(unsigned i=0;i<16;++i) {
            float error=fabsf(old_out[i]-new_out[i]);
            if(error>result->max_output_error) result->max_output_error=error;
            if(error>2e-5f+2e-6f*fabsf(old_out[i])) return 5;
            unsigned code_error=(unsigned)abs((int)deepfir_pcm16_from_float(old_out[i])-
                                            deepfir_pcm16_from_float(new_out[i]));
            if(code_error>result->max_pcm_error) result->max_pcm_error=code_error;
            if(code_error) {
                if(!result->pcm_mismatches) {result->first_hop=hop;result->first_sample=i;}
                ++result->pcm_mismatches;
            }
        }
    }
    return 0;
}
''')
includes=['-I',str(ROOT/'services/deepfir'),'-I',str(ROOT),'-I',str(ROOT/'libs/CMSIS/DSP/Include'),
          '-I',str(ROOT/'libs/CMSIS/DSP/PrivateInclude'),'-I',str(ROOT/'libs/CMSIS/Core/Include')]
commands=[]


def run(command):
    commands.append(command)
    r=subprocess.run(['rtk','proxy',*command],env=env,capture_output=True,text=True)
    if r.returncode: raise RuntimeError(r.stdout+r.stderr)


baseline=HERE/'baseline_reconstruction.c'
run(['gcc','-std=c11','-O2',*includes,'-Ddeepfir_reconstruct_taps=baseline_reconstruct_taps',
     '-c',str(baseline),'-o',str(WORK/'baseline.o')])
run(['gcc','-std=c11','-O2','-ffp-contract=off','-D__GNUC_PYTHON__',
     '-DARM_MATH_AUTOVECTORIZE=1',
     '-DDEEPFIR_DSP_CMSIS_FFT=1','-DM55M1_DEEPFIR_MVE=1','-D__ARM_FEATURE_MVE=2',
     '-I',str(ROOT/'tests/mve_model'),*includes,'-c',
     str(ROOT/'services/deepfir/deepfir_reconstruction.c'),'-o',str(WORK/'vector.o')])
sources=['services/deepfir/deepfir_dsp.c',
         'services/deepfir/deepfir_cmsis_fft_adapter.c']+[
    'libs/CMSIS/DSP/Source/'+p for p in ('ControllerFunctions/arm_sin_cos_f32.c',
    'TransformFunctions/arm_cfft_f32.c','TransformFunctions/arm_cfft_init_f32.c',
    'TransformFunctions/arm_cfft_radix8_f32.c','TransformFunctions/arm_bitreversal2.c',
    'CommonTables/arm_common_tables.c','CommonTables/arm_const_structs.c')]
run(['gcc','-shared','-std=c11','-O2','-D__GNUC_PYTHON__','-DDEEPFIR_DSP_CMSIS_FFT=1',
     *includes,str(source),str(WORK/'baseline.o'),str(WORK/'vector.o'),*[str(ROOT/p) for p in sources],
     '-lm','-o',str(WORK/'dsp.dll')])


class Result(ct.Structure):
    _fields_=[(name,ct.c_uint32) for name in ('hops','pcm_mismatches','max_pcm_error','first_hop','first_sample')]+[
        ('max_tap_error',ct.c_float),('max_output_error',ct.c_float)]


library=ct.CDLL(str(WORK/'dsp.dll'))
library.compare.argtypes=[ct.c_void_p,ct.c_void_p,ct.c_uint32,ct.POINTER(Result)]
package=ROOT/'deepfir_r2_scratch_ptq_20261107_151431_366715/firmware_deepfir_r2_C_34c5bd074ae6_69c1c3d2'
report={'mode':'CANONICAL_GAIN_PHASE_WITH_SYNTHETIC_PCM','backend':'HOST_SCALAR_CMSIS_WITH_VECTOR_RECONSTRUCTION_LANE_MODEL',
        'controller_invoked':False,'mve_executed':False,'vector_branch_lane_model':True,'commands':commands,'traces':{}}
for name in ('176f4e875a6dd550','c07ec5e74f1bd3dc'):
    path=package/'integer_replay'/(name+'.npz')
    with np.load(path,allow_pickle=False) as trace:
        gain=np.ascontiguousarray(trace['gain_codes'])
        phase=np.ascontiguousarray(trace['phase_codes'])
    result=Result()
    status=library.compare(gain.ctypes.data,phase.ctypes.data,len(gain),ct.byref(result))
    report['traces'][name]=dict(exit_code=status,source_sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
        **{name:getattr(result,name) for name,_ in Result._fields_})
(WORK/'receipt.json').write_text(json.dumps(report,indent=2))
print(json.dumps({k:v for k,v in report.items() if k!='commands'},indent=2))
if any(t['exit_code'] for t in report['traces'].values()): raise SystemExit(1)
