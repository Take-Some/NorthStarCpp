// NorthStarCpp monotonic clock provider ABI.
#pragma once
#include <northstarcpp/module.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NORTHSTARCPP_TIME_ABI 1u

// Return 1 and write the result on success, or return 0 on failure.
// Ticks are nondecreasing with a fixed frequency and arbitrary origin.
// Both functions must be thread-safe.
typedef struct NorthStarTimeApi {
    int (*read_counter)(uint64_t* ticks);
    int (*read_frequency)(uint64_t* ticks_per_second);
} NorthStarTimeApi;

#ifdef __cplusplus
}
#endif
