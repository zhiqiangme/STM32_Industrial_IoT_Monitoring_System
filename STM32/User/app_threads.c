#include "app_threads.h"

#include "main.h"
#include "BusService.h"
#include "G780s.h"
#include "LogService.h"
#include "Modbus_Master.h"
#include "Relay.h"
#include "Temperature.h"
#include "Upgrade.h"
#include "Weight.h"

extern TIM_HandleTypeDef g_tim2_handle;

#define APP_IWDG_RELOAD                         1875u
#define APP_MAINT_THREAD_PRIORITY               6u
#define APP_SENSOR_THREAD_PRIORITY              8u
#define APP_LOGGER_THREAD_PRIORITY              12u
#define APP_MAINT_THREAD_STACK_SIZE             1536u
#define APP_SENSOR_THREAD_STACK_SIZE            2560u
#define APP_LOGGER_THREAD_STACK_SIZE            2048u
#define APP_THREAD_TIMESLICE                    20u
#define APP_MAINT_PERIOD_MS                     10u
#define APP_SENSOR_LOOP_DELAY_MS                20u
#define APP_SENSOR_RETRY_DELAY_MS               30u
#define APP_STARTUP_SENSOR_SETTLE_MS            3000u
#define APP_LED_BLINK_PERIOD_MS                 500u
#define APP_UPGRADE_CONFIRM_DELAY_MS            60000u
#define APP_UPGRADE_CONFIRM_RETRY_MS            5000u
#define APP_UPGRADE_CONFIRM_MIN_HEALTHY_SAMPLES 3u
#define APP_HEALTH_MAILBOX_SIZE                 8u

/* 线程间共享的运行期状态：配置快照和 PUSH_SEQ。 */
typedef struct
{
    struct rt_mutex lock;
    G780sRemoteConfig runtime_config;
    uint16_t push_seq;
    rt_bool_t ready;
} AppSharedState;

typedef struct
{
    uint8_t initialized;
    uint8_t key0_last;
    uint8_t key1_last;
    uint8_t keya_last;
} AppKeyState;

/* DI 去抖状态只归 sensor 线程维护，避免多线程同时操作输入边沿。 */
typedef struct
{
    uint8_t initialized;
    uint16_t last_raw;
    uint16_t stable;
    uint32_t last_change[16];
} AppDiState;

static struct rt_thread s_maint_thread;
static struct rt_thread s_sensor_thread;
static struct rt_thread s_logger_thread;
static rt_uint8_t s_maint_stack[APP_MAINT_THREAD_STACK_SIZE];
static rt_uint8_t s_sensor_stack[APP_SENSOR_THREAD_STACK_SIZE];
static rt_uint8_t s_logger_stack[APP_LOGGER_THREAD_STACK_SIZE];

static struct rt_mailbox s_health_mailbox;
static rt_ubase_t s_health_mailbox_pool[APP_HEALTH_MAILBOX_SIZE];

static AppSharedState s_app_state;
static FlowmeterHandle s_flow_meter;
static IWDG_HandleTypeDef s_iwdg_handle = {0};
static uint8_t s_iwdg_ready = 0u;

static float App_ConfigToFloat2(uint32_t value_x100)
{
    return ((float)value_x100) / 100.0f;
}

static rt_bool_t App_IsManualMode(const G780sRemoteConfig *config)
{
    return (config != RT_NULL && config->control_mode == G780S_MODE_MANUAL) ? RT_TRUE : RT_FALSE;
}

