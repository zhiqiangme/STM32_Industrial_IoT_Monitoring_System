#include "sys.h"
#include "delay.h"

#ifdef RTOS_ENABLED
#include <rtthread.h>
#endif

/* SysTick 计数换算因子：72MHz 时 1us 对应 72 个计数。 */
static uint32_t g_fac_us = 0u;

void delay_init(uint16_t sysclk)
{
    g_fac_us = sysclk;
}

__weak void delay_us(uint32_t nus)
{
    uint32_t ticks;
    uint32_t told;
    uint32_t tnow;
    uint32_t tcnt = 0u;
    uint32_t reload = SysTick->LOAD;

    ticks = nus * g_fac_us;
    told = SysTick->VAL;

    while (1)
    {
        tnow = SysTick->VAL;
        if (tnow != told)
        {
            if (tnow < told)
            {
                tcnt += told - tnow;
            }
            else
            {
                tcnt += reload - tnow + told;
            }
            told = tnow;

            if (tcnt >= ticks)
            {
                break;
            }
        }
    }
}

__weak void delay_ms(uint16_t nms)
{
    if (nms == 0u)
    {
        return;
    }

    /* 线程上下文优先让出 CPU；只有无线程/中断上下文才退回忙等。 */
#ifdef RTOS_ENABLED
    if (rt_thread_self() != RT_NULL && rt_interrupt_get_nest() == 0u)
    {
        (void)rt_thread_mdelay((rt_int32_t)nms);
        return;
    }
#endif

    while (nms > 0u)
    {
        uint16_t chunk = (nms > 60u) ? 60u : nms;
        delay_us((uint32_t)chunk * 1000u);
        nms = (uint16_t)(nms - chunk);
    }
}

void HAL_Delay(uint32_t Delay)
{
    /* HAL 允许传入 32 位毫秒数，这里按 16 位分片复用 delay_ms。 */
    while (Delay > 0u)
    {
        uint16_t chunk = (Delay > 0xFFFFu) ? 0xFFFFu : (uint16_t)Delay;
        delay_ms(chunk);
        Delay -= chunk;
    }
}
