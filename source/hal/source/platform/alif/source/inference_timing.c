/* Copyright (C) 2025 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */

/**
 * @file inference_timing.c
 * @brief GPIO-based timing signals for inference profiling
 *
 * This module provides GPIO control for external timing measurement:
 * - Pre-inference GPIO (P1_4 / GPIO1_4): High 50ms before inference starts
 * - Post-inference GPIO (P1_5 / GPIO1_5): High 50ms after inference completes
 *
 * These signals can be measured with an oscilloscope or logic analyzer
 * to accurately measure inference execution time.
 */

#include "inference_timing.h"
#include "Driver_IO.h"
#include "RTE_Components.h"
#include CMSIS_device_header

/* GPIO pins for timing measurement on DevKit E7
 * P1_4 (GPIO1 PIN4) - Pre-inference signal
 * P1_5 (GPIO1 PIN5) - Post-inference signal
 * These pins are configured as GPIO outputs in pins.h
 */
#define PRE_INFERENCE_GPIO_PORT    1
#define PRE_INFERENCE_GPIO_PIN     4

#define POST_INFERENCE_GPIO_PORT   1
#define POST_INFERENCE_GPIO_PIN    5

/* GPIO Driver instances */
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(PRE_INFERENCE_GPIO_PORT);
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(POST_INFERENCE_GPIO_PORT);

static ARM_DRIVER_GPIO *gpio_pre = &ARM_Driver_GPIO_(PRE_INFERENCE_GPIO_PORT);
static ARM_DRIVER_GPIO *gpio_post = &ARM_Driver_GPIO_(POST_INFERENCE_GPIO_PORT);

static bool initialized = false;

int inference_timing_init(void)
{
    int32_t ret;

    /* Initialize pre-inference GPIO */
    ret = gpio_pre->Initialize(PRE_INFERENCE_GPIO_PIN, NULL);
    if (ret != ARM_DRIVER_OK) {
        return -1;
    }

    ret = gpio_pre->PowerControl(PRE_INFERENCE_GPIO_PIN, ARM_POWER_FULL);
    if (ret != ARM_DRIVER_OK) {
        return -2;
    }

    ret = gpio_pre->SetDirection(PRE_INFERENCE_GPIO_PIN, GPIO_PIN_DIRECTION_OUTPUT);
    if (ret != ARM_DRIVER_OK) {
        return -3;
    }

    /* Initialize post-inference GPIO */
    ret = gpio_post->Initialize(POST_INFERENCE_GPIO_PIN, NULL);
    if (ret != ARM_DRIVER_OK) {
        return -4;
    }

    ret = gpio_post->PowerControl(POST_INFERENCE_GPIO_PIN, ARM_POWER_FULL);
    if (ret != ARM_DRIVER_OK) {
        return -5;
    }

    ret = gpio_post->SetDirection(POST_INFERENCE_GPIO_PIN, GPIO_PIN_DIRECTION_OUTPUT);
    if (ret != ARM_DRIVER_OK) {
        return -6;
    }

    /* Set both pins low initially */
    gpio_pre->SetValue(PRE_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    gpio_post->SetValue(POST_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);

    initialized = true;
    return 0;
}

void inference_timing_pre_start(void)
{
    if (initialized) {
        gpio_pre->SetValue(PRE_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
    }
}

void inference_timing_pre_end(void)
{
    if (initialized) {
        gpio_pre->SetValue(PRE_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    }
}

void inference_timing_post_start(void)
{
    if (initialized) {
        gpio_post->SetValue(POST_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
    }
}

void inference_timing_post_end(void)
{
    if (initialized) {
        gpio_post->SetValue(POST_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    }
}

/************************ (C) COPYRIGHT ALIF SEMICONDUCTOR *****END OF FILE****/
