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

/**
 * @brief 写单个继电器线圈，控制指定通道吸合或断开。
 * @param channel: 继电器通道号，范围 1-16
 * @param on: 目标状态，1 为开，0 为关
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_WriteCoil(uint8_t channel, uint8_t on);

/**
 * @brief 读取单个继电器线圈当前状态。
 * @param channel: 继电器通道号，范围 1-16
 * @param state: 输出状态指针，1 为开，0 为关
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_ReadCoil(uint8_t channel, uint8_t *state);

/**
 * @brief 一次性读取全部 16 路继电器输出状态位。
 * @param mask: 输出位图指针，bit0-bit15 对应 CH1-CH16
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_ReadAllCoils(uint16_t *mask);

/**
 * @brief 读取继电器模块的输入打包寄存器。
 * @param mask: 输出输入位图指针
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_ReadInputPack(uint16_t *mask);

/**
 * @brief 批量控制全部输出通道全开或全关。
 * @param all_on: 1 表示全开，0 表示全关
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_BatchControl(uint8_t all_on);

/**
 * @brief 直接写入输出位图寄存器，按位控制多路继电器输出。
 * @param mask: 目标输出位图
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_SetOutputMask(uint16_t mask);

/**
 * @brief 翻转指定通道的继电器输出状态。
 * @param channel: 继电器通道号，范围 1-16
 * @retval uint8_t: 0 成功，1 失败
 */
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