static void App_StateSetRuntimeConfig(const G780sRemoteConfig *config)
{
    if (config == RT_NULL || s_app_state.ready == RT_FALSE)
    {
        return;
    }

    if (rt_mutex_take(&s_app_state.lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        /* 生效配置只保存一份，其他线程通过快照读取。 */
        s_app_state.runtime_config = *config;
        (void)rt_mutex_release(&s_app_state.lock);
    }
}

static void App_StateGetRuntimeConfig(G780sRemoteConfig *config)
{
    if (config == RT_NULL || s_app_state.ready == RT_FALSE)
    {
        return;
    }

    if (rt_mutex_take(&s_app_state.lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        *config = s_app_state.runtime_config;
        (void)rt_mutex_release(&s_app_state.lock);
    }
}

static uint16_t App_StateUpdatePushSeq(uint16_t delta)
{
    uint16_t push_seq = 0u;

    if (s_app_state.ready == RT_FALSE)
    {
        return 0u;
    }

    if (rt_mutex_take(&s_app_state.lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        /* PUSH_SEQ 统一在共享状态中递增，避免多个线程直接改全局变量。 */
        s_app_state.push_seq = (uint16_t)(s_app_state.push_seq + delta);
        push_seq = s_app_state.push_seq;
        (void)rt_mutex_release(&s_app_state.lock);
    }

    return push_seq;
}

static uint16_t App_StateGetPushSeq(void)
{
    uint16_t push_seq = 0u;

    if (s_app_state.ready == RT_FALSE)
    {
        return 0u;
    }

    if (rt_mutex_take(&s_app_state.lock, RT_WAITING_FOREVER) == RT_EOK)
    {
        push_seq = s_app_state.push_seq;
        (void)rt_mutex_release(&s_app_state.lock);
    }

    return push_seq;
}

static void App_IwdgInit(void)
{
    s_iwdg_handle.Instance = IWDG;
    s_iwdg_handle.Init.Prescaler = IWDG_PRESCALER_256;
    s_iwdg_handle.Init.Reload = APP_IWDG_RELOAD;

    if (HAL_IWDG_Init(&s_iwdg_handle) == HAL_OK)
    {
        s_iwdg_ready = 1u;
        printf("[IWDG] enabled (reload=%u)\r\n", (unsigned int)APP_IWDG_RELOAD);
    }
    else
    {
        s_iwdg_ready = 0u;
        printf("[IWDG] init failed\r\n");
    }
}

static void App_IwdgKick(void)
{
    if (s_iwdg_ready != 0u)
    {
        if (HAL_IWDG_Refresh(&s_iwdg_handle) != HAL_OK)
        {
            printf("[IWDG] refresh failed\r\n");
        }
    }
}

static void App_PrintBanner(void)
{
    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  STM32 Mill Control System\r\n");
    printf("========================================\r\n");
    printf("PT100:  Addr=1, CH4\r\n");
    printf("Relay:  Addr=2\r\n");
    printf("Weight: Addr=3, CH3\r\n");
    printf("G780S:  USART3+PA5 Slave Addr=10\r\n");
    printf("----------------------------------------\r\n");
    printf("KEY0(PE4): CH1  KEY1(PE3): CH2\r\n");
    printf("PA0: PUSH_SEQ +5\r\n");
    printf("========================================\r\n\r\n");
}

static void App_ApplyLatestConfig(FlowmeterHandle *flow_meter,
                                  G780sRemoteConfig *runtime_config,
                                  uint32_t *last_config_seq)
{
    G780sRemoteConfig latest_config;

    G780s_GetActiveConfig(&latest_config);
    if (runtime_config == RT_NULL || last_config_seq == RT_NULL)
    {
        return;
    }

    if (latest_config.sequence == *last_config_seq)
    {
        return;
    }

    *runtime_config = latest_config;
    *last_config_seq = latest_config.sequence;
    App_StateSetRuntimeConfig(runtime_config);

    if (flow_meter != RT_NULL)
    {
        /* 流量计换算参数支持热更新，不需要重启线程或设备。 */
        flow_meter->sample_ms = runtime_config->flow_sample_period_ms;
        flow_meter->pulses_per_liter = App_ConfigToFloat2(runtime_config->pulses_per_liter_x100);
        flow_meter->hz_per_lpm = App_ConfigToFloat2(runtime_config->hz_per_lpm_x100);
    }

    printf("[CFG] Applied seq=%lu mode=%s sensor=%ums flow_sample=%ums debounce=%ums temp_th=%.1fC ppl=%.2f hz_per_lpm=%.2f\r\n",
           (unsigned long)runtime_config->sequence,
           (runtime_config->control_mode == G780S_MODE_AUTO) ? "AUTO" : "MANUAL",
           runtime_config->sensor_period_ms,
           runtime_config->flow_sample_period_ms,
           runtime_config->di_debounce_ms,
           runtime_config->temp_change_threshold_x10 / 10.0f,
           App_ConfigToFloat2(runtime_config->pulses_per_liter_x100),
           App_ConfigToFloat2(runtime_config->hz_per_lpm_x100));
}

static void App_HandleKeys(const G780sRemoteConfig *runtime_config, AppKeyState *state)
{
    uint8_t key0_now;
    uint8_t key1_now;
    uint8_t keya_now;

    if (state == RT_NULL)
    {
        return;
    }

    key0_now = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_4);
    key1_now = HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_3);
    keya_now = HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_0);

    if (state->initialized == 0u)
    {
        state->key0_last = key0_now;
        state->key1_last = key1_now;
        state->keya_last = keya_now;
        state->initialized = 1u;
        return;
    }

    if (state->key0_last == 1u && key0_now == 0u)
    {
        if (App_IsManualMode(runtime_config) != RT_FALSE)
        {
            printf("[KEY] KEY0 -> CH1\r\n");
            (void)Relay_ToggleOutput(1);
        }
        else
        {
            printf("[KEY] KEY0 ignored in AUTO mode\r\n");
        }
    }
    state->key0_last = key0_now;

    if (state->key1_last == 1u && key1_now == 0u)
    {
        if (App_IsManualMode(runtime_config) != RT_FALSE)
        {
            printf("[KEY] KEY1 -> CH2\r\n");
            (void)Relay_ToggleOutput(2);
        }
        else
        {
            printf("[KEY] KEY1 ignored in AUTO mode\r\n");
        }
    }
    state->key1_last = key1_now;

    if (state->keya_last == 0u && keya_now == 1u)
    {
        uint16_t push_seq = App_StateUpdatePushSeq(5u);

        /* PA0 继续保留为调试入口，手动推进上报序号。 */
        LED_G_TOGGLE();
        printf("[KEY] PA0 -> PUSH_SEQ +5 (Now: %u)\r\n", push_seq);
    }
    state->keya_last = keya_now;
}

