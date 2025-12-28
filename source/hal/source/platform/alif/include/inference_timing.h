/* Copyright (C) 2025 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */

#ifndef INFERENCE_TIMING_H_
#define INFERENCE_TIMING_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize GPIO pins for inference timing measurement
 * @return 0 on success, negative on error
 */
int inference_timing_init(void);

/**
 * @brief Initialize GPIO pins P0_0, P0_1, P0_2, and P0_4 as outputs and set low
 * @return 0 on success, negative on error
 */
int inference_timing_special_init(void);

/**
 * @brief Cycle through P0_0, P0_1, P0_2, and P0_4, setting each high for 1 second
 */
void inference_timing_cycle_routine(void);

/**
 * @brief Set pre-inference GPIO pin high
 */
void inference_timing_pre_start(void);

/**
 * @brief Set pre-inference GPIO pin low
 */
void inference_timing_pre_end(void);

/**
 * @brief Set post-inference GPIO pin high
 */
void inference_timing_post_start(void);

/**
 * @brief Set post-inference GPIO pin low
 */
void inference_timing_post_end(void);

#ifdef __cplusplus
}
#endif

#endif /* INFERENCE_TIMING_H_ */
