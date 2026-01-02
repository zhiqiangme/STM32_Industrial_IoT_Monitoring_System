#include "tim2.h"

TIM_HandleTypeDef g_tim2_handle;

void tim2_init(void)
{
    TIM_ClockConfigTypeDef clock_config = {0};

    g_tim2_handle.Instance = TIM2;
    g_tim2_handle.Init.Prescaler = 0;
    g_tim2_handle.Init.CounterMode = TIM_COUNTERMODE_UP;
    g_tim2_handle.Init.Period = 0xFFFF;
    g_tim2_handle.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    g_tim2_handle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    HAL_TIM_Base_Init(&g_tim2_handle);

    clock_config.ClockSource = TIM_CLOCKSOURCE_TI1;
    clock_config.ClockPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
    clock_config.ClockPrescaler = TIM_CLOCKPRESCALER_DIV1;
    clock_config.ClockFilter = 0;
    HAL_TIM_ConfigClockSource(&g_tim2_handle, &clock_config);
}

uint16_t tim2_get_cnt(void)
{
    return (uint16_t)__HAL_TIM_GET_COUNTER(&g_tim2_handle);
}

void HAL_TIM_Base_MspInit(TIM_HandleTypeDef *htim)
{
    GPIO_InitTypeDef gpio_init_struct = {0};

    if (htim->Instance == TIM2)
    {
        /* 使能时钟 */
        TIM2_CH1_GPIO_CLK_ENABLE();
        TIM2_CLK_ENABLE();
        TIM2_AFIO_CLK_ENABLE();
        
        /* TIM2重映射: PA15/PB3/PA2/PA3 (Partial Remap 1) */
        /* 使用 AFIO_MAPR 的 TIM2_REMAP[1:0] = 01 */
        __HAL_AFIO_REMAP_TIM2_PARTIAL_1();
        
        /* 同时需要禁用JTAG以释放PA15 (保留SWD) */
        __HAL_AFIO_REMAP_SWJ_NOJTAG();

        /* 配置PA15为复用输入 */
        gpio_init_struct.Pin = TIM2_CH1_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_AF_INPUT;
        gpio_init_struct.Pull = GPIO_NOPULL;
        HAL_GPIO_Init(TIM2_CH1_GPIO_PORT, &gpio_init_struct);
    }
}
