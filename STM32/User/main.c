#include "main.h"
#include "bsp_rs485.h"
#include "bsp_pt100.h"
#include "bsp_zsg4.h"

extern TIM_HandleTypeDef g_tim2_handle;

#define PULSES_PER_LITER   660.0f     // Pulses per Liter (YF-B7)
#define HZ_PER_LPM         11.0f      // Hz to L/min conversion factor

// Sample Period (ms)
#define SAMPLE_PERIOD_MS   1000UL

static FlowMeter_HandleTypeDef g_flow_meter;

int main(void)
{
    /* 1. System Init */
    STM32_Init();
    
    /* 2. RS485 Init */
    RS485_Init();

    /* 3. Timer Init for Flow Meter (External Clock Mode) */
    HAL_TIM_Base_Start(&g_tim2_handle);

    printf("STM32 Mill Control System Start...\r\n");
    printf("Flow Meter: YF-B7\r\n");
    printf("Temp Sensor: PT100 Modbus (Addr=1)\r\n");
    printf("Weight Sensor: ZSG4 Modbus (Addr=2)\r\n\r\n");
    
    /* DIAGNOSTIC: Scan all ZSG4 channels once at startup */
    printf(">>> Running ZSG4 diagnostic scan...\r\n");
    ZSG4_Scan_All_Channels();
    printf(">>> Diagnostic complete. Starting main loop...\r\n\r\n");

    /* 4. Flow Meter Init */
    FlowMeter_Init(&g_flow_meter, &g_tim2_handle,
                   SAMPLE_PERIOD_MS, PULSES_PER_LITER, HZ_PER_LPM);

    while (1)
    {
        /* Blink LED to indicate running */
        LED_R_TOGGLE();
        delay_ms(500);

        /* --- Flow Meter Update --- */
        float flow_lpm = 0.0f;
        float total_l = 0.0f;
        float freq_hz = 0.0f;

        if (FlowMeter_Update(&g_flow_meter, &flow_lpm, &total_l, &freq_hz))
        {
            printf("[FLOW] f=%.1f Hz, Rate=%.2f L/min, Total=%.3f L\r\n",
                   freq_hz, flow_lpm, total_l);
        }
        
        /* --- PT100 Temperature Update --- */
        float temp_val = 0.0f;
        if (PT100_Read_Temperature(&temp_val) == 0)
        {
            printf("[TEMP] PT100: %.1f C\r\n", temp_val);
        }
        else
        {
            printf("[TEMP] PT100: Error/Timeout\r\n");
        }
        
        /* --- ZSG4 Weight Update --- */
        int32_t weight_g = 0;
        if (ZSG4_Read_Weight(&weight_g) == 0)
        {
            printf("[WEIGHT] ZSG4 CH3: %ld g (%.2f kg)\r\n", 
                   (long)weight_g, (float)weight_g / 1000.0f);
        }
        else
        {
            printf("[WEIGHT] ZSG4: Error/Timeout\r\n");
        }
    }
}
