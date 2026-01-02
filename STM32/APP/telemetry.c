/**
 * @file    telemetry.c
 * @brief   遥测数据采集与上报模块实现
 * @note    支持5秒周期上报 + 事件触发上报
 */

#include "telemetry.h"
#include "rs4853_uart.h"
#include <stdio.h>
#include <string.h>

/*----------------------- 外部全局变量声明 -----------------------*/
extern volatile float    g_telemetry_temp;
extern volatile int32_t  g_telemetry_weight;
extern volatile float    g_telemetry_flow;
extern volatile float    g_telemetry_total;
extern volatile uint16_t g_telemetry_do;
extern volatile uint16_t g_telemetry_di;

/*----------------------- 内部变量 -----------------------*/
static uint32_t g_seq = 0;
static uint32_t g_period_ms = TELEMETRY_PERIOD_MS;
static uint32_t g_last_report_tick = 0;
static bool     g_event_pending = false;
static char     g_line_buffer[TELEMETRY_LINE_MAX];

/* 系统状态 */
static uint16_t g_system_status = 0;

/* 设备通讯失败计数 */
static uint8_t g_pt100_fail_cnt = 0;
static uint8_t g_zsg4_fail_cnt = 0;
static uint8_t g_relay_fail_cnt = 0;

/* 上次继电器输出值 (用于变化检测) */
static uint16_t g_last_do = 0xFFFF;

/*----------------------- 初始化 -----------------------*/
void Telemetry_Init(void)
{
    g_seq = 0;
    g_period_ms = TELEMETRY_PERIOD_MS;
    g_last_report_tick = HAL_GetTick();
    g_event_pending = false;
    g_system_status = 0;
    g_last_do = 0xFFFF;  /* 初始值设为无效，确保首次检测到变化 */
    
    printf("[Telemetry] Init OK (period=%lums)\r\n", g_period_ms);
}

/*----------------------- 设备状态更新 -----------------------*/
void Telemetry_UpdateDeviceStatus(uint8_t pt100_ok, uint8_t zsg4_ok, uint8_t relay_ok)
{
    uint16_t old_status = g_system_status;
    
    /* PT100状态 */
    if (pt100_ok)
    {
        g_pt100_fail_cnt = 0;
        g_system_status &= ~STATUS_PT100_OFFLINE;
    }
    else
    {
        if (++g_pt100_fail_cnt >= TELEMETRY_OFFLINE_THRESHOLD)
        {
            g_system_status |= STATUS_PT100_OFFLINE;
        }
    }
    
    /* ZSG4状态 */
    if (zsg4_ok)
    {
        g_zsg4_fail_cnt = 0;
        g_system_status &= ~STATUS_ZSG4_OFFLINE;
    }
    else
    {
        if (++g_zsg4_fail_cnt >= TELEMETRY_OFFLINE_THRESHOLD)
        {
            g_system_status |= STATUS_ZSG4_OFFLINE;
        }
    }
    
    /* 继电器状态 */
    if (relay_ok)
    {
        g_relay_fail_cnt = 0;
        g_system_status &= ~STATUS_RELAY_OFFLINE;
    }
    else
    {
        if (++g_relay_fail_cnt >= TELEMETRY_OFFLINE_THRESHOLD)
        {
            g_system_status |= STATUS_RELAY_OFFLINE;
        }
    }
    
    /* 状态变化时触发事件上报 */
    if (old_status != g_system_status)
    {
        printf("[Telemetry] Status changed: 0x%04X -> 0x%04X\r\n", old_status, g_system_status);
        g_event_pending = true;
    }
}

/*----------------------- 继电器输出变化检测 -----------------------*/
void Telemetry_CheckRelayChange(uint16_t new_do)
{
    if (g_last_do != 0xFFFF && g_last_do != new_do)
    {
        printf("[Telemetry] DO changed: 0x%04X -> 0x%04X\r\n", g_last_do, new_do);
        g_event_pending = true;
    }
    g_last_do = new_do;
}

/*----------------------- 获取状态 -----------------------*/
uint16_t Telemetry_GetStatus(void)
{
    return g_system_status;
}

/*----------------------- 采集快照 -----------------------*/
void Telemetry_Collect(TelemetrySnapshot* s)
{
    if (s == NULL) return;
    
    __disable_irq();
    s->temperature = g_telemetry_temp;
    s->weight = g_telemetry_weight;
    s->flow_rate = g_telemetry_flow;
    s->flow_total = g_telemetry_total;
    s->relay_do = g_telemetry_do;
    s->relay_di = g_telemetry_di;
    __enable_irq();
    
    s->seq = ++g_seq;
    s->timestamp = HAL_GetTick() / 1000;
    s->status = g_system_status;
}

/*----------------------- 生成上报行 -----------------------*/
uint16_t Telemetry_BuildLine(const TelemetrySnapshot* s, char* out, uint16_t out_max)
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

/*----------------------- 执行上报 -----------------------*/
static void DoReport(void)
{
    TelemetrySnapshot snap;
    Telemetry_Collect(&snap);
    
    uint16_t len = Telemetry_BuildLine(&snap, g_line_buffer, TELEMETRY_LINE_MAX);
    if (len == 0)
    {
        printf("[Telemetry] Build failed\r\n");
        return;
    }
    
    if (RS4853_SendAsync((uint8_t*)g_line_buffer, len))
    {
        printf("[Telemetry] seq=%lu sent\r\n", (unsigned long)snap.seq);
    }
    else
    {
        printf("[Telemetry] seq=%lu busy\r\n", (unsigned long)snap.seq);
    }
}

/*----------------------- 遥测任务 -----------------------*/
void Telemetry_Task(void)
{
    uint32_t now = HAL_GetTick();
    
    /* 事件上报 (高优先级) */
    if (g_event_pending && RS4853_IsIdle())
    {
        g_event_pending = false;
        DoReport();
        g_last_report_tick = now;
        return;
    }
    
    /* 周期上报 */
    if ((now - g_last_report_tick) >= g_period_ms)
    {
        if (RS4853_IsIdle())
        {
            DoReport();
            g_last_report_tick = now;
        }
    }
}

/*----------------------- 触发事件上报 -----------------------*/
void Telemetry_TriggerEventReport(void)
{
    g_event_pending = true;
}

/*----------------------- 设置周期 -----------------------*/
void Telemetry_SetPeriod(uint32_t period_ms)
{
    if (period_ms >= 1000 && period_ms != g_period_ms)
    {
        g_period_ms = period_ms;
        printf("[Telemetry] Period=%lus\r\n", period_ms / 1000);
    }
}

/*----------------------- 获取序号 -----------------------*/
uint32_t Telemetry_GetSeq(void)
{
    return g_seq;
}
