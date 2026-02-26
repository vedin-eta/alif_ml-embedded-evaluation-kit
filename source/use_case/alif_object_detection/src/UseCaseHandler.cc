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

/* Camera and display configuration */
#define CAMERA_IMAGE_SIZE       512     // Full camera capture size
#define DISPLAY_IMAGE_SIZE      240     // Display crop size (240x240 centered)
#define MODEL_INPUT_SIZE        256     // Model inference input size (256x256 centered)

/* Display buffer configuration */
#define LIMAGE_X                DISPLAY_IMAGE_SIZE
#define LIMAGE_Y                DISPLAY_IMAGE_SIZE
#define LV_ZOOM                 (2 * 256)  // 2:1 scale (no zoom)

/* Crop offsets from 512x512 camera image */
#define DISPLAY_CROP_OFFSET     ((CAMERA_IMAGE_SIZE - DISPLAY_IMAGE_SIZE) / 2)
#define MODEL_CROP_OFFSET       ((CAMERA_IMAGE_SIZE - MODEL_INPUT_SIZE) / 2)

/* Bounding box coordinate mapping: model space (256x256) to display space (480x480) */
#define BBOX_DISPLAY_SCALE      ((float)DISPLAY_IMAGE_SIZE / (float)MODEL_INPUT_SIZE)
#define BBOX_DISPLAY_OFFSET     ((DISPLAY_IMAGE_SIZE - MODEL_INPUT_SIZE * BBOX_DISPLAY_SCALE) / 2)

// Model static data
#define PREDICT_TIME_MS 9.46f
#define ENERGY_MJ 1.16f
#define MODEL_RAM_KB 897.34f
#define MODEL_FLASH_KB 738.34f


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
    "Bird", "Cat", "Dog", "Horse", "Sheep",
    "Cow", "Elephant", "Bear", "Zebra", "Giraffe"
};

