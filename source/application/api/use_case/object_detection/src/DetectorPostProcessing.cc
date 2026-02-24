/*
 * SPDX-FileCopyrightText: Copyright 2022 Arm Limited and/or its affiliates <open-source-office@arm.com>
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
#include "DetectorPostProcessing.hpp"
#include "PlatformMath.hpp"
#include "log_macros.h"

#include <cmath>
#include <algorithm>
#include <vector>

namespace arm {
namespace app {

    DetectorPostProcess::DetectorPostProcess(
        TfLiteTensor* modelOutput0,
        TfLiteTensor* modelOutput1,
        TfLiteTensor* modelOutput2,
        std::vector<object_detection::DetectionResult>& results,
        const object_detection::PostProcessParams& postProcessParams)
        :   m_outputTensor0{modelOutput0},
            m_outputTensor1{modelOutput1},
            m_outputTensor2{modelOutput2},
            m_results{results},
            m_postProcessParams{postProcessParams}
{
    /* Init PostProcessing */
    this->m_net = object_detection::Network{
        .inputWidth  = postProcessParams.inputImgCols,
        .inputHeight = postProcessParams.inputImgRows,
        .numClasses  = postProcessParams.numClasses,
        .branches =
            {object_detection::Branch{.resolution  = postProcessParams.inputImgCols / 16,
                                      .numBox      = 3,
                                      .anchor      = postProcessParams.anchor2,
                                      .modelOutput = this->m_outputTensor0->data.int8,
                                      .scale       = (static_cast<TfLiteAffineQuantization*>(
                                                    this->m_outputTensor0->quantization.params))
                                                   ->scale->data[0],
                                      .zeroPoint = (static_cast<TfLiteAffineQuantization*>(
                                                        this->m_outputTensor0->quantization.params))
                                                       ->zero_point->data[0],
                                      .size = this->m_outputTensor0->bytes},
             object_detection::Branch{.resolution  = postProcessParams.inputImgCols / 8,
                                      .numBox      = 3,
                                      .anchor      = postProcessParams.anchor1,
                                      .modelOutput = this->m_outputTensor1->data.int8,
                                      .scale       = (static_cast<TfLiteAffineQuantization*>(
                                                    this->m_outputTensor1->quantization.params))
                                                   ->scale->data[0],
                                      .zeroPoint = (static_cast<TfLiteAffineQuantization*>(
                                                        this->m_outputTensor1->quantization.params))
                                                       ->zero_point->data[0],
                                      .size = this->m_outputTensor1->bytes},
             object_detection::Branch{.resolution  = postProcessParams.inputImgCols / 32,
                                      .numBox      = 3,
                                      .anchor      = postProcessParams.anchor3,
                                      .modelOutput = this->m_outputTensor2->data.int8,
                                      .scale       = (static_cast<TfLiteAffineQuantization*>(
                                                    this->m_outputTensor2->quantization.params))
                                                   ->scale->data[0],
                                      .zeroPoint = (static_cast<TfLiteAffineQuantization*>(
                                                        this->m_outputTensor2->quantization.params))
                                                       ->zero_point->data[0],
                                      .size = this->m_outputTensor2->bytes}},
        .topN = postProcessParams.topN};
    /* End init */
}

