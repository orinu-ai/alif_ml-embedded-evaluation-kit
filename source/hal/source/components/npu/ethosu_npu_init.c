/*
 * SPDX-FileCopyrightText: Copyright 2022-2024 Arm Limited and/or its affiliates
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

#include "ethosu_npu_init.h"

#include "RTE_Components.h"         /* For CPU related defintiions */
#include "log_macros.h"             /* Logging functions */
#include "cmsis_compiler.h"

#include "ethosu_mem_config.h"      /* Arm Ethos-U memory config */
#include "ethosu_driver.h"          /* Arm Ethos-U driver header */

/* Mandatory definition checks. */
#if !defined(ETHOS_U_BASE_ADDR)
#error "NPU base address is undefined."
#endif /* defnied(ETHOS_U_BASE_ADDR) */

#if !defined(ETHOS_U_IRQN)
#error "Arm NPU interrupt number is undefined."
#endif /* !defined(ETHOS_U_IRQN) */

#if !defined(ETHOS_U_SEC_ENABLED)
#error "Arm NPU security mode is undefined."
#endif /* !defined(ETHOS_U_SEC_ENABLED) */

#if !defined(ETHOS_U_PRIV_ENABLED)
#error "Arm NPU privilege mode is undefined."
#endif /* !defined(ETHOS_U_PRIV_ENABLED) */

#if defined(ETHOS_U_CACHE_BUF_SZ) && (ETHOS_U_CACHE_BUF_SZ > 0)
static uint8_t cache_arena[ETHOS_U_CACHE_BUF_SZ] CACHE_BUF_ATTRIBUTE;
#else  /* defined (ETHOS_U_CACHE_BUF_SZ) && (ETHOS_U_CACHE_BUF_SZ > 0) */
static uint8_t *cache_arena = NULL;
#endif /* defined (ETHOS_U_CACHE_BUF_SZ) && (ETHOS_U_CACHE_BUF_SZ > 0) */

struct ethosu_driver ethosu_drv; /* Default Ethos-U device driver */

static uint8_t *get_cache_arena()
{
    return cache_arena;
}

static size_t get_cache_arena_size()
{
#if defined(ETHOS_U_CACHE_BUF_SZ) && (ETHOS_U_CACHE_BUF_SZ > 0)
    return sizeof(cache_arena);
#else  /* defined (ETHOS_U_CACHE_BUF_SZ) && (ETHOS_U_CACHE_BUF_SZ > 0) */
    return 0;
#endif /* defined (ETHOS_U_CACHE_BUF_SZ) && (ETHOS_U_CACHE_BUF_SZ > 0) */
}


/**
 * @brief   Defines the Ethos-U interrupt handler: just a wrapper around the default
 *          implementation.
 **/
void arm_ethosu_npu_irq_handler(void)
{
    /* Call the default interrupt handler from the NPU driver */
    ethosu_irq_handler(&ethosu_drv);
}

static void arm_ethosu_npu_irq_init(void)
{
    /* ★ IRQ init entry */
    *(volatile uint32_t*)0x027DC454 = 0xBEEFA001;
    SCB_CleanDCache_by_Addr((void*)0x027DC454, 4); __DSB();
    
    const IRQn_Type ethosu_irqnum = (IRQn_Type)ETHOS_U_IRQN;
    
    /* IRQ 번호 + pending status 진단 */
    *(volatile uint32_t*)0x027DC458 = (uint32_t)ethosu_irqnum;
    *(volatile uint32_t*)0x027DC45C = NVIC_GetPendingIRQ(ethosu_irqnum);
    SCB_CleanDCache_by_Addr((void*)0x027DC458, 8); __DSB();
    
    /* ★ 이전 pending IRQ clear (가설 검증) */
    NVIC_ClearPendingIRQ(ethosu_irqnum);
    
    *(volatile uint32_t*)0x027DC454 = 0xBEEFA002;
    SCB_CleanDCache_by_Addr((void*)0x027DC454, 4); __DSB();
    
    NVIC_SetVector(ethosu_irqnum, (uint32_t)arm_ethosu_npu_irq_handler);
    
    *(volatile uint32_t*)0x027DC454 = 0xBEEFA003;
    SCB_CleanDCache_by_Addr((void*)0x027DC454, 4); __DSB();
    
    NVIC_EnableIRQ(ethosu_irqnum);
    
    *(volatile uint32_t*)0x027DC454 = 0xBEEFA004;
    SCB_CleanDCache_by_Addr((void*)0x027DC454, 4); __DSB();

    debug("EthosU IRQ#: %u, Handler: 0x%p\n",
          ethosu_irqnum, arm_ethosu_npu_irq_handler);
}
int arm_ethosu_npu_init(void)
{
    /* ★ Function entry marker */
    *(volatile uint32_t*)0x027DC450 = 0xBEEF0001;
    SCB_CleanDCache_by_Addr((void*)0x027DC450, 4);
    __DSB();
    
    int err = 0;

    /* Initialise the IRQ */
    arm_ethosu_npu_irq_init();
    *(volatile uint32_t*)0x027DC408 = 0xBEEF0002; SCB_CleanDCache_by_Addr((void*)0x027DC408, 4); __DSB();

    /* Initialise Ethos-U device */
    void* const ethosu_base_address = (void *)(ETHOS_U_BASE_ADDR);
    info("Initialising Ethos-U device@0x%" PRIx32 "\n", (uint32_t)(ETHOS_U_BASE_ADDR));

    *(volatile uint32_t*)0x027DC408 = 0xBEEF0003; SCB_CleanDCache_by_Addr((void*)0x027DC408, 4); __DSB();
    if (0 != (err = ethosu_init(&ethosu_drv,
                                ethosu_base_address,
                                get_cache_arena(),
                                get_cache_arena_size(),
                                ETHOS_U_SEC_ENABLED,
                                ETHOS_U_PRIV_ENABLED))) {
        printf_err("Failed to initialise Ethos-U device\n");
        return err;
    }

    *(volatile uint32_t*)0x027DC408 = 0xBEEF0004; SCB_CleanDCache_by_Addr((void*)0x027DC408, 4); __DSB();
    info("Ethos-U device initialised\n");

    /* Get Ethos-U version */
    struct ethosu_driver_version driver_version;
    struct ethosu_hw_info hw_info;

    ethosu_get_driver_version(&driver_version);
    ethosu_get_hw_info(&ethosu_drv, &hw_info);

    info("Ethos-U version info:\n");
    info("\tArch:       v%" PRIu32 ".%" PRIu32 ".%" PRIu32 "\n",
         hw_info.version.arch_major_rev,
         hw_info.version.arch_minor_rev,
         hw_info.version.arch_patch_rev);
    info("\tDriver:     v%" PRIu8 ".%" PRIu8 ".%" PRIu8 "\n",
         driver_version.major,
         driver_version.minor,
         driver_version.patch);
    info("\tMACs/cc:    %" PRIu32 "\n", (uint32_t)(1 << hw_info.cfg.macs_per_cc));
    info("\tCmd stream: v%" PRIu32 "\n", hw_info.cfg.cmd_stream_version);

    return 0;
}