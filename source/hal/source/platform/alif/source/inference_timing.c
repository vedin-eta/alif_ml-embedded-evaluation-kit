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
#include "delay.h"

#include <stdio.h>

/* GPIO pins for timing measurement on DevKit E7
 * P0_0 (GPIO0 PIN0) - Pre-inference signal
 * P0_1 (GPIO0 PIN1) - Post-inference signal
 * These pins are configured as GPIO outputs in pins.h
 */
#define PRE_INFERENCE_GPIO_PORT    0
#define PRE_INFERENCE_GPIO_PIN     0
#define POST_INFERENCE_GPIO_PORT   0
#define POST_INFERENCE_GPIO_PIN    1

/* Special pins for 'g' routine */
#define SPECIAL_GPIO_PORT          0
#define SPECIAL_PIN_0              0
#define SPECIAL_PIN_1              1
#define SPECIAL_PIN_2              2
#define SPECIAL_PIN_4              4

/* GPIO Driver instances */
extern ARM_DRIVER_GPIO ARM_Driver_GPIO_(0);

static ARM_DRIVER_GPIO *gpio0 = &ARM_Driver_GPIO_(0);

static bool initialized = false;
static bool special_initialized = false;

static int32_t init_pin_output(ARM_DRIVER_GPIO* driver, uint8_t pin)
{
    int32_t ret;
    ret = driver->Initialize(pin, NULL);
    if (ret != ARM_DRIVER_OK) return ret;

    ret = driver->PowerControl(pin, ARM_POWER_FULL);
    if (ret != ARM_DRIVER_OK) return ret;

    ret = driver->SetDirection(pin, GPIO_PIN_DIRECTION_OUTPUT);
    if (ret != ARM_DRIVER_OK) return ret;

    ret = driver->SetValue(pin, GPIO_PIN_OUTPUT_STATE_LOW);
    return ret;
}

int inference_timing_init(void)
{
    if (init_pin_output(gpio0, PRE_INFERENCE_GPIO_PIN) != ARM_DRIVER_OK) return -1;
    if (init_pin_output(gpio0, POST_INFERENCE_GPIO_PIN) != ARM_DRIVER_OK) return -2;

    initialized = true;
    return 0;
}

int inference_timing_special_init(void)
{
    if (init_pin_output(gpio0, SPECIAL_PIN_0) != ARM_DRIVER_OK) return -1;
    if (init_pin_output(gpio0, SPECIAL_PIN_1) != ARM_DRIVER_OK) return -2;
    if (init_pin_output(gpio0, SPECIAL_PIN_2) != ARM_DRIVER_OK) return -3;
    if (init_pin_output(gpio0, SPECIAL_PIN_4) != ARM_DRIVER_OK) return -4;

    special_initialized = true;
    return 0;
}

void inference_timing_cycle_routine(void)
{
    if (!special_initialized) {
        printf("Special GPIO pins not initialized!\n");
        return;
    }

    uint8_t pins[] = {SPECIAL_PIN_0, SPECIAL_PIN_1, SPECIAL_PIN_2, SPECIAL_PIN_4};

    for (int i = 0; i < 4; i++) {
        printf("Setting P0_%d HIGH for 1s\n", pins[i]);
        gpio0->SetValue(pins[i], GPIO_PIN_OUTPUT_STATE_HIGH);
        sleep_or_wait_msec(1000);
        gpio0->SetValue(pins[i], GPIO_PIN_OUTPUT_STATE_LOW);
    }

    /* Ensure all are low at the end */
    for (int i = 0; i < 4; i++) {
        gpio0->SetValue(pins[i], GPIO_PIN_OUTPUT_STATE_LOW);
    }
}

void inference_timing_pre_start(void)
{
    if (initialized) {
        printf("Setting pre-inference pin high.\n");
        gpio0->SetValue(PRE_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
    }
}

void inference_timing_pre_end(void)
{
    if (initialized) {
        printf("Setting pre-inference pin low.\n");
        gpio0->SetValue(PRE_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    }
}

void inference_timing_post_start(void)
{
    if (initialized) {
        printf("Setting post-inference pin high.\n");
        gpio0->SetValue(POST_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_HIGH);
    }
}

void inference_timing_post_end(void)
{
    if (initialized) {
        printf("Setting post-inference pin low.\n");
        gpio0->SetValue(POST_INFERENCE_GPIO_PIN, GPIO_PIN_OUTPUT_STATE_LOW);
    }
}

/************************ (C) COPYRIGHT ALIF SEMICONDUCTOR *****END OF FILE****/
