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

/* ★ Phase 15b Step 3 — ObjectDetectionInit progress markers */
#define ODI_MARKER(val) do { \
    *(volatile uint32_t*)0x027DC528 = (val); \
    __asm volatile ("dsb sy" ::: "memory"); \
} while(0)
/* ★ Phase 15b Step 3 — ObjectDetectionHandler markers */
#define OBH_MARKER(val) do { \
    *(volatile uint32_t*)0x027DC548 = (val); \
    __asm volatile ("dsb sy" ::: "memory"); \
} while(0)

#define LIMAGE_X        192
#define LIMAGE_Y        192
#define LV_ZOOM         (2 * 256)

namespace {
lv_style_t boxStyle;
lvgl_pixel_t lvgl_image[LIMAGE_Y][LIMAGE_X] __attribute__((section(".bss.lcd_image_buf")));                      // 192x192x2 = 73,728
};

using arm::app::Profiler;
using arm::app::ApplicationContext;
using arm::app::fwk::iface::Model;
using arm::app::fwk::tflm::YoloFastestModel;
using arm::app::DetectorPreProcess;
using arm::app::DetectorPostProcess;

namespace alif {
namespace app {

namespace object_detection {
using namespace arm::app::object_detection;
}

    bool ObjectDetectionInit(YoloFastestModel& model)
    {

        ODI_MARKER(0xD1000001); 

#if 0   // ★ Phase 15b Step 3 — skip LVGL UI (no LCD, will re-enable later)        
        ScreenLayoutInit(lvgl_image, sizeof lvgl_image, LIMAGE_X, LIMAGE_Y, LV_ZOOM);
        ODI_MARKER(0xD1000002);

        uint32_t lv_lock_state = lv_port_lock();
        ODI_MARKER(0xD1000003); 

        lv_label_set_text_static(ScreenLayoutHeaderObject(), "Face Detection");
        ODI_MARKER(0xD1000004); 
        lv_label_set_text_static(ScreenLayoutLabelObject(0), "Faces Detected: 0");
        ODI_MARKER(0xD1000005); 
        lv_label_set_text_static(ScreenLayoutLabelObject(1), "192px image (24-bit)");
        ODI_MARKER(0xD1000006); 

        lv_style_init(&boxStyle);
        ODI_MARKER(0xD1000007);

        lv_style_set_bg_opa(&boxStyle, LV_OPA_TRANSP);
        lv_style_set_pad_all(&boxStyle, 0);
        lv_style_set_border_width(&boxStyle, 0);
        ODI_MARKER(0xD1000008);

        lv_style_set_outline_width(&boxStyle, 2);
        lv_style_set_outline_pad(&boxStyle, 0);
        lv_style_set_outline_color(&boxStyle, lv_theme_get_color_primary(ScreenLayoutHeaderObject()));
        lv_style_set_radius(&boxStyle, 4);
        lv_port_unlock(lv_lock_state);
#endif
        ODI_MARKER(0xD1000009);   
        /* Initialise the camera */
        if (!hal_camera_init()) {
            printf_err("hal_camera_init failed!\n");
            return false;
        }
        ODI_MARKER(0xD100000A);        
        
        auto inputTensor = model.GetInputTensor(0);
        const auto inputShape = inputTensor->Shape();
        const uint32_t inputImgCols = inputShape[arm::app::fwk::tflm::YoloFastestModel::ms_inputColsIdx];
        const uint32_t inputImgRows   = inputShape[arm::app::fwk::tflm::YoloFastestModel::ms_inputRowsIdx];

        auto bCamera = hal_camera_configure(inputImgCols, inputImgRows, HAL_CAMERA_MODE_SINGLE_FRAME, HAL_CAMERA_COLOUR_FORMAT_RGB888);
        if (!bCamera) {
            printf_err("Failed to configure camera.\n");
            return false;
        }

        // 함수 끝 직전 (return true; 바로 위)에:
        ODI_MARKER(0xD100000F);  
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
        OBH_MARKER(0xDD000001);                   // entry

        auto& profiler = ctx.Get<Profiler&>("profiler");
        auto& model = ctx.Get<Model&>("model");

        if (!model.IsInited()) {
            printf_err("Model is not initialised! Terminating processing.\n");
            return false;
        }
        OBH_MARKER(0xDD000002);

        auto inputTensor = model.GetInputTensor(0);
        auto outputTensor0 = model.GetOutputTensor(0);
        auto outputTensor1 = model.GetOutputTensor(1);
        const auto inputShape = inputTensor->Shape();
        if (inputShape.empty()) {
            printf_err("Invalid input tensor dims\n");
            return false;
        } else if (inputShape.size() < 3) {
            printf_err("Input tensor dimension should be >= 3\n");
            return false;
        }

        const int inputImgCols = inputShape[arm::app::fwk::tflm::YoloFastestModel::ms_inputColsIdx];
        const int inputImgRows   = inputShape[arm::app::fwk::tflm::YoloFastestModel::ms_inputRowsIdx];

        /* Set up pre and post-processing. */
        DetectorPreProcess preProcess = DetectorPreProcess(inputTensor, true, model.IsDataSigned());

        std::vector<object_detection::DetectionResult> results;
        const object_detection::PostProcessParams postProcessParams {
            inputImgRows, inputImgCols, object_detection::originalImageSize,
            object_detection::anchor1, object_detection::anchor2
        };
        DetectorPostProcess postProcess = DetectorPostProcess(outputTensor0, outputTensor1,
                results, postProcessParams);

        /* Ensure there are no results leftover from previous inference when running all. */
        results.clear();
        OBH_MARKER(0xDD000003);

        hal_camera_start();
        OBH_MARKER(0xDD000004);

        uint32_t capturedFrameSize = 0;
        const uint8_t* currImage = hal_camera_get_captured_frame(&capturedFrameSize);
        if (!currImage || !capturedFrameSize) {
            printf_err("hal_camera_get_captured_frame failed");
            return false;
        }
        OBH_MARKER(0xDD000005);

        const size_t copySz = inputTensor->Bytes();

#if 0   // ★★ Phase 15b Step 3 — skip ALL LVGL UI (no LCD)
        {
            ScopedLVGLLock lv_lock;

            /* Display this image on the LCD. */
            write_to_lvgl_buf(inputImgCols, inputImgRows,
                            currImage, &lvgl_image[0][0]);
            lv_obj_invalidate(ScreenLayoutImageObject());

            if (!run_requested()) {
               lv_led_off(ScreenLayoutLEDObject());
               return false;
            }

            lv_led_on(ScreenLayoutLEDObject());

#if SHOW_INF_TIME
            uint32_t inf_prof = Get_SysTick_Cycle_Count32();
#endif

            /* (preProcess/inference/postProcess moved outside this block) */

#if SHOW_INF_TIME
            inf_prof = Get_SysTick_Cycle_Count32() - inf_prof;
            lv_label_set_text_fmt(ScreenLayoutLabelObject(2), "Inference time: %.3f ms", (double)inf_prof / SystemCoreClock * 1000);
            lv_label_set_text_fmt(ScreenLayoutLabelObject(3), "Inferences / sec: %.2f", (double) SystemCoreClock / inf_prof);
#endif

            lv_label_set_text_fmt(ScreenLayoutLabelObject(0), "Faces Detected: %i", results.size());
            DrawDetectionBoxes(results, inputImgCols, inputImgRows);

        } // ScopedLVGLLock
#endif

        /* ★★ Inference path — outside LVGL block ★★ */

        /* Pre-processing */
        if (!preProcess.DoPreProcess(currImage, copySz)) {
            printf_err("Pre-processing failed.");
            return false;
        }
        OBH_MARKER(0xDD000006);

        /* ★★★ NPU INFERENCE ★★★ */
        if (!RunInference(model, profiler)) {
            printf_err("Inference failed.");
            return false;
        }
        OBH_MARKER(0xDD000007);

        /* Post-processing */
        if (!postProcess.DoPostProcess()) {
            printf_err("Post-processing failed.");
            return false;
        }
        OBH_MARKER(0xDD000008);

#if VERIFY_TEST_OUTPUT
        DumpTensor(modelOutput0);
        DumpTensor(modelOutput1);
#endif /* VERIFY_TEST_OUTPUT */

        OBH_MARKER(0xDD000009);

        if (!PresentInferenceResult(results)) {
            return false;
        }
        OBH_MARKER(0xDD00000A);

        profiler.PrintProfilingResult();

        OBH_MARKER(0xDD00000F);                       // exit
        return true;
    }
    
