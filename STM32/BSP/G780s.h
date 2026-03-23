#ifndef __G780S_H
#define __G780S_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#define MODBUS_SLAVE_ADDR           10

#define REG_PUSH_SEQ                0x0000
#define REG_PT100_CH1               0x0001
#define REG_PT100_CH2               0x0002
#define REG_PT100_CH3               0x0003
#define REG_PT100_CH4               0x0004
#define REG_WEIGHT_CH1_H            0x0005
#define REG_WEIGHT_CH1_L            0x0006
#define REG_WEIGHT_CH2_H            0x0007
#define REG_WEIGHT_CH2_L            0x0008
#define REG_WEIGHT_CH3_H            0x0009
#define REG_WEIGHT_CH3_L            0x000A
#define REG_WEIGHT_CH4_H            0x000B
#define REG_WEIGHT_CH4_L            0x000C
#define REG_FLOW_RATE               0x000D
#define REG_FLOW_TOTAL_HIGH         0x000E
#define REG_FLOW_TOTAL_LOW          0x000F
#define REG_RELAY_CTRL              0x0010
#define REG_RELAY_DO                0x0011
#define REG_RELAY_DI                0x0012
#define REG_SYSTEM_STATUS           0x0013
#define REG_RELAY_BITS              0x0014  /* 继电器状态位图 (只读) */
#define REG_RELAY_CMD_BITS          0x0015  /* 继电器命令位图 (上位机写) */

