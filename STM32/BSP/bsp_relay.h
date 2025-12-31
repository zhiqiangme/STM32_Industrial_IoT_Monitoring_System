/**
 * @file    bsp_relay.h
 * @brief   16路继电器输入输出模块驱动头文件
 * @note    Modbus RTU协议, 从站地址=2, 波特率38400
 *          支持功能码: 01/02/03/04/05/06/0F/10
 */

#ifndef __BSP_RELAY_H
#define __BSP_RELAY_H

#include <stdint.h>

/*----------------------- 基本参数定义 -----------------------*/
#define RELAY_SLAVE_ID      0x02   /* 从站地址 */

/* 功能码定义 */
#define RELAY_FC_READ_COILS         0x01  /* 读线圈状态 */
#define RELAY_FC_READ_DISCRETE      0x02  /* 读离散输入 */
#define RELAY_FC_READ_HOLDING       0x03  /* 读保持寄存器 */
#define RELAY_FC_READ_INPUT         0x04  /* 读输入寄存器 */
#define RELAY_FC_WRITE_COIL         0x05  /* 写单个线圈 */
#define RELAY_FC_WRITE_REGISTER     0x06  /* 写单个保持寄存器 */
#define RELAY_FC_WRITE_COILS        0x0F  /* 写多个线圈 */
#define RELAY_FC_WRITE_REGISTERS    0x10  /* 写多个保持寄存器 */

/*----------------------- 线圈地址 (输出控制) -----------------------*/
/* 线圈地址 0x0000~0x000F 对应输出通道 CH1~CH16 */
/* 写入值: 0xFF00=ON, 0x0000=OFF */
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

/*----------------------- 离散输入地址 -----------------------*/
/* 地址 0x0000~0x000F 对应输入通道 CH1~CH16 (1=触发) */

/*----------------------- 输入寄存器地址 -----------------------*/
/* 0x0000~0x000F: 各通道输入状态 (0/1) */
/* 0x0032 (30051): 输入位打包 CH1~CH16 (bit0=CH1 ... bit15=CH16) */
#define RELAY_REG_INPUT_PACK    0x0032

/*----------------------- 保持寄存器地址 -----------------------*/
#define RELAY_REG_SLAVE_ADDR    0x0032  /* 从站地址 (1~255, 需重启生效) */
#define RELAY_REG_BAUDRATE      0x0033  /* 波特率枚举 (需重启生效) */
#define RELAY_REG_BATCH_CTRL    0x0034  /* 批量控制: 0=全部关闭, 1=全部打开 */
#define RELAY_REG_OUTPUT_MASK   0x0035  /* 输出位掩码 CH1~CH16 */

/*----------------------- 线圈值定义 -----------------------*/
#define RELAY_COIL_ON       0xFF00
#define RELAY_COIL_OFF      0x0000

/*----------------------- 函数声明 -----------------------*/

/**
 * @brief  写单个线圈 (FC05) - 控制单个输出通道
 * @param  channel: 通道号 (1-16)
 * @param  on: 1=开启, 0=关闭
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Write_Coil(uint8_t channel, uint8_t on);

/**
 * @brief  读单个线圈状态 (FC01)
 * @param  channel: 通道号 (1-16)
 * @param  state: 输出状态指针 (0/1)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Read_Coil(uint8_t channel, uint8_t *state);

/**
 * @brief  读所有输出线圈状态 (FC01, 16个)
 * @param  mask: 输出位掩码指针 (bit0=CH1 ... bit15=CH16)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Read_All_Coils(uint16_t *mask);

/**
 * @brief  读输入位打包寄存器 (FC04)
 * @param  mask: 输入位掩码指针 (bit0=CH1 ... bit15=CH16)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Read_Input_Pack(uint16_t *mask);

/**
 * @brief  批量控制所有输出 (FC06)
 * @param  all_on: 1=全部打开, 0=全部关闭
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Batch_Control(uint8_t all_on);

/**
 * @brief  设置输出位掩码 (FC06)
 * @param  mask: 输出位掩码 (bit0=CH1 ... bit15=CH16)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Set_Output_Mask(uint16_t mask);

/**
 * @brief  翻转指定通道输出状态
 * @param  channel: 通道号 (1-16)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Toggle_Output(uint8_t channel);

#endif