static void App_HandleRelayInputs(uint16_t di_mask,
                                  const G780sRemoteConfig *runtime_config,
                                  AppDiState *state)
{
    uint32_t debounce_ms;
    uint32_t now;
    int i;

    if (runtime_config == RT_NULL || state == RT_NULL)
    {
        return;
    }

    debounce_ms = runtime_config->di_debounce_ms;
    now = HAL_GetTick();

    if (state->initialized == 0u)
    {
        state->last_raw = di_mask;
        state->stable = di_mask;
        for (i = 0; i < 16; i++)
        {
            state->last_change[i] = now;
        }
        state->initialized = 1u;
        return;
    }

    for (i = 0; i < 16; i++)
    {
        uint16_t bit = (uint16_t)(1u << i);
        uint8_t raw = (di_mask & bit) ? 1u : 0u;
        uint8_t last_raw = (state->last_raw & bit) ? 1u : 0u;
        uint8_t stable = (state->stable & bit) ? 1u : 0u;

        if (raw != last_raw)
        {
            state->last_raw = raw ? (uint16_t)(state->last_raw | bit) : (uint16_t)(state->last_raw & (~bit));
            state->last_change[i] = now;
        }

        if (raw != stable && (now - state->last_change[i]) >= debounce_ms)
        {
            state->stable = raw ? (uint16_t)(state->stable | bit) : (uint16_t)(state->stable & (~bit));

            /* 输入上升沿在手动模式下翻转对应输出，沿用原有现场联动逻辑。 */
            if (raw != 0u && App_IsManualMode(runtime_config) != RT_FALSE)
            {
                (void)Relay_ToggleOutput((uint8_t)(i + 1));
            }
        }
    }
}