bool DetectorPostProcess::DoPostProcess()
{
    info("\n=== DoPostProcess ENTRY ===\n");
    info("Model type: %s\n", m_postProcessParams.modelType == object_detection::ModelType::SSD ? "SSD" : "YOLO");

    /* Check model type and use appropriate post-processing */
    if (m_postProcessParams.modelType == object_detection::ModelType::SSD) {
        info("Entering SSD post-processing path\n");
        return ProcessSSD();
    }

    /* YOLO post-processing */
    info("Entering YOLO post-processing path\n");
    info("YOLO: originalImageSize=%d x %d\n", m_postProcessParams.originalImageSize, m_postProcessParams.originalImageSize);
    info("YOLO: threshold=%.4f, nms=%.4f, numClasses=%d\n",
         m_postProcessParams.threshold, m_postProcessParams.nms, this->m_net.numClasses);
    info("YOLO: Number of branches=%zu\n", this->m_net.branches.size());

    for (size_t i = 0; i < this->m_net.branches.size(); ++i) {
        info("YOLO:   Branch %zu: resolution=%d, numBox=%d\n",
             i, this->m_net.branches[i].resolution, this->m_net.branches[i].numBox);
    }

    int originalImageWidth  = m_postProcessParams.originalImageSize;
    int originalImageHeight = m_postProcessParams.originalImageSize;

    info("YOLO: Creating detections list (std::forward_list)\n");
    std::forward_list<image::Detection> detections;

    info("YOLO: Calling GetNetworkBoxes...\n");
    GetNetworkBoxes(this->m_net, originalImageWidth, originalImageHeight, m_postProcessParams.threshold, detections);

    /* Count detections */
    int detection_count = 0;
    for (auto& it: detections) { detection_count++; }
    info("YOLO: GetNetworkBoxes found %d detections above threshold\n", detection_count);

    /* Do nms */
    info("YOLO: Applying NMS (threshold=%.4f)...\n", this->m_postProcessParams.nms);
    CalculateNMS(detections, this->m_net.numClasses, this->m_postProcessParams.nms);

    /* Count after NMS */
    detection_count = 0;
    for (auto& it: detections) { detection_count++; }
    info("YOLO: After NMS: %d detections remain\n", detection_count);

    info("YOLO: Converting detections to results vector...\n");
    int result_count = 0;

    for (auto& it: detections) {
        float xMin = it.bbox.x - it.bbox.w / 2.0f;
        float xMax = it.bbox.x + it.bbox.w / 2.0f;
        float yMin = it.bbox.y - it.bbox.h / 2.0f;
        float yMax = it.bbox.y + it.bbox.h / 2.0f;

        if (xMin < 0) {
            xMin = 0;
        }
        if (yMin < 0) {
            yMin = 0;
        }
        if (xMax > originalImageWidth) {
            xMax = originalImageWidth;
        }
        if (yMax > originalImageHeight) {
            yMax = originalImageHeight;
        }

        float boxX = xMin;
        float boxY = yMin;
        float boxWidth = xMax - xMin;
        float boxHeight = yMax - yMin;

        for (int j = 0; j < this->m_net.numClasses; ++j) {
            if (it.prob[j] > 0) {

                object_detection::DetectionResult tmpResult = {};
                tmpResult.m_normalisedVal = it.prob[j];
                tmpResult.m_x0 = boxX;
                tmpResult.m_y0 = boxY;
                tmpResult.m_w = boxWidth;
                tmpResult.m_h = boxHeight;
                tmpResult.m_classIndex = j;

                this->m_results.push_back(tmpResult);
                result_count++;
            }
        }
    }

    info("YOLO: Post-processing complete. Total results: %d\n", result_count);
    info("=== DoPostProcess EXIT ===\n\n");
    return true;
}

void DetectorPostProcess::InsertTopNDetections(std::forward_list<image::Detection>& detections, image::Detection& det)
{
    std::forward_list<image::Detection>::iterator it;
    std::forward_list<image::Detection>::iterator last_it;
    for ( it = detections.begin(); it != detections.end(); ++it ) {
        if(it->objectness > det.objectness)
            break;
        last_it = it;
    }
    if(it != detections.begin()) {
        detections.emplace_after(last_it, det);
        detections.pop_front();
    }
}

