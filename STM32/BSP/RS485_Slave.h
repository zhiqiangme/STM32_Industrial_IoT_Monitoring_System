#ifndef __RS485_SLAVE_H
#define __RS485_SLAVE_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/*
 * RS485 从站链路（USART2+PD7方向控制）
 * 用途：作为 Modbus 从站与 G780S 网关通信，按帧静默时间切换收发。
 */
#define RS485_SLAVE_BAUDRATE         38400
#define RS485_SLAVE_TX_BUFFER_SIZE   256
#define RS485_SLAVE_RX_BUFFER_SIZE   256
#define RS485_SLAVE_SILENCE_MS       65 /* 必须大于网关的 3.5 字符间隔（约 50ms） */

typedef enum {
    RS485_SLAVE_STATE_IDLE = 0,      /* 空闲，允许装载新帧 */
    RS485_SLAVE_STATE_SENDING,       /* 正在阻塞发送 */
    RS485_SLAVE_STATE_POST_SILENCE   /* 发送完成后静默等待，避免帧黏连 */
} RS485SlaveState;

/* 初始化：打开GPIO/USART2，默认接收模式 */
void RS485_Slave_Init(void);
/* 异步发送：复制数据到内部缓存，等待主循环触发发送 */
bool RS485_Slave_SendAsync(const uint8_t *data, uint16_t len);
/* 周期调用：执行发送及静默计时状态机 */
void RS485_Slave_Process(void);
/* 读取环形接收缓存 */
uint16_t RS485_Slave_ReadRx(uint8_t *out, uint16_t maxlen);
/* 状态/空闲判断 */
RS485SlaveState RS485_Slave_GetState(void);
bool RS485_Slave_IsIdle(void);
/* 中断回调：发送完成/接收字节 */
void RS485_Slave_TxCpltCallback(void);
void RS485_Slave_RxCallback(uint8_t byte);
/* 获取底层 UART 句柄 */
UART_HandleTypeDef *RS485_Slave_GetHandle(void);

/* 兼容旧宏名 */
#define RS4853_Init()                 RS485_Slave_Init()
#define RS4853_SendAsync(data, len)   RS485_Slave_SendAsync((data), (len))
#define RS4853_Task()                 RS485_Slave_Process()
#define RS4853_ReadRx(out, maxlen)    RS485_Slave_ReadRx((out), (maxlen))
#define RS4853_GetState()             RS485_Slave_GetState()
#define RS4853_IsIdle()               RS485_Slave_IsIdle()
#define RS4853_TxCpltCallback()       RS485_Slave_TxCpltCallback()
#define RS4853_RxCallback(byte)       RS485_Slave_RxCallback((byte))
#define RS4853_GetHandle()            RS485_Slave_GetHandle()

#endif