/*
 * =========================== 远程参数维护寄存器表 ===========================
 *
 * 一、现场数据寄存器区（保持原有功能，供 G780S / 服务器轮询读取）
 * ┌────────┬──────────────────────────────┬──────────────┬────────────────────┐
 * │ 地址   │ 名称                         │ 读写属性     │ 说明               │
 * ├────────┼──────────────────────────────┼──────────────┼────────────────────┤
 * │ 0x0000 │ REG_PUSH_SEQ                 │ R            │ 上报序号           │
 * │ 0x0001 │ REG_PT100_CH1                │ R            │ PT100 CH1，×10 C   │
 * │ 0x0002 │ REG_PT100_CH2                │ R            │ PT100 CH2，×10 C   │
 * │ 0x0003 │ REG_PT100_CH3                │ R            │ PT100 CH3，×10 C   │
 * │ 0x0004 │ REG_PT100_CH4                │ R            │ PT100 CH4，×10 C   │
 * │ 0x0005 │ REG_WEIGHT_CH1_H             │ R            │ 称重 CH1 高 16 位  │
 * │ 0x0006 │ REG_WEIGHT_CH1_L             │ R            │ 称重 CH1 低 16 位  │
 * │ 0x0007 │ REG_WEIGHT_CH2_H             │ R            │ 称重 CH2 高 16 位  │
 * │ 0x0008 │ REG_WEIGHT_CH2_L             │ R            │ 称重 CH2 低 16 位  │
 * │ 0x0009 │ REG_WEIGHT_CH3_H             │ R            │ 称重 CH3 高 16 位  │
 * │ 0x000A │ REG_WEIGHT_CH3_L             │ R            │ 称重 CH3 低 16 位  │
 * │ 0x000B │ REG_WEIGHT_CH4_H             │ R            │ 称重 CH4 高 16 位  │
 * │ 0x000C │ REG_WEIGHT_CH4_L             │ R            │ 称重 CH4 低 16 位  │
 * │ 0x000D │ REG_FLOW_RATE                │ R            │ 瞬时流量，×100     │
 * │ 0x000E │ REG_FLOW_TOTAL_HIGH          │ R            │ 累计流量高 16 位   │
 * │ 0x000F │ REG_FLOW_TOTAL_LOW           │ R            │ 累计流量低 16 位   │
 * │ 0x0010 │ REG_RELAY_CTRL               │ R/W          │ 继电器控制命令     │
 * │ 0x0011 │ REG_RELAY_DO                 │ R            │ 继电器输出位图     │
 * │ 0x0012 │ REG_RELAY_DI                 │ R            │ 继电器输入位图     │
 * │ 0x0013 │ REG_SYSTEM_STATUS            │ R            │ 系统状态位         │
 * │ 0x0014 │ REG_RELAY_BITS               │ R            │ 继电器状态位图     │
 * │ 0x0015 │ REG_RELAY_CMD_BITS           │ R/W          │ 继电器按位控制命令 │
 * └────────┴──────────────────────────────┴──────────────┴────────────────────┘
 *
 * 二、远程配置寄存器区（必须先解锁，写入后只是暂存，不直接写 Flash）
 * ┌────────┬──────────────────────────────┬──────────────┬────────────────────┐
 * │ 地址   │ 名称                         │ 读写属性     │ 说明               │
 * ├────────┼──────────────────────────────┼──────────────┼────────────────────┤
 * │ 0x0020 │ REG_CFG_SENSOR_PERIOD_MS     │ R/W          │ 传感器采集周期 ms  │
 * │ 0x0021 │ REG_CFG_FLOW_SAMPLE_MS       │ R/W          │ 流量采样周期 ms    │
 * │ 0x0022 │ REG_CFG_DI_DEBOUNCE_MS       │ R/W          │ DI 去抖时间 ms     │
 * │ 0x0023 │ REG_CFG_TEMP_THRESHOLD_X10   │ R/W          │ 温差阈值，×10 ℃    │
 * │ 0x0024 │ REG_CFG_PPL_X100_H           │ R/W          │ 每升脉冲数高 16 位 │
 * │ 0x0025 │ REG_CFG_PPL_X100_L           │ R/W          │ 每升脉冲数低 16 位 │
 * │ 0x0026 │ REG_CFG_HZ_PER_LPM_X100_H    │ R/W          │ Hz/LPM 高 16 位    │
 * │ 0x0027 │ REG_CFG_HZ_PER_LPM_X100_L    │ R/W          │ Hz/LPM 低 16 位    │
 * │ 0x0028 │ REG_CFG_CONTROL_MODE         │ R/W          │ 控制模式           │
 * └────────┴──────────────────────────────┴──────────────┴────────────────────┘
 *
 * 三、维护控制/状态寄存器区
 * ┌────────┬──────────────────────────────┬──────────────┬────────────────────┐
 * │ 地址   │ 名称                         │ 读写属性     │ 说明               │
 * ├────────┼──────────────────────────────┼──────────────┼────────────────────┤
 * │ 0x0030 │ REG_MAINT_UNLOCK             │ W            │ 写入解锁口令       │
 * │ 0x0031 │ REG_MAINT_COMMAND            │ W/R          │ 维护命令触发寄存器 │
 * │ 0x0032 │ REG_MAINT_STATUS             │ R            │ 维护状态位         │
 * │ 0x0033 │ REG_MAINT_LAST_ERROR         │ R            │ 最近错误码         │
 * │ 0x0034 │ REG_MAINT_CFG_VERSION        │ R            │ 配置结构版本号     │
 * │ 0x0035 │ REG_MAINT_CFG_SEQUENCE_H     │ R            │ 生效配置序号高 16  │
 * │ 0x0036 │ REG_MAINT_CFG_SEQUENCE_L     │ R            │ 生效配置序号低 16  │
 * │ 0x0037 │ REG_MAINT_UNLOCK_REMAIN_S    │ R            │ 解锁剩余时间（秒） │
 * └────────┴──────────────────────────────┴──────────────┴────────────────────┘
 *
 * 四、诊断寄存器区（只读，用于远程排障）
 * ┌────────┬──────────────────────────────┬──────────────┬────────────────────┐
 * │ 地址   │ 名称                         │ 读写属性     │ 说明               │
 * ├────────┼──────────────────────────────┼──────────────┼────────────────────┤
 * │ 0x0038 │ REG_DIAG_FW_VERSION          │ R            │ 固件版本号         │
 * │ 0x0039 │ REG_DIAG_PROTOCOL_VERSION    │ R            │ 协议版本号         │
 * │ 0x003A │ REG_DIAG_BUILD_YEAR          │ R            │ 编译年份           │
 * │ 0x003B │ REG_DIAG_BUILD_MONTH_DAY     │ R            │ 编译月/日          │
 * │ 0x003C │ REG_DIAG_BUILD_HOUR_MIN      │ R            │ 编译时/分          │
 * │ 0x003D │ REG_DIAG_UPTIME_H            │ R            │ 运行时长高 16 位   │
 * │ 0x003E │ REG_DIAG_UPTIME_L            │ R            │ 运行时长低 16 位   │
 * │ 0x003F │ REG_DIAG_POWER_ON_COUNT_H    │ R            │ 上电次数高 16 位   │
 * │ 0x0040 │ REG_DIAG_POWER_ON_COUNT_L    │ R            │ 上电次数低 16 位   │
 * │ 0x0041 │ REG_DIAG_RESET_REASON        │ R            │ 最近一次重启原因   │
 * │ 0x0042 │ REG_DIAG_LAST_BAD_ADDR       │ R            │ 最近非法写入地址   │
 * │ 0x0043 │ REG_DIAG_LAST_BAD_VALUE      │ R            │ 最近非法写入值     │
 * │ 0x0044 │ REG_DIAG_LAST_CFG_SOURCE     │ R            │ 当前配置来源       │
 * │ 0x0045 │ REG_DIAG_MODBUS_CRC_ERR_H    │ R            │ CRC 错误计数高 16  │
 * │ 0x0046 │ REG_DIAG_MODBUS_CRC_ERR_L    │ R            │ CRC 错误计数低 16  │
 * │ 0x0047 │ REG_DIAG_UART_ERR_H          │ R            │ 串口异常计数高 16  │
 * │ 0x0048 │ REG_DIAG_UART_ERR_L          │ R            │ 串口异常计数低 16  │
 * │ 0x0049 │ REG_DIAG_LAST_CMD_RESULT     │ R            │ 最近维护命令结果   │
 * │ 0x004A │ REG_DIAG_LAST_BAD_READ_ADDR  │ R            │ 最近非法读取地址   │
 * └────────┴──────────────────────────────┴──────────────┴────────────────────┘
 *
 * 五、维护命令定义（写入 REG_MAINT_COMMAND）
 *   0x0000: G780S_CMD_NONE
 *           空命令 / 默认值
 *   0x0001: G780S_CMD_APPLY_SAVE
 *           校验配置区暂存值 -> 保存到 Flash -> 切换为生效配置
 *   0x0002: G780S_CMD_DISCARD_STAGED
 *           放弃暂存值，恢复为当前生效配置
 *   0x0003: G780S_CMD_RESTORE_DEFAULTS
 *           恢复默认配置并保存到 Flash
 *   0x0004: G780S_CMD_CLEAR_ERROR
 *           清除最近错误码和错误状态位
 *
 * 六、维护状态位定义（REG_MAINT_STATUS）
 *   bit0: G780S_STATUS_UNLOCKED
 *         当前处于解锁窗口，可写配置区
 *   bit1: G780S_STATUS_STAGED_DIRTY
 *         配置区已修改但尚未提交保存
 *   bit2: G780S_STATUS_CONFIG_LOADED
 *         当前配置来自 Flash 成功加载
 *   bit3: G780S_STATUS_SAVE_OK
 *         最近一次提交保存成功
 *   bit4: G780S_STATUS_ERROR
 *         当前存在维护错误，详见 REG_MAINT_LAST_ERROR
 *
 * 七、错误码定义（REG_MAINT_LAST_ERROR）
 *   0: G780S_ERR_NONE
 *      无错误
 *   1: G780S_ERR_LOCKED
 *      设备未解锁，拒绝配置写入或命令执行
 *   2: G780S_ERR_INVALID_UNLOCK_KEY
 *      解锁口令错误
 *   3: G780S_ERR_FLASH_ERASE
 *      Flash 擦除失败
 *   4: G780S_ERR_FLASH_PROGRAM
 *      Flash 编程失败
 *   5: G780S_ERR_FLASH_VERIFY
 *      Flash 回读校验失败
 *   6: G780S_ERR_BAD_COMMAND
 *      维护命令非法
 *   7: G780S_ERR_FLASH_EMPTY
 *      Flash 参数页为空，系统已回退默认值
 *   8: G780S_ERR_FLASH_CRC
 *      Flash 参数页 CRC/版本非法，系统已回退默认值
 *   9: G780S_ERR_SENSOR_PERIOD_RANGE
 *      传感器采集周期越界
 *   10: G780S_ERR_FLOW_SAMPLE_RANGE
 *       流量采样周期越界
 *   11: G780S_ERR_DI_DEBOUNCE_RANGE
 *       DI 去抖时间越界
 *   12: G780S_ERR_TEMP_THRESHOLD_RANGE
 *       温差触发阈值越界
 *   13: G780S_ERR_PPL_RANGE
 *       每升脉冲数越界
 *   14: G780S_ERR_HZ_PER_LPM_RANGE
 *       Hz/LPM 换算参数越界
 *   15: G780S_ERR_CONTROL_MODE_INVALID
 *       控制模式非法
 *   16: G780S_ERR_CONFIG_CONFLICT
 *       配置组合关系不合法
 *
 * 八、推荐远程维护流程
 *   1. 读取 0x0032~0x0037，确认设备在线、查看状态和错误码
 *   2. 写 0x0030 = 0xA55A，打开 30 秒维护窗口
 *   3. 写 0x0020~0x0028，更新参数暂存值
 *   4. 再次读取 0x0020~0x0028，确认暂存值正确
 *   5. 写 0x0031 = 0x0001，提交保存
 *   6. 轮询 0x0032/0x0033/0x0035/0x0036，确认保存成功并获取新配置序号
 *
 * 九、说明
 *   1. 配置区写入成功仅表示“暂存成功”，不会立刻写 Flash
 *   2. 只有执行 G780S_CMD_APPLY_SAVE 后才会真正保存并生效
 *   3. 解锁超时后设备会自动重新上锁
 *   4. 远程配置与现场数据轮询共存，不影响原先 G780S 读寄存器功能
 *   5. REG_CFG_CONTROL_MODE:
 *      0 = 手动模式，允许按键和远程直接控制继电器
 *      1 = 自动模式，当前暂时屏蔽手动继电器控制，等待后续自动逻辑接入
 *   6. REG_DIAG_LAST_CFG_SOURCE:
 *      1 = 默认配置，2 = Flash 加载，3 = 远程保存，4 = 恢复默认值
 *   7. REG_DIAG_RESET_REASON:
 *      1 = 上电复位，2 = NRST 引脚复位，3 = 软件复位，
 *      4 = 独立看门狗，5 = 窗口看门狗，6 = 低功耗复位
 * ============================================================================
 */

