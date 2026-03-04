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
#include <cstdio>

#include "lvgl.h"
#include "lv_port.h"
#include "lv_paint_utils.h"

/* Camera and display configuration */
#define CAMERA_IMAGE_SIZE       240     // Full camera capture size
#define MODEL_INPUT_SIZE        224     // Model inference input size (256x256 centered)

/* Display buffer configuration */
#define LV_ZOOM                 (2 * 256)  // 1:1 scale (no zoom)
#define MODEL_CROP_OFFSET       ((CAMERA_IMAGE_SIZE - MODEL_INPUT_SIZE) / 2)
/* Bounding box coordinate mapping: model space (256x256) to display space (480x480) */

// Model static data
#define PREDICT_TIME_MS 9.46f
#define ENERGY_MJ 1.16f
#define MODEL_RAM_KB 897.34f
#define MODEL_FLASH_KB 738.34f


namespace {
lv_style_t boxStyle;
lv_style_t activeAreaStyle;
lv_obj_t* activeAreaBox = nullptr;

/* Metrics labels */
lv_obj_t* metricLabels[6];      // Text labels on the left
lv_obj_t* metricValues[6];      // Value labels on the right

lvgl_pixel_t lvgl_image[CAMERA_IMAGE_SIZE][CAMERA_IMAGE_SIZE] __attribute__((section(".bss.lcd_image_buf")));                      // 196x196x2 = 76,832
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
    "Bird", "Cat", "Dog", "Horse", "Sheep",
    "Cow", "Elephant", "Bear", "Zebra", "Giraffe"
};

#if VERIFY_TEST_OUTPUT
static void DumpInputs(const Model& model, const char* message)
{
    info("%s\n", message);
    for (size_t inputIndex = 0; inputIndex < model.GetNumInputs(); inputIndex++) {
        arm::app::DumpTensor(model.GetInputTensor(inputIndex));
    }
}

static void DumpOutputs(const Model& model, const char* message)
{
    info("%s\n", message);
    for (size_t outputIndex = 0; outputIndex < model.GetNumOutputs(); outputIndex++) {
        arm::app::DumpTensor(model.GetOutputTensor(outputIndex));
    }
}
#endif /* VERIFY_TEST_OUTPUT */

