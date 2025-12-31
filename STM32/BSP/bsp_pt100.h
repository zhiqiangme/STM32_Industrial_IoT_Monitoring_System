#ifndef __BSP_PT100_H
#define __BSP_PT100_H

#include <stdint.h>

#define PT100_SLAVE_ID      0x01
#define PT100_FUNC_READ     0x04   /* Read Input Registers */
#define PT100_REG_CH4       0x0003 /* Channel 4 Register Address (0-indexed: CH1=0, CH2=1, CH3=2, CH4=3) */

/* 读取PT100温度 */
/* 返回值: 0=成功, 1=失败/超时 */
/* temp_val: 输出温度值(摄氏度) */
uint8_t PT100_Read_Temperature(float *temp_val);

#endif
