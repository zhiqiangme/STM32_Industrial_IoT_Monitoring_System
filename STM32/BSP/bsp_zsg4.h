/**
 * @file    bsp_zsg4.h
 * @brief   ZSG4称重变送器驱动头文件
 * @note    Modbus RTU协议, 从站地址=3, 波特率38400
 */

#ifndef __BSP_ZSG4_H
#define __BSP_ZSG4_H

#include <stdint.h>

/*----------------------- 基本参数定义 -----------------------*/
#define ZSG4_SLAVE_ID       0x03   /* 从站地址 */
#define ZSG4_FUNC_READ      0x03   /* 读保持寄存器 */
#define ZSG4_FUNC_WRITE     0x06   /* 写单个保持寄存器 */

/*----------------------- 重量值寄存器地址 -----------------------*/
/* 每个通道占用2个寄存器(int32有符号, ABCD大端) */
#define ZSG4_REG_CH1_WEIGHT 0x0000 /* 通道1: 0x0000~0x0001 (PLC 40001~40002) */
#define ZSG4_REG_CH2_WEIGHT 0x0002 /* 通道2: 0x0002~0x0003 (PLC 40003~40004) */
#define ZSG4_REG_CH3_WEIGHT 0x0004 /* 通道3: 0x0004~0x0005 (PLC 40005~40006) */
#define ZSG4_REG_CH4_WEIGHT 0x0006 /* 通道4: 0x0006~0x0007 (PLC 40007~40008) */

/*----------------------- 操作寄存器地址 -----------------------*/
#define ZSG4_REG_TARE       0x0034 /* 去皮触发 (PLC 40053) */
#define ZSG4_REG_ZERO_CAL   0x0035 /* 零点校准触发 (PLC 40054) */

/*----------------------- 通道3配置寄存器 -----------------------*/
#define ZSG4_REG_TRACK_ZERO_CH3  0x0038  /* 跟踪零点范围 (0-100, 掉电不保存) */
#define ZSG4_REG_DIVISION_CH3    0x0040  /* 分度值 (1/2/5/10/20/50/100, 掉电不保存) */
#define ZSG4_REG_SAMPLE_RATE_CH3 0x0045  /* 采样率 (1=10Hz, 2=40Hz, 掉电不保存) */
#define ZSG4_REG_COEFF_CH3       0x0068  /* 修正系数 (float32, 2个寄存器) */

/*----------------------- 通道操作值 -----------------------*/
#define ZSG4_CH3_ACTION     0x0003 /* 通道3的操作值 */

/*----------------------- 函数声明 -----------------------*/

/* 读取指定通道重量 (1-4) */
uint8_t ZSG4_Read_Weight_Channel(uint8_t channel, int32_t *weight_g);

/* 读取通道3重量 (快捷函数) */
uint8_t ZSG4_Read_Weight(int32_t *weight_g);

/* 扫描所有通道 (诊断用) */
void ZSG4_Scan_All_Channels(void);

/* 读取通道3配置参数 */
void ZSG4_Read_CH3_Config(void);

/* 写入通道3默认配置 */
void ZSG4_Write_CH3_Defaults(void);

/* 设置通道3修正系数 */
void ZSG4_Write_CH3_Coefficient(float coeff);

/* 通道3去皮 (将当前重量设为零) */
uint8_t ZSG4_Tare_CH3(void);

/* 通道3零点校准 */
uint8_t ZSG4_Zero_Cal_CH3(void);

/* 读取单个保持寄存器 */
uint8_t ZSG4_Read_Register(uint16_t reg_addr, uint16_t *value);

/* 写入单个保持寄存器 */
uint8_t ZSG4_Write_Register(uint16_t reg_addr, uint16_t value);

#endif
