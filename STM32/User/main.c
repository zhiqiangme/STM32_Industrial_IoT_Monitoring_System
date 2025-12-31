#include "main.h"
#include "bsp_rs485.h"
#include "bsp_pt100.h"
#include "bsp_zsg4.h"

extern TIM_HandleTypeDef g_tim2_handle;

#define PULSES_PER_LITER   660.0f
#define HZ_PER_LPM         11.0f
#define SAMPLE_PERIOD_MS   1000UL

static FlowMeter_HandleTypeDef g_flow_meter;

int main(void)
{
    STM32_Init();
    RS485_Init();
    HAL_TIM_Base_Start(&g_tim2_handle);

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  STM32 Mill Control System\r\n");
    printf("========================================\r\n");
    printf("PT100:  Addr=1, CH4\r\n");
    printf("Relay:  Addr=2 (16ch I/O)\r\n");
    printf("ZSG4:   Addr=3, CH3\r\n");
    printf("========================================\r\n\r\n");

    /* 1. Read ZSG4 CH3 configuration */
    printf(">>> Step 1: Read ZSG4 CH3 Config\r\n");
    ZSG4_Read_CH3_Config();
    
    /* 2. Write debug defaults */
    printf(">>> Step 2: Write ZSG4 CH3 Defaults\r\n");
    ZSG4_Write_CH3_Defaults();
    
    /* 2b. Write correction coefficient = 1.0 */
    printf(">>> Step 2b: Set Correction Coefficient = 1.0\r\n");
    ZSG4_Write_CH3_Coefficient(1.0f);
    
    /* 3. Read config again to verify */
    printf(">>> Step 3: Verify Config After Write\r\n");
    ZSG4_Read_CH3_Config();
    
    /* 4. Perform Zero Calibration */
    printf(">>> Step 4: Zero Calibration CH3\r\n");
    ZSG4_Zero_Cal_CH3();
    HAL_Delay(500);
    
    /* 5. Scan all channels */
    printf(">>> Step 5: Scan All Channels\r\n");
    ZSG4_Scan_All_Channels();
    
    printf(">>> Diagnostic complete. Starting main loop...\r\n\r\n");

    FlowMeter_Init(&g_flow_meter, &g_tim2_handle,
                   SAMPLE_PERIOD_MS, PULSES_PER_LITER, HZ_PER_LPM);

    while (1)
    {
        LED_R_TOGGLE();
        delay_ms(500);

        /* Flow Meter */
        float flow_lpm = 0.0f;
        float total_l = 0.0f;
        float freq_hz = 0.0f;
        if (FlowMeter_Update(&g_flow_meter, &flow_lpm, &total_l, &freq_hz))
        {
            printf("[FLOW] f=%.1f Hz, Rate=%.2f L/min, Total=%.3f L\r\n",
                   freq_hz, flow_lpm, total_l);
        }

        /* PT100 Temperature */
        float temp_val = 0.0f;
        if (PT100_Read_Temperature(&temp_val) == 0)
            printf("[TEMP] %.1f C\r\n", temp_val);
        else
            printf("[TEMP] Error\r\n");

        /* ZSG4 Weight */
        int32_t weight_g = 0;
        if (ZSG4_Read_Weight(&weight_g) == 0)
            printf("[WEIGHT] %ld g (%.2f kg)\r\n", (long)weight_g, (float)weight_g/1000.0f);
        else
            printf("[WEIGHT] Error\r\n");
    }
}