#define REG_CFG_SENSOR_PERIOD_MS    0x0020
#define REG_CFG_FLOW_SAMPLE_MS      0x0021
#define REG_CFG_DI_DEBOUNCE_MS      0x0022
#define REG_CFG_TEMP_THRESHOLD_X10  0x0023
#define REG_CFG_PPL_X100_H          0x0024
#define REG_CFG_PPL_X100_L          0x0025
#define REG_CFG_HZ_PER_LPM_X100_H   0x0026
#define REG_CFG_HZ_PER_LPM_X100_L   0x0027
#define REG_CFG_CONTROL_MODE        0x0028

#define REG_MAINT_UNLOCK            0x0030
#define REG_MAINT_COMMAND           0x0031
#define REG_MAINT_STATUS            0x0032
#define REG_MAINT_LAST_ERROR        0x0033
#define REG_MAINT_CFG_VERSION       0x0034
#define REG_MAINT_CFG_SEQUENCE_H    0x0035
#define REG_MAINT_CFG_SEQUENCE_L    0x0036
#define REG_MAINT_UNLOCK_REMAIN_S   0x0037
#define REG_DIAG_FW_VERSION         0x0038
#define REG_DIAG_PROTOCOL_VERSION   0x0039
#define REG_DIAG_BUILD_YEAR         0x003A
#define REG_DIAG_BUILD_MONTH_DAY    0x003B
#define REG_DIAG_BUILD_HOUR_MIN     0x003C
#define REG_DIAG_UPTIME_H           0x003D
#define REG_DIAG_UPTIME_L           0x003E
#define REG_DIAG_POWER_ON_COUNT_H   0x003F
#define REG_DIAG_POWER_ON_COUNT_L   0x0040
#define REG_DIAG_RESET_REASON       0x0041
#define REG_DIAG_LAST_BAD_ADDR      0x0042
#define REG_DIAG_LAST_BAD_VALUE     0x0043
#define REG_DIAG_LAST_CFG_SOURCE    0x0044
#define REG_DIAG_MODBUS_CRC_ERR_H   0x0045
#define REG_DIAG_MODBUS_CRC_ERR_L   0x0046
#define REG_DIAG_UART_ERR_H         0x0047
#define REG_DIAG_UART_ERR_L         0x0048
#define REG_DIAG_LAST_CMD_RESULT    0x0049
#define REG_DIAG_LAST_BAD_READ_ADDR 0x004A

