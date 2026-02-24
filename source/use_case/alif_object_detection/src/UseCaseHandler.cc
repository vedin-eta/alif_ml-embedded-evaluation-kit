/* This file was ported to work on Alif Semiconductor devices. */

/* Copyright (C) 2023 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */

/*
 * Copyright (c) 2022 Arm Limited. All rights reserved.
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
#include "UseCaseHandler.hpp"
#include "YoloFastestModel.hpp"
#include "UseCaseCommonUtils.hpp"
#include "DetectorPostProcessing.hpp"
#include "DetectorPreProcessing.hpp"
#include "ScreenLayout.hpp"
#include "hal.h"
#include "log_macros.h"

#include <cinttypes>
#include <cmath>

#include "lvgl.h"
#include "lv_port.h"
#include "lv_paint_utils.h"

#define LIMAGE_X        192
#define LIMAGE_Y        192
#define LV_ZOOM         (2 * 256)

namespace {
lv_style_t boxStyle;
lvgl_pixel_t lvgl_image[LIMAGE_Y][LIMAGE_X] __attribute__((section(".bss.lcd_image_buf")));                      // 196x196x2 = 76,832
};

using arm::app::Profiler;
using arm::app::ApplicationContext;
using arm::app::Model;
using arm::app::YoloFastestModel;
using arm::app::DetectorPreProcess;
using arm::app::DetectorPostProcess;

namespace alif {
namespace app {

/* Animal detection class labels */
constexpr int numClasses = 10;
constexpr const char* classLabels[] = {
    "bird", "cat", "dog", "horse", "sheep",
    "cow", "elephant", "bear", "zebra", "giraffe"
};

