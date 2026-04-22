/*
 * Copyright (c) 2006-2019, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2021-05-24                  the first version
 * 2026-04-14     zhiqiangme   adapt minimal board init for STM32 Mill
 */

#include <rthw.h>
#include <rtthread.h>

/*
 * 当前工程仍沿用原来的 HAL_Init + SysTick + 裸机外设初始化流程，
 * 这里只补最小的 RT-Thread 板级接线：堆和组件初始化。
 */
#if defined(RT_USING_HEAP)
#define RT_HEAP_SIZE (12 * 1024)
static rt_uint8_t rt_heap[RT_HEAP_SIZE];

RT_WEAK void *rt_heap_begin_get(void)
{
    return rt_heap;
}

RT_WEAK void *rt_heap_end_get(void)
{
    return rt_heap + RT_HEAP_SIZE;
}
#endif

void rt_hw_board_init(void)
{
#ifdef RT_USING_COMPONENTS_INIT
    rt_components_board_init();
#endif

#if defined(RT_USING_HEAP)
    rt_system_heap_init(rt_heap_begin_get(), rt_heap_end_get());
#endif
}