namespace object_detection {
using namespace arm::app::object_detection;
}

    bool ObjectDetectionInit(YoloFastestModel& model)
    {

        ScreenLayoutInit(lvgl_image, sizeof lvgl_image, LIMAGE_X, LIMAGE_Y, LV_ZOOM);
        uint32_t lv_lock_state = lv_port_lock();

        lv_label_set_text_static(ScreenLayoutHeaderObject(), "No animals detected");
        lv_label_set_text_fmt(ScreenLayoutLabelObject(0), "Predict time: %.2f ms, energy per inference: %.2f mJ", PREDICT_TIME_MS, ENERGY_MJ);
        lv_label_set_text_fmt(ScreenLayoutLabelObject(1), "RAM usage: %.2f KB, NVME usage: %.2f KB", MODEL_RAM_KB, MODEL_FLASH_KB);

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

        auto bCamera = hal_camera_configure(CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE, HAL_CAMERA_MODE_SINGLE_FRAME, HAL_CAMERA_COLOUR_FORMAT_RGB888);
        if (!bCamera) {
            printf_err("Failed to configure camera.\n");
            return false;
        }

        info("DEBUG: Camera configured for %dx%d RGB888\n", CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE);
        info("DEBUG: Display crop: %dx%d (offset %d)\n", DISPLAY_IMAGE_SIZE, DISPLAY_IMAGE_SIZE, DISPLAY_CROP_OFFSET);
        info("DEBUG: Model input crop: %dx%d (offset %d)\n", MODEL_INPUT_SIZE, MODEL_INPUT_SIZE, MODEL_CROP_OFFSET);

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
        const object_detection::PostProcessParams postProcessParams {
            inputImgRows, inputImgCols, object_detection::originalImageSize,
            object_detection::anchor1, object_detection::anchor2, object_detection::anchor3,
            0.1f, 0.9f, numClasses, 0,
            object_detection::ModelType::SSD
        };
#else
        const object_detection::PostProcessParams postProcessParams {
            inputImgRows, inputImgCols, object_detection::originalImageSize,
            object_detection::anchor1, object_detection::anchor2, object_detection::anchor3,
            0.45f, 0.9f, numClasses, 10,
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
        info("Full frame captured successfully, size: %u bytes (%dx%d RGB)\n",
             capturedFrameSize, CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE);

        /* Allocate buffer for display crop only - place in external memory to avoid DTCM overflow */
        static uint8_t displayCrop[DISPLAY_IMAGE_SIZE * DISPLAY_IMAGE_SIZE * 3] __attribute__((section(".bss.lcd_image_buf")));

        /* Extract 480x480 display crop (offset 16,16 from 512x512) */
        info("\n=== EXTRACTING DISPLAY CROP ===\n");
        info("Display crop: %dx%d from offset (%d, %d)\n",
             DISPLAY_IMAGE_SIZE, DISPLAY_IMAGE_SIZE, DISPLAY_CROP_OFFSET, DISPLAY_CROP_OFFSET);
        for (int y = 0; y < DISPLAY_IMAGE_SIZE; y++) {
            const uint8_t* src_row = fullImage + ((DISPLAY_CROP_OFFSET + y) * CAMERA_IMAGE_SIZE + DISPLAY_CROP_OFFSET) * 3;
            uint8_t* dst_row = displayCrop + y * DISPLAY_IMAGE_SIZE * 3;
            memcpy(dst_row, src_row, DISPLAY_IMAGE_SIZE * 3);
        }

        /* Model crop (256x256 from offset 128,128) will be extracted on-the-fly during preprocessing */
        info("\n=== MODEL INPUT ===\n");
        info("Model will process 256x256 center crop from 512x512 image (offset %d,%d)\n",
             MODEL_CROP_OFFSET, MODEL_CROP_OFFSET);

        {
            ScopedLVGLLock lv_lock;

            info("\n=== LCD DISPLAY ===\n");
            info("Displaying %dx%d image on LCD...\n", DISPLAY_IMAGE_SIZE, DISPLAY_IMAGE_SIZE);
            /* Display the 480x480 crop on the LCD */
            write_to_lvgl_buf(DISPLAY_IMAGE_SIZE, DISPLAY_IMAGE_SIZE,
                            displayCrop, &lvgl_image[0][0]);
            lv_obj_invalidate(ScreenLayoutImageObject());
            info("Display updated\n");

            lv_led_on(ScreenLayoutLEDObject());

#if SHOW_INF_TIME
        uint32_t inf_prof = Get_SysTick_Cycle_Count32();
#endif

            /* Run the pre-processing, inference and post-processing. */
            info("\n=== PRE-PROCESSING ===\n");
            info("Starting pre-processing with on-the-fly crop from 512x512 to 256x256...\n");
            if (!preProcess.DoPreProcessWithCrop(fullImage,
                                                 CAMERA_IMAGE_SIZE, CAMERA_IMAGE_SIZE,
                                                 MODEL_CROP_OFFSET, MODEL_CROP_OFFSET,
                                                 MODEL_INPUT_SIZE, MODEL_INPUT_SIZE,
                                                 3)) {
                printf_err("Pre-processing failed.");
                return false;
            }

            /* Run inference over this image. */
            info("\n=== MODEL INFERENCE ===\n");
            info("Running model inference...\n");
            if (!RunInference(model, profiler)) {
                printf_err("Inference failed.");
                return false;
            }
            info("Model inference successful!\n");

            info("\n=== POST-PROCESSING (SSD) ===\n");
            info("Starting post-processing...\n");
            if (!postProcess.DoPostProcess()) {
                printf_err("Post-processing failed.");
                return false;
            }
            info("Post-processing completed\n");
            if (results.empty()) {
                lv_label_set_text(ScreenLayoutHeaderObject(), "No animals detected");
            } else {
                const char* className = "unknown";
                if (results[0].m_classIndex >= 0 && results[0].m_classIndex < numClasses) {
                    className = classLabels[results[0].m_classIndex];
                }
                lv_label_set_text_fmt(ScreenLayoutHeaderObject(), "%s detected", className);
            }
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
                                   int imgInputCols __attribute__((unused)),
                                   int imgInputRows __attribute__((unused)))
    {
        info("\n=== DrawDetectionBoxes ===\n");
        info("Number of results to draw: %zu\n", results.size());

        lv_obj_t *frame = ScreenLayoutImageHolderObject();

        /* Bounding boxes come in model space (256x256), need to map to display space (480x480)
         * The model inference was done on a 256x256 center crop
         * The display shows a 480x480 center crop
         * Both crops are centered on the same 512x512 camera image
         * Therefore: bbox needs to be scaled by 480/256 = 1.875 and offset by (480-256*1.875)/2 = 0
         * Actually, since both are centered, we just need to scale, no offset needed!
         */
        const float bboxToDisplayScale = BBOX_DISPLAY_SCALE;  // 480/256 = 1.875

        info("Bbox scaling: model space (256x256) -> display space (480x480)\n");
        info("  bboxToDisplayScale = %.3f\n", bboxToDisplayScale);

        /* Additional scaling from LVGL if frame is zoomed */
        float frameWidth = (float) lv_obj_get_content_width(frame);
        float frameHeight = (float) lv_obj_get_content_height(frame);
        float lvglXScale = frameWidth / (DISPLAY_IMAGE_SIZE * 2);
        float lvglYScale = frameHeight / (DISPLAY_IMAGE_SIZE * 2);

        info("LVGL frame dimensions: %.1f x %.1f\n", frameWidth, frameHeight);
        info("  lvglXScale = %.3f, lvglYScale = %.3f\n", lvglXScale, lvglYScale);

        DeleteBoxes(frame);

        int box_num = 0;
        for (const auto& result: results) {
            box_num++;

            info("\nBox %d (class=%d, conf=%.3f):\n", box_num, result.m_classIndex, result.m_normalisedVal);
            info("  Model space (256x256): x0=%d y0=%d w=%d h=%d\n",
                 result.m_x0, result.m_y0, result.m_w, result.m_h);

            /* Scale bbox from model space (256x256) to display space (480x480) */
            float displayX = result.m_x0 * bboxToDisplayScale;
            float displayY = result.m_y0 * bboxToDisplayScale;
            float displayW = result.m_w * bboxToDisplayScale;
            float displayH = result.m_h * bboxToDisplayScale;

            info("  Display space (480x480): x=%.1f y=%.1f w=%.1f h=%.1f\n",
                 displayX, displayY, displayW, displayH);

            /* Apply additional LVGL scaling if needed */
            int frameX = floor(displayX * lvglXScale) + 120;
            int frameY = floor(displayY * lvglYScale) + 120;
            int frameW = ceil(displayW * lvglXScale);
            int frameH = ceil(displayH * lvglYScale);

            info("  Frame coords: x=%d y=%d w=%d h=%d\n", frameX, frameY, frameW, frameH);

            const char* className = nullptr;
            if (result.m_classIndex >= 0 && result.m_classIndex < numClasses) {
                className = classLabels[result.m_classIndex];
                info("  Class: %s\n", className);
            }

            CreateBox(frame, frameX, frameY, frameH, frameW, className);
        }

        info("=== DrawDetectionBoxes complete ===\n\n");
    }

} /* namespace app */
} /* namespace alif */