#define MODBUS_REG_COUNT            75

#define G780S_UNLOCK_KEY            0xA55A

#define G780S_CMD_NONE              0x0000
#define G780S_CMD_APPLY_SAVE        0x0001
#define G780S_CMD_DISCARD_STAGED    0x0002
#define G780S_CMD_RESTORE_DEFAULTS  0x0003
#define G780S_CMD_CLEAR_ERROR       0x0004
#define G780S_CMD_ENTER_BOOT_UPGRADE 0x0005

#define G780S_STATUS_UNLOCKED       (1u << 0)
#define G780S_STATUS_STAGED_DIRTY   (1u << 1)
#define G780S_STATUS_CONFIG_LOADED  (1u << 2)
#define G780S_STATUS_SAVE_OK        (1u << 3)
#define G780S_STATUS_ERROR          (1u << 4)

#define G780S_ERR_NONE                  0u
#define G780S_ERR_LOCKED                1u
#define G780S_ERR_INVALID_UNLOCK_KEY    2u
#define G780S_ERR_FLASH_ERASE           3u
#define G780S_ERR_FLASH_PROGRAM         4u
#define G780S_ERR_FLASH_VERIFY          5u
#define G780S_ERR_BAD_COMMAND           6u
#define G780S_ERR_FLASH_EMPTY           7u
#define G780S_ERR_FLASH_CRC             8u
#define G780S_ERR_SENSOR_PERIOD_RANGE   9u
#define G780S_ERR_FLOW_SAMPLE_RANGE     10u
#define G780S_ERR_DI_DEBOUNCE_RANGE     11u
#define G780S_ERR_TEMP_THRESHOLD_RANGE  12u
#define G780S_ERR_PPL_RANGE             13u
#define G780S_ERR_HZ_PER_LPM_RANGE      14u
#define G780S_ERR_CONTROL_MODE_INVALID  15u
#define G780S_ERR_CONFIG_CONFLICT       16u
#define G780S_ERR_UPGRADE_REQUEST       17u

