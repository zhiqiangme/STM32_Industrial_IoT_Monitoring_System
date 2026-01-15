#ifndef __RS485_MASTER_H
#define __RS485_MASTER_H

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

extern UART_HandleTypeDef g_rs485_uart;

/* 初始化 USART3+PA5 方向控制的 RS485 主站接口 */
void RS485_MasterInit(void);
/* 阻塞发送一帧数据（内部切换收发） */
void RS485_MasterSend(const uint8_t *buf, uint16_t len);
/* 阻塞接收指定长度数据，返回 HAL_OK/超时/错误 */
HAL_StatusTypeDef RS485_MasterReceive(uint8_t *buf, uint16_t len, uint32_t timeout);

/* Legacy names for compatibility */
#define RS485_Init()                        RS485_MasterInit()
#define RS485_SendData(buf, len)            RS485_MasterSend((buf), (len))
#define RS485_ReceiveData(buf, len, to)     RS485_MasterReceive((buf), (len), (to))
#define RS485_Send_Data(buf, len)           RS485_MasterSend((buf), (len))
#define RS485_Receive_Data(buf, len, to)    RS485_MasterReceive((buf), (len), (to))

#endif
