#ifndef __TIM2_H
#define __TIM2_H

#include "stm32f1xx_hal.h"

/* TIM2 CH1: default PA0 */
#define TIM2_CH1_GPIO_PORT         GPIOA
#define TIM2_CH1_GPIO_PIN          GPIO_PIN_0
#define TIM2_CH1_GPIO_CLK_ENABLE() do { __HAL_RCC_GPIOA_CLK_ENABLE(); } while (0)

#define TIM2_CLK_ENABLE()          do { __HAL_RCC_TIM2_CLK_ENABLE(); } while (0)

extern TIM_HandleTypeDef g_tim2_handle;

void tim2_init(void);
uint16_t tim2_get_cnt(void);

#endif
