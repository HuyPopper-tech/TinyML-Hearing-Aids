#include <stdint.h>
#include <stdio.h>
int16_t deepfir_state_feedback(int16_t code, int64_t multiplier, unsigned shift,
                               int32_t source_zero, int32_t target_zero) {
    if (shift > 46 || multiplier < 0 || multiplier > INT64_C(2147483648) || source_zero < -32768 ||
        source_zero > 32767 || target_zero < -32768 || target_zero > 32767) {
        return 0;
    }
    // Round the magnitude then restore the sign so negative ties also round away.
    const int64_t denominator = INT64_C(1) << shift;
    const int64_t value = ((int64_t)code - source_zero) * multiplier;
    // The rounded magnitude is nonnegative; division by 2^shift is exactly a shift.
    int64_t rounded = ((value < 0 ? -value : value) + denominator / 2) >> shift;
    if (value < 0) {
        rounded = -rounded;
    }
    rounded += target_zero;
    return (int16_t)(rounded < -32768 ? -32768 : rounded > 32767 ? 32767 : rounded);
}


int main(void) {
    const int64_t multiplier[2] = {2147371134,1075624845};
    const unsigned shift[2] = {31, 30};
    for (unsigned state=0; state<2; ++state) {
        unsigned mismatches=0, nonidentity=0, max_difference=0, max_error=0;
        for (int code=-32768; code<=32767; ++code) {
            int32_t magnitude=code < 0 ? -code : code;
            int32_t operand=state == 0 ? magnitude : magnitude * 2;
            // Emulate positive-lane VQRDMULH.S32 semantics; no MVE instruction is run.
            int32_t value=(int32_t)(((int64_t)operand * multiplier[state] + (INT64_C(1)<<30)) >> 31);
            if (code<0) value=-value;
            if (value>32767) value=32767;
            if (value<-32768) value=-32768;
            int expected=deepfir_state_feedback((int16_t)code, multiplier[state], shift[state], 0, 0);
            unsigned error=value>expected ? value-expected : expected-value;
            if (error) ++mismatches;
            if (error>max_error) max_error=error;
            if (value!=code) ++nonidentity;
            unsigned difference=value>code ? value-code : code-value;
            if (difference>max_difference) max_difference=difference;
        }
        printf("state=%c codes=65536 mismatches=%u max_error=%u nonidentity=%u max_change=%u\n",
               state==0 ? 'h' : 'c', mismatches, max_error, nonidentity, max_difference);
        if (mismatches) return 1;
    }
    return 0;
}
