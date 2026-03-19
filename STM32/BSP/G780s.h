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

/* 初始化：打开 GPIO/USART3，默认接收 */
void G780s_Init(void);
/* 周期处理：检测帧间静默并解析完整帧 */
void G780s_Process(void);
/* 兼容旧接口：当前 nanoMODBUS 轮询模式下无需逐字节中断接收 */
void G780s_RxCallback(uint8_t byte);
/* 更新从站寄存器数据（需在关中断状态下写入） */
void G780s_UpdateData(const G780sSlaveData *data);
/* 获取底层 UART 句柄，供中断或测试使用 */
UART_HandleTypeDef *G780s_GetHandle(void);
/* 云端命令：获取继电器翻转控制值 */
uint16_t G780s_GetRelayCtrl(void);
/* 云端命令：获取继电器按位控制值 */
uint16_t G780s_GetRelayBits(void);

#define ModbusSlave_Task()           G780s_Process()
#define ModbusSlave_Init             G780s_Init
#define ModbusSlave_RxCallback       G780s_RxCallback
#define ModbusSlave_UpdateData       G780s_UpdateData
#define ModbusSlave_GetHandle        G780s_GetHandle
#define ModbusSlave_GetRelayCtrl     G780s_GetRelayCtrl
#define ModbusSlave_GetRelayBits     G780s_GetRelayBits

#endif
