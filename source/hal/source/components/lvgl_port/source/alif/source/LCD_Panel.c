/* Copyright (C) 2022 Alif Semiconductor - All Rights Reserved.
 * Use, distribution and modification of this code is permitted under the
 * terms stated in the Alif Semiconductor Software License Agreement
 *
 * You should have received a copy of the Alif Semiconductor Software
 * License Agreement with this file. If not, please write to:
 * contact@alifsemi.com, or visit: https://alifsemi.com/license
 *
 */
#include <stdio.h>
#include <inttypes.h>
#include "Driver_CDC200.h"
#include "RTE_Components.h"
#include "RTE_Device.h"
#include "board_defs.h"
#include "board_utils.h"
#include "delay.h"
#include "platform_drivers.h"
#include "sys_utils.h"

extern ARM_DRIVER_CDC200 Driver_CDC200;

static void (*tear_handler)(void);

static void cdc_event_handler(uint32_t int_event)
{
    if (int_event == ARM_CDC_SCANLINE0_EVENT) {
        tear_handler();
    }
}

int LCD_current_v_pos(void)
{
    return 0;   // ★ stub
    return Driver_CDC200.GetVerticalPosition();
}

void LCD_enable_tear_interrupt(void (*handler)(void), uint8_t prio)
{
    (void)handler;
    (void)prio;
    return;   // ★ stub

    tear_handler = handler;
    Driver_CDC200.Control(CDC200_SCANLINE0_EVENT, ENABLE);
    NVIC_SetPriority(CDC_SCANLINE0_IRQ_IRQn, prio);
    NVIC_EnableIRQ(CDC_SCANLINE0_IRQ_IRQn);
}

int Display_initialization(uint8_t *buffer)
{
    return 0;

    int32_t ret = (int32_t)enable_peripheral_clocks();
    if(ret != ARM_DRIVER_OK)
    {
        printf("Display_initialization enable_peripheral_clocks failed: %" PRIi32 "\n", ret);
        return 1;
    }

    enable_mipi_power();

	////////////////////////////////////////////////////////////////////////////
	// MIPI DPI Controller Setup (CDC200)
	////////////////////////////////////////////////////////////////////////////
	ret = Driver_CDC200.Initialize(cdc_event_handler);
	if(ret != ARM_DRIVER_OK)
	{
		printf("Driver_CDC200.Initialize: %" PRIi32 "\n", ret);
		return 1;
	}

	ret = Driver_CDC200.PowerControl(ARM_POWER_FULL);
	if(ret != ARM_DRIVER_OK)
	{
		printf("Driver_CDC200.PowerControl: %" PRIi32 "\n", ret);
		return 1;
	}

	ret = Driver_CDC200.Control(CDC200_CONFIGURE_DISPLAY, (uint32_t) buffer);
	if(ret != ARM_DRIVER_OK)
	{
		printf("Driver_CDC200.Control: %" PRIi32 "\n", ret);
		return 1;
	}

	ret = Driver_CDC200.Start();
	if(ret != ARM_DRIVER_OK)
	{
		printf("Driver_CDC200.Start: %" PRIi32 "\n", ret);
		return 1;
	}

	return 0;
}

int LCD_Panel_init(uint8_t *buffer)
{
    *(volatile uint32_t*)0x027DC540 = 0xCC000001;
    __asm volatile ("dsb sy" ::: "memory");


    volatile int test = 3;

    while(test) {
         *(volatile uint32_t*)0x027DC540 = 0xCC000010 + test;  // ★ LED1 iteration
        __asm volatile ("dsb sy" ::: "memory");

        BOARD_LED1_Control(BOARD_LED_STATE_HIGH);
        sleep_or_wait_msec(300);
        BOARD_LED1_Control(BOARD_LED_STATE_LOW);
        sleep_or_wait_msec(300);
        test--;
    }
     *(volatile uint32_t*)0x027DC540 = 0xCC000002;   // ★ after LED blink
    __asm volatile ("dsb sy" ::: "memory");

    static volatile int dinit = 0;
    dinit = Display_initialization(buffer);

    *(volatile uint32_t*)0x027DC540 = 0xCC000003;   // ★ after Display_init
    __asm volatile ("dsb sy" ::: "memory");

    if (dinit != 0) {
        *(volatile uint32_t*)0x027DC540 = 0xCC00FA01;  // ★ FAIL
        __asm volatile ("dsb sy" ::: "memory");

        while(1) {
            BOARD_LED2_Control(BOARD_LED_STATE_LOW);
            sleep_or_wait_msec(300);
            BOARD_LED2_Control(BOARD_LED_STATE_HIGH);
            sleep_or_wait_msec(300);
        }
    }
    *(volatile uint32_t*)0x027DC540 = 0xCC000004;   // ★ before LED2 HIGH
    __asm volatile ("dsb sy" ::: "memory");
    BOARD_LED2_Control(BOARD_LED_STATE_HIGH);

    *(volatile uint32_t*)0x027DC540 = 0xCC000005;   // ★ before LED2 HIGH
    __asm volatile ("dsb sy" ::: "memory");

    return 0;
}
