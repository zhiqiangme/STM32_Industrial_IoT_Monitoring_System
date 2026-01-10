#ifndef __485_H
#define __485_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* USART3 + PA5方向控制 (MAX485) */
/* PB10 -> TX, PB11 -> RX, PA5 -> DE/RE */

#define RS485_EN_PORT       GPIOA
#define RS485_EN_PIN        GPIO_PIN_5
#define RS485_TX_PORT       GPIOB
#define RS485_TX_PIN        GPIO_PIN_10
#define RS485_RX_PORT       GPIOB
#define RS485_RX_PIN        GPIO_PIN_11

#define RS485_TX_MODE()     HAL_GPIO_WritePin(RS485_EN_PORT, RS485_EN_PIN, GPIO_PIN_SET)
#define RS485_RX_MODE()     HAL_GPIO_WritePin(RS485_EN_PORT, RS485_EN_PIN, GPIO_PIN_RESET)

extern UART_HandleTypeDef huart_485;

void RS485_Init(void);
void RS485_SendData(uint8_t *buf, uint16_t len);
HAL_StatusTypeDef RS485_ReceiveData(uint8_t *buf, uint16_t len, uint32_t timeout);

/* 兼容旧函数名 */
#define RS485_Send_Data(buf, len)           RS485_SendData(buf, len)
#define RS485_Receive_Data(buf, len, to)    RS485_ReceiveData(buf, len, to)

#endif
