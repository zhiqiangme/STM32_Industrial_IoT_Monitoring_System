/*
 ****************************************************************************************************
 * @file        led.c
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
#include "LED.h"

/**
 * @brief       初始化LED相关IO口, 并使能时钟
 * @param       无
 * @retval      无
 */
void LED_Init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    LED_R_GPIO_CLK_ENABLE();                                 /* LED_R时钟使能 */
    LED_G_GPIO_CLK_ENABLE();                                 /* LED_G时钟使能 */

    gpio_init_struct.Pin = LED_R_GPIO_PIN;                   /* LED0引脚 */
    gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;            /* 推挽输出 */
    gpio_init_struct.Pull = GPIO_PULLUP;                    /* 上拉 */
    gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;          /* 高速 */
    HAL_GPIO_Init(LED_R_GPIO_PORT, &gpio_init_struct);       /* 初始化LED_R引脚 */

    gpio_init_struct.Pin = LED_G_GPIO_PIN;                   /* LED1引脚 */
    HAL_GPIO_Init(LED_G_GPIO_PORT, &gpio_init_struct);       /* 初始化LED_R引脚 */

    LED_R(1);                                                /* 关闭 LED_R */
    LED_G(1);                                                /* 关闭 LED_G */
	
}
