/*
 * SPDX-FileCopyrightText: Copyright 2022, 2025 Arm Limited and/or its affiliates
 * <open-source-office@arm.com> SPDX-License-Identifier: Apache-2.0
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
#include "YoloFastestModel.hpp"

#include "log_macros.h"

/* Phase 15b verification marker */
#define YFM_MARKER(val) do { \
    *(volatile uint32_t*)0x027DC520 = (val); \
    __asm volatile ("dsb sy" ::: "memory"); \
} while(0)

const tflite::MicroOpResolver& arm::app::fwk::tflm::YoloFastestModel::GetOpResolver()
{
    return this->m_opResolver;
}

bool arm::app::fwk::tflm::YoloFastestModel::EnlistOperations()
{
    YFM_MARKER(0xC1000001);    // ★ entry

#ifndef ETHOS_U_NPU_ASSUMED
    this->m_opResolver.AddDepthwiseConv2D();
    this->m_opResolver.AddConv2D();
    this->m_opResolver.AddAdd();
    this->m_opResolver.AddResizeNearestNeighbor();
    /*These are needed for UT to work, not needed on FVP */
    this->m_opResolver.AddPad();
    this->m_opResolver.AddMaxPool2D();
    this->m_opResolver.AddConcatenation();
#endif

    YFM_MARKER(0xC1000002);    // ★ before AddEthosU
    TfLiteStatus status = this->m_opResolver.AddEthosU();
    YFM_MARKER(0xC1000003);    // ★ after AddEthosU (return값 무관)

    if (kTfLiteOk == status) {
        YFM_MARKER(0xC1000004);    // ★ AddEthosU OK
        info("Added %s support to op resolver\n", tflite::GetString_ETHOSU());
    } else {
        YFM_MARKER(0xC100FA01);    // ★ AddEthosU FAIL
        printf_err("Failed to add Arm NPU support to op resolver.");
        return false;
    }
    
    YFM_MARKER(0xC1000005);    // ★ EnlistOperations exit OK
    return true;
}
