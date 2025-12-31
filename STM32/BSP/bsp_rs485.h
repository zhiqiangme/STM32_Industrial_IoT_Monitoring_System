#ifndef __BSP_RS485_H
#define __BSP_RS485_H

#include "stm32f1xx_hal.h"
#include <stdio.h>

/* RS485相关引脚定义 */
/* PA2 -> USART2_TX */
/* PA3 -> USART2_RX */
/* PD7 -> RS485_RE/DE (高电平发送，低电平接收) */

#define RS485_TX_GPIO_PORT       GPIOA
#define RS485_TX_PIN             GPIO_PIN_2
#define RS485_RX_GPIO_PORT       GPIOA
#define RS485_RX_PIN             GPIO_PIN_3

#define RS485_RE_GPIO_PORT       GPIOD
#define RS485_RE_PIN             GPIO_PIN_7

/* 控制收发模式 */
#define RS485_TX_ENABLE()        HAL_GPIO_WritePin(RS485_RE_GPIO_PORT, RS485_RE_PIN, GPIO_PIN_SET)
#define RS485_RX_ENABLE()        HAL_GPIO_WritePin(RS485_RE_GPIO_PORT, RS485_RE_PIN, GPIO_PIN_RESET)

void RS485_Init(void);
void RS485_Send_Data(uint8_t *buf, uint16_t len);
/* 简单的接收函数，供Modbus轮询使用 */
HAL_StatusTypeDef RS485_Receive_Data(uint8_t *buf, uint16_t len, uint32_t timeout);

#endif