namespace object_detection {
using namespace arm::app::object_detection;
}

    bool ObjectDetectionInit(YoloFastestModel& model)
    {

        ScreenLayoutInit(lvgl_image, sizeof lvgl_image, LIMAGE_X, LIMAGE_Y, LV_ZOOM);
        uint32_t lv_lock_state = lv_port_lock();

        lv_label_set_text_static(ScreenLayoutHeaderObject(), "Animal Detection");
        lv_label_set_text_static(ScreenLayoutLabelObject(0), "Animals Detected: 0");
        lv_label_set_text_static(ScreenLayoutLabelObject(1), "512x512 -> 192x192 center crop");

        lv_style_init(&boxStyle);
        lv_style_set_bg_opa(&boxStyle, LV_OPA_TRANSP);
        lv_style_set_pad_all(&boxStyle, 0);
        lv_style_set_border_width(&boxStyle, 0);
        lv_style_set_outline_width(&boxStyle, 2);
        lv_style_set_outline_pad(&boxStyle, 0);
        lv_style_set_outline_color(&boxStyle, lv_theme_get_color_primary(ScreenLayoutHeaderObject()));
        lv_style_set_radius(&boxStyle, 4);
        lv_port_unlock(lv_lock_state);

        /* Initialise the camera */
        if (!hal_camera_init()) {
            printf_err("hal_camera_init failed!\n");
            return false;
        }

        TfLiteIntArray* inputShape = model.GetInputShape(0);

        const int inputImgCols = inputShape->data[YoloFastestModel::ms_inputColsIdx];
        const int inputImgRows = inputShape->data[YoloFastestModel::ms_inputRowsIdx];

        info("DEBUG: Model input shape from tensor:\n");
        info("DEBUG:   inputImgCols = %d\n", inputImgCols);
        info("DEBUG:   inputImgRows = %d\n", inputImgRows);
        info("DEBUG:   Expected model input size = %d bytes (%d x %d x 3)\n",
             inputImgCols * inputImgRows * 3, inputImgCols, inputImgRows);

        /* Configure camera for full 512x512 RGB888 - we'll crop from center */
        auto bCamera = hal_camera_configure(512, 512, HAL_CAMERA_MODE_SINGLE_FRAME, HAL_CAMERA_COLOUR_FORMAT_RGB888);
        if (!bCamera) {
            printf_err("Failed to configure camera.\n");
            return false;
        }

        info("DEBUG: Camera configured for 512x512 RGB888, will crop to %d x %d\n", inputImgCols, inputImgRows);

        return true;
    }


    /**
     * @brief           Presents inference results along using the data presentation
     *                  object.
     * @param[in]       results            Vector of detection results to be displayed.
     * @return          true if successful, false otherwise.
     **/
    static bool PresentInferenceResult(const std::vector<object_detection::DetectionResult>& results);

    /**
     * @brief           Draw boxes directly on the LCD for all detected objects.
     * @param[in]       results            Vector of detection results to be displayed.
     **/
    static void DrawDetectionBoxes(
           const std::vector<object_detection::DetectionResult>& results,
           int imgInputCols, int imgInputRows);

    /* Object detection inference handler. */
    bool ObjectDetectionHandler(ApplicationContext& ctx)
    {
        auto& profiler = ctx.Get<Profiler&>("profiler");
        auto& model = ctx.Get<Model&>("model");

        if (!model.IsInited()) {
            printf_err("Model is not initialised! Terminating processing.\n");
            return false;
        }

        TfLiteTensor* inputTensor = model.GetInputTensor(0);
        TfLiteTensor* outputTensor0 = model.GetOutputTensor(0);
        TfLiteTensor* outputTensor1 = model.GetOutputTensor(1);
        TfLiteTensor* outputTensor2 = model.GetOutputTensor(2);

        info("\n=== MODEL TENSOR INFORMATION ===\n");
        info("Input tensor - type: %d, bytes: %zu\n", inputTensor->type, inputTensor->bytes);
        info("Output tensor 0 - type: %d, bytes: %zu\n", outputTensor0->type, outputTensor0->bytes);
        info("Output tensor 1 - type: %d, bytes: %zu\n", outputTensor1->type, outputTensor1->bytes);
        info("Output tensor 2 - type: %d, bytes: %zu\n", outputTensor2->type, outputTensor2->bytes);

        if (!inputTensor->dims) {
            printf_err("Invalid input tensor dims\n");
            return false;
        } else if (inputTensor->dims->size < 3) {
            printf_err("Input tensor dimension should be >= 3\n");
            return false;
        }

        info("Input tensor dims: [");
        for (int i = 0; i < inputTensor->dims->size; i++) {
            info("%d%s", inputTensor->dims->data[i], i < inputTensor->dims->size - 1 ? ", " : "");
        }
        info("]\n");

        info("Output tensor 0 dims: [");
        for (int i = 0; i < outputTensor0->dims->size; i++) {
            info("%d%s", outputTensor0->dims->data[i], i < outputTensor0->dims->size - 1 ? ", " : "");
        }
        info("]\n");

        info("Output tensor 1 dims: [");
        for (int i = 0; i < outputTensor1->dims->size; i++) {
            info("%d%s", outputTensor1->dims->data[i], i < outputTensor1->dims->size - 1 ? ", " : "");
        }
        info("]\n");

        info("Output tensor 2 dims: [");
        for (int i = 0; i < outputTensor2->dims->size; i++) {
            info("%d%s", outputTensor2->dims->data[i], i < outputTensor2->dims->size - 1 ? ", " : "");
        }
        info("]\n");

        TfLiteIntArray* inputShape = model.GetInputShape(0);

        const int inputImgCols = inputShape->data[YoloFastestModel::ms_inputColsIdx];
        const int inputImgRows = inputShape->data[YoloFastestModel::ms_inputRowsIdx];

        info("Parsed input dimensions: %dx%d (expecting RGB, 3 channels)\n", inputImgCols, inputImgRows);

        /* Set up pre and post-processing. */
        info("Model data signed: %s\n", model.IsDataSigned() ? "YES" : "NO");
        DetectorPreProcess preProcess = DetectorPreProcess(inputTensor, true, model.IsDataSigned());

        std::vector<object_detection::DetectionResult> results;
#ifdef MODEL_TYPE_SSD
        info("\n=== SSD POST-PROCESSING PARAMETERS ===\n");
        info("Network input: %dx%d\n", inputImgRows, inputImgCols);
        info("Original image size: %d\n", object_detection::originalImageSize);
        info("Confidence threshold: 0.5\n");
        info("NMS threshold: 0.45\n");
        info("Number of classes: %d\n", numClasses);
        const object_detection::PostProcessParams postProcessParams {
            inputImgRows, inputImgCols, object_detection::originalImageSize,
            object_detection::anchor1, object_detection::anchor2, object_detection::anchor3,
            0.1f, 0.45f, numClasses, 0,
            object_detection::ModelType::SSD
        };
#else
        const object_detection::PostProcessParams postProcessParams {
            inputImgRows, inputImgCols, object_detection::originalImageSize,
            object_detection::anchor1, object_detection::anchor2, object_detection::anchor3,
            0.5f, 0.45f, 1, 0,
            object_detection::ModelType::YOLO
        };
#endif
        DetectorPostProcess postProcess = DetectorPostProcess(outputTensor0, outputTensor1, outputTensor2,
                results, postProcessParams);

        /* Ensure there are no results leftover from previous inference when running all. */
        results.clear();

        info("\n=== CAMERA CAPTURE ===\n");
        info("Starting camera capture...\n");
        hal_camera_start();

        info("Waiting for captured frame...\n");
        uint32_t capturedFrameSize = 0;
        const uint8_t* fullImage = hal_camera_get_captured_frame(&capturedFrameSize);
        if (!fullImage || !capturedFrameSize) {
            printf_err("hal_camera_get_captured_frame failed");
            return false;
        }
        info("Full frame captured successfully, size: %u bytes (512x512 RGB)\n", capturedFrameSize);

        /* Extract center crop from 512x512 image */
        const int full_image_size = 512;
        const int crop_start_x = (full_image_size - inputImgCols) / 2;
        const int crop_start_y = (full_image_size - inputImgRows) / 2;

        info("Extracting center crop: %dx%d from offset (%d, %d)\n",
             inputImgCols, inputImgRows, crop_start_x, crop_start_y);

        /* Allocate buffer for center crop */
        static uint8_t croppedImage[192 * 192 * 3];

        /* Copy center crop row by row */
        for (int y = 0; y < inputImgRows; y++) {
            const uint8_t* src_row = fullImage + ((crop_start_y + y) * full_image_size + crop_start_x) * 3;
            uint8_t* dst_row = croppedImage + y * inputImgCols * 3;
            memcpy(dst_row, src_row, inputImgCols * 3);
        }

        // Debug: Check first few RGB pixel values of cropped image
        if (inputImgCols * inputImgRows * 3 >= 12) {
            info("First 4 RGB pixels of crop: R=%d G=%d B=%d | R=%d G=%d B=%d | R=%d G=%d B=%d | R=%d G=%d B=%d\n",
                 croppedImage[0], croppedImage[1], croppedImage[2],
                 croppedImage[3], croppedImage[4], croppedImage[5],
                 croppedImage[6], croppedImage[7], croppedImage[8],
                 croppedImage[9], croppedImage[10], croppedImage[11]);
        }

        {
            ScopedLVGLLock lv_lock;

            info("\n=== LCD DISPLAY ===\n");
            info("Preparing to display cropped image on LCD...\n");
            /* Display the cropped image on the LCD. */
            write_to_lvgl_buf(inputImgCols, inputImgRows,
                            croppedImage, &lvgl_image[0][0]);
            lv_obj_invalidate(ScreenLayoutImageObject());
            info("Cropped image displayed on LCD\n");
            info("Run requested - proceeding with inference\n");

            lv_led_on(ScreenLayoutLEDObject());

            const size_t copySz = inputTensor->bytes;

#if SHOW_INF_TIME
        uint32_t inf_prof = Get_SysTick_Cycle_Count32();
#endif

            /* Run the pre-processing, inference and post-processing. */
            info("\n=== PRE-PROCESSING ===\n");
            info("Starting pre-processing (RGB input from cropped image)...\n");
            info("Input tensor bytes to copy: %zu\n", copySz);
            if (!preProcess.DoPreProcess(croppedImage, copySz)) {
                printf_err("Pre-processing failed.");
                return false;
            }
            info("Pre-processing completed\n");

            // Debug: Check first few values in input tensor after pre-processing
            if (inputTensor->type == kTfLiteUInt8 && inputTensor->bytes >= 12) {
                uint8_t* tensorData = inputTensor->data.uint8;
                info("First 12 values in input tensor (uint8): %d %d %d %d %d %d %d %d %d %d %d %d\n",
                     tensorData[0], tensorData[1], tensorData[2], tensorData[3],
                     tensorData[4], tensorData[5], tensorData[6], tensorData[7],
                     tensorData[8], tensorData[9], tensorData[10], tensorData[11]);
            } else if (inputTensor->type == kTfLiteInt8 && inputTensor->bytes >= 12) {
                int8_t* tensorData = inputTensor->data.int8;
                info("First 12 values in input tensor (int8): %d %d %d %d %d %d %d %d %d %d %d %d\n",
                     tensorData[0], tensorData[1], tensorData[2], tensorData[3],
                     tensorData[4], tensorData[5], tensorData[6], tensorData[7],
                     tensorData[8], tensorData[9], tensorData[10], tensorData[11]);
            }

            /* Run inference over this image. */
            info("\n=== MODEL INFERENCE ===\n");
            info("Running model inference...\n");
            if (!RunInference(model, profiler)) {
                printf_err("Inference failed.");
                return false;
            }
            info("Model inference successful!\n");

            // Debug: Check output tensor values
            info("\n=== OUTPUT TENSORS ===\n");
            if (outputTensor0->type == kTfLiteUInt8 && outputTensor0->bytes >= 10) {
                uint8_t* out0Data = outputTensor0->data.uint8;
                info("Output tensor 0 first 10 values (uint8): %d %d %d %d %d %d %d %d %d %d\n",
                     out0Data[0], out0Data[1], out0Data[2], out0Data[3], out0Data[4],
                     out0Data[5], out0Data[6], out0Data[7], out0Data[8], out0Data[9]);
            } else if (outputTensor0->type == kTfLiteFloat32 && outputTensor0->bytes >= 40) {
                float* out0Data = outputTensor0->data.f;
                info("Output tensor 0 first 10 values (float32): %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                     out0Data[0], out0Data[1], out0Data[2], out0Data[3], out0Data[4],
                     out0Data[5], out0Data[6], out0Data[7], out0Data[8], out0Data[9]);
            }

            if (outputTensor1->type == kTfLiteUInt8 && outputTensor1->bytes >= 10) {
                uint8_t* out1Data = outputTensor1->data.uint8;
                info("Output tensor 1 first 10 values (uint8): %d %d %d %d %d %d %d %d %d %d\n",
                     out1Data[0], out1Data[1], out1Data[2], out1Data[3], out1Data[4],
                     out1Data[5], out1Data[6], out1Data[7], out1Data[8], out1Data[9]);
            } else if (outputTensor1->type == kTfLiteFloat32 && outputTensor1->bytes >= 40) {
                float* out1Data = outputTensor1->data.f;
                info("Output tensor 1 first 10 values (float32): %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f\n",
                     out1Data[0], out1Data[1], out1Data[2], out1Data[3], out1Data[4],
                     out1Data[5], out1Data[6], out1Data[7], out1Data[8], out1Data[9]);
            }

            info("\n=== POST-PROCESSING (SSD) ===\n");
            info("Starting post-processing...\n");
            if (!postProcess.DoPostProcess()) {
                printf_err("Post-processing failed.");
                return false;
            }
            info("Post-processing completed\n");
            lv_label_set_text_fmt(ScreenLayoutLabelObject(0), "Animals Detected: %i", results.size());
            info("\n=== DETECTION RESULTS ===\n");
            info("Number of animals detected: %zu\n", results.size());
#if SHOW_INF_TIME
            inf_prof = Get_SysTick_Cycle_Count32() - inf_prof;
            lv_label_set_text_fmt(ScreenLayoutLabelObject(2), "Inference time: %.3f ms", (double)inf_prof / SystemCoreClock * 1000);
            lv_label_set_text_fmt(ScreenLayoutLabelObject(3), "Inferences / sec: %.2f", (double) SystemCoreClock / inf_prof);
            //lv_label_set_text_fmt(ScreenLayoutLabelObject(3), "Inferences / second: %.2f", (double) SystemCoreClock / (inf_loop_time_end - inf_loop_time_start));
#endif

#ifdef MODEL_TYPE_SSD
            // Print details for top 2 detections
            size_t numToPrint = results.size() < 2 ? results.size() : 2;
            if (numToPrint > 0) {
                info("\nTop %zu detection(s):\n", numToPrint);
                for (size_t i = 0; i < numToPrint; ++i) {
                    const char* className = "unknown";
                    if (results[i].m_classIndex >= 0 && results[i].m_classIndex < numClasses) {
                        className = classLabels[results[i].m_classIndex];
                    }
                    info("  [%zu] Class: %s (index=%d), Confidence: %.4f, BBox: x=%d y=%d w=%d h=%d\n",
                         i + 1,
                         className,
                         results[i].m_classIndex,
                         results[i].m_normalisedVal,
                         results[i].m_x0, results[i].m_y0,
                         results[i].m_w, results[i].m_h);
                }
            } else {
                info("No detections above confidence threshold\n");
            }
#else
            // Print details for top 2 detections
            size_t numToPrint = results.size() < 2 ? results.size() : 2;
            if (numToPrint > 0) {
                info("\nTop %zu detection(s):\n", numToPrint);
                for (size_t i = 0; i < numToPrint; ++i) {
                    info("  [%zu] Confidence: %.4f, BBox: x=%d y=%d w=%d h=%d\n",
                         i + 1,
                         results[i].m_normalisedVal,
                         results[i].m_x0, results[i].m_y0,
                         results[i].m_w, results[i].m_h);
                }
            } else {
                info("No detections above confidence threshold\n");
            }
#endif

            /* Draw boxes. */
            info("Drawing detection boxes...\n");
            DrawDetectionBoxes(results, inputImgCols, inputImgRows);
            info("Boxes drawn\n");

        } // ScopedLVGLLock

#if VERIFY_TEST_OUTPUT
        DumpTensor(modelOutput0);
        DumpTensor(modelOutput1);
#endif /* VERIFY_TEST_OUTPUT */

        if (!PresentInferenceResult(results)) {
            return false;
        }

        profiler.PrintProfilingResult();

        return true;
    }

    static bool PresentInferenceResult(const std::vector<object_detection::DetectionResult>& results)
    {
        /* If profiling is enabled, and the time is valid. */
        info("Final results:\n");
        info("Total number of inferences: 1\n");

        for (uint32_t i = 0; i < results.size(); ++i) {
#ifdef MODEL_TYPE_SSD
            const char* className = "unknown";
            if (results[i].m_classIndex >= 0 && results[i].m_classIndex < numClasses) {
                className = classLabels[results[i].m_classIndex];
            }
            info("%" PRIu32 ") (%f) -> %s {x=%d,y=%d,w=%d,h=%d}\n", i,
                results[i].m_normalisedVal, className,
                results[i].m_x0, results[i].m_y0, results[i].m_w, results[i].m_h );
#else
            info("%" PRIu32 ") (%f) -> %s {x=%d,y=%d,w=%d,h=%d}\n", i,
                results[i].m_normalisedVal, "Detection box:",
                results[i].m_x0, results[i].m_y0, results[i].m_w, results[i].m_h );
#endif
        }

        return true;
    }

    static void DeleteBoxes(lv_obj_t *frame)
    {
        // Assume that child 0 of the frame is the image itself
        int children = lv_obj_get_child_count(frame);
        while (children > 1) {
            lv_obj_del(lv_obj_get_child(frame, 1));
            children--;
        }
    }

    static void CreateBox(lv_obj_t *frame, int x0, int y0, int w, int h, const char* label = nullptr)
    {
        lv_obj_t *box = lv_obj_create(frame);
        lv_obj_set_size(box, w, h);
        lv_obj_add_style(box, &boxStyle, LV_PART_MAIN);
        lv_obj_set_pos(box, x0, y0);

        /* Add label if provided */
        if (label != nullptr) {
            lv_obj_t *text = lv_label_create(box);
            lv_label_set_text(text, label);
            lv_obj_set_style_text_color(text, lv_color_white(), LV_PART_MAIN);
            lv_obj_set_style_bg_color(text, lv_theme_get_color_primary(frame), LV_PART_MAIN);
            lv_obj_set_style_bg_opa(text, LV_OPA_70, LV_PART_MAIN);
            lv_obj_set_style_pad_all(text, 2, LV_PART_MAIN);
            lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 0);
        }
    }

    static void DrawDetectionBoxes(const std::vector<object_detection::DetectionResult>& results,
                                   int imgInputCols, int imgInputRows)
    {
        lv_obj_t *frame = ScreenLayoutImageHolderObject();
        float xScale = (float) lv_obj_get_content_width(frame) / imgInputCols;
        float yScale = (float) lv_obj_get_content_height(frame) / imgInputRows;

        DeleteBoxes(frame);

        for (const auto& result: results) {
#ifdef MODEL_TYPE_SSD
            const char* className = nullptr;
            if (result.m_classIndex >= 0 && result.m_classIndex < numClasses) {
                className = classLabels[result.m_classIndex];
            }
            CreateBox(frame,
                      floor(result.m_x0 * xScale),
                      floor(result.m_y0 * yScale),
                      ceil(result.m_w * xScale),
                      ceil(result.m_h * yScale),
                      className);
#else
            CreateBox(frame,
                      floor(result.m_x0 * xScale),
                      floor(result.m_y0 * yScale),
                      ceil(result.m_w * xScale),
                      ceil(result.m_h * yScale));
#endif
        }
    }

} /* namespace app */
} /* namespace alif */
