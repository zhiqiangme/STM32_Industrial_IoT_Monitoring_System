#include "main.h"
#include "app_threads.h"

#include <rthw.h>

void rt_hw_board_init(void);

static void App_StartScheduler(void)
{
    /* 当前仍由原有 main() 做硬件启动，然后手动接管 RT-Thread 内核启动。 */
    rt_hw_interrupt_disable();
    rt_hw_board_init();
    rt_show_version();
    rt_system_timer_init();
    rt_system_scheduler_init();

    if (AppThreads_Create() != RT_EOK)
    {
        while (1)
        {
        }
    }

    rt_system_timer_thread_init();
    rt_thread_idle_init();
    rt_system_scheduler_start();
}

int main(void)
{
    /* 先完成 HAL/时钟/串口/基础外设初始化，再进入 RTOS。 */
    AppThreads_HardwareInit();
    App_StartScheduler();

    while (1)
    {
    }
}
