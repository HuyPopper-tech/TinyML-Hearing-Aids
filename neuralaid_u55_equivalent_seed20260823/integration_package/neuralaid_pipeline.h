#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
void neuralaid_init(void*, size_t);
void neuralaid_reset_stream(void);
void neuralaid_push_audio(const int16_t*, size_t);
bool neuralaid_pop_output(int16_t out96[96]);
