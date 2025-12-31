#ifndef __FLOW_METER_H
#define __FLOW_METER_H

#include "stm32f1xx_hal.h"

typedef struct
{
    TIM_HandleTypeDef *htim;
    uint16_t last_cnt;
    uint32_t total_pulses;
    uint32_t last_tick;
    uint32_t sample_ms;
    float pulses_per_liter;
    float hz_per_lpm;
} FlowMeter_HandleTypeDef;

void FlowMeter_Init(FlowMeter_HandleTypeDef *fm,
                    TIM_HandleTypeDef *htim,
                    uint32_t sample_ms,
                    float pulses_per_liter,
                    float hz_per_lpm);

uint8_t FlowMeter_Update(FlowMeter_HandleTypeDef *fm,
                         float *flow_lpm,
                         float *total_l,
                         float *freq_hz);

#endif
