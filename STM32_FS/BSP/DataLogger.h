/**
 ****************************************************************************************************
 * @file        DataLogger.h
 * @brief       传感器数据与错误日志文件记录模块 (基于 LittleFS)
 *
 * 功能:
 *   - 将传感器采样数据以 CSV 格式追加写入 Flash 文件 (/data/NNNNNN.csv)
 *   - 将错误/事件日志以纯文本格式写入 Flash 文件 (/log/NNNNNN.log)
 *   - 内置写缓冲, 积累约 6 条记录后批量刷盘, 减少 Flash 擦写次数
 *   - 自动管理文件按天编号、清理过期文件
 *
 * Flash 文件系统目录结构:
 *   /
 *   ├── data/
 *   │   ├── 000001.csv    第 1 天的传感器数据
 *   │   ├── 000002.csv    第 2 天的传感器数据
 *   │   └── ...
 *   ├── log/
 *   │   ├── 000001.log    第 1 天的错误/事件日志
 *   │   └── ...
 *   └── meta.dat          元数据 (启动计数器、当前日序号)
 *
 * CSV 文件列格式:
 *   uptime_s, temp_x10, weight_g, flow_rate_x100, flow_total_x1000, relay_do, relay_di
 *   (运行秒数, 温度×10整数, 重量克, 流量×100整数, 总流量×1000整数, 继电器DO位图, DI位图)
 *
 * 注意:
 *   本项目无 RTC, 使用启动次数递增的"日序号"替代日历日期.
 *   如后续添加 RTC, 可修改 build_data_path / build_log_path 使用真实日期命名文件.
 ****************************************************************************************************
 */
#ifndef __DATALOGGER_H
#define __DATALOGGER_H

#include "sys.h"

/**
 * @brief 传感器快照结构体, 存储一次采样的所有传感器数值.
 */
typedef struct {
    uint32_t uptime_s;      /**< 系统运行时间 (秒), 由 HAL_GetTick()/1000 得到 */
    float    temperature;   /**< PT100 温度值 (°C), 精度 0.1°C */
    int32_t  weight;        /**< 称重模块读数 (克), 有符号整数 */
    float    flow_rate;     /**< 当前流量 (L/min) */
    float    flow_total;    /**< 累计总流量 (L) */
    uint16_t relay_do;      /**< 继电器输出状态位图 (bit0=CH1, bit15=CH16) */
    uint16_t relay_di;      /**< 继电器输入状态位图 (bit0=CH1, bit15=CH16) */
} SensorSnapshot;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 DataLogger 模块.
 * @note   内部依次执行:
 *           1. 挂载 LittleFS (W25Q128 Flash)
 *           2. 创建 /data 和 /log 目录 (已存在则忽略)
 *           3. 加载 /meta.dat 元数据, 将启动计数 +1 并保存
 *           4. 向日志文件写入一条启动事件记录
 *         已初始化时重复调用直接返回 0.
 * @retval  0: 成功
 * @retval -1: LittleFS 初始化或挂载失败
 */
int DataLogger_Init(void);

/**
 * @brief  记录一条传感器快照到 CSV 文件.
 * @note   数据先写入 RAM 缓冲区 (512 字节). 缓冲区满时自动刷盘.
 *         每 60 秒还会由 main.c 调用 DataLogger_Flush() 强制刷盘.
 *         未初始化时调用直接返回 -1.
 * @param  snap  指向已填充的 SensorSnapshot 结构体
 * @retval  0: 成功
 * @retval -1: 未初始化或参数为空
 * @retval -2: snprintf 格式化失败
 */
int DataLogger_LogSensor(const SensorSnapshot *snap);

/**
 * @brief  记录一条错误/事件日志, 立即写入 Flash.
 * @note   每条日志格式: "[运行秒数] 格式化消息\r\n"
 *         例: "[12345] PT100 read failed"
 *         与传感器数据不同, 日志立即写盘而非缓冲, 以确保异常信息不丢失.
 *         支持 printf 风格的格式化字符串.
 * @param  fmt  格式字符串 (printf 风格)
 * @param  ...  可变参数
 * @retval  0: 成功
 * @retval <0: 失败 (未初始化 / 文件打开失败 / 格式化错误)
 */
int DataLogger_LogError(const char *fmt, ...);

/**
 * @brief  立即将 RAM 缓冲区中的传感器数据刷写到 Flash.
 * @note   建议在主循环中每 60 秒调用一次, 防止掉电丢失数据.
 *         也可在关机前或执行 OTA 前调用以确保数据完整.
 * @retval  0: 成功或缓冲区为空 (无需刷写)
 * @retval <0: 文件系统错误
 */
int DataLogger_Flush(void);

/**
 * @brief  切换到新的一天 (递增日序号, 后续数据写入新文件).
 * @note   调用此函数前会先刷新当前缓冲区.
 *         无 RTC 时可在每次上电时调用一次, 或按运行时间定期调用.
 * @retval  0: 成功
 * @retval <0: 元数据保存失败
 */
int DataLogger_RollDay(void);

/**
 * @brief  清理过期文件, 只保留最近 keep_days 天的数据和日志.
 * @note   遍历 /data 和 /log 目录, 删除日序号小于 (当前日 - keep_days) 的文件.
 *         建议在每次启动后调用一次, 防止 Flash 空间耗尽.
 * @param  keep_days  保留天数. 为 0 或超出现有天数时不执行任何删除.
 * @retval 成功删除的文件总数; <0 表示初始化错误
 */
int DataLogger_Cleanup(uint32_t keep_days);

/**
 * @brief  查询 LittleFS 分区剩余可用字节数.
 * @note   可用于判断是否需要提前清理旧文件.
 * @retval 剩余字节数; 文件系统未挂载时返回 0
 */
uint32_t DataLogger_GetFreeBytes(void);

#ifdef __cplusplus
}
#endif

#endif /* __DATALOGGER_H */
