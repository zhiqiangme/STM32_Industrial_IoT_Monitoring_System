/**
 * @file    rs4853_uart.h
 * @brief   RS485-B上云链路驱动 (USART2 + TP3485)
 * @note    用于STM32与G780S网关通信 (Modbus从站)
 *          硬件连接: PA2(TX), PA3(RX), PD7->DE/RE
 *          波特率: 38400 8N1
 */

#ifndef __RS4853_UART_H
#define __RS4853_UART_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/*----------------------- 配置参数 -----------------------*/
#define RS4853_BAUDRATE         38400
#define RS4853_TX_BUF_SIZE      256     /* 发送缓冲区大小 */
#define RS4853_RX_BUF_SIZE      256     /* 接收环形缓冲区大小 */
#define RS4853_SILENCE_MS       65      /* 静默间隔(ms), 必须>G780S的UARTFT(50ms) */

/*----------------------- 发送状态定义 -----------------------*/
typedef enum {
    RS4853_STATE_IDLE = 0,      /* 空闲,可发送 */
    RS4853_STATE_SENDING,       /* 正在发送 */
    RS4853_STATE_POST_SILENCE   /* 发送完成,等待静默间隔 */
} RS4853_State_t;

/*----------------------- 函数声明 -----------------------*/

/**
 * @brief  初始化RS485-B链路 (USART3 + PF15)
 * @note   配置UART为38400 8N1, 使能RX中断, 初始为接收模式
 */
void RS4853_Init(void);

/**
 * @brief  异步发送数据 (非阻塞)
 * @param  data: 数据指针
 * @param  len: 数据长度
 * @retval true:成功入队, false:队列满或正在发送
 * @note   数据会被复制到内部缓冲区
 */
bool RS4853_SendAsync(const uint8_t* data, uint16_t len);

/**
 * @brief  发送状态机任务 (主循环调用)
 * @note   处理发送完成、静默间隔等状态转换
 *         必须在主循环中频繁调用
 */
void RS4853_Task(void);

/**
 * @brief  从接收缓冲区读取数据
 * @param  out: 输出缓冲区
 * @param  maxlen: 最大读取长度
 * @retval 实际读取的字节数
 */
uint16_t RS4853_ReadRx(uint8_t* out, uint16_t maxlen);

/**
 * @brief  获取当前发送状态
 * @retval RS4853_State_t
 */
RS4853_State_t RS4853_GetState(void);

/**
 * @brief  检查是否可以发送
 * @retval true:空闲可发送, false:忙
 */
bool RS4853_IsIdle(void);

/**
 * @brief  UART发送完成回调 (在stm32f1xx_it.c中调用)
 */
void RS4853_TxCpltCallback(void);

/**
 * @brief  UART接收回调 (在stm32f1xx_it.c中调用)
 * @param  byte: 接收到的字节
 */
void RS4853_RxCallback(uint8_t byte);

/**
 * @brief  获取USART3句柄指针
 * @retval UART_HandleTypeDef指针
 */
UART_HandleTypeDef* RS4853_GetHandle(void);

#endif
