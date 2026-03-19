#ifndef __MODBUS_MASTER_H
#define __MODBUS_MASTER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* USART2 + PD7方向控制 (MAX485) */
/* PA2 -> TX, PA3 -> RX, PD7 -> DE/RE */

extern UART_HandleTypeDef g_rs485_uart;

/* 初始化 USART2+PD7 方向控制的 RS485 主站接口 */
void Modbus_MasterInit(void);
/* 阻塞发送一帧数据（内部切换收发） */
void Modbus_MasterSend(const uint8_t *buf, uint16_t len);
/* 阻塞接收指定长度数据，返回 HAL_OK/超时/错误 */
HAL_StatusTypeDef Modbus_MasterReceive(uint8_t *buf, uint16_t len, uint32_t timeout);
/* nanoMODBUS RTU transport callbacks */
int32_t Modbus_Master_NMBS_Read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);
int32_t Modbus_Master_NMBS_Write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);

/* Legacy names for compatibility */
#define Modbus_Init()                       Modbus_MasterInit()
#define Modbus_SendData(buf, len)           Modbus_MasterSend((buf), (len))
#define Modbus_ReceiveData(buf, len, to)    Modbus_MasterReceive((buf), (len), (to))
#define Modbus_Send_Data(buf, len)          Modbus_MasterSend((buf), (len))
#define Modbus_Receive_Data(buf, len, to)   Modbus_MasterReceive((buf), (len), (to))

#endif
