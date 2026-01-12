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
#define MODBUS_BAUDRATE         115200
#define MODBUS_RX_BUF_SIZE      64      /* 接收缓冲区大小 */
#define MODBUS_TX_BUF_SIZE      64      /* 发送缓冲区大小 */
#define MODBUS_FRAME_TIMEOUT_MS 10      /* 帧间隔超时(ms), 3.5字符时间 */

/*----------------------- 功能码定义 -----------------------*/
#define FC_READ_INPUT_REGS      0x04    /* 读输入寄存器 */
#define FC_READ_HOLD_REGS       0x03    /* 读保持寄存器 */
#define FC_WRITE_SINGLE_REG     0x06    /* 写单个寄存器 */
#define FC_WRITE_SINGLE_COIL    0x05    /* 写单个线圈 */

/*----------------------- 寄存器地址定义 -----------------------*/
#define REG_PUSH_SEQ            0x0000  /* 上报序号 */
#define REG_PT100_CH1           0x0001  /* PT100通道1温度 (×10) */
#define REG_PT100_CH2           0x0002
#define REG_PT100_CH3           0x0003
#define REG_PT100_CH4           0x0004  /* 当前使用 */
/* ZSG4: 32位大端模式, 每通道2个寄存器 (HIGH+LOW) */
#define REG_ZSG4_CH1_H          0x0005  /* ZSG4通道1高16位 (g), -1=未连接 */
#define REG_ZSG4_CH1_L          0x0006  /* ZSG4通道1优16位 */
#define REG_ZSG4_CH2_H          0x0007
#define REG_ZSG4_CH2_L          0x0008
#define REG_ZSG4_CH3_H          0x0009  /* 当前使用 */
#define REG_ZSG4_CH3_L          0x000A
#define REG_ZSG4_CH4_H          0x000B
#define REG_ZSG4_CH4_L          0x000C
#define REG_FLOW_RATE           0x000D  /* 流量 (×100 L/min) */
#define REG_FLOW_TOTAL_HIGH     0x000E  /* 累计流量高16位 (×1000 L) */
#define REG_FLOW_TOTAL_LOW      0x000F  /* 累计流量低16位 */
#define REG_RELAY_CTRL          0x0010  /* 继电器控制 - 可写 (0=全关,1-16=翻转) */
#define REG_RELAY_DO            0x0011  /* 继电器输出 (16位位图) - 只读 */
#define REG_RELAY_DI            0x0012  /* 继电器输入 (16位位图) - 只读 */
#define REG_SYSTEM_STATUS       0x0013  /* 系统状态位 - 只读 */
#define REG_RELAY_BITS          0x0014  /* 继电器按位控制 - 可写 */

#define MODBUS_REG_COUNT        22      /* 寄存器总数 */

/*----------------------- 数据结构 -----------------------*/
typedef struct {
    uint16_t push_seq;          /* 上报序号 */
    int16_t  pt100_ch[4];       /* PT100温度×10 */
    int32_t  zsg4_ch[4];        /* ZSG4称重(g), -1=未连接 */
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

/**
 * @brief  获取继电器控制值 (云端下发)
 * @return 16位位图，bit0=CH1, bit1=CH2, ...
 */
uint16_t ModbusSlave_GetRelayCtrl(void);

/**
 * @brief  获取继电器按位控制值 (云端按位下发)
 * @return 16位位图
 */
uint16_t ModbusSlave_GetRelayBits(void);

#endif
