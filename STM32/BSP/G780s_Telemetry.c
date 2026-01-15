#include "G780s_Telemetry.h"
#include "RS485_Slave.h"
#include <stdio.h>
#include <string.h>

extern volatile float    g_G780sTelemetry_temp;
extern volatile int32_t  g_G780sTelemetry_weight;
extern volatile float    g_G780sTelemetry_flow;
extern volatile float    g_G780sTelemetry_total;
extern volatile uint16_t g_G780sTelemetry_do;
extern volatile uint16_t g_G780sTelemetry_di;

static uint32_t g_seq = 0;
static uint32_t g_period_ms = TELEMETRY_PERIOD_MS;
static uint32_t g_last_report_tick = 0;
static bool     g_event_pending = false;
static char     g_line_buffer[TELEMETRY_LINE_MAX];

static uint16_t g_system_status = 0;

static uint8_t g_pt100_fail_cnt = 0;
static uint8_t g_weight_fail_cnt = 0;
static uint8_t g_relay_fail_cnt = 0;

static uint16_t g_last_do = 0xFFFF;

static void G780sTelemetry_DoReport(void);

void G780sTelemetry_Init(void)
{
    g_seq = 0;
    g_period_ms = TELEMETRY_PERIOD_MS;
    g_last_report_tick = HAL_GetTick();
    g_event_pending = false;
    g_system_status = 0;
    g_last_do = 0xFFFF;

    printf("[Telemetry] Init OK (period=%lums)\r\n", g_period_ms);
}

void G780sTelemetry_UpdateDeviceStatus(uint8_t pt100_ok, uint8_t weight_ok, uint8_t relay_ok)
{
    uint16_t old_status = g_system_status;

    if (pt100_ok)
    {
        g_pt100_fail_cnt = 0;
        g_system_status &= ~STATUS_PT100_OFFLINE;
    }
    else if (++g_pt100_fail_cnt >= TELEMETRY_OFFLINE_THRESHOLD)
    {
        g_system_status |= STATUS_PT100_OFFLINE;
    }

    if (weight_ok)
    {
        g_weight_fail_cnt = 0;
        g_system_status &= ~STATUS_WEIGHT_OFFLINE;
    }
    else if (++g_weight_fail_cnt >= TELEMETRY_OFFLINE_THRESHOLD)
    {
        g_system_status |= STATUS_WEIGHT_OFFLINE;
    }

    if (relay_ok)
    {
        g_relay_fail_cnt = 0;
        g_system_status &= ~STATUS_RELAY_OFFLINE;
    }
    else if (++g_relay_fail_cnt >= TELEMETRY_OFFLINE_THRESHOLD)
    {
        g_system_status |= STATUS_RELAY_OFFLINE;
    }

    if (old_status != g_system_status)
    {
        printf("[Telemetry] Status changed: 0x%04X -> 0x%04X\r\n", old_status, g_system_status);
        g_event_pending = true;
    }
}

void G780sTelemetry_CheckRelayChange(uint16_t new_do)
{
    if (g_last_do != 0xFFFF && g_last_do != new_do)
    {
        printf("[Telemetry] DO changed: 0x%04X -> 0x%04X\r\n", g_last_do, new_do);
        g_event_pending = true;
    }
    g_last_do = new_do;
}

uint16_t G780sTelemetry_GetStatus(void)
{
    return g_system_status;
}

void G780sTelemetry_Collect(G780sTelemetrySnapshot* s)
{
    if (s == NULL) return;

    /* 采集全局数据，使用关中断保护多字节一致性 */
    __disable_irq();
    s->temperature = g_G780sTelemetry_temp;
    s->weight = g_G780sTelemetry_weight;
    s->flow_rate = g_G780sTelemetry_flow;
    s->flow_total = g_G780sTelemetry_total;
    s->relay_do = g_G780sTelemetry_do;
    s->relay_di = g_G780sTelemetry_di;
    __enable_irq();

    s->seq = ++g_seq;
    s->timestamp = HAL_GetTick() / 1000;
    s->status = g_system_status;
}

uint16_t G780sTelemetry_BuildLine(const G780sTelemetrySnapshot* s, char* out, uint16_t out_max)
{
    if (s == NULL || out == NULL || out_max < 50) return 0;

    int len = snprintf(out, out_max,
        "@@TEL,dev=%s,seq=%lu,ts=%lu,t=%.1f,w=%ld,f=%.2f,tot=%.3f,do=0x%04X,di=0x%04X,st=0x%04X\r\n",
        TELEMETRY_DEVICE_ID,
        (unsigned long)s->seq,
        (unsigned long)s->timestamp,
        s->temperature,
        (long)s->weight,
        s->flow_rate,
        s->flow_total,
        s->relay_do,
        s->relay_di,
        s->status
    );

    if (len < 0 || len >= out_max) return 0;
    return (uint16_t)len;
}

static void G780sTelemetry_DoReport(void)
{
    G780sTelemetrySnapshot snap;
    G780sTelemetry_Collect(&snap);

    uint16_t len = G780sTelemetry_BuildLine(&snap, g_line_buffer, TELEMETRY_LINE_MAX);
    if (len == 0)
    {
        printf("[Telemetry] Build failed\r\n");
        return;
    }

    if (RS485_Slave_SendAsync((uint8_t*)g_line_buffer, len))
    {
        printf("[Telemetry] seq=%lu sent\r\n", (unsigned long)snap.seq);
    }
    else
    {
        printf("[Telemetry] seq=%lu busy\r\n", (unsigned long)snap.seq);
    }
}

void G780sTelemetry_Task(void)
{
    uint32_t now = HAL_GetTick();

    /* 事件上报优先，其次按周期上报 */
    if (g_event_pending && RS485_Slave_IsIdle())
    {
        g_event_pending = false;
        G780sTelemetry_DoReport();
        g_last_report_tick = now;
        return;
    }

    if ((now - g_last_report_tick) >= g_period_ms)
    {
        if (RS485_Slave_IsIdle())
        {
            G780sTelemetry_DoReport();
            g_last_report_tick = now;
        }
    }
}

void G780sTelemetry_TriggerEventReport(void)
{
    g_event_pending = true;
}

void G780sTelemetry_SetPeriod(uint32_t period_ms)
{
    if (period_ms >= 1000 && period_ms != g_period_ms)
    {
        g_period_ms = period_ms;
        printf("[Telemetry] Period=%lus\r\n", period_ms / 1000);
    }
}

uint32_t G780sTelemetry_GetSeq(void)
{
    return g_seq;
}
