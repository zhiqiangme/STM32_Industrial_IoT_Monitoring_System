#include "main.h"

extern TIM_HandleTypeDef g_tim2_handle;

#define PULSES_PER_LITER   660.0f     // 每升水对应的脉冲数 (YF-B7规格)
#define HZ_PER_LPM         11.0f      // 流量(L/min)与频率(Hz)的换算系数

// 定义采样周期 (ms)
#define SAMPLE_PERIOD_MS   1000UL

static FlowMeter_HandleTypeDef g_flow_meter;  // 流量计实例对象

int main(void)
{
    STM32_Init();

    // 开启定时器 (确保已配置为外部时钟模式)
    HAL_TIM_Base_Start(&g_tim2_handle);

    printf("YF-B7 flow sensor demo start...\r\n");

    // 初始化流量计参数
    FlowMeter_Init(&g_flow_meter, &g_tim2_handle,
                   SAMPLE_PERIOD_MS, PULSES_PER_LITER, HZ_PER_LPM);

    while (1)
    {
        // LED 闪烁指示系统运行
        LED_R_TOGGLE();
        delay_ms(500);

        float flow_lpm = 0.0f;  // 瞬时流量 (L/min)
        float total_l = 0.0f;   // 累计流量 (L)
        float freq_hz = 0.0f;   // 实时频率 (Hz)

        // 定期更新并读取流量计数据
        if (FlowMeter_Update(&g_flow_meter, &flow_lpm, &total_l, &freq_hz))
        {
            // 打印调试信息: 频率、瞬时流量、累计流量
            printf("f=%.1f Hz, Flow=%.2f L/min, Total=%.3f L\r\n",
                   freq_hz, flow_lpm, total_l);
        }

       
    }
}