void DetectorPostProcess::GetNetworkBoxes(
        object_detection::Network& net,
        int imageWidth,
        int imageHeight,
        float threshold,
        std::forward_list<image::Detection>& detections)
{
    info("GetNetworkBoxes: START\n");
    info("  imageWidth=%d, imageHeight=%d, threshold=%.4f\n", imageWidth, imageHeight, threshold);

    int numClasses = net.numClasses;
    int num = 0;
    auto det_objectness_comparator = [](image::Detection& pa, image::Detection& pb) {
        return pa.objectness < pb.objectness;
    };
    for (size_t i = 0; i < net.branches.size(); ++i) {
        info("  Processing branch %zu: resolution=%d, numBox=%d\n",
             i, net.branches[i].resolution, net.branches[i].numBox);

        int height   = net.branches[i].resolution;
        int width    = net.branches[i].resolution;
        int channel  = net.branches[i].numBox*(5+numClasses);

        int boxes_checked = 0;
        int boxes_above_threshold = 0;

        for (int h = 0; h < net.branches[i].resolution; h++) {
            for (int w = 0; w < net.branches[i].resolution; w++) {
                for (int anc = 0; anc < net.branches[i].numBox; anc++) {
                    boxes_checked++;

                    /* Objectness score */
                    int bbox_obj_offset = h * width * channel + w * channel + anc * (numClasses + 5) + 4;
                    float objectness = math::MathUtils::SigmoidF32(
                            (static_cast<float>(net.branches[i].modelOutput[bbox_obj_offset])
                            - net.branches[i].zeroPoint
                            ) * net.branches[i].scale);

                    if(objectness > threshold) {
                        boxes_above_threshold++;

                        if (boxes_above_threshold <= 5) {
                            info("    Box %d passed threshold: objectness=%.4f\n", boxes_above_threshold, objectness);
                        }

                        image::Detection det;
                        det.objectness = objectness;
                        /* Get bbox prediction data for each anchor, each feature point */
                        int bbox_x_offset = bbox_obj_offset -4;
                        int bbox_y_offset = bbox_x_offset + 1;
                        int bbox_w_offset = bbox_x_offset + 2;
                        int bbox_h_offset = bbox_x_offset + 3;
                        int bbox_scores_offset = bbox_x_offset + 5;

                        det.bbox.x = (static_cast<float>(net.branches[i].modelOutput[bbox_x_offset])
                                - net.branches[i].zeroPoint) * net.branches[i].scale;
                        det.bbox.y = (static_cast<float>(net.branches[i].modelOutput[bbox_y_offset])
                                - net.branches[i].zeroPoint) * net.branches[i].scale;
                        det.bbox.w = (static_cast<float>(net.branches[i].modelOutput[bbox_w_offset])
                                - net.branches[i].zeroPoint) * net.branches[i].scale;
                        det.bbox.h = (static_cast<float>(net.branches[i].modelOutput[bbox_h_offset])
                                - net.branches[i].zeroPoint) * net.branches[i].scale;

                        float bbox_x, bbox_y;

                        /* Eliminate grid sensitivity trick involved in YOLOv4 */
                        bbox_x = math::MathUtils::SigmoidF32(det.bbox.x);
                        bbox_y = math::MathUtils::SigmoidF32(det.bbox.y);
                        det.bbox.x = (bbox_x + w) / width;
                        det.bbox.y = (bbox_y + h) / height;

                        det.bbox.w = std::exp(det.bbox.w) * net.branches[i].anchor[anc*2] / net.inputWidth;
                        det.bbox.h = std::exp(det.bbox.h) * net.branches[i].anchor[anc*2+1] / net.inputHeight;

                        if (boxes_above_threshold <= 5) {
                            info("    Allocating prob vector for %d classes\n", numClasses);
                        }

                        for (int s = 0; s < numClasses; s++) {
                            float sig = math::MathUtils::SigmoidF32(
                                    (static_cast<float>(net.branches[i].modelOutput[bbox_scores_offset + s]) -
                                    net.branches[i].zeroPoint) * net.branches[i].scale
                                    ) * objectness;
                            det.prob.emplace_back((sig > threshold) ? sig : 0);  // HEAP ALLOCATION HERE
                        }

                        if (boxes_above_threshold <= 5) {
                            info("    Prob vector allocated successfully\n");
                        }

                        /* Correct_YOLO_boxes */
                        det.bbox.x *= imageWidth;
                        det.bbox.w *= imageWidth;
                        det.bbox.y *= imageHeight;
                        det.bbox.h *= imageHeight;

                        if (num < net.topN || net.topN <=0) {
                            detections.emplace_front(det);
                            num += 1;
                        } else if (num == net.topN) {
                            detections.sort(det_objectness_comparator);
                            InsertTopNDetections(detections, det);
                            num += 1;
                        } else {
                            InsertTopNDetections(detections, det);
                        }
                    }
                }
            }
        }

        info("  Branch %zu complete: checked=%d boxes, above_threshold=%d\n",
             i, boxes_checked, boxes_above_threshold);
    }

    info("GetNetworkBoxes: COMPLETE (total detections added=%d)\n", num);

    if(num > net.topN)
        num -=1;
}

