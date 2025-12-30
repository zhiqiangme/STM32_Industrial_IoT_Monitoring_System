#include "main.h"


/* TIM2 handle: external clock counter */
extern TIM_HandleTypeDef g_tim2_handle;
/* -------- 参数：按 YF-B7 规格 -------- */
#define PULSES_PER_LITER   660.0f     // 660 脉冲 / L
#define HZ_PER_LPM         11.0f      // f(Hz) = 11 * Q(L/min)

/* 采样周期（ms） */
#define SAMPLE_PERIOD_MS   1000UL

/* 计数相关 */
static uint16_t last_cnt = 0;
static uint32_t total_pulses = 0;
static uint32_t last_tick = 0;


int main(void)
{
	STM32_Init();


    /* 启动 TIM2：外部脉冲驱动计数器 */
    HAL_TIM_Base_Start(&g_tim2_handle);

    printf("YF-B7 flow sensor demo start...\r\n");

    last_tick = HAL_GetTick();
    last_cnt  = tim2_get_cnt();

    while (1)
    {
		//运行提示灯
		LED_R_TOGGLE();
		delay_ms(500);
		
        uint32_t now = HAL_GetTick();
        if ((now - last_tick) >= SAMPLE_PERIOD_MS)
        {
            last_tick += SAMPLE_PERIOD_MS; // 减少长期漂移

            uint16_t cur_cnt = tim2_get_cnt();

            /* 16位计数差分，自动处理回绕（只要采样周期内脉冲数 << 65535 即可） */
            uint16_t delta16 = (uint16_t)(cur_cnt - last_cnt);
            last_cnt = cur_cnt;

            total_pulses += (uint32_t)delta16;

            /* 计算：1秒窗口 -> delta16 近似等于 Hz */
            float freq_hz = (float)delta16 * (1000.0f / (float)SAMPLE_PERIOD_MS);
            float flow_lpm = freq_hz / HZ_PER_LPM;
            float total_l  = (float)total_pulses / PULSES_PER_LITER;

            printf("f=%.1f Hz, Flow=%.2f L/min, Total=%.3f L (pulses=%lu)\r\n",
                   freq_hz, flow_lpm, total_l, (unsigned long)total_pulses);
        }

        /* 这里不做其他联动，保持示例纯粹 */
		

    }
}
