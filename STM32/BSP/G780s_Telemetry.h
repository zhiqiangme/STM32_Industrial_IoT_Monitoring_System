#ifndef __G780S_TELEMETRY_H
#define __G780S_TELEMETRY_H

#include <stdbool.h>
#include <stdint.h>

#define TELEMETRY_DEVICE_ID          "DEV001"
#define TELEMETRY_PERIOD_MS          60000
#define TELEMETRY_LINE_MAX           200
#define TELEMETRY_OFFLINE_THRESHOLD  3

#define STATUS_PT100_OFFLINE         (1 << 0)
#define STATUS_WEIGHT_OFFLINE        (1 << 1)
#define STATUS_RELAY_OFFLINE         (1 << 2)
#define STATUS_TEMP_ALARM            (1 << 4)
#define STATUS_WEIGHT_ALARM          (1 << 5)
#define STATUS_FLOW_ALARM            (1 << 6)

typedef struct {
    uint32_t seq;         /* 序号 */
    uint32_t timestamp;   /* 启动后秒计时 */
    float    temperature; /* 温度 (°C) */
    int32_t  weight;      /* 重量 (g) */
    float    flow_rate;   /* 瞬时流量 (L/min) */
    float    flow_total;  /* 累计流量 (L) */
    uint16_t relay_do;    /* 继电器输出位图 */
    uint16_t relay_di;    /* 继电器输入位图 */
    uint16_t status;      /* 状态位 */
} G780sTelemetrySnapshot;

/* 初始化遥测上下文，重置序号与默认周期 */
void G780sTelemetry_Init(void);
/* 快照采集：从全局变量复制数据到结构体 */
void G780sTelemetry_Collect(G780sTelemetrySnapshot* s);
/* 生成一行遥测报文，返回长度 */
uint16_t G780sTelemetry_BuildLine(const G780sTelemetrySnapshot* s, char* out, uint16_t out_max);
/* 周期任务：事件优先，其次按周期上报 */
void G780sTelemetry_Task(void);
/* 触发一次事件上报（立即发送） */
void G780sTelemetry_TriggerEventReport(void);
/* 设置上报周期（ms） */
void G780sTelemetry_SetPeriod(uint32_t period_ms);
/* 获取当前序号 */
uint32_t G780sTelemetry_GetSeq(void);
/* 按设备通讯状态更新状态位，自动触发事件上报 */
void G780sTelemetry_UpdateDeviceStatus(uint8_t pt100_ok, uint8_t weight_ok, uint8_t relay_ok);
/* 检测继电器输出变化触发事件上报 */
void G780sTelemetry_CheckRelayChange(uint16_t new_do);
/* 获取当前状态位 */
uint16_t G780sTelemetry_GetStatus(void);

/* 兼容旧名 */
#define Telemetry_Init                 G780sTelemetry_Init
#define Telemetry_Collect              G780sTelemetry_Collect
#define Telemetry_BuildLine            G780sTelemetry_BuildLine
#define Telemetry_Task                 G780sTelemetry_Task
#define Telemetry_TriggerEventReport   G780sTelemetry_TriggerEventReport
#define Telemetry_SetPeriod            G780sTelemetry_SetPeriod
#define Telemetry_GetSeq               G780sTelemetry_GetSeq
#define Telemetry_UpdateDeviceStatus   G780sTelemetry_UpdateDeviceStatus
#define Telemetry_CheckRelayChange     G780sTelemetry_CheckRelayChange
#define Telemetry_GetStatus            G780sTelemetry_GetStatus

#endif