static void App_UpdateSlaveData(float sensor_temp,
                                int32_t sensor_weight,
                                float flow_rate_lpm,
                                float flow_total_l,
                                uint16_t relay_do_state,
                                uint16_t relay_di_state,
                                const G780sRemoteConfig *runtime_config,
                                uint8_t pt100_ok,
                                uint8_t weight_ok,
                                uint8_t relay_ok)
{
    static int16_t s_last_temp[4] = {0};
    static int32_t s_last_weight = 0;
    static uint16_t s_last_flow_rate = 0u;
    static uint32_t s_last_flow_total = 0u;
    static uint16_t s_last_relay_do = 0u;
    static uint16_t s_last_relay_di = 0u;
    static uint16_t s_last_status = 0u;
    static uint8_t s_first_run = 1u;
    ModbusSlaveData_t slave_data;
    int16_t cur_temp[4];
    uint16_t push_seq;
    uint8_t temp_changed = 0u;
    uint8_t telemetry_changed = 0u;
    uint16_t flow_rate_raw;
    uint32_t flow_total_raw;
    uint16_t status_raw;
    int i;

    if (runtime_config == RT_NULL)
    {
        return;
    }

    memset(&slave_data, 0, sizeof(slave_data));
    cur_temp[0] = 0;
    cur_temp[1] = 0;
    cur_temp[2] = 0;
    cur_temp[3] = (int16_t)(sensor_temp * 10.0f);

    flow_rate_raw = (uint16_t)(flow_rate_lpm * 100.0f);
    flow_total_raw = (uint32_t)(flow_total_l * 1000.0f);
    status_raw = (pt100_ok ? 0x01u : 0u) |
                 (weight_ok ? 0x02u : 0u) |
                 (relay_ok ? 0x04u : 0u) |
                 ((runtime_config->control_mode == G780S_MODE_AUTO) ? 0x08u : 0u);

    for (i = 0; i < 4; i++)
    {
        int16_t diff = (int16_t)(cur_temp[i] - s_last_temp[i]);
        if (diff < 0)
        {
            diff = (int16_t)(-diff);
        }

        if ((uint16_t)diff >= runtime_config->temp_change_threshold_x10)
        {
            temp_changed = 1u;
            break;
        }
    }

    /* 历史样本去重依赖 push_seq，必须跟随实际遥测值变化推进。
       以前只在温度跨大阈值时才递增，会让 Flutter 历史曲线长时间保持直线。 */
    if (s_first_run != 0u)
    {
        telemetry_changed = 1u;
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            if (cur_temp[i] != s_last_temp[i])
            {
                telemetry_changed = 1u;
                break;
            }
        }
        if (telemetry_changed == 0u &&
            (sensor_weight != s_last_weight ||
             flow_rate_raw != s_last_flow_rate ||
             flow_total_raw != s_last_flow_total ||
             relay_do_state != s_last_relay_do ||
             relay_di_state != s_last_relay_di ||
             status_raw != s_last_status))
        {
            telemetry_changed = 1u;
        }
    }

    if (telemetry_changed != 0u)
    {
        push_seq = App_StateUpdatePushSeq(1u);
        for (i = 0; i < 4; i++)
        {
            s_last_temp[i] = cur_temp[i];
        }
        s_last_weight = sensor_weight;
        s_last_flow_rate = flow_rate_raw;
        s_last_flow_total = flow_total_raw;
        s_last_relay_do = relay_do_state;
        s_last_relay_di = relay_di_state;
        s_last_status = status_raw;
        s_first_run = 0u;
        if (temp_changed != 0u)
        {
            printf("[PUSH_SEQ] %u (temp changed)\r\n", push_seq);
        }
    }
    else
    {
        push_seq = App_StateGetPushSeq();
    }

    slave_data.push_seq = push_seq;
    slave_data.pt100_ch[0] = cur_temp[0];
    slave_data.pt100_ch[1] = cur_temp[1];
    slave_data.pt100_ch[2] = cur_temp[2];
    slave_data.pt100_ch[3] = cur_temp[3];
    slave_data.weight_ch[0] = -1;
    slave_data.weight_ch[1] = -1;
    slave_data.weight_ch[2] = sensor_weight;
    slave_data.weight_ch[3] = -1;
    slave_data.flow_rate = flow_rate_raw;
    slave_data.flow_total = flow_total_raw;
    slave_data.relay_do = relay_do_state;
    slave_data.relay_di = relay_di_state;
    slave_data.status = status_raw;

    /* G780S 对外读取的寄存器镜像统一在采集线程这里提交。 */
    G780s_UpdateData(&slave_data);
}