    static bool PresentInferenceResult(const std::vector<object_detection::DetectionResult>& results)
    {
        /* If profiling is enabled, and the time is valid. */
        info("Final results:\n");
        info("Total number of inferences: 1\n");

        for (uint32_t i = 0; i < results.size(); ++i) {
            info("%" PRIu32 ") (%f) -> %s {x=%d,y=%d,w=%d,h=%d}\n", i,
                results[i].m_normalisedVal, "Detection box:",
                results[i].m_x0, results[i].m_y0, results[i].m_w, results[i].m_h );
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

    static void CreateBox(lv_obj_t *frame, int x0, int y0, int w, int h)
    {
        lv_obj_t *box = lv_obj_create(frame);
        lv_obj_set_size(box, w, h);
        lv_obj_add_style(box, &boxStyle, LV_PART_MAIN);
        lv_obj_set_pos(box, x0, y0);
    }

    static void DrawDetectionBoxes(const std::vector<object_detection::DetectionResult>& results,
                                   int imgInputCols, int imgInputRows)
    {
        lv_obj_t *frame = ScreenLayoutImageHolderObject();
        float xScale = (float) lv_obj_get_content_width(frame) / imgInputCols;
        float yScale = (float) lv_obj_get_content_height(frame) / imgInputRows;

        DeleteBoxes(frame);

        for (const auto& result: results) {
            CreateBox(frame,
                      floor(result.m_x0 * xScale),
                      floor(result.m_y0 * yScale),
                      ceil(result.m_w * xScale),
                      ceil(result.m_h * yScale));
        }
    }

} /* namespace app */
} /* namespace alif */