bool DetectorPostProcess::ProcessSSD()
{
    /* SSD post-processing
     * Output 0: Bounding boxes [batch, num_boxes, 4] - normalized [xmin, ymin, xmax, ymax]
     * Output 1: Class scores [batch, num_boxes, num_classes+1] - includes background class at index 0
     */

    int originalImageWidth  = m_postProcessParams.originalImageSize;
    int originalImageHeight = m_postProcessParams.originalImageSize;

    /* Get quantization parameters */
    const auto* box_quantization = static_cast<TfLiteAffineQuantization*>(
        this->m_outputTensor0->quantization.params);
    const float box_scale = box_quantization->scale->data[0];
    const int box_zero_point = box_quantization->zero_point->data[0];

    const auto* score_quantization = static_cast<TfLiteAffineQuantization*>(
        this->m_outputTensor1->quantization.params);
    const float score_scale = score_quantization->scale->data[0];
    const int score_zero_point = score_quantization->zero_point->data[0];

    /* Get tensor dimensions */
    int num_boxes = this->m_outputTensor0->dims->data[1];
    int num_classes_with_bg = this->m_outputTensor1->dims->data[2];
    int num_classes = num_classes_with_bg - 1; /* Remove background class */

    info("SSD ProcessSSD: num_boxes=%d, num_classes=%d (with bg=%d)\n", num_boxes, num_classes, num_classes_with_bg);
    info("SSD Box quantization: scale=%.6f, zero_point=%d\n", box_scale, box_zero_point);
    info("SSD Score quantization: scale=%.6f, zero_point=%d\n", score_scale, score_zero_point);
    info("SSD Confidence threshold: %.4f\n", m_postProcessParams.threshold);

    int8_t* boxes = this->m_outputTensor0->data.int8;
    int8_t* scores = this->m_outputTensor1->data.int8;

    /* Track highest confidence per class */
    std::vector<float> max_scores_per_class(num_classes, 0.0f);
    std::vector<int> max_scores_box_idx(num_classes, -1);
    int detections_above_threshold = 0;

    /* Process each box */
    for (int box_idx = 0; box_idx < num_boxes; ++box_idx) {
        /* Get box coordinates (normalized [0, 1]) */
        int box_offset = box_idx * 4;
        float xmin = (static_cast<float>(boxes[box_offset + 0]) - box_zero_point) * box_scale;
        float ymin = (static_cast<float>(boxes[box_offset + 1]) - box_zero_point) * box_scale;
        float xmax = (static_cast<float>(boxes[box_offset + 2]) - box_zero_point) * box_scale;
        float ymax = (static_cast<float>(boxes[box_offset + 3]) - box_zero_point) * box_scale;

        /* Clamp to [0, 1] */
        xmin = std::max(0.0f, std::min(1.0f, xmin));
        ymin = std::max(0.0f, std::min(1.0f, ymin));
        xmax = std::max(0.0f, std::min(1.0f, xmax));
        ymax = std::max(0.0f, std::min(1.0f, ymax));

        /* Convert to pixel coordinates */
        float boxX = xmin * originalImageWidth;
        float boxY = ymin * originalImageHeight;
        float boxWidth = (xmax - xmin) * originalImageWidth;
        float boxHeight = (ymax - ymin) * originalImageHeight;

        /* Check each class (skip background at index 0) */
        for (int class_idx = 1; class_idx < num_classes_with_bg; ++class_idx) {
            int score_offset = box_idx * num_classes_with_bg + class_idx;
            float score = (static_cast<float>(scores[score_offset]) - score_zero_point) * score_scale;

            /* Track max score per class */
            int adjusted_class_idx = class_idx - 1;
            if (score > max_scores_per_class[adjusted_class_idx]) {
                max_scores_per_class[adjusted_class_idx] = score;
                max_scores_box_idx[adjusted_class_idx] = box_idx;
            }

            /* Apply threshold */
            if (score > m_postProcessParams.threshold) {
                object_detection::DetectionResult tmpResult = {};
                tmpResult.m_normalisedVal = score;
                tmpResult.m_x0 = boxX;
                tmpResult.m_y0 = boxY;
                tmpResult.m_w = boxWidth;
                tmpResult.m_h = boxHeight;
                tmpResult.m_classIndex = adjusted_class_idx; /* Adjust for removed background class */

                this->m_results.push_back(tmpResult);
                detections_above_threshold++;
            }
        }
    }

    info("SSD: Processed %d boxes, found %d detections above threshold\n", num_boxes, detections_above_threshold);

    /* Print highest confidence per class */
    info("SSD: Highest confidences per class:\n");
    for (int c = 0; c < num_classes && c < 10; ++c) {
        info("  Class %d: %.4f (box_idx=%d)\n", c, max_scores_per_class[c], max_scores_box_idx[c]);
    }

    /* Apply NMS per class */
    if (this->m_results.size() > 0) {
        info("SSD: Applying NMS with threshold %.4f to %zu detections\n", m_postProcessParams.nms, this->m_results.size());
        /* Simple NMS implementation per class */
        std::vector<object_detection::DetectionResult> filtered_results;

        for (int c = 0; c < num_classes; ++c) {
            /* Get all detections for this class */
            std::vector<object_detection::DetectionResult> class_detections;
            for (const auto& det : this->m_results) {
                if (det.m_classIndex == c) {
                    class_detections.push_back(det);
                }
            }

            /* Sort by score (descending) */
            std::sort(class_detections.begin(), class_detections.end(),
                     [](const object_detection::DetectionResult& a, const object_detection::DetectionResult& b) {
                         return a.m_normalisedVal > b.m_normalisedVal;
                     });

            /* Apply NMS */
            std::vector<bool> suppressed(class_detections.size(), false);
            for (size_t i = 0; i < class_detections.size(); ++i) {
                if (suppressed[i]) continue;

                filtered_results.push_back(class_detections[i]);

                /* Suppress overlapping boxes */
                for (size_t j = i + 1; j < class_detections.size(); ++j) {
                    if (suppressed[j]) continue;

                    /* Calculate IoU */
                    float x1 = std::max(class_detections[i].m_x0, class_detections[j].m_x0);
                    float y1 = std::max(class_detections[i].m_y0, class_detections[j].m_y0);
                    float x2 = std::min(class_detections[i].m_x0 + class_detections[i].m_w,
                                       class_detections[j].m_x0 + class_detections[j].m_w);
                    float y2 = std::min(class_detections[i].m_y0 + class_detections[i].m_h,
                                       class_detections[j].m_y0 + class_detections[j].m_h);

                    float intersection = std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1);
                    float area_i = class_detections[i].m_w * class_detections[i].m_h;
                    float area_j = class_detections[j].m_w * class_detections[j].m_h;
                    float union_area = area_i + area_j - intersection;

                    float iou = (union_area > 0) ? (intersection / union_area) : 0;

                    if (iou > m_postProcessParams.nms) {
                        suppressed[j] = true;
                    }
                }
            }
        }

        this->m_results = filtered_results;
        info("SSD: After NMS, %zu detections remain\n", this->m_results.size());

        /* Print top 2 detections with their labels */
        size_t num_to_print = this->m_results.size() < 2 ? this->m_results.size() : 2;
        if (num_to_print > 0) {
            info("SSD: Top %zu detection(s) after NMS:\n", num_to_print);
            for (size_t i = 0; i < num_to_print; ++i) {
                info("  [%zu] Class_idx=%d, Confidence=%.4f, BBox=[%.1f, %.1f, %.1f, %.1f]\n",
                     i + 1,
                     this->m_results[i].m_classIndex,
                     this->m_results[i].m_normalisedVal,
                     this->m_results[i].m_x0, this->m_results[i].m_y0,
                     this->m_results[i].m_w, this->m_results[i].m_h);
            }
        }
    } else {
        info("SSD: No detections found above threshold %.4f\n", m_postProcessParams.threshold);
    }

    return true;
}

} /* namespace app */
} /* namespace arm */