static void App_HandlePendingUpgrade(void)
{
    if (G780s_ConsumeBootUpgradeRequest() == 0u)
    {
        return;
    }

    printf("[UPGRADE] Bootloader request accepted, system reset...\r\n");
    (void)LogService_SubmitFlush();
    (void)rt_thread_mdelay(20);
    HAL_NVIC_SystemReset();
}

static void App_TryConfirmRunningSlot(uint32_t *confirm_start_tick,
                                      uint32_t *confirm_next_retry_tick,
                                      uint16_t *healthy_samples,
                                      uint8_t *confirmed)
{
    uint32_t now;

    if (confirm_start_tick == RT_NULL ||
        confirm_next_retry_tick == RT_NULL ||
        healthy_samples == RT_NULL ||
        confirmed == RT_NULL ||
        *confirmed != 0u)
    {
        return;
    }

    now = HAL_GetTick();
    if ((now - *confirm_start_tick) < APP_UPGRADE_CONFIRM_DELAY_MS)
    {
        return;
    }
    if (*healthy_samples < APP_UPGRADE_CONFIRM_MIN_HEALTHY_SAMPLES)
    {
        return;
    }
    if (now < *confirm_next_retry_tick)
    {
        return;
    }

    if (Upgrade_ConfirmRunningSlot() == 0u)
    {
        *confirmed = 1u;
        printf("[UPGRADE] running slot confirmed (healthy_samples=%u)\r\n",
               (unsigned int)(*healthy_samples));
        return;
    }

    *confirm_next_retry_tick = now + APP_UPGRADE_CONFIRM_RETRY_MS;
    printf("[UPGRADE] running slot confirm failed, retry in %lu ms\r\n",
           (unsigned long)APP_UPGRADE_CONFIRM_RETRY_MS);
}

static void App_HandleCloudRelayCommands(const G780sRemoteConfig *runtime_config,
                                         uint16_t *last_relay_ctrl,
                                         uint16_t *last_relay_bits)
{
    uint16_t relay_ctrl;
    uint16_t relay_bits;

    if (runtime_config == RT_NULL || last_relay_ctrl == RT_NULL || last_relay_bits == RT_NULL)
    {
        return;
    }

    relay_ctrl = G780s_GetRelayCtrl();
    if (relay_ctrl != *last_relay_ctrl)
    {
        printf("[Cloud] RELAY_CTRL: %u\r\n", relay_ctrl);

        if (relay_ctrl == 0u)
        {
            if (App_IsManualMode(runtime_config) != RT_FALSE)
            {
                printf("[Cloud] All OFF\r\n");
                (void)Relay_SetOutputMask(0u);
            }
            else
            {
                printf("[Cloud] RELAY_CTRL ignored in AUTO mode\r\n");
            }
        }
        else if (relay_ctrl >= 1u && relay_ctrl <= 16u)
        {
            if (App_IsManualMode(runtime_config) != RT_FALSE)
            {
                printf("[Cloud] Toggle CH%u\r\n", relay_ctrl);
                (void)Relay_ToggleOutput((uint8_t)relay_ctrl);
            }
            else
            {
                printf("[Cloud] RELAY_CTRL ignored in AUTO mode\r\n");
            }
        }
        else
        {
            printf("[Cloud] Invalid value: %u\r\n", relay_ctrl);
        }

        *last_relay_ctrl = relay_ctrl;
    }

    relay_bits = G780s_GetRelayBits();
    if (relay_bits != *last_relay_bits)
    {
        printf("[Cloud] RELAY_CMD_BITS: 0x%04X -> 0x%04X\r\n", *last_relay_bits, relay_bits);

        if (App_IsManualMode(runtime_config) != RT_FALSE)
        {
            (void)Relay_SetOutputMask(relay_bits);
        }
        else
        {
            printf("[Cloud] RELAY_CMD_BITS ignored in AUTO mode\r\n");
        }

        *last_relay_bits = relay_bits;
    }
}

