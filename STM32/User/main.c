#include "main.h"
#include "485.h"
#include "bsp_pt100.h"
#include "bsp_zsg4.h"
#include "bsp_relay.h"
#include "rs4853_uart.h"
#include "telemetry.h"
#include "modbus_slave.h"

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

/* Modbus PUSH_SEQ (上报序号) */
static volatile uint16_t g_push_seq = 0;

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
    
    /* KEY_UP (PA0) - 上升沿触发 (按下=高) -> 增加PUSH_SEQ (触发上传) */
    if (g_keyA_last == 0 && keyA_now == 1)
    {
		LED_G_TOGGLE();
        g_push_seq += 5;
        printf("[KEY] PA0 -> PUSH_SEQ +5 (Now: %u)\r\n", g_push_seq);
        
        /* 旧测试代码: 发送HEX数据
        static const uint8_t test_data[] = {0x0A, 0x04, 0x02, 0x01, 0x04, 0x1C, 0xA2};
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_SET);
        for(volatile int i=0; i<500; i++);
        UART_HandleTypeDef *huart = ModbusSlave_GetHandle();
        HAL_UART_Transmit(huart, (uint8_t*)test_data, sizeof(test_data), 100);
        while(__HAL_UART_GET_FLAG(huart, UART_FLAG_TC) == RESET);
        for(volatile int i=0; i<200; i++);
        HAL_GPIO_WritePin(GPIOD, GPIO_PIN_7, GPIO_PIN_RESET);
        */
    }
    g_keyA_last = keyA_now;
}

