/**
 * @file    modbus_slave.h
 * @brief   Modbus RTU从站驱动 (USART2 + PD7)
 * @note    供G780S读取STM32采集的传感器数据
 *          从站地址: 10
 *          波特率: 38400 8N1
 */

#ifndef __MODBUS_SLAVE_H
#define __MODBUS_SLAVE_H

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/*----------------------- 配置参数 -----------------------*/
#define MODBUS_SLAVE_ADDR       10      /* 从站地址 */
#define MODBUS_BAUDRATE         38400
#define MODBUS_RX_BUF_SIZE      64      /* 接收缓冲区大小 */
#define MODBUS_TX_BUF_SIZE      64      /* 发送缓冲区大小 */
#define MODBUS_FRAME_TIMEOUT_MS 10      /* 帧间隔超时(ms), 3.5字符时间 */

/*----------------------- 功能码定义 -----------------------*/
#define FC_READ_INPUT_REGS      0x04    /* 读输入寄存器 */
#define FC_READ_HOLD_REGS       0x03    /* 读保持寄存器 */

/*----------------------- 寄存器地址定义 -----------------------*/
#define REG_PUSH_SEQ            0x0000  /* 上报序号 */
#define REG_PT100_CH1           0x0001  /* PT100通道1温度 (×10) */
#define REG_PT100_CH2           0x0002
#define REG_PT100_CH3           0x0003
#define REG_PT100_CH4           0x0004  /* 当前使用 */
#define REG_WEIGHT_LOW          0x0005  /* 重量优16位 (g) */
#define REG_WEIGHT_HIGH         0x0006  /* 重量高16位 */
#define REG_RELAY_DO            0x0007  /* 继电器输出 */
#define REG_RELAY_DI            0x0008  /* 继电器输入 */
#define REG_FLOW_RATE           0x0009  /* 流量 (×100 L/min) */
#define REG_FLOW_TOTAL_LOW      0x000A  /* 累计流量优16位 (×1000 L) */
#define REG_FLOW_TOTAL_HIGH     0x000B  /* 累计流量高16位 */
#define REG_SYSTEM_STATUS       0x000C  /* 系统状态位 */

#define MODBUS_REG_COUNT        16      /* 寄存器总数 */

/*----------------------- 数据结构 -----------------------*/
typedef struct {
    uint16_t push_seq;          /* 上报序号 */
    int16_t  pt100_ch[4];       /* PT100温度×10 */
    int32_t  weight;            /* 重量(g) */
    uint16_t relay_do;          /* 输出状态 */
    uint16_t relay_di;          /* 输入状态 */
    uint16_t flow_rate;         /* 流量×100 */
    uint32_t flow_total;        /* 累计×1000 */
    uint16_t status;            /* 系统状态 */
} ModbusSlaveData_t;

/*----------------------- API函数 -----------------------*/

/**
 * @brief  初始化Modbus从站
 */
void ModbusSlave_Init(void);

/**
 * @brief  从站任务处理 (主循环调用)
 */
void ModbusSlave_Task(void);

/**
 * @brief  接收回调 (USART2中断调用)
 */
void ModbusSlave_RxCallback(uint8_t byte);

/**
 * @brief  更新寄存器数据
 */
void ModbusSlave_UpdateData(const ModbusSlaveData_t *data);

/**
 * @brief  获取USART2句柄
 */
UART_HandleTypeDef* ModbusSlave_GetHandle(void);

#endif