static void App_HandleJsonCommands(const G780sRemoteConfig *runtime_config)
{
    G780sJsonCommand command;

    if (runtime_config == RT_NULL)
    {
        return;
    }

    while (G780s_ConsumeJsonCommand(&command) != 0u)
    {
        switch (command.type)
        {
            case G780S_JSON_CMD_RELAY_SET:
            {
                uint16_t relay_do_state = 0u;
                uint16_t relay_di_state = 0u;

                if (App_IsManualMode(runtime_config) == RT_FALSE)
                {
                    if (Relay_ReadAllCoils(&relay_do_state) != 0u)
                    {
                        relay_do_state = 0u;
                    }
                    if (Relay_ReadInputPack(&relay_di_state) != 0u)
                    {
                        relay_di_state = 0u;
                    }
                    G780s_ReportCommandAck(&command, "auto_mode", relay_do_state, relay_di_state);
                    break;
                }

                printf("[JSON] relay_set mask=0x%04X\r\n", command.relay_mask);
                (void)Relay_SetOutputMask(command.relay_mask);
                (void)rt_thread_mdelay(20);
                (void)Relay_ReadAllCoils(&relay_do_state);
                (void)Relay_ReadInputPack(&relay_di_state);
                G780s_ReportCommandAck(&command, "ok", relay_do_state, relay_di_state);
                break;
            }

            case G780S_JSON_CMD_OTA_PREPARE:
                printf("[JSON] ota_prepare seq=%lu\r\n", (unsigned long)command.cmd_seq);
                if (Upgrade_RequestBootMode(UPGRADE_REQUEST_SOURCE_REMOTE, 0u) == 0u)
                {
                    G780s_ReportCommandAck(&command, "ok", 0u, 0u);
                    (void)LogService_SubmitFlush();
                    (void)rt_thread_mdelay(50);
                    HAL_NVIC_SystemReset();
                }
                else
                {
                    G780s_ReportCommandAck(&command, "upgrade_request_failed", 0u, 0u);
                }
                break;

            default:
                break;
        }
    }
}

static void App_MaintThreadEntry(void *parameter)
{
    AppKeyState key_state = {0};
    G780sRemoteConfig runtime_config = {0};
    uint32_t confirm_start_tick = HAL_GetTick();
    uint32_t confirm_next_retry_tick = confirm_start_tick + APP_UPGRADE_CONFIRM_DELAY_MS;
    uint16_t healthy_samples = 0u;
    uint16_t last_relay_ctrl = 0xFFFFu;
    uint16_t last_relay_bits = 0u;
    uint32_t led_tick = HAL_GetTick();
    uint8_t confirmed = 0u;

    RT_UNUSED(parameter);

    App_StateGetRuntimeConfig(&runtime_config);
    printf("[UPGRADE] confirm delayed: wait %lu ms and >=%u healthy samples\r\n",
           (unsigned long)APP_UPGRADE_CONFIRM_DELAY_MS,
           (unsigned int)APP_UPGRADE_CONFIRM_MIN_HEALTHY_SAMPLES);

    while (1)
    {
        rt_ubase_t health_flags;

        /* 维护线程短周期推进从站处理，尽量不被传感器采集拖住。 */
        App_StateGetRuntimeConfig(&runtime_config);
        App_HandleKeys(&runtime_config, &key_state);
        App_HandleCloudRelayCommands(&runtime_config, &last_relay_ctrl, &last_relay_bits);
        G780s_Process();
        App_HandleJsonCommands(&runtime_config);
        App_HandlePendingUpgrade();

        /* sensor 线程只上报健康结果，确认运行槽位由 maint 线程集中判定。 */
        while (rt_mb_recv(&s_health_mailbox, &health_flags, RT_WAITING_NO) == RT_EOK)
        {
            if (health_flags != 0u && healthy_samples < 0xFFFFu)
            {
                healthy_samples++;
            }
        }

        App_TryConfirmRunningSlot(&confirm_start_tick,
                                  &confirm_next_retry_tick,
                                  &healthy_samples,
                                  &confirmed);

        if ((HAL_GetTick() - led_tick) >= APP_LED_BLINK_PERIOD_MS)
        {
            led_tick = HAL_GetTick();
            LED_R_TOGGLE();
        }

        App_IwdgKick();
        (void)rt_thread_mdelay(APP_MAINT_PERIOD_MS);
    }
}

