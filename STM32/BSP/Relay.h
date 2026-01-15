#ifndef __RELAY_H
#define __RELAY_H

#include <stdint.h>

#define RELAY_SLAVE_ID              0x02

#define RELAY_FC_READ_COILS         0x01
#define RELAY_FC_READ_DISCRETE      0x02
#define RELAY_FC_READ_HOLDING       0x03
#define RELAY_FC_READ_INPUT         0x04
#define RELAY_FC_WRITE_COIL         0x05
#define RELAY_FC_WRITE_REGISTER     0x06
#define RELAY_FC_WRITE_COILS        0x0F
#define RELAY_FC_WRITE_REGISTERS    0x10

#define RELAY_COIL_CH1      0x0000
#define RELAY_COIL_CH2      0x0001
#define RELAY_COIL_CH3      0x0002
#define RELAY_COIL_CH4      0x0003
#define RELAY_COIL_CH5      0x0004
#define RELAY_COIL_CH6      0x0005
#define RELAY_COIL_CH7      0x0006
#define RELAY_COIL_CH8      0x0007
#define RELAY_COIL_CH9      0x0008
#define RELAY_COIL_CH10     0x0009
#define RELAY_COIL_CH11     0x000A
#define RELAY_COIL_CH12     0x000B
#define RELAY_COIL_CH13     0x000C
#define RELAY_COIL_CH14     0x000D
#define RELAY_COIL_CH15     0x000E
#define RELAY_COIL_CH16     0x000F

#define RELAY_REG_INPUT_PACK    0x0032
#define RELAY_REG_SLAVE_ADDR    0x0032
#define RELAY_REG_BAUDRATE      0x0033
#define RELAY_REG_BATCH_CTRL    0x0034
#define RELAY_REG_OUTPUT_MASK   0x0035

#define RELAY_COIL_ON       0xFF00
#define RELAY_COIL_OFF      0x0000

/* 写单个线圈（1-16），on=1 开，0 关 */
uint8_t Relay_WriteCoil(uint8_t channel, uint8_t on);
/* 读单个线圈状态 */
uint8_t Relay_ReadCoil(uint8_t channel, uint8_t *state);
/* 读全部输出位（16 位掩码） */
uint8_t Relay_ReadAllCoils(uint16_t *mask);
/* 读输入打包寄存器（16 位掩码） */
uint8_t Relay_ReadInputPack(uint16_t *mask);
/* 批量控制：all_on=1 全开，0 全关 */
uint8_t Relay_BatchControl(uint8_t all_on);
/* 按位设置输出掩码 */
uint8_t Relay_SetOutputMask(uint16_t mask);
/* 翻转指定通道输出 */
uint8_t Relay_ToggleOutput(uint8_t channel);

/* 兼容旧宏名 */
#define RELAY_COIL_CH1      0x0000
#define RELAY_COIL_CH2      0x0001
#define RELAY_COIL_CH3      0x0002
#define RELAY_COIL_CH4      0x0003
#define RELAY_COIL_CH5      0x0004
#define RELAY_COIL_CH6      0x0005
#define RELAY_COIL_CH7      0x0006
#define RELAY_COIL_CH8      0x0007
#define RELAY_COIL_CH9      0x0008
#define RELAY_COIL_CH10     0x0009
#define RELAY_COIL_CH11     0x000A
#define RELAY_COIL_CH12     0x000B
#define RELAY_COIL_CH13     0x000C
#define RELAY_COIL_CH14     0x000D
#define RELAY_COIL_CH15     0x000E
#define RELAY_COIL_CH16     0x000F

#endif
