/**
 ****************************************************************************************************
 * @file        led.h
 * @author      正点原子团队(ALIENTEK)
 * @version     V1.0
 * @date        2024-01-01
 * @brief       LED 驱动代码
 * @license     Copyright (c) 2020-2032, 广州市星翼电子科技有限公司
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:正点原子 STM32F103开发板
 * 在线视频:www.yuanzige.com
 * 技术论坛:www.openedv.com
 * 公司网址:www.alientek.com
 * 购买地址:openedv.taobao.com
 *
 * 修改说明
 * V1.0 20240101
 * 第一次发布
 *
 ****************************************************************************************************
 */

#ifndef __LED_H
#define __LED_H
#include "sys.h"
/******************************************************************************************/
/* 引脚 定义 */
#define LED_R_GPIO_PORT                  GPIOB
#define LED_R_GPIO_PIN                   GPIO_PIN_5
#define LED_R_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)             /* PB口时钟使能 */

#define LED_G_GPIO_PORT                  GPIOE
#define LED_G_GPIO_PIN                   GPIO_PIN_5
#define LED_G_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOE_CLK_ENABLE(); }while(0)             /* PE口时钟使能 */

/******************************************************************************************/
/* LED端口定义 */
#define LED_R(x)   do{ x ? \
                      HAL_GPIO_WritePin(LED_R_GPIO_PORT, LED_R_GPIO_PIN, GPIO_PIN_SET) : \
                      HAL_GPIO_WritePin(LED_R_GPIO_PORT, LED_R_GPIO_PIN, GPIO_PIN_RESET); \
                  }while(0)      /* LED_R 0量1灭 */

#define LED_G(x)   do{ x ? \
                      HAL_GPIO_WritePin(LED_G_GPIO_PORT, LED_G_GPIO_PIN, GPIO_PIN_SET) : \
                      HAL_GPIO_WritePin(LED_G_GPIO_PORT, LED_G_GPIO_PIN, GPIO_PIN_RESET); \
                  }while(0)      /* LED_G 0量1灭 */

/* LED取反定义 */
#define LED_R_TOGGLE()   do{ HAL_GPIO_TogglePin(LED_R_GPIO_PORT, LED_R_GPIO_PIN); }while(0)        /* 翻转LED_R */
#define LED_G_TOGGLE()   do{ HAL_GPIO_TogglePin(LED_G_GPIO_PORT, LED_G_GPIO_PIN); }while(0)        /* 翻转LED_G */
/******************************************************************************************/
/* 外部接口函数*/
void LED_Init(void);                                                                            /* 初始化 */

#endif /* LED_H_ */