static void App_SensorThreadEntry(void *parameter)
{
    AppDiState di_state = {0};
    G780sRemoteConfig runtime_config = {0};
    uint32_t last_config_seq;
    uint32_t last_sensor_tick = 0u;
    float sensor_temp = 0.0f;
    int32_t sensor_weight = 0;
    float flow_rate_lpm = 0.0f;
    float flow_total_l = 0.0f;
    uint16_t relay_do_state = 0u;
    uint16_t relay_di_state = 0u;

    RT_UNUSED(parameter);

    G780s_GetActiveConfig(&runtime_config);
    App_StateSetRuntimeConfig(&runtime_config);
    last_config_seq = runtime_config.sequence;

    Flowmeter_Init(&s_flow_meter,
                   &g_tim2_handle,
                   runtime_config.flow_sample_period_ms,
                   App_ConfigToFloat2(runtime_config.pulses_per_liter_x100),
                   App_ConfigToFloat2(runtime_config.hz_per_lpm_x100));

    printf(">>> Init Relay...\r\n");
    (void)Relay_BatchControl(0u);
    (void)rt_thread_mdelay(100);
    if (Relay_ReadAllCoils(&relay_do_state) == 0)
    {
        printf("Relay DO: 0x%04X\r\n", relay_do_state);
    }

    printf(">>> Sensor thread warmup...\r\n");
    (void)rt_thread_mdelay(APP_STARTUP_SENSOR_SETTLE_MS);
    printf(">>> Sensor acquisition started...\r\n\r\n");

    while (1)
    {
        uint32_t now;
        float freq_hz = 0.0f;

        App_ApplyLatestConfig(&s_flow_meter, &runtime_config, &last_config_seq);

        /* 流量计高频更新，但只有到采样窗口才真正刷新输出值。 */
        if (Flowmeter_Update(&s_flow_meter, &flow_rate_lpm, &flow_total_l, &freq_hz) != 0u)
        {
            RT_UNUSED(freq_hz);
        }

        now = HAL_GetTick();
        if ((now - last_sensor_tick) >= runtime_config.sensor_period_ms)
        {
            SensorSnapshot snap;
            uint8_t pt100_ok = 0u;
            uint8_t weight_ok = 0u;
            uint8_t relay_ok = 0u;
            rt_ubase_t health_flags = 0u;

            /* 现场总线采集仍是阻塞式，但已被拆到独立线程中运行。 */
            last_sensor_tick = now;

            if (Temperature_Read(&sensor_temp) == 0u)
            {
                pt100_ok = 1u;
                health_flags |= 0x01u;
            }
            else
            {
                (void)LogService_SubmitErrorf("PT100 read failed");
            }

            (void)rt_thread_mdelay(APP_SENSOR_RETRY_DELAY_MS);

            if (Weight_Read(&sensor_weight) == 0u)
            {
                weight_ok = 1u;
                health_flags |= 0x02u;
            }
            else
            {
                (void)LogService_SubmitErrorf("Weight read failed");
            }

            (void)rt_thread_mdelay(APP_SENSOR_RETRY_DELAY_MS);

            if (Relay_ReadAllCoils(&relay_do_state) == 0u)
            {
                relay_ok = 1u;
                health_flags |= 0x04u;
            }
            else
            {
                (void)LogService_SubmitErrorf("Relay DO read failed");
            }

            (void)rt_thread_mdelay(APP_SENSOR_RETRY_DELAY_MS);

            if (Relay_ReadInputPack(&relay_di_state) == 0u)
            {
                App_HandleRelayInputs(relay_di_state, &runtime_config, &di_state);
            }
            else
            {
                (void)LogService_SubmitErrorf("Relay DI read failed");
            }

            if (pt100_ok != 0u || weight_ok != 0u || relay_ok != 0u)
            {
                snap.uptime_s = HAL_GetTick() / 1000u;
                snap.temperature = sensor_temp;
                snap.weight = sensor_weight;
                snap.flow_rate = flow_rate_lpm;
                snap.flow_total = flow_total_l;
                snap.relay_do = relay_do_state;
                snap.relay_di = relay_di_state;
                /* 采样记录只投递到队列，真正刷 Flash 交给 logger 线程。 */
                (void)LogService_SubmitSensor(&snap);
            }

            App_UpdateSlaveData(sensor_temp,
                                sensor_weight,
                                flow_rate_lpm,
                                flow_total_l,
                                relay_do_state,
                                relay_di_state,
                                &runtime_config,
                                pt100_ok,
                                weight_ok,
                                relay_ok);

            (void)rt_mb_send(&s_health_mailbox, health_flags);
        }

        App_IwdgKick();
        (void)rt_thread_mdelay(APP_SENSOR_LOOP_DELAY_MS);
    }
}

