#include "main.h"
#include "bsp_rs485.h"
#include "bsp_pt100.h"
#include "bsp_zsg4.h"
#include "bsp_relay.h"
#include "rs4853_uart.h"
#include "telemetry.h"

extern TIM_HandleTypeDef g_tim2_handle;

#define PULSES_PER_LITER   660.0f
#define HZ_PER_LPM         11.0f
#define SAMPLE_PERIOD_MS   1000UL

static FlowMeter_HandleTypeDef g_flow_meter;

/* 按键状态 (在主循环首次运行时根据实际引脚状态初始化) */
static uint8_t g_key0_last = 1;  /* PE4, 低电平触发 */
static uint8_t g_key1_last = 1;  /* PE3, 低电平触发 */
static uint8_t g_keyA_last = 1;  /* PA0, 高电平触发 - 初始化为1避免启动时误触发 */
static uint8_t g_keys_initialized = 0;

/*----------------------- 遥测全局变量 -----------------------*/
volatile float    g_telemetry_temp   = 0.0f;
volatile int32_t  g_telemetry_weight = 0;
volatile float    g_telemetry_flow   = 0.0f;
volatile float    g_telemetry_total  = 0.0f;
volatile uint16_t g_telemetry_do     = 0;
volatile uint16_t g_telemetry_di     = 0;

/**
 * @brief  检查并处理按键事件
 */
static void Check_Keys(void)
{
    uint8_t key0_now = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4);
    uint8_t key1_now = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3);
    uint8_t keyA_now = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);  /* PA0 */
    
    /* 首次运行时初始化为当前状态，避免启动时误触发 */
    if (!g_keys_initialized)
    {
        g_key0_last = key0_now;
        g_key1_last = key1_now;
        g_keyA_last = keyA_now;
        g_keys_initialized = 1;
        return;
    }
    
    /* KEY0 (PE4) - 下降沿触发 (按下=低) */
    if (g_key0_last == 1 && key0_now == 0)
    {
        printf("[KEY] KEY0 -> CH1\r\n");
        Relay_Toggle_Output(1);
    }
    g_key0_last = key0_now;
    
    /* KEY1 (PE3) - 下降沿触发 (按下=低) */
    if (g_key1_last == 1 && key1_now == 0)
    {
        printf("[KEY] KEY1 -> CH2\r\n");
        Relay_Toggle_Output(2);
    }
    g_key1_last = key1_now;
    
    /* KEY_A (PA0) - 上升沿触发 (按下=高) -> 立即上报 */
    if (g_keyA_last == 0 && keyA_now == 1)
    {
        printf("[KEY] PA0 -> Report\r\n");
        Telemetry_TriggerEventReport();
    }
    g_keyA_last = keyA_now;
}

int main(void)
{
    STM32_Init();
    RS485_Init();       /* Modbus RS485 */
    RS4853_Init();      /* 上云 RS485 */
    HAL_TIM_Base_Start(&g_tim2_handle);

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  STM32 Mill Control System\r\n");
    printf("========================================\r\n");
    printf("PT100:  Addr=1, CH4\r\n");
    printf("Relay:  Addr=2\r\n");
    printf("ZSG4:   Addr=3, CH3\r\n");
    printf("G780S:  USART3 (60s period)\r\n");
    printf("----------------------------------------\r\n");
    printf("KEY0(PE4): CH1  KEY1(PE3): CH2\r\n");
    printf("PA0: Manual Report\r\n");
    printf("========================================\r\n\r\n");

    /* 继电器初始化 */
    printf(">>> Init Relay...\r\n");
    Relay_Batch_Control(0);
    HAL_Delay(100);

    uint16_t coil_mask = 0;
    if (Relay_Read_All_Coils(&coil_mask) == 0)
        printf("Relay DO: 0x%04X\r\n", coil_mask);

    /* 流量计初始化 */
    FlowMeter_Init(&g_flow_meter, &g_tim2_handle,
                   SAMPLE_PERIOD_MS, PULSES_PER_LITER, HZ_PER_LPM);

    /* 遥测初始化 */
    Telemetry_Init();

    printf("\r\n>>> Main loop started...\r\n\r\n");
    
    /* 上电延时后自动上报一次 */
    HAL_Delay(3000);  /* 等待3秒让传感器数据稳定 */
    Telemetry_TriggerEventReport();
    printf("[Startup] First report triggered\r\n");

    uint32_t last_sensor_tick = 0;

    while (1)
    {
        /* ===== 高优先级任务 ===== */
        Check_Keys();
        RS4853_Task();
        Telemetry_Task();
        
        /* LED闪烁 */
        static uint32_t led_tick = 0;
        if (HAL_GetTick() - led_tick >= 500)
        {
            led_tick = HAL_GetTick();
            LED_R_TOGGLE();
        }

        /* ===== 传感器采集 (每2秒) ===== */
        if (HAL_GetTick() - last_sensor_tick >= 2000)
        {
            last_sensor_tick = HAL_GetTick();
            
            uint8_t pt100_ok = 0, zsg4_ok = 0, relay_ok = 0;
            
            /* 流量计 */
            float flow_lpm = 0.0f, total_l = 0.0f, freq_hz = 0.0f;
            if (FlowMeter_Update(&g_flow_meter, &flow_lpm, &total_l, &freq_hz))
            {
                g_telemetry_flow = flow_lpm;
                g_telemetry_total = total_l;
            }
            printf("[FLOW] %.2fL/min, %.3fL\r\n", g_telemetry_flow, g_telemetry_total);
            
            /* 根据流量动态调整上报周期 */
            if (g_telemetry_flow > 0.1f)
            {
                Telemetry_SetPeriod(10000);  /* 有流量: 10秒 */
            }
            else
            {
                Telemetry_SetPeriod(60000);  /* 无流量: 60秒 */
            }
            
            Check_Keys();

            /* PT100温度 */
            float temp_val = 0.0f;
            if (PT100_Read_Temperature(&temp_val) == 0)
            {
                g_telemetry_temp = temp_val;
                printf("[TEMP] %.1fC\r\n", temp_val);
                pt100_ok = 1;
            }
            else
            {
                printf("[TEMP] --\r\n");
            }
            
            Check_Keys();
            delay_ms(30);

            /* ZSG4称重 */
            int32_t weight_g = 0;
            if (ZSG4_Read_Weight(&weight_g) == 0)
            {
                g_telemetry_weight = weight_g;
                printf("[WEIGHT] %ldg\r\n", (long)weight_g);
                zsg4_ok = 1;
            }
            else
            {
                printf("[WEIGHT] --\r\n");
            }
            
            Check_Keys();
            delay_ms(30);
            
            /* 继电器状态 */
            uint16_t do_mask = 0, di_mask = 0;
            if (Relay_Read_All_Coils(&do_mask) == 0)
            {
                g_telemetry_do = do_mask;
                relay_ok = 1;
                
                /* 检测继电器输出变化 */
                Telemetry_CheckRelayChange(do_mask);
            }
            
            delay_ms(30);
            
            if (Relay_Read_Input_Pack(&di_mask) == 0)
            {
                g_telemetry_di = di_mask;
                printf("[IO] DO=0x%04X DI=0x%04X\r\n\r\n", g_telemetry_do, g_telemetry_di);
            }
            
            /* 更新设备状态 */
            Telemetry_UpdateDeviceStatus(pt100_ok, zsg4_ok, relay_ok);
        }
        
        delay_ms(5);
    }
}
