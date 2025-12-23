/*
 * SPDX-FileCopyrightText: Copyright 2021, 2024 Arm Limited and/or its
 * affiliates <open-source-office@arm.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "hal.h"                    /* Brings in platform definitions. */
#include "TestModel.hpp"            /* Model class for running inference. */
#include "UseCaseHandler.hpp"       /* Handlers for different user options. */
#include "UseCaseCommonUtils.hpp"   /* Utils functions. */
#include "log_macros.h"             /* Logging functions */
#include "BufAttributes.hpp"        /* Buffer attributes to be applied */

extern "C" {
#include "uart_tracelib.h"          /* UART communication functions */
#include "inference_timing.h"       /* GPIO timing signals */
#include "delay.h"                  /* Accurate delay functions */
}

#include <cstring>                  /* For memcpy */

namespace arm {
namespace app {
    static uint8_t tensorArena[ACTIVATION_BUF_SZ] ACTIVATION_BUF_ATTRIBUTE;
    namespace inference_runner {
#if defined(DYNAMIC_MODEL_BASE) && defined(DYNAMIC_MODEL_SIZE)

static uint8_t* GetModelPointer()
{
    info("Model pointer: 0x%08x\n", DYNAMIC_MODEL_BASE);
    return reinterpret_cast<uint8_t*>(DYNAMIC_MODEL_BASE);
}

static size_t GetModelLen()
{
    /* TODO: Can we get the actual model size here somehow?
     * Currently we return the reserved space. It is possible to do
     * so by reading the memory pattern but it will not be reliable. */
    return static_cast<size_t>(DYNAMIC_MODEL_SIZE);
}

#else /* defined(DYNAMIC_MODEL_BASE) && defined(DYNAMIC_MODEL_SIZE) */

extern uint8_t* GetModelPointer();
extern size_t GetModelLen();

#endif /* defined(DYNAMIC_MODEL_BASE) && defined(DYNAMIC_MODEL_SIZE) */

/**
 * @brief Wait for user to send 'y' or 'n' to decide whether to load input from UART
 * @return true if user wants to load input from UART, false otherwise
 */
static bool WaitForInputChoice()
{
    info("\n=== Input Data Selection ===\n");
    printf("Load input tensor data from UART? (y/n): ");
    fflush(stdout);  // Force output to display immediately

    while (true) {
        char c = uart_getchar();

        if (c == 'y' || c == 'Y') {
            printf("Y\n");
            return true;
        } else if (c == 'n' || c == 'N') {
            printf("N\n");
            return false;
        }
        // Ignore other characters and wait for valid input
    }
}

/**
 * @brief Load model input tensor from UART using optimized bulk transfer
 * @param model Reference to the model
 */
static void LoadInputFromUART(const arm::app::Model& model)
{
    const size_t numInputs = model.GetNumInputs();

    info("\n=== Loading Input from UART ===\n");
    info("Number of input tensors: %zu\n", numInputs);

    for (size_t inputIndex = 0; inputIndex < numInputs; inputIndex++) {
        TfLiteTensor* inputTensor = model.GetInputTensor(inputIndex);

        if (!inputTensor || inputTensor->bytes == 0) {
            printf_err("Invalid input tensor at index %zu\n", inputIndex);
            continue;
        }

        info("\nInput tensor %zu:\n", inputIndex);
        info("  Size: %zu bytes (%.2f KB)\n",
             inputTensor->bytes,
             inputTensor->bytes / 1024.0f);
        info("  Type: ");
        switch (inputTensor->type) {
            case kTfLiteInt8:   info("int8\n");   break;
            case kTfLiteUInt8:  info("uint8\n");  break;
            case kTfLiteInt16:  info("int16\n");  break;
            case kTfLiteFloat32: info("float32\n"); break;
            default:            info("other (%d)\n", inputTensor->type); break;
        }

        info("\nReady to receive %zu bytes...\n", inputTensor->bytes);
        info("Start sending data from host PC now!\n");

        uint8_t* destPtr = reinterpret_cast<uint8_t*>(inputTensor->data.data);
        uint32_t bytesRemaining = inputTensor->bytes;
        uint32_t totalRead = 0;

        // Use optimized bulk transfer with chunking for progress display
        // and to handle potential UART buffer limitations
        const uint32_t CHUNK_SIZE = 4096;  // 4KB chunks for good performance

        while (bytesRemaining > 0) {
            uint32_t chunkSize = (bytesRemaining > CHUNK_SIZE) ? CHUNK_SIZE : bytesRemaining;

            // Perform bulk UART receive for this chunk
            int result = uart_receive_bulk(destPtr + totalRead, chunkSize);

            if (result != 0) {
                printf_err("\nUART receive error: %d\n", result);
                printf_err("Received %u / %u bytes before error\n",
                          totalRead, inputTensor->bytes);

                switch (result) {
                    case -2: printf_err("RX Overflow error\n"); break;
                    case -3: printf_err("RX Timeout error\n"); break;
                    case -4: printf_err("RX Break error\n"); break;
                    case -5: printf_err("RX Framing error\n"); break;
                    case -6: printf_err("RX Parity error\n"); break;
                    default: printf_err("Unknown error\n"); break;
                }
                return;
            }

            totalRead += chunkSize;
            bytesRemaining -= chunkSize;

            // Print progress
            float progress = (100.0f * totalRead) / inputTensor->bytes;
            info("  Progress: %u / %u bytes (%.1f%%)%s\r",
                 totalRead, inputTensor->bytes, progress,
                 (bytesRemaining == 0) ? "\n" : "");
        }

        info("Input tensor %zu loaded successfully!\n", inputIndex);
    }

    info("\n=== All input tensors loaded ===\n\n");
}

    }  /* namespace inference_runner */
} /* namespace app */
} /* namespace arm */