void AppThreads_HardwareInit(void)
{
    HAL_Init();
    sys_stm32_clock_init(RCC_PLL_MUL9);

    delay_init(72);
    delay_us(1000);

    usart_init(115200);
    LED_Init();
    Key_Init();
    OLED_Init();

    TIM2_Init();
    HAL_TIM_Base_Start(&g_tim2_handle);
    App_IwdgInit();
}

rt_err_t AppThreads_Create(void)
{
    G780sRemoteConfig runtime_config;
    rt_err_t err;

    Modbus_MasterInit();
    G780s_Init();
    G780s_GetActiveConfig(&runtime_config);

    memset(&s_app_state, 0, sizeof(s_app_state));
    err = rt_mutex_init(&s_app_state.lock, "appst", RT_IPC_FLAG_PRIO);
    if (err != RT_EOK)
    {
        return err;
    }

    s_app_state.runtime_config = runtime_config;
    s_app_state.push_seq = 0u;
    s_app_state.ready = RT_TRUE;

    err = BusService_Init();
    if (err != RT_EOK)
    {
        return err;
    }

    err = LogService_Init();
    if (err != RT_EOK)
    {
        return err;
    }

    err = rt_mb_init(&s_health_mailbox,
                     "health",
                     s_health_mailbox_pool,
                     APP_HEALTH_MAILBOX_SIZE,
                     RT_IPC_FLAG_PRIO);
    if (err != RT_EOK)
    {
        return err;
    }

    App_PrintBanner();

    err = rt_thread_init(&s_maint_thread,
                         "maint",
                         App_MaintThreadEntry,
                         RT_NULL,
                         s_maint_stack,
                         sizeof(s_maint_stack),
                         APP_MAINT_THREAD_PRIORITY,
                         APP_THREAD_TIMESLICE);
    if (err != RT_EOK)
    {
        return err;
    }

    err = rt_thread_init(&s_sensor_thread,
                         "sensor",
                         App_SensorThreadEntry,
                         RT_NULL,
                         s_sensor_stack,
                         sizeof(s_sensor_stack),
                         APP_SENSOR_THREAD_PRIORITY,
                         APP_THREAD_TIMESLICE);
    if (err != RT_EOK)
    {
        return err;
    }

    err = rt_thread_init(&s_logger_thread,
                         "logger",
                         LogService_ThreadEntry,
                         RT_NULL,
                         s_logger_stack,
                         sizeof(s_logger_stack),
                         APP_LOGGER_THREAD_PRIORITY,
                         APP_THREAD_TIMESLICE);
    if (err != RT_EOK)
    {
        return err;
    }

    /* 先起 maint，再起 sensor/logger，保证从站与维护链路优先可用。 */
    err = rt_thread_startup(&s_maint_thread);
    if (err != RT_EOK)
    {
        return err;
    }

    err = rt_thread_startup(&s_sensor_thread);
    if (err != RT_EOK)
    {
        return err;
    }

    err = rt_thread_startup(&s_logger_thread);
    if (err != RT_EOK)
    {
        return err;
    }

    return RT_EOK;
}
