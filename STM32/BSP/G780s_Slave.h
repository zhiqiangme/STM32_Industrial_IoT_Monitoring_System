#ifndef __G780S_SLAVE_H
#define __G780S_SLAVE_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define MODBUS_SLAVE_ADDR           10
#define MODBUS_BAUDRATE             115200
#define MODBUS_RX_BUF_SIZE          64
#define MODBUS_TX_BUF_SIZE          64
#define MODBUS_FRAME_TIMEOUT_MS     10

#define FC_READ_INPUT_REGS          0x04
#define FC_READ_HOLD_REGS           0x03
#define FC_WRITE_SINGLE_REG         0x06
#define FC_WRITE_SINGLE_COIL        0x05

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
#define REG_RELAY_BITS              0x0014

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

/* 初始化：打开 GPIO/USART2，默认接收，启用 RX 中断 */
void G780sSlave_Init(void);
/* 周期处理：检测帧间静默并解析完整帧 */
void G780sSlave_Process(void);
/* USART2 RX 中断回调，逐字节缓存 */
void G780sSlave_RxCallback(uint8_t byte);
/* 更新从站寄存器数据（需在关中断状态下写入） */
void G780sSlave_UpdateData(const G780sSlaveData *data);
/* 获取底层 UART 句柄，供中断或测试使用 */
UART_HandleTypeDef *G780sSlave_GetHandle(void);
/* 云端命令：获取继电器翻转控制值 */
uint16_t G780sSlave_GetRelayCtrl(void);
/* 云端命令：获取继电器按位控制值 */
uint16_t G780sSlave_GetRelayBits(void);

#define ModbusSlave_Task()           G780sSlave_Process()
#define ModbusSlave_Init             G780sSlave_Init
#define ModbusSlave_RxCallback       G780sSlave_RxCallback
#define ModbusSlave_UpdateData       G780sSlave_UpdateData
#define ModbusSlave_GetHandle        G780sSlave_GetHandle
#define ModbusSlave_GetRelayCtrl     G780sSlave_GetRelayCtrl
#define ModbusSlave_GetRelayBits     G780sSlave_GetRelayBits

#endif
