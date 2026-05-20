/*
 * SPDX-FileCopyrightText: Copyright 2021-2025 Arm Limited and/or its
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

/****************************************************************************\
 *               Main application file for ARM NPU on MPS3 board             *
\****************************************************************************/

#include "hal.h"                    /* our hardware abstraction api */
#include "log_macros.h"

#include <cstdio>
#include <new>
#include <exception>

extern void MainLoop();
extern "C" void hp_phase15b_step1_init(void);   /* Phase 15b: C function in platform_drivers.c */

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
__ASM(" .global __ARM_use_no_argv\n");
#endif

/* Print application information. */
[[maybe_unused]] static void PrintApplicationIntro()
{
    info("%s\n", PRJ_DES_STR);
    info("Version %s Build date: " __DATE__ " @ " __TIME__ "\n", PRJ_VER_STR);
    info("Compiler: %s\n", PRJ_COMPILER);
    info("Copyright 2021-2025 Arm Limited and/or "
         "its affiliates <open-source-office@arm.com>\n\n");
}

[[maybe_unused]] static void out_of_heap()
{
    warn("Out of heap\n");
    std::terminate();
}

int main ()
{
#if defined(M55_HP) || defined(RTSS_HP)
    /* Em-boxer Phase 15b Step 1+2+3+4: HP full init + obj_det MainLoop */
    hp_phase15b_step1_init();
    
    /* Phase 15b Step 4: obj_det MainLoop (model + camera + inference) */
    MainLoop();
    
    /* Should be unreachable */
    while (1) {
        __WFI();
    }
#else
    if (hal_platform_init()) {
        /* Application information, UART should have been initialised. */
        PrintApplicationIntro();

        std::set_new_handler(out_of_heap);

        /* Run the application. */
        MainLoop();
    }

    /* This is unreachable without errors. */
    info("program terminating...\n");

    /* Release platform. */
    hal_platform_release();
    return 0;
#endif
}