namespace object_detection {
using namespace arm::app::object_detection;
}

    bool ObjectDetectionInit(YoloFastestModel& model)
    {

        ScreenLayoutInit(lvgl_image, sizeof lvgl_image, CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE, LV_ZOOM);
        uint32_t lv_lock_state = lv_port_lock();

        lv_label_set_text_static(ScreenLayoutHeaderObject(), "No animals detected");

        /* Create metrics table - 6 rows with label and value columns */
        const char* metricNames[] = {
            "Image acquisition and display",
            "Image pre-processing",
            "Prediction time",
            "Results post-processing",
            "Inference loop",
            "FPS"
        };

        int yStart = 10;
        int rowHeight = 30;
        int labelX = 10;
        int valueX = 310;

        for (int i = 0; i < 6; i++) {
            /* Text label on left - black text */
            metricLabels[i] = lv_label_create(ScreenLayoutLabelObject(0));
            lv_label_set_text(metricLabels[i], metricNames[i]);
            lv_obj_set_style_text_color(metricLabels[i], lv_color_black(), LV_PART_MAIN);
            lv_obj_set_width(metricLabels[i], 300);
            lv_obj_set_pos(metricLabels[i], labelX, yStart + i * rowHeight);

            /* Value label on right - red text */
            metricValues[i] = lv_label_create(ScreenLayoutLabelObject(0));
            lv_label_set_text(metricValues[i], "0.00 ms");
            lv_obj_set_style_text_color(metricValues[i], lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
            lv_obj_set_width(metricValues[i], 180);
            lv_obj_set_pos(metricValues[i], valueX, yStart + i * rowHeight);
        }

        /* Set FPS label differently */
        lv_label_set_text(metricValues[5], "0.00");

        lv_style_init(&boxStyle);
        lv_style_set_bg_opa(&boxStyle, LV_OPA_TRANSP);
        lv_style_set_pad_all(&boxStyle, 0);
        lv_style_set_border_width(&boxStyle, 0);
        lv_style_set_outline_width(&boxStyle, 2);
        lv_style_set_outline_pad(&boxStyle, 0);
        lv_style_set_outline_color(&boxStyle, lv_theme_get_color_primary(ScreenLayoutHeaderObject()));
        lv_style_set_radius(&boxStyle, 4);

        /* Create style for active area box (will be drawn each frame) */
        lv_style_init(&activeAreaStyle);
        lv_style_set_bg_opa(&activeAreaStyle, LV_OPA_TRANSP);
        lv_style_set_pad_all(&activeAreaStyle, 0);
        lv_style_set_border_width(&activeAreaStyle, 0);
        lv_style_set_outline_width(&activeAreaStyle, 2);
        lv_style_set_outline_pad(&activeAreaStyle, 0);
        lv_style_set_outline_color(&activeAreaStyle, lv_palette_main(LV_PALETTE_RED));
        lv_style_set_radius(&activeAreaStyle, 0);

        lv_port_unlock(lv_lock_state);

        /* Initialise the camera */
        if (!hal_camera_init()) {
            printf_err("hal_camera_init failed!\n");
            return false;
        }

        TfLiteIntArray* inputShape = model.GetInputShape(0);

        const int inputImgCols = inputShape->data[YoloFastestModel::ms_inputColsIdx];
        const int inputImgRows = inputShape->data[YoloFastestModel::ms_inputRowsIdx];

        debug("Model input shape from tensor:\n");
        debug("  inputImgCols = %d\n", inputImgCols);
        debug("  inputImgRows = %d\n", inputImgRows);
        debug("  Expected model input size = %d bytes (%d x %d x 3)\n",
             inputImgCols * inputImgRows * 3, inputImgCols, inputImgRows);

        auto bCamera = hal_camera_configure(CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE, HAL_CAMERA_MODE_SINGLE_FRAME, HAL_CAMERA_COLOUR_FORMAT_RGB888);
        if (!bCamera) {
            printf_err("Failed to configure camera.\n");
            return false;
        }

        debug("Camera configured for %dx%d RGB888\n", CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE);

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

    /**
     * @brief           Draw the active area box showing model input region.
     **/
    static void DrawActiveAreaBox();

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

        if (!inputTensor->dims) {
            printf_err("Invalid input tensor dims\n");
            return false;
        } else if (inputTensor->dims->size < 3) {
            printf_err("Input tensor dimension should be >= 3\n");
            return false;
        }

        TfLiteIntArray* inputShape = model.GetInputShape(0);

        const int inputImgCols = inputShape->data[YoloFastestModel::ms_inputColsIdx];
        const int inputImgRows = inputShape->data[YoloFastestModel::ms_inputRowsIdx];

        debug("Parsed input dimensions: %dx%d (expecting RGB, 3 channels)\n", inputImgCols, inputImgRows);

        DetectorPreProcess preProcess = DetectorPreProcess(inputTensor, false, model.IsDataSigned());

        std::vector<object_detection::DetectionResult> results;
#ifdef MODEL_TYPE_SSD
        const object_detection::PostProcessParams postProcessParams {
            inputImgRows, inputImgCols, MODEL_INPUT_SIZE,
            object_detection::anchor1, object_detection::anchor2, object_detection::anchor3,
            0.45f, 0.2f, numClasses, 10,
            object_detection::ModelType::SSD
        };
#else
        const object_detection::PostProcessParams postProcessParams {
            inputImgRows, inputImgCols, object_detection::MODEL_INPUT_SIZE,
            object_detection::anchor1, object_detection::anchor2, object_detection::anchor3,
            0.45f, 0.2f, numClasses, 10,
            object_detection::ModelType::YOLO
        };
#endif
        DetectorPostProcess postProcess = DetectorPostProcess(outputTensor0, outputTensor1, outputTensor2,
                results, postProcessParams);

        /* Ensure there are no results leftover from previous inference when running all. */
        results.clear();

        /* Timing variables */
        uint32_t loopStart = Get_SysTick_Cycle_Count32();
        uint32_t acquireStart, acquireEnd;
        uint32_t preprocessStart, preprocessEnd;
        uint32_t inferenceStart, inferenceEnd;
        uint32_t postprocessStart, postprocessEnd;

        acquireStart = Get_SysTick_Cycle_Count32();
        hal_camera_start();

        uint32_t capturedFrameSize = 0;
        const uint8_t* fullImage = hal_camera_get_captured_frame(&capturedFrameSize);
        if (!fullImage || !capturedFrameSize) {
            printf_err("hal_camera_get_captured_frame failed");
            return false;
        }

        {
            ScopedLVGLLock lv_lock;

            /* Display the 480x480 crop on the LCD */
            write_to_lvgl_buf(CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE,
                            fullImage, &lvgl_image[0][0]);
            lv_obj_invalidate(ScreenLayoutImageObject());
            acquireEnd = Get_SysTick_Cycle_Count32();

            lv_led_on(ScreenLayoutLEDObject());

            /* Run the pre-processing, inference and post-processing. */
            preprocessStart = Get_SysTick_Cycle_Count32();
            debug("Starting pre-processing with on-the-fly crop from 512x512 to 256x256...\n");
            if (!preProcess.DoPreProcessWithCrop(fullImage,
                                                 CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE,
                                                 MODEL_CROP_OFFSET, MODEL_CROP_OFFSET,
                                                 MODEL_INPUT_SIZE, MODEL_INPUT_SIZE,
                                                 3)) {
                printf_err("Pre-processing failed.");
                return false;
            }
            preprocessEnd = Get_SysTick_Cycle_Count32();

#if VERIFY_TEST_OUTPUT
            DumpInputs(model, "=INPUT TENSOR=");
#endif

            /* Run inference over this image. */
            inferenceStart = Get_SysTick_Cycle_Count32();
            debug("Running model inference...\n");
            if (!RunInference(model, profiler)) {
                printf_err("Inference failed.");
                return false;
            }
            inferenceEnd = Get_SysTick_Cycle_Count32();
            debug("Model inference successful!\n");

            postprocessStart = Get_SysTick_Cycle_Count32();
            debug("Starting post-processing...\n");
            if (!postProcess.DoPostProcess()) {
                printf_err("Post-processing failed.");
                return false;
            }
            postprocessEnd = Get_SysTick_Cycle_Count32();
            debug("Post-processing completed\n");
            if (results.empty()) {
                lv_label_set_text(ScreenLayoutHeaderObject(), "No animals detected");
            } else {
                const char* className = "unknown";
                if (results[0].m_classIndex >= 0 && results[0].m_classIndex < numClasses) {
                    className = classLabels[results[0].m_classIndex];
                }
                lv_label_set_text_fmt(ScreenLayoutHeaderObject(), "%s detected", className);
            }
            debug("Number of animals detected: %zu\n", results.size());
            uint32_t loopEnd = Get_SysTick_Cycle_Count32();
#if SHOW_INF_TIME
            inf_prof = Get_SysTick_Cycle_Count32() - inf_prof;
            lv_label_set_text_fmt(ScreenLayoutLabelObject(2), "Inference time: %.3f ms", (double)inf_prof / SystemCoreClock * 1000);
            lv_label_set_text_fmt(ScreenLayoutLabelObject(3), "Inferences / sec: %.2f", (double) SystemCoreClock / inf_prof);
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
                debug("\nTop %zu detection(s):\n", numToPrint);
                for (size_t i = 0; i < numToPrint; ++i) {
                    debug("  [%zu] Confidence: %.4f, BBox: x=%d y=%d w=%d h=%d\n",
                         i + 1,
                         results[i].m_normalisedVal,
                         results[i].m_x0, results[i].m_y0,
                         results[i].m_w, results[i].m_h);
                }
            } else {
                debug("No detections above confidence threshold\n");
            }
#endif

            /* Draw active area box first (behind detection boxes) */
            // DrawActiveAreaBox();

            /* Draw boxes. */
            DrawDetectionBoxes(results, CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE);

            /* Calculate metrics */
            float acquireMs = (acquireEnd - acquireStart) / (float)SystemCoreClock * 1000.0f;
            float preprocessMs = (preprocessEnd - preprocessStart) / (float)SystemCoreClock * 1000.0f;
            float inferenceMs = (inferenceEnd - inferenceStart) / (float)SystemCoreClock * 1000.0f;
            float postprocessMs = (postprocessEnd - postprocessStart) / (float)SystemCoreClock * 1000.0f;
            float loopMs = (loopEnd - loopStart) / (float)SystemCoreClock * 1000.0f;
            float fps = 1000.0f / loopMs;

            /* Update metric labels */
            lv_label_set_text_fmt(metricValues[0], "%.2f ms", acquireMs);
            lv_label_set_text_fmt(metricValues[1], "%.2f ms", preprocessMs);
            lv_label_set_text_fmt(metricValues[2], "%.2f ms", inferenceMs);
            lv_label_set_text_fmt(metricValues[3], "%.2f ms", postprocessMs);
            lv_label_set_text_fmt(metricValues[4], "%.2f ms", loopMs);
            lv_label_set_text_fmt(metricValues[5], "%.2f", fps);

            /* Debug log metrics table */
            debug("\n=== Performance Metrics ===\n");
            debug("Image acquisition and display: %.2f ms\n", acquireMs);
            debug("Image pre-processing:          %.2f ms\n", preprocessMs);
            debug("Prediction time:               %.2f ms\n", inferenceMs);
            debug("Results post-processing:       %.2f ms\n", postprocessMs);
            debug("Inference loop:                %.2f ms\n", loopMs);
            debug("FPS:                           %.2f\n", fps);
            debug("===========================\n\n");

        } // ScopedLVGLLock


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
        // Delete all children except child 0 (the image) and the active area box
        int children = lv_obj_get_child_count(frame);
        for (int i = children - 1; i >= 1; i--) {
            lv_obj_t* child = lv_obj_get_child(frame, i);
            // Don't delete the active area box
            if (child != activeAreaBox) {
                lv_obj_del(child);
            }
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
            lv_obj_set_style_text_font(text, &lv_font_montserrat_16, LV_PART_MAIN);
            lv_obj_align(text, LV_ALIGN_TOP_LEFT, 0, 0);
        }
    }

    static void DrawDetectionBoxes(const std::vector<object_detection::DetectionResult>& results,
                                   int imgInputCols,
                                   int imgInputRows)
    {
        debug("\n=== DrawDetectionBoxes ===\n");
        debug("Number of results to draw: %zu\n", results.size());
        lv_obj_t *frame = ScreenLayoutImageHolderObject();

        float xScale = (float) lv_obj_get_content_width(frame) / imgInputCols;
        float yScale = (float) lv_obj_get_content_height(frame) / imgInputRows;


        DeleteBoxes(frame);

        int box_num = 0;
        for (const auto& result: results) {
            box_num++;

            debug("\nBox %d (class=%d, conf=%.3f):\n", box_num, result.m_classIndex, result.m_normalisedVal);
            debug("  Model space (256x256): x0=%d y0=%d w=%d h=%d\n",
                 result.m_x0, result.m_y0, result.m_w, result.m_h);

            /* Apply additional LVGL scaling if needed */
            int frameX = floor(result.m_x0 * xScale + MODEL_CROP_OFFSET * xScale);
            int frameY = floor(result.m_y0 * yScale + MODEL_CROP_OFFSET * yScale);
            int frameW = ceil(result.m_w * xScale);
            int frameH = ceil(result.m_h * yScale);

            debug("Frame coords: x=%d y=%d w=%d h=%d\n", frameX, frameY, frameW, frameH);

            const char* className = "unknown";
            if (result.m_classIndex >= 0 && result.m_classIndex < numClasses) {
                className = classLabels[result.m_classIndex];
                debug("  Class: %s\n", className);
            }

            char labelText[64];
            snprintf(labelText, sizeof(labelText), "%s %.2f", className, result.m_normalisedVal);
            CreateBox(frame, frameX, frameY, frameW, frameH, labelText);
        }
    }

    static void DrawActiveAreaBox()
    {
        lv_obj_t *frame = ScreenLayoutImageHolderObject();

        /* Calculate scale factors from frame size */
        float xScale = (float) lv_obj_get_content_width(frame) / CAMERA_IMAGE_SIZE;
        float yScale = (float) lv_obj_get_content_height(frame) / CAMERA_IMAGE_SIZE;

        /* Delete old active area box if it exists */
        if (activeAreaBox != nullptr) {
            lv_obj_del(activeAreaBox);
        }

        /* Create new active area box */
        activeAreaBox = lv_obj_create(frame);

        /* Calculate scaled position to center MODEL_INPUT_SIZE box on display */
        int activeAreaDisplaySize = MODEL_INPUT_SIZE * xScale;  // Scale the box size
        int centerOffset = MODEL_CROP_OFFSET * xScale;          // Scale the offset

        lv_obj_set_size(activeAreaBox, activeAreaDisplaySize, activeAreaDisplaySize);
        lv_obj_add_style(activeAreaBox, &activeAreaStyle, LV_PART_MAIN);
        lv_obj_set_pos(activeAreaBox, centerOffset, centerOffset);

        /* Add label above the box */
        lv_obj_t *activeAreaLabel = lv_label_create(activeAreaBox);
        lv_label_set_text(activeAreaLabel, "Active Area");
        lv_obj_set_style_text_color(activeAreaLabel, lv_palette_main(LV_PALETTE_RED), LV_PART_MAIN);
        lv_obj_set_style_bg_color(activeAreaLabel, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(activeAreaLabel, LV_OPA_70, LV_PART_MAIN);
        lv_obj_set_style_pad_all(activeAreaLabel, 2, LV_PART_MAIN);
        lv_obj_align(activeAreaLabel, LV_ALIGN_OUT_TOP_LEFT, 0, -5);
    }

} /* namespace app */
} /* namespace alif */