int main(void)
{
    STM32_Init();
    RS485_Init();       /* 总线1: Modbus主站 (USART3+PA5) */
    ModbusSlave_Init(); /* 总线2: Modbus从站 (USART2+PD7) 供G780S读取 */
    // RS4853_Init();   /* 已禁用: 与ModbusSlave冲突 */
    HAL_TIM_Base_Start(&g_tim2_handle);

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  STM32 Mill Control System\r\n");
    printf("========================================\r\n");
    printf("PT100:  Addr=1, CH4\r\n");
    printf("Relay:  Addr=2\r\n");
    printf("ZSG4:   Addr=3, CH3\r\n");
    printf("G780S:  USART2+PD7 Slave Addr=10\r\n");
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
        ModbusSlave_Task();  /* 响应G780S读取请求 */
        // RS4853_Task();    /* 已禁用 */
        Telemetry_Task();
        
        /* LED闪烁 */
        static uint32_t led_tick = 0;
        if (HAL_GetTick() - led_tick >= 500)
        {
            led_tick = HAL_GetTick();
            LED_R_TOGGLE();
        }
        
        /* ===== 处理云端继电器控制命令 ===== */
        /* 输入值含义: 0=全部关闭, 1-16=翻转对应通道 */
        {
            static uint16_t last_relay_ctrl = 0xFFFF;  /* 初始化为无效值 */
            uint16_t relay_ctrl = ModbusSlave_GetRelayCtrl();
            
            if (relay_ctrl != last_relay_ctrl)
            {
                printf("[Cloud] RELAY_CTRL: %u\r\n", relay_ctrl);
                
                if (relay_ctrl == 0)
                {
                    /* 0 = 全部关闭 */
                    printf("[Cloud] All OFF\r\n");
                    Relay_Set_Output_Mask(0);
                }
                else if (relay_ctrl >= 1 && relay_ctrl <= 16)
                {
                    /* 1-16 = 翻转对应通道 */
                    printf("[Cloud] Toggle CH%u\r\n", relay_ctrl);
                    Relay_Toggle_Output((uint8_t)relay_ctrl);
                }
                else
                {
                    printf("[Cloud] Invalid value: %u\r\n", relay_ctrl);
                }
                
                last_relay_ctrl = relay_ctrl;
            }
        }
        
        /* ===== 处理云端按位控制命令 ===== */
        {
            static uint16_t last_relay_bits = 0;
            uint16_t relay_bits = ModbusSlave_GetRelayBits();
            
            if (relay_bits != last_relay_bits)
            {
                printf("[Cloud] RELAY_BITS: 0x%04X -> 0x%04X\r\n", last_relay_bits, relay_bits);
                
                /* 按位控制继电器输出 */
                Relay_Set_Output_Mask(relay_bits);
                
                last_relay_bits = relay_bits;
            }
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
            
            /* ===== 更新Modbus从站寄存器 (供G780S读取) ===== */
            {
                // static uint16_t s_push_seq = 0;   /* 移除局部变量，使用全局 g_push_seq */
                static int16_t s_last_temp[4] = {0}; /* 上次温度值 (×10) */
                static uint8_t s_first_run = 1;      /* 首次运行标志 */
                ModbusSlaveData_t slave_data = {0};
                
                /* 当前温度值 (×10) */
                int16_t cur_temp[4];
                cur_temp[0] = 0;  /* CH1未连接 */
                cur_temp[1] = 0;  /* CH2未连接 */
                cur_temp[2] = 0;  /* CH3未连接 */
                cur_temp[3] = (int16_t)(g_telemetry_temp * 10.0f);  /* CH4 */
                
                /* 检测温度变化超过1℃ (10×0.1℃) */
                uint8_t temp_changed = 0;
                for (int i = 0; i < 4; i++)
                {
                    int16_t diff = cur_temp[i] - s_last_temp[i];
                    if (diff < 0) diff = -diff;  /* 取绝对值 */
                    if (diff >= 10)  /* 10 = 1.0℃ */
                    {
                        temp_changed = 1;
                        break;
                    }
                }
                
                /* 首次运行或温度变化超过1℃时，递增序号 */
                if (s_first_run || temp_changed)
                {
                    g_push_seq++;
                    /* 更新上次温度值 */
                    for (int i = 0; i < 4; i++)
                        s_last_temp[i] = cur_temp[i];
                    s_first_run = 0;
                    printf("[PUSH_SEQ] %u (temp changed)\r\n", g_push_seq);
                }
                
                /* 地址0: 上报序号 */
                slave_data.push_seq = g_push_seq;
                
                /* PT100温度 */
                slave_data.pt100_ch[0] = cur_temp[0];  /* 地址1: CH1 */
                slave_data.pt100_ch[1] = cur_temp[1];  /* 地址2: CH2 */
                slave_data.pt100_ch[2] = cur_temp[2];  /* 地址3: CH3 */
                slave_data.pt100_ch[3] = cur_temp[3];  /* 地址4: CH4 */
                
                /* ZSG4称重 - 只有CH3连接，其他写-1 (32位格式) */
                slave_data.zsg4_ch[0] = -1;  /* 地址5-6: CH1未连接 */
                slave_data.zsg4_ch[1] = -1;  /* 地址7-8: CH2未连接 */
                slave_data.zsg4_ch[2] = g_telemetry_weight;  /* 地址9-10: CH3 */
                slave_data.zsg4_ch[3] = -1;  /* 地址11-12: CH4未连接 */
                
                /* 流量 (×100 L/min) 和累计 (×1000 L) */
                slave_data.flow_rate = (uint16_t)(g_telemetry_flow * 100.0f);   /* 地址13 */
                slave_data.flow_total = (uint32_t)(g_telemetry_total * 1000.0f); /* 地址14-15 */
                
                /* 继电器 (16位位图) */
                slave_data.relay_do = g_telemetry_do;  /* 地址16 */
                slave_data.relay_di = g_telemetry_di;  /* 地址17 */
                
                /* 状态位 */
                slave_data.status = (pt100_ok ? 0x01 : 0) |   /* 地址18 */
                                    (zsg4_ok ? 0x02 : 0) | 
                                    (relay_ok ? 0x04 : 0);
                
                ModbusSlave_UpdateData(&slave_data);
            }
        }
        
        delay_ms(5);
    }
}
