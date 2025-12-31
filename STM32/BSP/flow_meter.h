#ifndef __FLOW_METER_H
#define __FLOW_METER_H

#include "stm32f1xx_hal.h"


typedef struct
{
    TIM_HandleTypeDef *htim;     // 指向定时器句柄，用于读取计数器值
    uint16_t last_cnt;           // 上一次读取的定时器计数值 (0~65535)
    uint32_t total_pulses;       // 累计总脉冲数 (用于计算总流量)
    uint32_t last_tick;          // 上一次计算的时间戳 (ms)
    uint32_t sample_ms;          // 采样与计算周期 (ms)
    float pulses_per_liter;      // 每升对应的脉冲数 (默认660)
    float hz_per_lpm;            // 频率(Hz)与流量(L/min)的换算系数 (默认11)
} FlowMeter_HandleTypeDef;

/**
 * @brief  初始化流量计结构体
 * @param  fm: 流量计句柄
 * @param  htim: 定时器句柄 (需配置为外部时钟模式)
 * @param  sample_ms: 采样周期 (单位: ms)，例如1000ms
 * @param  pulses_per_liter: 每升水对应的脉冲数 (对于YF-B7通常是660)
 * @param  hz_per_lpm: 频率与流量的换算系数 (对于YF-B7通常是11, 即 F = 11*Q)
 */
void FlowMeter_Init(FlowMeter_HandleTypeDef *fm,
                    TIM_HandleTypeDef *htim,
                    uint32_t sample_ms,
                    float pulses_per_liter,
                    float hz_per_lpm);

/**
 * @brief  定期调用此函数以更新流量数据
 * @param  fm: 流量计句柄
 * @param  flow_lpm: [输出] 瞬时流量 (L/min)
 * @param  total_l:  [输出] 累计流量 (L)
 * @param  freq_hz:  [输出] 当前脉冲频率 (Hz)
 * @return 1:包含有效更新数据; 0:未到采样时间或参数错误
 */
uint8_t FlowMeter_Update(FlowMeter_HandleTypeDef *fm,
                         float *flow_lpm,
                         float *total_l,
                         float *freq_hz);

#endif
