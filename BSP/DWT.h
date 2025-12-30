#ifndef __DWT_H
#define __DWT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx.h"

void DWT_Init(void);    // 初始化DWT计数器，使用下列函数前必须调用
uint32_t DWT_GetUs(uint32_t cycles);   // 获取当前系统运行时间（微秒）
uint32_t DWT_GetTick(void);	// 获取当前时间戳

void delay_us(uint32_t us);  // 微秒级延时（1μs精度，72MHz下为72个时钟周期）
void delay_ms(uint16_t ms);  // 毫秒级延时（1ms = 1000μs）
void delay_s(uint32_t s);    // 秒级延时（1s = 1000ms）


#ifdef __cplusplus
}
#endif

#endif /* __DWT_H */