void MainLoop()
{
    arm::app::TestModel model;  /* Model wrapper object. */

    /* Load the model. */
    if (!model.Init(arm::app::tensorArena,
                    sizeof(arm::app::tensorArena),
                    arm::app::inference_runner::GetModelPointer(),
                    arm::app::inference_runner::GetModelLen())) {
        printf_err("Failed to initialise model\n");
        return;
    }

    /* Initialize GPIO timing pins */
    if (inference_timing_init() == 0) {
        info("GPIO timing pins initialized (P1_4=pre, P1_5=post)\n");
    } else {
        printf_err("Warning: Failed to initialize GPIO timing pins\n");
    }

    /* Instantiate application context. */
    arm::app::ApplicationContext caseContext;

    arm::app::Profiler profiler{"inference_runner"};
    caseContext.Set<arm::app::Profiler&>("profiler", profiler);
    caseContext.Set<arm::app::Model&>("model", model);

    /* Main loop - run inference with optional UART input */
    while (true) {
        info("\n");
        info("========================================\n");
        info("  Inference Runner - Ready\n");
        info("========================================\n");

        /* Ask user if they want to load input from UART */
        bool loadFromUART = arm::app::inference_runner::WaitForInputChoice();

        if (loadFromUART) {
            /* Load input tensor data from UART */
            arm::app::inference_runner::LoadInputFromUART(model);
        } else {
            info("Using default/random input data (from PopulateInputTensor)\n");
        }

        /* Run inference with GPIO timing signals */
        info("\n--- Starting Inference ---\n");

        /* Set pre-inference GPIO high for 50ms */
        inference_timing_pre_start();
        sleep_or_wait_msec(50);  /* Accurate delay using SysTick or PMU */
        inference_timing_pre_end();

        /* Run the inference */
        bool inference_success = arm::app::RunInferenceHandler(caseContext);

        /* Set post-inference GPIO high for 50ms */
        inference_timing_post_start();
        sleep_or_wait_msec(50);  /* Accurate delay using SysTick or PMU */
        inference_timing_post_end();

        if (inference_success) {
            info("--- Inference completed successfully ---\n");
        } else {
            printf_err("--- Inference failed ---\n");
        }

        while (true) {}
    }
}
