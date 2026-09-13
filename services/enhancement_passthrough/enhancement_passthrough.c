/**
 * @file enhancement_passthrough.c
 * @brief Stateless PCM16 passthrough implementation.
 * @author Bui Minh Huy
 * @date 2026-09-13
 */

#include <string.h>

#include "services/enhancement_passthrough/enhancement_passthrough.h"

bool enhancement_passthrough_process(const int16_t *input,
                                     int16_t *output,
                                     size_t sample_count) {
    if (sample_count == 0U) {
        return true;
    }
    if ((input == NULL) || (output == NULL)) {
        return false;
    }

    memmove(output, input, sample_count * sizeof(*output));
    return true;
}
