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
        return 0;
    }

    uint32_t now = HAL_GetTick();
    if ((now - fm->last_tick) < fm->sample_ms)
    {
        return 0;
    }

    fm->last_tick += fm->sample_ms;

    uint16_t cur_cnt = (uint16_t)__HAL_TIM_GET_COUNTER(fm->htim);
    uint16_t delta16 = (uint16_t)(cur_cnt - fm->last_cnt);
    fm->last_cnt = cur_cnt;
    fm->total_pulses += (uint32_t)delta16;

    if (freq_hz != NULL)
    {
        *freq_hz = (float)delta16 * (1000.0f / (float)fm->sample_ms);
    }
    if (flow_lpm != NULL)
    {
        float f = (freq_hz != NULL) ? *freq_hz :
                  (float)delta16 * (1000.0f / (float)fm->sample_ms);
        *flow_lpm = (fm->hz_per_lpm != 0.0f) ? (f / fm->hz_per_lpm) : 0.0f;
    }
    if (total_l != NULL)
    {
        *total_l = (fm->pulses_per_liter != 0.0f) ?
                   ((float)fm->total_pulses / fm->pulses_per_liter) : 0.0f;
    }

    return 1;
}