#define G780S_CFG_VERSION           0x0001
#define G780S_FW_VERSION            0x0100
#define G780S_PROTOCOL_VERSION      0x0100

#define G780S_MODE_MANUAL           0x0000
#define G780S_MODE_AUTO             0x0001

#define G780S_CFG_SOURCE_DEFAULT            1u
#define G780S_CFG_SOURCE_FLASH              2u
#define G780S_CFG_SOURCE_REMOTE_SAVE        3u
#define G780S_CFG_SOURCE_RESTORE_DEFAULTS   4u

#define G780S_RESET_REASON_UNKNOWN          0u
#define G780S_RESET_REASON_POWER_ON         1u
#define G780S_RESET_REASON_PIN              2u
#define G780S_RESET_REASON_SOFTWARE         3u
#define G780S_RESET_REASON_IWDG             4u
#define G780S_RESET_REASON_WWDG             5u
#define G780S_RESET_REASON_LOW_POWER        6u

typedef struct {
    uint16_t push_seq;       /* 上报序号 */
    int16_t  pt100_ch[4];    /* PT100 温度值 ×10 */
    int32_t  weight_ch[4];   /* 称重值，32 位大端，未连接填 -1 */
    uint16_t relay_do;       /* 继电器输出位图 */
    uint16_t relay_di;       /* 继电器输入位图 */
    uint16_t flow_rate;      /* 流量 ×100 */
    uint32_t flow_total;     /* 累计流量 ×1000 */
    uint16_t status;         /* 状态位 */
} G780sSlaveData;

