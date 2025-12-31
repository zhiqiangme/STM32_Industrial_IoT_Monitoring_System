#include "flow_meter.h"


void FlowMeter_Init(FlowMeter_HandleTypeDef *fm,
                    TIM_HandleTypeDef *htim,
                    uint32_t sample_ms,
                    float pulses_per_liter, 
                    float hz_per_lpm)
{
    if (fm == NULL)
    {
        return;
    }

    fm->htim = htim;
    // 获取当前计数值作为初始基准
    fm->last_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(htim);
    fm->total_pulses = 0;
    fm->last_tick = HAL_GetTick();
    fm->sample_ms = sample_ms;
    fm->pulses_per_liter = pulses_per_liter;
    fm->hz_per_lpm = hz_per_lpm;
}


uint8_t FlowMeter_Update(FlowMeter_HandleTypeDef *fm,
                         float *flow_lpm,
                         float *total_l,
                         float *freq_hz)
{
    if (fm == NULL || fm->htim == NULL)
    {
        return 0; // 参数无效
    }

    // 检查是否达到采样周期
    uint32_t now = HAL_GetTick();
    if ((now - fm->last_tick) < fm->sample_ms)
    {
        return 0; // 时间未到，不更新
    }

    // 更新时间戳
    fm->last_tick += fm->sample_ms;

    // 读取当前计数器值 (16位)
    uint16_t cur_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(fm->htim);
    
    // 计算脉冲增量 (自动处理16位计数器溢出回绕)
    uint16_t delta16 = (uint16_t)(cur_cnt - fm->last_cnt);
    fm->last_cnt = cur_cnt;
    
    // 累加到总脉冲数
    fm->total_pulses += (uint32_t)delta16;

    // 1. 计算频率 (Hz) = 脉冲数 / 时间(s)
    // 时间(s) = sample_ms / 1000.0
    if (freq_hz != NULL)
    {
        *freq_hz = (float)delta16 * (1000.0f / (float)fm->sample_ms);
    }
    
    // 2. 计算瞬时流量 (L/min)
    // 公式: Freq = hz_per_lpm * Flow --> Flow = Freq / hz_per_lpm
    if (flow_lpm != NULL)
    {
        float f = (freq_hz != NULL) ? *freq_hz :
                  (float)delta16 * (1000.0f / (float)fm->sample_ms);
        *flow_lpm = (fm->hz_per_lpm != 0.0f) ? (f / fm->hz_per_lpm) : 0.0f;
    }
    
    // 3. 计算累计流量 (L)
    // 公式: Total = TotalPulses / pulses_per_liter
    if (total_l != NULL)
    {
        *total_l = (fm->pulses_per_liter != 0.0f) ?
                   ((float)fm->total_pulses / fm->pulses_per_liter) : 0.0f;
    }

    return 1; // 数据已更新
}
