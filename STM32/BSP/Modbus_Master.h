#ifndef __MODBUS_MASTER_H
#define __MODBUS_MASTER_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/* USART2 + PD7方向控制 (MAX485) */
/* PA2 -> TX, PA3 -> RX, PD7 -> DE/RE */

extern UART_HandleTypeDef g_rs485_uart;

/**
 * @brief 初始化 USART2 + PD7 方向控制的 RS485 Modbus 主站接口。
 * @param 无
 * @retval 无
 */
void Modbus_MasterInit(void);

/**
 * @brief 阻塞发送一帧 Modbus 请求数据，内部负责 485 收发切换。
 * @param buf: 待发送数据缓冲区
 * @param len: 待发送字节数
 * @retval 无
 */
void Modbus_MasterSend(const uint8_t *buf, uint16_t len);

/**
 * @brief 阻塞接收指定长度的 Modbus 响应数据。
 * @param buf: 接收缓冲区
 * @param len: 期望接收的字节数
 * @param timeout: 接收超时，单位毫秒
 * @retval HAL_StatusTypeDef: HAL_OK/HAL_TIMEOUT/HAL_ERROR
 */
HAL_StatusTypeDef Modbus_MasterReceive(uint8_t *buf, uint16_t len, uint32_t timeout);

/**
 * @brief nanoMODBUS 的底层读适配函数。
 * @param buf: 输出缓冲区
 * @param count: 期望读取字节数
 * @param byte_timeout_ms: 字节超时，0 表示仅取当前已有数据
 * @param arg: 保留参数，当前未使用
 * @retval >0 读取字节数，0 超时，<0 错误
 */
int32_t Modbus_Master_NMBS_Read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);

/**
 * @brief nanoMODBUS 的底层写适配函数。
 * @param buf: 待发送数据缓冲区
 * @param count: 待发送字节数
 * @param byte_timeout_ms: 发送超时，单位毫秒
 * @param arg: 保留参数，当前未使用
 * @retval >0 发送字节数，0 超时，<0 错误
 */
int32_t Modbus_Master_NMBS_Write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);

/* Legacy names for compatibility */
#define Modbus_Init()                       Modbus_MasterInit()
#define Modbus_SendData(buf, len)           Modbus_MasterSend((buf), (len))
#define Modbus_ReceiveData(buf, len, to)    Modbus_MasterReceive((buf), (len), (to))
#define Modbus_Send_Data(buf, len)          Modbus_MasterSend((buf), (len))
#define Modbus_Receive_Data(buf, len, to)   Modbus_MasterReceive((buf), (len), (to))

#endif
