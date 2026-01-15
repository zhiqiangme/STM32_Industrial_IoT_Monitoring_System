#ifndef __TEMPERATURE_H
#define __TEMPERATURE_H

#include <stdint.h>

#define PT100_SLAVE_ID      0x01
#define PT100_FUNC_READ     0x04   /* Read Input Registers */
#define PT100_REG_CH4       0x0003 /* Channel 4 register address (0-indexed: CH1=0, CH2=1, CH3=2, CH4=3) */

/* 读取 PT100 温度（单位：℃），成功返回 0，失败返回 1 */
uint8_t Temperature_Read(float *temp_val);

/* 兼容旧宏 */
#define PT100_ReadTemperature Temperature_Read
#define PT100_Read_Temperature(val) Temperature_Read((val))

#endif
