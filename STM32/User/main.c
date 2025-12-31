#include "main.h"
#include "bsp_rs485.h"
#include "bsp_pt100.h"
#include "bsp_zsg4.h"
#include "bsp_relay.h"

extern TIM_HandleTypeDef g_tim2_handle;

#define PULSES_PER_LITER   660.0f
#define HZ_PER_LPM         11.0f
#define SAMPLE_PERIOD_MS   1000UL

static FlowMeter_HandleTypeDef g_flow_meter;

/* 按键状态记录 (用于边沿检测) */
static uint8_t g_key0_last = 1;
static uint8_t g_key1_last = 1;

/**
 * @brief  检查并处理按键事件
 * @note   在主循环中频繁调用以保证响应
 */
static void Check_Keys(void)
{
    uint8_t key0_now = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4);
    uint8_t key1_now = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3);
    
    /* KEY0 下降沿检测 */
    if (g_key0_last == 1 && key0_now == 0)
    {
        printf("[KEY] KEY0 -> CH1翻转\r\n");
        Relay_Toggle_Output(1);
    }
    g_key0_last = key0_now;
    
    /* KEY1 下降沿检测 */
    if (g_key1_last == 1 && key1_now == 0)
    {
        printf("[KEY] KEY1 -> CH2翻转\r\n");
        Relay_Toggle_Output(2);
    }
    g_key1_last = key1_now;
}

int main(void)
{
    STM32_Init();
    RS485_Init();
    HAL_TIM_Base_Start(&g_tim2_handle);

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  STM32 Mill Control System\r\n");
    printf("========================================\r\n");
    printf("PT100:  Addr=1, CH4 (温度)\r\n");
    printf("Relay:  Addr=2 (16路继电器)\r\n");
    printf("ZSG4:   Addr=3, CH3 (称重)\r\n");
    printf("----------------------------------------\r\n");
    printf("KEY0(PE4): CH1  KEY1(PE3): CH2\r\n");
    printf("========================================\r\n\r\n");

    /* 继电器初始化 - 全部关闭 */
    printf(">>> 继电器初始化...\r\n");
    Relay_Batch_Control(0);
    HAL_Delay(100);

    uint16_t coil_mask = 0;
    if (Relay_Read_All_Coils(&coil_mask) == 0)
        printf("输出状态: 0x%04X\r\n", coil_mask);

    FlowMeter_Init(&g_flow_meter, &g_tim2_handle,
                   SAMPLE_PERIOD_MS, PULSES_PER_LITER, HZ_PER_LPM);

    printf("\r\n>>> 主循环开始...\r\n\r\n");

    uint32_t last_sensor_tick = 0;

    while (1)
    {
        /* ===== 按键检测 (高优先级, 频繁检查) ===== */
        Check_Keys();
        
        /* LED闪烁 */
        static uint32_t led_tick = 0;
        if (HAL_GetTick() - led_tick >= 500)
        {
            led_tick = HAL_GetTick();
            LED_R_TOGGLE();
        }

        /* ===== 传感器采集 (每2秒, 降低频率减少阻塞) ===== */
        if (HAL_GetTick() - last_sensor_tick >= 2000)
        {
            last_sensor_tick = HAL_GetTick();
            
            /* 流量计 (本地计算,不阻塞) */
            float flow_lpm = 0.0f, total_l = 0.0f, freq_hz = 0.0f;
            if (FlowMeter_Update(&g_flow_meter, &flow_lpm, &total_l, &freq_hz))
            {
                if (freq_hz > 0.1f)  /* 有流量才显示 */
                    printf("[FLOW] %.1fHz, %.2fL/min, %.3fL\r\n", freq_hz, flow_lpm, total_l);
            }
            
            Check_Keys();  /* 中间检查按键 */

            /* PT100温度 */
            float temp_val = 0.0f;
            if (PT100_Read_Temperature(&temp_val) == 0)
                printf("[TEMP] %.1fC\r\n", temp_val);
            
            Check_Keys();  /* 中间检查按键 */

            /* ZSG4称重 */
            int32_t weight_g = 0;
            if (ZSG4_Read_Weight(&weight_g) == 0)
                printf("[WEIGHT] %ldg\r\n", (long)weight_g);
            
            Check_Keys();  /* 中间检查按键 */
            delay_ms(20);  /* 总线间隔延时 */
            
            /* 继电器输入状态 */
            uint16_t input_mask = 0;
            if (Relay_Read_Input_Pack(&input_mask) == 0)
            {
                printf("[INPUT] 0x%04X\r\n", input_mask);
            }
            /* 超时不显示错误，避免刷屏 */
        }
        
        /* 小延时避免CPU空转 */
        delay_ms(10);
    }
}
