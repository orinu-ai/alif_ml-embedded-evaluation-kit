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
#include "hal.h"                      /* Brings in platform definitions. */
#include "YoloFastestModel.hpp"       /* Model class for running inference. */
#include "UseCaseHandler.hpp"         /* Handlers for different user options. */
#include "UseCaseCommonUtils.hpp"     /* Utils functions. */
#include "log_macros.h"             /* Logging functions */
#include "BufAttributes.hpp"        /* Buffer attributes to be applied */

#include "DetectorPreProcessing.hpp"   // ★ 추가

/* ============================================================ */
/* Phase 15b Step 3: NPU driver access + semaphore timeout       */
/* ============================================================ */
extern "C" {
#include "ethosu_driver.h"

/* Driver's internal semaphore struct (baremetal default) */
struct ethosu_semaphore_t {
    uint8_t count;
};

/* External NPU driver instance (defined in ethosu_npu_init.c) */
extern struct ethosu_driver ethosu_drv;

/* ★ Override soft_reset - skip HW reset for diagnostic */
int ethosu_soft_reset(struct ethosu_driver *drv) {
    *(volatile uint32_t*)0x027DC590 = 0xCAFE0001;   // bypass marker
    *(volatile uint32_t*)0x027DC594 = (uint32_t)drv; // drv pointer
    __asm volatile ("dsb sy" ::: "memory");
    return 0;   // ★ Skip dev_soft_reset, just return OK
}

/* ★ Override weak default - add timeout to prevent forever WFE */
int ethosu_semaphore_take(void *sem, uint64_t timeout) {
    (void)timeout;  // baremetal: ignore
    
    volatile struct ethosu_semaphore_t* s = (volatile struct ethosu_semaphore_t*)sem;
    
    uint32_t loops = 0;
    while (s->count == 0) {
      //  __WFE();
        loops++;
        if (loops > 10000000) {
            /* ★ Timeout markers */
            *(volatile uint32_t*)0x027DC580 = 0xDEAD0001;
            *(volatile uint32_t*)0x027DC584 = loops;
            __asm volatile ("dsb sy" ::: "memory");
            return -1;
        }
    }
    s->count--;
    return 0;
}

}  // extern "C"

/* Phase 15b verification markers */
#define ML_MARKER(val) do { \
    *(volatile uint32_t*)0x027DC500 = (val); \
    __asm volatile ("dsb sy" ::: "memory"); \
} while(0)

/* Inference iteration counter — increments every loop */
#define ML_COUNTER() do { \
    static volatile uint32_t* const counter = (volatile uint32_t*)0x027DC508; \
    *counter = *counter + 1; \
    __asm volatile ("dsb sy" ::: "memory"); \
} while(0)

namespace arm {
namespace app {
    static uint8_t tensorArena[ACTIVATION_BUF_SZ] ACTIVATION_BUF_ATTRIBUTE;
    namespace object_detection {
        extern uint8_t* GetModelPointer();
        extern size_t GetModelLen();
    } /* namespace object_detection */
} /* namespace app */
} /* namespace arm */

void MainLoop()
{
    ML_MARKER(0xA1000001);
    init_trigger_rx();
    arm::app::fwk::tflm::YoloFastestModel model;
    arm::app::fwk::iface::MemoryRegion modelMem{arm::app::object_detection::GetModelPointer(),
                                                arm::app::object_detection::GetModelLen()};
    arm::app::fwk::iface::MemoryRegion computeMem{arm::app::tensorArena,
                                                  sizeof(arm::app::tensorArena)};
    ML_MARKER(0xA1000002);
    if (!model.Init(computeMem, modelMem)) {
        printf_err("Failed to initialise model\n");
        return;
    }
    ML_MARKER(0xA1000003);
    if (!alif::app::ObjectDetectionInit(model)) {
        printf_err("Failed to initialise use case handler\n");
        return;
    }
    ML_MARKER(0xA1000004);

    /* Instantiate application context. */
    arm::app::ApplicationContext caseContext;
    ML_MARKER(0xA1000040);

    arm::app::Profiler profiler{"object_detection"};
    ML_MARKER(0xA1000041);

    /* ★ Phase 15b Step 3 — Step 3a: DetectorPreProcess only */
    auto inputTensor = model.GetInputTensor(0);
    const auto inputShape = inputTensor->Shape();
    const int inputImgCols = inputShape[arm::app::fwk::tflm::YoloFastestModel::ms_inputColsIdx];
    const int inputImgRows = inputShape[arm::app::fwk::tflm::YoloFastestModel::ms_inputRowsIdx];
    const size_t copySz = inputTensor->Bytes();
    ML_MARKER(0xA1000060);          // ★ tensors OK

    arm::app::DetectorPreProcess preProcess(inputTensor, true, model.IsDataSigned());
    ML_MARKER(0xA1000061);          // ★ preProcess constructed

    /* ★ Static fake frame buffer (no hal_camera) */
    static uint8_t fake_frame[192 * 192 * 3] __attribute__((aligned(4))) = {0};
    ML_MARKER(0xA1000066);   // ★ fake buffer OK

#if 0   // ★ skip Set (heap)
    caseContext.Set<arm::app::Profiler&>("profiler", profiler);
    caseContext.Set<arm::app::fwk::iface::Model&>("model", model);
#endif

    ML_MARKER(0xA1000005);

    /* Inference success counter */
    volatile uint32_t* inf_count = (volatile uint32_t*)0x027DC550;
    *inf_count = 0;
    __asm volatile ("dsb sy" ::: "memory");

    /* Loop */
   do {
    ML_COUNTER();
    
    /* Driver state BEFORE */
    *(volatile uint32_t*)0x027DC560 = (uint32_t)ethosu_drv.job.state;
    *(volatile uint32_t*)0x027DC564 = (uint32_t)ethosu_drv.job.result;
    if (ethosu_drv.semaphore) {
        *(volatile uint32_t*)0x027DC568 = 
            ((struct ethosu_semaphore_t*)ethosu_drv.semaphore)->count;
    }
    __asm volatile ("dsb sy" ::: "memory");
    
    /* ★ Progress markers */
    *(volatile uint32_t*)0x027DC500 = 0xA1000063;  // before preProcess
    __asm volatile ("dsb sy" ::: "memory");
    
    const uint8_t* frame = fake_frame;
    if (!preProcess.DoPreProcess(frame, copySz)) continue;
    
    /* ★ */
    *(volatile uint32_t*)0x027DC500 = 0xA1000064;  // before RunInference
    __asm volatile ("dsb sy" ::: "memory");
    
    if (!model.RunInference()) {
        *(volatile uint32_t*)0x027DC500 = 0xA10000F1;  // RunInference fail
        __asm volatile ("dsb sy" ::: "memory");
        continue;
    }
    
    /* ★ */
    *(volatile uint32_t*)0x027DC500 = 0xA1000065;  // RunInference OK
    __asm volatile ("dsb sy" ::: "memory");
    
    /* Driver state AFTER */
    *(volatile uint32_t*)0x027DC56C = (uint32_t)ethosu_drv.job.state;
    if (ethosu_drv.semaphore) {
        *(volatile uint32_t*)0x027DC570 = 
            ((struct ethosu_semaphore_t*)ethosu_drv.semaphore)->count;
    }
    __asm volatile ("dsb sy" ::: "memory");
    
    *inf_count = *inf_count + 1;
    
    /* ★ */
    *(volatile uint32_t*)0x027DC500 = 0xA1000066;  // iter end
    __asm volatile ("dsb sy" ::: "memory");
} while (1);
}
