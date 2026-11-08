"""Host-only installed-CMSIS RFFT candidate; does not modify the active backend."""
import json
import os
from pathlib import Path
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
runtime = ROOT/'diagnostics/i2c_stuck_trace_20261108_162019/host_compiler_runtime'
env = dict(os.environ, PATH=str(runtime)+';'+os.environ['PATH'])
source = HERE/'real_fft_candidate.c'
source.write_text(r'''
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "services/deepfir/deepfir_dsp.h"
#include "dsp/transform_functions.h"
static arm_cfft_instance_f32 complex_fft;
static arm_rfft_fast_instance_f32 real_fft;
static int cfft(float *p, uint32_t n, uint8_t inv, void *ctx) {
    (void)ctx; if (n!=256 || inv) return -1;
    arm_cfft_f32(&complex_fft,p,0,1); return 0;
}
static int rfft(float *p, uint32_t n, uint8_t inv, void *ctx) {
    (void)ctx; if (n!=256 || inv) return -1;
    float result[256];
    for (unsigned i=0;i<256;++i) p[i]=p[2*i];
    arm_rfft_fast_f32(&real_fft,p,result,0);
    p[0]=result[0]; p[1]=0; p[256]=result[1]; p[257]=0;
    for (unsigned i=1;i<128;++i) {p[2*i]=result[2*i];p[2*i+1]=result[2*i+1];}
    return 0;
}
int main(void) {
    static deepfir_feature_state_t a,b;
    a.scale=b.scale=9.672559826867655e-05; a.fft=cfft;b.fft=rfft;
    if (arm_cfft_init_256_f32(&complex_fft) || arm_rfft_fast_init_256_f32(&real_fft)) return 1;
    deepfir_feature_reset(&a);deepfir_feature_reset(&b);
    unsigned rng=12345, mismatches=0, comparisons=0; int max_error=0,first_hop=-1,first_code=-1;
    const unsigned bins[]={0,1,2,16,64,127,128};
    for (unsigned hop=0;hop<8192;++hop) {
        int16_t pcm[16],x[387],y[387];
        if (hop%128==0) {deepfir_feature_reset(&a);deepfir_feature_reset(&b);}
        for (unsigned i=0;i<16;++i) {
            rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;
            if (hop<2048) pcm[i]=(int16_t)rng;
            else if(hop<3072) pcm[i]=(hop%128==0 && i==0)?32767:0;
            else if(hop<4096) pcm[i]=0;
            else {
                unsigned bin=bins[(hop/128)%7];
                pcm[i]=(int16_t)(16384*cos(6.283185307179586*bin*((hop%128)*16+i)/256));
            }
        }
        if (deepfir_feature_pcm16(pcm,16,x,&a) || deepfir_feature_pcm16(pcm,16,y,&b)) return 2;
        for (unsigned i=0;i<387;++i) {
            int error=abs((int)x[i]-y[i]); ++comparisons;
            if(error) {++mismatches;if(first_hop<0){first_hop=(int)hop;first_code=(int)i;}}
            if(error>max_error) max_error=error;
        }
    }
    printf("{\"backend\":\"HOST_SCALAR_CMSIS\",\"hops\":8192,\"codes\":%u,"
           "\"changed_codes\":%u,\"max_code_error\":%d,\"first_changed_hop\":%d,\"first_changed_code\":%d}\n",
           comparisons,mismatches,max_error,first_hop,first_code);
    return 0;
}
''')
sources = ['services/deepfir/deepfir_features.c'] + [
    'libs/CMSIS/DSP/Source/'+s for s in (
    'TransformFunctions/arm_cfft_f32.c', 'TransformFunctions/arm_cfft_init_f32.c',
    'TransformFunctions/arm_cfft_radix8_f32.c', 'TransformFunctions/arm_bitreversal2.c',
    'TransformFunctions/arm_rfft_fast_f32.c', 'TransformFunctions/arm_rfft_fast_init_f32.c',
    'CommonTables/arm_common_tables.c', 'CommonTables/arm_const_structs.c')]
command=['rtk','proxy','gcc','-std=c11','-O2','-D__GNUC_PYTHON__','-I',str(ROOT),
         '-I',str(ROOT/'libs/CMSIS/DSP/Include'),'-I',str(ROOT/'libs/CMSIS/DSP/PrivateInclude'),
         '-I',str(ROOT/'libs/CMSIS/Core/Include'),str(source),*[str(ROOT/s) for s in sources],
         '-lm','-o',str(HERE/'real_fft_candidate.exe')]
build=subprocess.run(command,env=env,capture_output=True,text=True)
if build.returncode: raise RuntimeError(build.stderr)
run=subprocess.run([str(HERE/'real_fft_candidate.exe')],capture_output=True,text=True)
if run.returncode: raise RuntimeError(run.stderr)
report=json.loads(run.stdout)
report['active_backend_changed']=False
report['device_execution']='NOT_RUN'
report['compile_command']=command
(HERE/'real_fft_candidate.json').write_text(json.dumps(report,indent=2))
print(run.stdout)
