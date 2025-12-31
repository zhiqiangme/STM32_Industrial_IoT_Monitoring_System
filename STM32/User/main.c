#include "main.h"


/* TIM2 handle: external clock counter */
extern TIM_HandleTypeDef g_tim2_handle;
/* -------- 参数：按 YF-B7 规格 -------- */
#define PULSES_PER_LITER   660.0f     // 660 脉冲 / L
#define HZ_PER_LPM         11.0f      // f(Hz) = 11 * Q(L/min)

/* 采样周期（ms） */
#define SAMPLE_PERIOD_MS   1000UL

static FlowMeter_HandleTypeDef g_flow_meter;




int main(void)
{
	STM32_Init();


    /* 启动 TIM2：外部脉冲驱动计数器 */
    HAL_TIM_Base_Start(&g_tim2_handle);

    printf("YF-B7 flow sensor demo start...\r\n");

    FlowMeter_Init(&g_flow_meter, &g_tim2_handle,
                   SAMPLE_PERIOD_MS, PULSES_PER_LITER, HZ_PER_LPM);



    while (1)
    {
		//运行提示灯
		LED_R_TOGGLE();
		delay_ms(500);
		
        float flow_lpm = 0.0f;
        float total_l = 0.0f;
        float freq_hz = 0.0f;

        if (FlowMeter_Update(&g_flow_meter, &flow_lpm, &total_l, &freq_hz))
        {
            printf("f=%.1f Hz, Flow=%.2f L/min, Total=%.3f L\r\n",
                   freq_hz, flow_lpm, total_l);
        }


        /* 这里不做其他联动，保持示例纯粹 */
		

    }
}
