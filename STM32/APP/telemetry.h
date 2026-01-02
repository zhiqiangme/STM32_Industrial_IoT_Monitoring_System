/**
 * @file    telemetry.h
 * @brief   遥测数据采集与上报模块
 * @note    支持周期上报和事件触发上报
 */

#ifndef __TELEMETRY_H
#define __TELEMETRY_H

#include <stdint.h>
#include <stdbool.h>

/*----------------------- 配置参数 -----------------------*/
#define TELEMETRY_DEVICE_ID     "DEV001"    /* 设备ID */
#define TELEMETRY_PERIOD_MS     60000       /* 默认上报周期(ms) = 60秒 */
#define TELEMETRY_LINE_MAX      200         /* 上报行最大长度 */

/* 设备离线判定阈值 */
#define TELEMETRY_OFFLINE_THRESHOLD  3      /* 连续N次通讯失败判定为离线 */

/*----------------------- 系统状态位定义 -----------------------*/
#define STATUS_PT100_OFFLINE    (1 << 0)    /* bit0: PT100离线 */
#define STATUS_ZSG4_OFFLINE     (1 << 1)    /* bit1: ZSG4离线 */
#define STATUS_RELAY_OFFLINE    (1 << 2)    /* bit2: 继电器离线 */
#define STATUS_TEMP_ALARM       (1 << 4)    /* bit4: 温度超限 */
#define STATUS_WEIGHT_ALARM     (1 << 5)    /* bit5: 重量超限 */
#define STATUS_FLOW_ALARM       (1 << 6)    /* bit6: 流量超限 */

/*----------------------- 遥测数据快照结构 -----------------------*/
typedef struct {
    uint32_t seq;           /* 递增序号 (sequence) */
    uint32_t timestamp;     /* 时间戳 (timestamp, 启动后秒数) */
    float    temperature;   /* 温度 (temperature, °C) */
    int32_t  weight;        /* 重量 (weight, g) */
    float    flow_rate;     /* 瞬时流量 (flow, L/min) */
    float    flow_total;    /* 累计流量 (total, L) */
    uint16_t relay_do;      /* 继电器输出 (digital_output) */
    uint16_t relay_di;      /* 继电器输入 (digital_input) */
    uint16_t status;        /* 系统状态 (status) */
} TelemetrySnapshot;

/*----------------------- 函数声明 -----------------------*/

void Telemetry_Init(void);
void Telemetry_Collect(TelemetrySnapshot* s);
uint16_t Telemetry_BuildLine(const TelemetrySnapshot* s, char* out, uint16_t out_max);
void Telemetry_Task(void);
void Telemetry_TriggerEventReport(void);
void Telemetry_SetPeriod(uint32_t period_ms);
uint32_t Telemetry_GetSeq(void);

/* 设备状态更新 (由main.c调用) */
void Telemetry_UpdateDeviceStatus(uint8_t pt100_ok, uint8_t zsg4_ok, uint8_t relay_ok);

/* 继电器输出变化检测 (由main.c调用) */
void Telemetry_CheckRelayChange(uint16_t new_do);

/* 获取当前系统状态 */
uint16_t Telemetry_GetStatus(void);

#endif
