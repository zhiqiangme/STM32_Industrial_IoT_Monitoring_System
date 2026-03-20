#ifndef __G780S_H
#define __G780S_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define MODBUS_SLAVE_ADDR           10

#define REG_PUSH_SEQ                0x0000
#define REG_PT100_CH1               0x0001
#define REG_PT100_CH2               0x0002
#define REG_PT100_CH3               0x0003
#define REG_PT100_CH4               0x0004
#define REG_WEIGHT_CH1_H            0x0005
#define REG_WEIGHT_CH1_L            0x0006
#define REG_WEIGHT_CH2_H            0x0007
#define REG_WEIGHT_CH2_L            0x0008
#define REG_WEIGHT_CH3_H            0x0009
#define REG_WEIGHT_CH3_L            0x000A
#define REG_WEIGHT_CH4_H            0x000B
#define REG_WEIGHT_CH4_L            0x000C
#define REG_FLOW_RATE               0x000D
#define REG_FLOW_TOTAL_HIGH         0x000E
#define REG_FLOW_TOTAL_LOW          0x000F
#define REG_RELAY_CTRL              0x0010
#define REG_RELAY_DO                0x0011
#define REG_RELAY_DI                0x0012
#define REG_SYSTEM_STATUS           0x0013
#define REG_RELAY_BITS              0x0014  /* 继电器状态位图 (只读) */
#define REG_RELAY_CMD_BITS          0x0015  /* 继电器命令位图 (上位机写) */

#define MODBUS_REG_COUNT            22

typedef struct {
    uint16_t push_seq;       /* 上报序号 */
    int16_t  pt100_ch[4];    /* PT100 温度值 ×10 */
    int32_t  weight_ch[4];   /* 称重值，32 位大端，未连接填 -1 */
    uint16_t relay_do;       /* 继电器输出位图 */
    uint16_t relay_di;       /* 继电器输入位图 */
    uint16_t flow_rate;      /* 流量 ×100 */
    uint32_t flow_total;     /* 累计流量 ×1000 */
    uint16_t status;         /* 状态位 */
} G780sSlaveData;

typedef G780sSlaveData ModbusSlaveData_t;

/**
 * @brief 初始化 G780S 业务层和底层 Modbus 从站引擎。
 * @param 无
 * @retval 无
 */
void G780s_Init(void);

/**
 * @brief 在主循环中处理 G780S 对应的 Modbus 从站任务。
 * @param 无
 * @retval 无
 */
void G780s_Process(void);

/**
 * @brief 兼容旧接口的接收字节入口，内部转发给通用从站引擎。
 * @param byte: 串口收到的单个字节
 * @retval 无
 */
void G780s_RxCallback(uint8_t byte);

/**
 * @brief 更新 G780S 业务寄存器镜像。
 * @param data: 最新的业务数据快照
 * @retval 无
 */
void G780s_UpdateData(const G780sSlaveData *data);

/**
 * @brief 获取 G780S 使用的底层 UART 句柄。
 * @param 无
 * @retval UART_HandleTypeDef*: USART3 句柄指针
 */
UART_HandleTypeDef *G780s_GetHandle(void);

/**
 * @brief 获取继电器翻转控制寄存器的当前值。
 * @param 无
 * @retval 当前寄存器值
 */
uint16_t G780s_GetRelayCtrl(void);

/**
 * @brief 获取继电器按位命令位图寄存器的当前值。
 * @param 无
 * @retval 当前寄存器值
 */
uint16_t G780s_GetRelayBits(void);

#define ModbusSlave_Task()           G780s_Process()
#define ModbusSlave_Init             G780s_Init
#define ModbusSlave_RxCallback       G780s_RxCallback
#define ModbusSlave_UpdateData       G780s_UpdateData
#define ModbusSlave_GetHandle        G780s_GetHandle
#define ModbusSlave_GetRelayCtrl     G780s_GetRelayCtrl
#define ModbusSlave_GetRelayBits     G780s_GetRelayBits

#endif
