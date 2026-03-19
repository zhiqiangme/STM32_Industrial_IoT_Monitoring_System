#ifndef __MODBUS_SLAVE_H
#define __MODBUS_SLAVE_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * RS485 从站链路（USART3+PA5方向控制）
 * 用途：作为 Modbus 从站与 G780S 网关通信，按帧静默时间切换收发。
 */
#define MODBUS_SLAVE_BAUDRATE         115200
#define MODBUS_SLAVE_TX_BUFFER_SIZE   256
#define MODBUS_SLAVE_RX_BUFFER_SIZE   256
#define MODBUS_SLAVE_SILENCE_MS       65 /* 必须大于网关的 3.5 字符间隔（约 50ms） */

typedef enum {
    MODBUS_SLAVE_STATE_IDLE = 0,      /* 空闲，允许装载新帧 */
    MODBUS_SLAVE_STATE_SENDING,       /* 正在阻塞发送 */
    MODBUS_SLAVE_STATE_POST_SILENCE   /* 发送完成后静默等待，避免帧黏连 */
} ModbusSlaveState;

/* 初始化：打开GPIO/USART2，默认接收模式 */
void Modbus_Slave_Init(void);
/* 异步发送：复制数据到内部缓存，等待主循环触发发送 */
bool Modbus_Slave_SendAsync(const uint8_t *data, uint16_t len);
/* 周期调用：执行发送及静默计时状态机 */
void Modbus_Slave_Process(void);
/* 读取环形接收缓存 */
uint16_t Modbus_Slave_ReadRx(uint8_t *out, uint16_t maxlen);
/* 状态/空闲判断 */
ModbusSlaveState Modbus_Slave_GetState(void);
bool Modbus_Slave_IsIdle(void);
/* 中断回调：发送完成/接收字节 */
void Modbus_Slave_TxCpltCallback(void);
void Modbus_Slave_RxCallback(uint8_t byte);
/* 获取底层 UART 句柄 */
UART_HandleTypeDef *Modbus_Slave_GetHandle(void);
/* nanoMODBUS RTU transport callbacks */
int32_t Modbus_Slave_NMBS_Read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);
int32_t Modbus_Slave_NMBS_Write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg);

/* 兼容旧宏名 */
#define Modbus3_Init()                 Modbus_Slave_Init()
#define Modbus3_SendAsync(data, len)   Modbus_Slave_SendAsync((data), (len))
#define Modbus3_Task()                 Modbus_Slave_Process()
#define Modbus3_ReadRx(out, maxlen)    Modbus_Slave_ReadRx((out), (maxlen))
#define Modbus3_GetState()             Modbus_Slave_GetState()
#define Modbus3_IsIdle()               Modbus_Slave_IsIdle()
#define Modbus3_TxCpltCallback()       Modbus_Slave_TxCpltCallback()
#define Modbus3_RxCallback(byte)       Modbus_Slave_RxCallback((byte))
#define Modbus3_GetHandle()            Modbus_Slave_GetHandle()

#endif