typedef G780sSlaveData ModbusSlaveData_t;

typedef struct
{
    uint16_t sensor_period_ms;
    uint16_t flow_sample_period_ms;
    uint16_t di_debounce_ms;
    uint16_t temp_change_threshold_x10;
    uint32_t pulses_per_liter_x100;
    uint32_t hz_per_lpm_x100;
    uint16_t control_mode;
    uint32_t sequence;
} G780sRemoteConfig;

/**
 * @brief 初始化 G780S 业务层和底层 Modbus 从站引擎。
 * @param 无
 * @retval 无
 */
void G780s_Init(void);

/**
 * @brief 在主循环中处理 G780S 对应的 Modbus 从站任务。
 * @param 无
 * @retval 无
 */
void G780s_Process(void);

/**
 * @brief 兼容旧接口的接收字节入口，内部转发给通用从站引擎。
 * @param byte: 串口收到的单个字节
 * @retval 无
 */
void G780s_RxCallback(uint8_t byte);

/**
 * @brief 更新 G780S 业务寄存器镜像。
 * @param data: 最新的业务数据快照
 * @retval 无
 */
void G780s_UpdateData(const G780sSlaveData *data);

/**
 * @brief 获取 G780S 使用的底层 UART 句柄。
 * @param 无
 * @retval UART_HandleTypeDef*: USART3 句柄指针
 */
UART_HandleTypeDef *G780s_GetHandle(void);

/**
 * @brief 获取继电器翻转控制寄存器的当前值。
 * @param 无
 * @retval 当前寄存器值
 */
uint16_t G780s_GetRelayCtrl(void);

/**
 * @brief 获取继电器按位命令位图寄存器的当前值。
 * @param 无
 * @retval 当前寄存器值
 */
uint16_t G780s_GetRelayBits(void);

/**
 * @brief 获取当前已生效的远程配置快照。
 * @param out_config: 输出配置指针
 * @retval 无
 */
void G780s_GetActiveConfig(G780sRemoteConfig *out_config);

/**
 * @brief 获取当前是否处于自动模式。
 * @param 无
 * @retval uint8_t: 1 表示自动模式，0 表示手动模式
 */
uint8_t G780s_IsAutoMode(void);
uint8_t G780s_ConsumeBootUpgradeRequest(void);

#define ModbusSlave_Task()           G780s_Process()
#define ModbusSlave_Init             G780s_Init
#define ModbusSlave_RxCallback       G780s_RxCallback
#define ModbusSlave_UpdateData       G780s_UpdateData
#define ModbusSlave_GetHandle        G780s_GetHandle
#define ModbusSlave_GetRelayCtrl     G780s_GetRelayCtrl
#define ModbusSlave_GetRelayBits     G780s_GetRelayBits

#endif
