#ifndef __TIM2_H
#define __TIM2_H

#include "stm32f1xx_hal.h"

/* TIM2 is configured as an external clock counter on CH1 (remapped to PA15). */
#define TIM2_CH1_GPIO_PORT         GPIOA
#define TIM2_CH1_GPIO_PIN          GPIO_PIN_15
#define TIM2_CH1_GPIO_CLK_ENABLE() do { __HAL_RCC_GPIOA_CLK_ENABLE(); } while (0)

#define TIM2_CLK_ENABLE()          do { __HAL_RCC_TIM2_CLK_ENABLE(); } while (0)
#define TIM2_AFIO_CLK_ENABLE()     do { __HAL_RCC_AFIO_CLK_ENABLE(); } while (0)

extern TIM_HandleTypeDef g_tim2_handle;

void TIM2_Init(void);
uint16_t TIM2_GetCounter(void);

#endif
