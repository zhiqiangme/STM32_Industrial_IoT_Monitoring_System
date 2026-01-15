#ifndef __FLOWMETER_H
#define __FLOWMETER_H

#include "stm32f1xx_hal.h"

typedef struct
{
    TIM_HandleTypeDef *htim;   /* 外部时钟计数的定时器句柄 */
    uint16_t last_cnt;         /* 上次读取的计数值 */
    uint32_t total_pulses;     /* 累计脉冲数 */
    uint32_t last_tick;        /* 上次计算时间点 (ms) */
    uint32_t sample_ms;        /* 采样周期 (ms) */
    float pulses_per_liter;    /* 每升水对应脉冲数 */
    float hz_per_lpm;          /* 频率与流量换算系数 */
} FlowmeterHandle;

void Flowmeter_Init(FlowmeterHandle *fm,
                    TIM_HandleTypeDef *htim,
                    uint32_t sample_ms,
                    float pulses_per_liter,
                    float hz_per_lpm);

/* 到达采样周期后计算瞬时流量/累计流量，返回 1 表示已更新 */
uint8_t Flowmeter_Update(FlowmeterHandle *fm,
                         float *flow_lpm,
                         float *total_l,
                         float *freq_hz);

#endif
