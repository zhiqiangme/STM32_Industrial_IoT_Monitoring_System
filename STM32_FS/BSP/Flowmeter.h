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

/**
 * @brief 初始化流量计句柄和采样参数。
 * @param fm: 流量计状态句柄
 * @param htim: 用于外部脉冲计数的定时器句柄
 * @param sample_ms: 采样周期，单位毫秒
 * @param pulses_per_liter: 每升液体对应的脉冲数
 * @param hz_per_lpm: 频率到瞬时流量 L/min 的换算系数
 * @retval 无
 */
void Flowmeter_Init(FlowmeterHandle *fm,
                    TIM_HandleTypeDef *htim,
                    uint32_t sample_ms,
                    float pulses_per_liter,
                    float hz_per_lpm);

/**
 * @brief 到达采样周期后更新频率、瞬时流量和累计流量。
 * @param fm: 流量计状态句柄
 * @param flow_lpm: 输出瞬时流量指针，可为 NULL
 * @param total_l: 输出累计流量指针，可为 NULL
 * @param freq_hz: 输出当前脉冲频率指针，可为 NULL
 * @retval uint8_t: 1 表示本次完成更新，0 表示未到采样时间或句柄无效
 */
uint8_t Flowmeter_Update(FlowmeterHandle *fm,
                         float *flow_lpm,
                         float *total_l,
                         float *freq_hz);

#endif
