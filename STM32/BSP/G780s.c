#include "G780s.h"
#include "Modbus_Slave.h"
#include "Upgrade.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint16_t g_registers[MODBUS_REG_COUNT];
static G780sRemoteConfig g_active_config;
static G780sRemoteConfig g_staged_config;
static uint32_t g_unlock_deadline = 0;
static uint16_t g_maint_status = 0;
static uint16_t g_last_error = G780S_ERR_NONE;
static uint16_t g_last_bad_addr = 0;
static uint16_t g_last_bad_value = 0;
static uint16_t g_last_bad_read_addr = 0;
static uint16_t g_last_command_result = G780S_ERR_NONE;
static uint16_t g_last_config_source = G780S_CFG_SOURCE_DEFAULT;
static uint16_t g_reset_reason = G780S_RESET_REASON_UNKNOWN;
static uint32_t g_power_on_count = 0;
static uint8_t g_boot_upgrade_pending = 0;

#define G780S_DIAG_FLASH_PAGE_ADDR  0x0807F000UL
#define G780S_CFG_FLASH_PAGE_ADDR   0x0807F800UL
#define G780S_UNLOCK_WINDOW_MS      30000UL
#define G780S_CFG_MAGIC             0x47584346UL  /* GXCF */
#define G780S_DIAG_MAGIC            0x47584449UL  /* GXDI */
#define G780S_DIAG_VERSION          0x0001u

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    G780sRemoteConfig config;
    uint16_t crc16;
} G780sConfigImage;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint32_t power_on_count;
    uint16_t crc16;
} G780sDiagImage;

/**
 * @brief 解析英文月份缩写为数字月份。
 * @param month_str: 3 字节英文月份缩写
 * @retval uint8_t: 解析后的月份，失败返回 0
 */
static uint8_t G780s_ParseBuildMonth(const char *month_str)
{
    static const char *const k_months[12] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    if (month_str == NULL)
    {
        return 0u;
    }

    for (uint8_t i = 0; i < 12u; i++)
    {
        if (strncmp(month_str, k_months[i], 3) == 0)
        {
            return (uint8_t)(i + 1u);
        }
    }

    return 0u;
}

/**
 * @brief 读取编译年份。
 * @param 无
 * @retval uint16_t: 编译年份
 */
static uint16_t G780s_GetBuildYear(void)
{
    return (uint16_t)(((__DATE__[7] - '0') * 1000) +
                      ((__DATE__[8] - '0') * 100) +
                      ((__DATE__[9] - '0') * 10) +
                      (__DATE__[10] - '0'));
}

/**
 * @brief 读取编译月/日并按高字节月份、低字节日期打包。
 * @param 无
 * @retval uint16_t: 打包后的编译月/日
 */
static uint16_t G780s_GetBuildMonthDay(void)
{
    uint8_t month = G780s_ParseBuildMonth(__DATE__);
    uint8_t day_tens = (__DATE__[4] == ' ') ? 0u : (uint8_t)(__DATE__[4] - '0');
    uint8_t day = (uint8_t)(day_tens * 10u + (uint8_t)(__DATE__[5] - '0'));

    return (uint16_t)(((uint16_t)month << 8) | day);
}

/**
 * @brief 读取编译时/分并按高字节小时、低字节分钟打包。
 * @param 无
 * @retval uint16_t: 打包后的编译时/分
 */
static uint16_t G780s_GetBuildHourMinute(void)
{
    uint8_t hour = (uint8_t)((__TIME__[0] - '0') * 10 + (__TIME__[1] - '0'));
    uint8_t minute = (uint8_t)((__TIME__[3] - '0') * 10 + (__TIME__[4] - '0'));

    return (uint16_t)(((uint16_t)hour << 8) | minute);
}

/**
 * @brief 根据 RCC 复位标志判断最近一次重启原因。
 * @param 无
 * @retval uint16_t: 重启原因枚举值
 */
static uint16_t G780s_DetectResetReason(void)
{
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET)
    {
        return G780S_RESET_REASON_IWDG;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_WWDGRST) != RESET)
    {
        return G780S_RESET_REASON_WWDG;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) != RESET)
    {
        return G780S_RESET_REASON_SOFTWARE;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_LPWRRST) != RESET)
    {
        return G780S_RESET_REASON_LOW_POWER;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) != RESET)
    {
        return G780S_RESET_REASON_PIN;
    }
    if (__HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) != RESET)
    {
        return G780S_RESET_REASON_POWER_ON;
    }

    return G780S_RESET_REASON_UNKNOWN;
}

/**
 * @brief 计算远程配置镜像使用的 CRC16 校验值。
 * @param buf: 待计算数据缓冲区首地址
 * @param len: 参与计算的数据长度，单位字节
 * @retval uint16_t: 计算得到的 CRC16
 */
static uint16_t G780s_CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 0x0001U)
            {
                crc >>= 1;
                crc ^= 0xA001U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/**
 * @brief 填充一份默认远程配置。
 * @param config: 待填充的配置结构体指针
 * @retval 无
 */
static void G780s_SetDefaults(G780sRemoteConfig *config)
{
    if (config == NULL)
    {
        return;
    }

    config->sensor_period_ms = 2000;
    config->flow_sample_period_ms = 1000;
    config->di_debounce_ms = 50;
    config->temp_change_threshold_x10 = 10;
    config->pulses_per_liter_x100 = 66000UL;
    config->hz_per_lpm_x100 = 1100UL;
    config->control_mode = G780S_MODE_MANUAL;
    config->sequence = 1UL;
}

/**
 * @brief 校验单个 16 位配置字段的取值是否合法。
 * @param reg_addr: 配置寄存器地址
 * @param reg_value: 待写入的寄存器值
 * @retval uint8_t: 0 表示合法，非 0 表示对应的维护错误码
 */
static uint8_t G780s_ValidateConfigRegisterValue(uint16_t reg_addr, uint16_t reg_value)
{
    switch (reg_addr)
    {
        case REG_CFG_SENSOR_PERIOD_MS:
            return (reg_value >= 200u && reg_value <= 60000u) ? G780S_ERR_NONE : G780S_ERR_SENSOR_PERIOD_RANGE;

        case REG_CFG_FLOW_SAMPLE_MS:
            return (reg_value >= 100u && reg_value <= 10000u) ? G780S_ERR_NONE : G780S_ERR_FLOW_SAMPLE_RANGE;

        case REG_CFG_DI_DEBOUNCE_MS:
            return (reg_value >= 10u && reg_value <= 5000u) ? G780S_ERR_NONE : G780S_ERR_DI_DEBOUNCE_RANGE;

        case REG_CFG_TEMP_THRESHOLD_X10:
            return (reg_value >= 1u && reg_value <= 500u) ? G780S_ERR_NONE : G780S_ERR_TEMP_THRESHOLD_RANGE;

        case REG_CFG_CONTROL_MODE:
            return ((reg_value == G780S_MODE_MANUAL) || (reg_value == G780S_MODE_AUTO)) ?
                G780S_ERR_NONE : G780S_ERR_CONTROL_MODE_INVALID;

        default:
            break;
    }

    return G780S_ERR_NONE;
}

/**
 * @brief 校验整份远程配置参数范围和组合关系是否合法。
 * @param config: 待校验的配置结构体指针
 * @retval uint8_t: 0 表示合法，非 0 表示对应的维护错误码
 */
static uint8_t G780s_ValidateConfig(const G780sRemoteConfig *config)
{
    if (config == NULL)
    {
        return G780S_ERR_CONFIG_CONFLICT;
    }

    if (G780s_ValidateConfigRegisterValue(REG_CFG_SENSOR_PERIOD_MS, config->sensor_period_ms) != G780S_ERR_NONE)
    {
        return G780S_ERR_SENSOR_PERIOD_RANGE;
    }
    if (G780s_ValidateConfigRegisterValue(REG_CFG_FLOW_SAMPLE_MS, config->flow_sample_period_ms) != G780S_ERR_NONE)
    {
        return G780S_ERR_FLOW_SAMPLE_RANGE;
    }
    if (G780s_ValidateConfigRegisterValue(REG_CFG_DI_DEBOUNCE_MS, config->di_debounce_ms) != G780S_ERR_NONE)
    {
        return G780S_ERR_DI_DEBOUNCE_RANGE;
    }
    if (G780s_ValidateConfigRegisterValue(REG_CFG_TEMP_THRESHOLD_X10, config->temp_change_threshold_x10) != G780S_ERR_NONE)
    {
        return G780S_ERR_TEMP_THRESHOLD_RANGE;
    }
    if (config->pulses_per_liter_x100 < 100u || config->pulses_per_liter_x100 > 10000000UL)
    {
        return G780S_ERR_PPL_RANGE;
    }
    if (config->hz_per_lpm_x100 < 10u || config->hz_per_lpm_x100 > 1000000UL)
    {
        return G780S_ERR_HZ_PER_LPM_RANGE;
    }
    if (G780s_ValidateConfigRegisterValue(REG_CFG_CONTROL_MODE, config->control_mode) != G780S_ERR_NONE)
    {
        return G780S_ERR_CONTROL_MODE_INVALID;
    }

    /* 组合校验，避免明显不合理的采样关系进入系统。 */
    if (config->flow_sample_period_ms > config->sensor_period_ms)
    {
        return G780S_ERR_CONFIG_CONFLICT;
    }
    if (config->di_debounce_ms >= config->sensor_period_ms)
    {
        return G780S_ERR_CONFIG_CONFLICT;
    }

    return G780S_ERR_NONE;
}

/**
 * @brief 记录最近一次非法写入的地址和值。
 * @param reg_addr: 非法写入的寄存器地址
 * @param reg_value: 非法写入的寄存器值
 * @retval 无
 */
static void G780s_RecordBadWrite(uint16_t reg_addr, uint16_t reg_value)
{
    g_last_bad_addr = reg_addr;
    g_last_bad_value = reg_value;
    g_registers[REG_DIAG_LAST_BAD_ADDR] = reg_addr;
    g_registers[REG_DIAG_LAST_BAD_VALUE] = reg_value;
}

/**
 * @brief 更新维护错误码，并同步错误状态位。
 * @param error_code: 最新错误码，取值见 G780S_ERR_xxx
 * @retval 无
 */
static void G780s_SetError(uint16_t error_code)
{
    g_last_error = error_code;
    g_registers[REG_MAINT_LAST_ERROR] = error_code;

    if (error_code == G780S_ERR_NONE)
    {
        g_maint_status &= (uint16_t)~G780S_STATUS_ERROR;
    }
    else
    {
        g_maint_status |= G780S_STATUS_ERROR;
    }
}

/**
 * @brief 把配置结构体展开到 Modbus 配置寄存器区。
 * @param config: 待同步到寄存器的配置结构体指针
 * @retval 无
 */
static void G780s_SyncConfigRegisters(const G780sRemoteConfig *config)
{
    if (config == NULL)
    {
        return;
    }

    g_registers[REG_CFG_SENSOR_PERIOD_MS] = config->sensor_period_ms;
    g_registers[REG_CFG_FLOW_SAMPLE_MS] = config->flow_sample_period_ms;
    g_registers[REG_CFG_DI_DEBOUNCE_MS] = config->di_debounce_ms;
    g_registers[REG_CFG_TEMP_THRESHOLD_X10] = config->temp_change_threshold_x10;
    g_registers[REG_CFG_PPL_X100_H] = (uint16_t)((config->pulses_per_liter_x100 >> 16) & 0xFFFFu);
    g_registers[REG_CFG_PPL_X100_L] = (uint16_t)(config->pulses_per_liter_x100 & 0xFFFFu);
    g_registers[REG_CFG_HZ_PER_LPM_X100_H] = (uint16_t)((config->hz_per_lpm_x100 >> 16) & 0xFFFFu);
    g_registers[REG_CFG_HZ_PER_LPM_X100_L] = (uint16_t)(config->hz_per_lpm_x100 & 0xFFFFu);
    g_registers[REG_CFG_CONTROL_MODE] = config->control_mode;
}

/**
 * @brief 刷新维护状态相关寄存器，供远端轮询查看。
 * @param 无
 * @retval 无
 */
static void G780s_UpdateMaintenanceRegisters(void)
{
    uint32_t uptime_seconds = HAL_GetTick() / 1000UL;
    uint32_t crc_error_count = Modbus_Slave_GetCrcErrorCount();
    uint32_t uart_error_count = Modbus_Slave_GetUartErrorCount();
    uint16_t remain_s = 0;

    /* 解锁状态下对外提供剩余操作窗口，便于上位机判断是否需要重新解锁。 */
    if ((g_maint_status & G780S_STATUS_UNLOCKED) != 0u)
    {
        uint32_t now = HAL_GetTick();
        if (g_unlock_deadline > now)
        {
            remain_s = (uint16_t)((g_unlock_deadline - now + 999UL) / 1000UL);
        }
    }

    g_registers[REG_MAINT_STATUS] = g_maint_status;
    g_registers[REG_MAINT_LAST_ERROR] = g_last_error;
    g_registers[REG_MAINT_CFG_VERSION] = G780S_CFG_VERSION;
    g_registers[REG_MAINT_CFG_SEQUENCE_H] = (uint16_t)((g_active_config.sequence >> 16) & 0xFFFFu);
    g_registers[REG_MAINT_CFG_SEQUENCE_L] = (uint16_t)(g_active_config.sequence & 0xFFFFu);
    g_registers[REG_MAINT_UNLOCK_REMAIN_S] = remain_s;
    g_registers[REG_DIAG_FW_VERSION] = G780S_FW_VERSION;
    g_registers[REG_DIAG_PROTOCOL_VERSION] = G780S_PROTOCOL_VERSION;
    g_registers[REG_DIAG_BUILD_YEAR] = G780s_GetBuildYear();
    g_registers[REG_DIAG_BUILD_MONTH_DAY] = G780s_GetBuildMonthDay();
    g_registers[REG_DIAG_BUILD_HOUR_MIN] = G780s_GetBuildHourMinute();
    g_registers[REG_DIAG_UPTIME_H] = (uint16_t)((uptime_seconds >> 16) & 0xFFFFu);
    g_registers[REG_DIAG_UPTIME_L] = (uint16_t)(uptime_seconds & 0xFFFFu);
    g_registers[REG_DIAG_POWER_ON_COUNT_H] = (uint16_t)((g_power_on_count >> 16) & 0xFFFFu);
    g_registers[REG_DIAG_POWER_ON_COUNT_L] = (uint16_t)(g_power_on_count & 0xFFFFu);
    g_registers[REG_DIAG_RESET_REASON] = g_reset_reason;
    g_registers[REG_DIAG_LAST_BAD_ADDR] = g_last_bad_addr;
    g_registers[REG_DIAG_LAST_BAD_VALUE] = g_last_bad_value;
    g_registers[REG_DIAG_LAST_CFG_SOURCE] = g_last_config_source;
    g_registers[REG_DIAG_MODBUS_CRC_ERR_H] = (uint16_t)((crc_error_count >> 16) & 0xFFFFu);
    g_registers[REG_DIAG_MODBUS_CRC_ERR_L] = (uint16_t)(crc_error_count & 0xFFFFu);
    g_registers[REG_DIAG_UART_ERR_H] = (uint16_t)((uart_error_count >> 16) & 0xFFFFu);
    g_registers[REG_DIAG_UART_ERR_L] = (uint16_t)(uart_error_count & 0xFFFFu);
    g_registers[REG_DIAG_LAST_CMD_RESULT] = g_last_command_result;
    g_registers[REG_DIAG_LAST_BAD_READ_ADDR] = g_last_bad_read_addr;
}

/**
 * @brief 从配置寄存器区读取一份待提交的远程配置。
 * @param config: 输出配置结构体指针
 * @retval uint8_t: 0 表示读取并校验成功，非 0 表示维护错误码
 */
static uint8_t G780s_LoadConfigFromRegisters(G780sRemoteConfig *config)
{
    if (config == NULL)
    {
        return G780S_ERR_CONFIG_CONFLICT;
    }

    config->sensor_period_ms = g_registers[REG_CFG_SENSOR_PERIOD_MS];
    config->flow_sample_period_ms = g_registers[REG_CFG_FLOW_SAMPLE_MS];
    config->di_debounce_ms = g_registers[REG_CFG_DI_DEBOUNCE_MS];
    config->temp_change_threshold_x10 = g_registers[REG_CFG_TEMP_THRESHOLD_X10];
    config->pulses_per_liter_x100 = ((uint32_t)g_registers[REG_CFG_PPL_X100_H] << 16) |
                                    g_registers[REG_CFG_PPL_X100_L];
    config->hz_per_lpm_x100 = ((uint32_t)g_registers[REG_CFG_HZ_PER_LPM_X100_H] << 16) |
                              g_registers[REG_CFG_HZ_PER_LPM_X100_L];
    config->control_mode = g_registers[REG_CFG_CONTROL_MODE];
    config->sequence = g_active_config.sequence + 1UL;

    return G780s_ValidateConfig(config);
}

/**
 * @brief 将配置镜像保存到片内 Flash 最后一页。
 * @param config: 待保存的配置结构体指针
 * @retval uint8_t: 0 表示保存成功，非 0 表示维护错误码
 */
static uint8_t G780s_SaveConfigToFlash(const G780sRemoteConfig *config)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;
    G780sConfigImage image;
    uint16_t *halfwords = (uint16_t *)&image;
    uint32_t halfword_count = sizeof(image) / 2U;
    const G780sConfigImage *flash_image = (const G780sConfigImage *)G780S_CFG_FLASH_PAGE_ADDR;

    if (config == NULL)
    {
        return G780S_ERR_CONFIG_CONFLICT;
    }

    memset(&image, 0xFF, sizeof(image));
    image.magic = G780S_CFG_MAGIC;
    image.version = G780S_CFG_VERSION;
    image.payload_size = (uint16_t)sizeof(G780sRemoteConfig);
    image.config = *config;
    image.crc16 = G780s_CRC16((const uint8_t *)&image, (uint16_t)offsetof(G780sConfigImage, crc16));

    /* 参数页采用“整页擦除 + 顺序写入 + 回读校验”的简单可靠策略。 */
    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = G780S_CFG_FLASH_PAGE_ADDR;
    erase.NbPages = 1;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return G780S_ERR_FLASH_ERASE;
    }

    for (uint32_t i = 0; i < halfword_count; i++)
    {
        uint32_t address = G780S_CFG_FLASH_PAGE_ADDR + i * 2UL;
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, halfwords[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return G780S_ERR_FLASH_PROGRAM;
        }
    }

    HAL_FLASH_Lock();

    if (memcmp(flash_image, &image, sizeof(image)) != 0)
    {
        return G780S_ERR_FLASH_VERIFY;
    }

    return G780S_ERR_NONE;
}

/**
 * @brief 从诊断持久化页读取上电次数。
 * @param power_on_count: 输出上电次数指针
 * @retval uint8_t: 0 表示读取成功，非 0 表示诊断页为空或损坏
 */
static uint8_t G780s_LoadDiagFromFlash(uint32_t *power_on_count)
{
    const G780sDiagImage *image = (const G780sDiagImage *)G780S_DIAG_FLASH_PAGE_ADDR;
    uint16_t crc_calc;

    if (power_on_count == NULL)
    {
        return G780S_ERR_FLASH_CRC;
    }

    if (image->magic == 0xFFFFFFFFUL && image->version == 0xFFFFu)
    {
        return G780S_ERR_FLASH_EMPTY;
    }
    if (image->magic != G780S_DIAG_MAGIC ||
        image->version != G780S_DIAG_VERSION ||
        image->payload_size != sizeof(uint32_t))
    {
        return G780S_ERR_FLASH_CRC;
    }

    crc_calc = G780s_CRC16((const uint8_t *)image, (uint16_t)offsetof(G780sDiagImage, crc16));
    if (crc_calc != image->crc16)
    {
        return G780S_ERR_FLASH_CRC;
    }

    *power_on_count = image->power_on_count;
    return G780S_ERR_NONE;
}

/**
 * @brief 把上电次数保存到独立诊断页。
 * @param power_on_count: 待保存的上电次数
 * @retval uint8_t: 0 表示保存成功，非 0 表示维护错误码
 */
static uint8_t G780s_SaveDiagToFlash(uint32_t power_on_count)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;
    G780sDiagImage image;
    uint16_t *halfwords = (uint16_t *)&image;
    uint32_t halfword_count = sizeof(image) / 2U;
    const G780sDiagImage *flash_image = (const G780sDiagImage *)G780S_DIAG_FLASH_PAGE_ADDR;

    memset(&image, 0xFF, sizeof(image));
    image.magic = G780S_DIAG_MAGIC;
    image.version = G780S_DIAG_VERSION;
    image.payload_size = (uint16_t)sizeof(uint32_t);
    image.power_on_count = power_on_count;
    image.crc16 = G780s_CRC16((const uint8_t *)&image, (uint16_t)offsetof(G780sDiagImage, crc16));

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = G780S_DIAG_FLASH_PAGE_ADDR;
    erase.NbPages = 1;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return G780S_ERR_FLASH_ERASE;
    }

    for (uint32_t i = 0; i < halfword_count; i++)
    {
        uint32_t address = G780S_DIAG_FLASH_PAGE_ADDR + i * 2UL;
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, halfwords[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return G780S_ERR_FLASH_PROGRAM;
        }
    }

    HAL_FLASH_Lock();

    if (memcmp(flash_image, &image, sizeof(image)) != 0)
    {
        return G780S_ERR_FLASH_VERIFY;
    }

    return G780S_ERR_NONE;
}

/**
 * @brief 刷新上电次数并持久化到独立诊断页。
 * @param 无
 * @retval 无
 */
static void G780s_InitPowerOnCounter(void)
{
    uint32_t stored_count = 0;
    uint8_t err = G780s_LoadDiagFromFlash(&stored_count);

    if (err == G780S_ERR_NONE)
    {
        g_power_on_count = stored_count + 1UL;
    }
    else
    {
        g_power_on_count = 1UL;
    }

    if (G780s_SaveDiagToFlash(g_power_on_count) != G780S_ERR_NONE)
    {
        printf("[G780s] Warn: power-on counter persist failed\r\n");
    }
}

/**
 * @brief 从片内 Flash 读取并校验远程配置镜像。
 * @param config: 输出配置结构体指针
 * @retval uint8_t: 0 表示读取成功，非 0 表示维护错误码
 */
static uint8_t G780s_LoadConfigFromFlash(G780sRemoteConfig *config)
{
    const G780sConfigImage *image = (const G780sConfigImage *)G780S_CFG_FLASH_PAGE_ADDR;
    uint16_t crc_calc;

    if (config == NULL)
    {
        return G780S_ERR_CONFIG_CONFLICT;
    }

    if (image->magic == 0xFFFFFFFFUL && image->version == 0xFFFFu)
    {
        return G780S_ERR_FLASH_EMPTY;
    }
    if (image->magic != G780S_CFG_MAGIC ||
        image->version != G780S_CFG_VERSION ||
        image->payload_size != sizeof(G780sRemoteConfig))
    {
        return G780S_ERR_FLASH_CRC;
    }

    crc_calc = G780s_CRC16((const uint8_t *)image, (uint16_t)offsetof(G780sConfigImage, crc16));
    if (crc_calc != image->crc16)
    {
        return G780S_ERR_FLASH_CRC;
    }

    *config = image->config;
    return G780s_ValidateConfig(config);
}

/**
 * @brief 将一份配置应用为当前生效配置，并同步寄存器镜像。
 * @param config: 待生效的配置结构体指针
 * @param loaded_from_flash: 1 表示本次来自 Flash 加载，0 表示默认值或运行时应用
 * @retval 无
 */
static void G780s_ApplyConfig(const G780sRemoteConfig *config, uint8_t loaded_from_flash)
{
    if (config == NULL)
    {
        return;
    }

    g_active_config = *config;
    g_staged_config = *config;
    G780s_SyncConfigRegisters(config);

    g_maint_status &= (uint16_t)~G780S_STATUS_STAGED_DIRTY;
    g_maint_status &= (uint16_t)~G780S_STATUS_SAVE_OK;

    if (loaded_from_flash != 0u)
    {
        g_maint_status |= G780S_STATUS_CONFIG_LOADED;
    }

    G780s_UpdateMaintenanceRegisters();
}

/**
 * @brief 判断指定寄存器地址是否属于远程配置区。
 * @param reg_addr: 待判断的 Modbus 寄存器地址
 * @retval uint8_t: 1 表示属于配置区，0 表示不属于
 */
static uint8_t G780s_IsConfigRegister(uint16_t reg_addr)
{
    return (reg_addr >= REG_CFG_SENSOR_PERIOD_MS && reg_addr <= REG_CFG_CONTROL_MODE) ? 1u : 0u;
}

/**
 * @brief 结束维护解锁窗口并更新状态寄存器。
 * @param 无
 * @retval 无
 */
static void G780s_LockMaintenance(void)
{
    g_unlock_deadline = 0;
    g_maint_status &= (uint16_t)~G780S_STATUS_UNLOCKED;
    G780s_UpdateMaintenanceRegisters();
}

/**
 * @brief 打开维护解锁窗口，允许远端在限定时间内修改参数。
 * @param 无
 * @retval 无
 */
static void G780s_UnlockMaintenance(void)
{
    g_unlock_deadline = HAL_GetTick() + G780S_UNLOCK_WINDOW_MS;
    g_maint_status |= G780S_STATUS_UNLOCKED;
    G780s_SetError(G780S_ERR_NONE);
    G780s_UpdateMaintenanceRegisters();
}

/**
 * @brief 将暂存寄存器中的配置校验并保存到 Flash，同时切换为生效配置。
 * @param 无
 * @retval uint8_t: 0 表示提交成功，非 0 表示维护错误码
 */
static uint8_t G780s_CommitStagedConfig(void)
{
    uint8_t err = G780s_LoadConfigFromRegisters(&g_staged_config);
    if (err != G780S_ERR_NONE)
    {
        return err;
    }

    err = G780s_SaveConfigToFlash(&g_staged_config);
    if (err != G780S_ERR_NONE)
    {
        return err;
    }

    G780s_ApplyConfig(&g_staged_config, 1u);
    g_maint_status |= G780S_STATUS_SAVE_OK;
    return G780S_ERR_NONE;
}

/**
 * @brief 执行远端下发的维护命令。
 * @param command: 维护命令字，取值见 G780S_CMD_xxx
 * @retval uint8_t: 0 表示命令执行成功，非 0 表示维护错误码
 */
static uint8_t G780s_HandleMaintenanceCommand(uint16_t command)
{
    uint8_t err = G780S_ERR_NONE;
    G780sRemoteConfig defaults;

    switch (command)
    {
        case G780S_CMD_NONE:
            break;

        case G780S_CMD_APPLY_SAVE:
            if ((g_maint_status & G780S_STATUS_UNLOCKED) == 0u)
            {
                return G780S_ERR_LOCKED;
            }
            err = G780s_CommitStagedConfig();
            if (err == G780S_ERR_NONE)
            {
                g_last_config_source = G780S_CFG_SOURCE_REMOTE_SAVE;
            }
            break;

        case G780S_CMD_DISCARD_STAGED:
            if ((g_maint_status & G780S_STATUS_UNLOCKED) == 0u)
            {
                return G780S_ERR_LOCKED;
            }
            /* 放弃暂存时直接把当前生效配置重新覆盖到配置寄存器区。 */
            G780s_SyncConfigRegisters(&g_active_config);
            g_staged_config = g_active_config;
            g_maint_status &= (uint16_t)~G780S_STATUS_STAGED_DIRTY;
            break;

        case G780S_CMD_RESTORE_DEFAULTS:
            if ((g_maint_status & G780S_STATUS_UNLOCKED) == 0u)
            {
                return G780S_ERR_LOCKED;
            }
            G780s_SetDefaults(&defaults);
            defaults.sequence = g_active_config.sequence + 1UL;
            err = G780s_SaveConfigToFlash(&defaults);
            if (err == G780S_ERR_NONE)
            {
                G780s_ApplyConfig(&defaults, 1u);
                g_last_config_source = G780S_CFG_SOURCE_RESTORE_DEFAULTS;
                g_maint_status |= G780S_STATUS_SAVE_OK;
            }
            break;

        case G780S_CMD_CLEAR_ERROR:
            G780s_SetError(G780S_ERR_NONE);
            break;

        case G780S_CMD_ENTER_BOOT_UPGRADE:
            if ((g_maint_status & G780S_STATUS_UNLOCKED) == 0u)
            {
                return G780S_ERR_LOCKED;
            }
            err = Upgrade_RequestBootMode(UPGRADE_REQUEST_SOURCE_G780S, 0u);
            if (err == 0u)
            {
                g_boot_upgrade_pending = 1u;
            }
            else
            {
                err = G780S_ERR_UPGRADE_REQUEST;
            }
            break;

        default:
            err = G780S_ERR_BAD_COMMAND;
            break;
    }

    g_registers[REG_MAINT_COMMAND] = G780S_CMD_NONE;
    g_last_command_result = err;
    G780s_UpdateMaintenanceRegisters();
    return err;
}

/**
 * @brief 基于当前暂存配置和寄存器写入值构造一份候选配置。
 * @param reg_addr: 正在写入的配置寄存器地址
 * @param reg_value: 正在写入的寄存器值
 * @param candidate: 输出候选配置结构体指针
 * @retval uint8_t: 0 表示构造成功，非 0 表示维护错误码
 */
static uint8_t G780s_BuildCandidateConfig(uint16_t reg_addr,
                                          uint16_t reg_value,
                                          G780sRemoteConfig *candidate)
{
    if (candidate == NULL)
    {
        return G780S_ERR_CONFIG_CONFLICT;
    }

    *candidate = g_staged_config;

    switch (reg_addr)
    {
        case REG_CFG_SENSOR_PERIOD_MS:
            candidate->sensor_period_ms = reg_value;
            break;

        case REG_CFG_FLOW_SAMPLE_MS:
            candidate->flow_sample_period_ms = reg_value;
            break;

        case REG_CFG_DI_DEBOUNCE_MS:
            candidate->di_debounce_ms = reg_value;
            break;

        case REG_CFG_TEMP_THRESHOLD_X10:
            candidate->temp_change_threshold_x10 = reg_value;
            break;

        case REG_CFG_PPL_X100_H:
            candidate->pulses_per_liter_x100 &= 0x0000FFFFUL;
            candidate->pulses_per_liter_x100 |= ((uint32_t)reg_value << 16);
            break;

        case REG_CFG_PPL_X100_L:
            candidate->pulses_per_liter_x100 &= 0xFFFF0000UL;
            candidate->pulses_per_liter_x100 |= reg_value;
            break;

        case REG_CFG_HZ_PER_LPM_X100_H:
            candidate->hz_per_lpm_x100 &= 0x0000FFFFUL;
            candidate->hz_per_lpm_x100 |= ((uint32_t)reg_value << 16);
            break;

        case REG_CFG_HZ_PER_LPM_X100_L:
            candidate->hz_per_lpm_x100 &= 0xFFFF0000UL;
            candidate->hz_per_lpm_x100 |= reg_value;
            break;

        case REG_CFG_CONTROL_MODE:
            candidate->control_mode = reg_value;
            break;

        default:
            return G780S_ERR_BAD_COMMAND;
    }

    return G780S_ERR_NONE;
}

/**
 * @brief 校验并接收一笔远程配置寄存器写入。
 * @param reg_addr: 配置寄存器地址
 * @param reg_value: 待写入的寄存器值
 * @retval uint8_t: 0 表示写入成功，非 0 表示维护错误码
 */
static uint8_t G780s_WriteConfigRegister(uint16_t reg_addr, uint16_t reg_value)
{
    G780sRemoteConfig candidate;
    uint8_t err;

    err = G780s_BuildCandidateConfig(reg_addr, reg_value, &candidate);
    if (err != G780S_ERR_NONE)
    {
        return err;
    }

    /* 16 位单寄存器参数在写入当下直接做范围拦截。 */
    err = G780s_ValidateConfigRegisterValue(reg_addr, reg_value);
    if (err != G780S_ERR_NONE)
    {
        return err;
    }

    /* 组合关系校验只针对不会影响 32 位分拆写入的字段。 */
    switch (reg_addr)
    {
        case REG_CFG_SENSOR_PERIOD_MS:
        case REG_CFG_FLOW_SAMPLE_MS:
        case REG_CFG_DI_DEBOUNCE_MS:
        case REG_CFG_CONTROL_MODE:
            err = G780s_ValidateConfig(&candidate);
            if (err != G780S_ERR_NONE)
            {
                return err;
            }
            break;

        default:
            break;
    }

    g_staged_config = candidate;
    G780s_SyncConfigRegisters(&g_staged_config);
    g_maint_status |= G780S_STATUS_STAGED_DIRTY;
    g_maint_status &= (uint16_t)~G780S_STATUS_SAVE_OK;
    return G780S_ERR_NONE;
}

/**
 * @brief 读取一段 G780S 业务寄存器镜像。
 * @param start_addr: 起始寄存器地址
 * @param reg_count: 连续读取寄存器数量
 * @param out_regs: 输出寄存器缓冲区
 * @param context: 业务上下文，当前未使用
 * @retval uint8_t: 0 表示成功，非 0 为 Modbus 异常码
 */
static uint8_t G780s_ReadRegisters(uint16_t start_addr,
                                   uint16_t reg_count,
                                   uint16_t *out_regs,
                                   void *context)
{
    (void)context;

    if (out_regs == NULL)
    {
        return 0x03;
    }

    if ((uint32_t)start_addr + reg_count > MODBUS_REG_COUNT)
    {
        g_last_bad_read_addr = start_addr;
        return 0x02;
    }

    __disable_irq();
    for (uint16_t i = 0; i < reg_count; i++)
    {
        out_regs[i] = g_registers[start_addr + i];
    }
    __enable_irq();

    return 0;
}

/**
 * @brief 处理云端写单寄存器请求，包含继电器控制和远程维护入口。
 * @param reg_addr: 目标寄存器地址
 * @param reg_value: 待写入的寄存器值
 * @param context: 业务上下文，当前未使用
 * @retval uint8_t: 0 表示成功，非 0 为 Modbus 异常码
 */
static uint8_t G780s_WriteSingleRegister(uint16_t reg_addr,
                                         uint16_t reg_value,
                                         void *context)
{
    uint8_t err;

    (void)context;

    if (reg_addr == REG_MAINT_UNLOCK)
    {
        if (reg_value == G780S_UNLOCK_KEY)
        {
            G780s_UnlockMaintenance();
            g_registers[REG_MAINT_UNLOCK] = 0;
            return 0;
        }

        G780s_RecordBadWrite(reg_addr, reg_value);
        G780s_SetError(G780S_ERR_INVALID_UNLOCK_KEY);
        return 0x03;
    }

    if (reg_addr == REG_MAINT_COMMAND)
    {
        /* 维护命令寄存器是一次性触发型写入，执行完成后自动清零。 */
        g_registers[REG_MAINT_COMMAND] = reg_value;
        err = G780s_HandleMaintenanceCommand(reg_value);
        if (err != G780S_ERR_NONE)
        {
            G780s_RecordBadWrite(reg_addr, reg_value);
            g_last_command_result = err;
            G780s_SetError(err);
            G780s_UpdateMaintenanceRegisters();
            return (err == G780S_ERR_LOCKED) ? 0x03 : 0x04;
        }

        g_last_command_result = G780S_ERR_NONE;
        G780s_SetError(G780S_ERR_NONE);
        G780s_UpdateMaintenanceRegisters();
        return 0;
    }

    if (G780s_IsConfigRegister(reg_addr) != 0u)
    {
        /* 参数区写入只更新暂存值，不直接写 Flash。 */
        if ((g_maint_status & G780S_STATUS_UNLOCKED) == 0u)
        {
            G780s_RecordBadWrite(reg_addr, reg_value);
            G780s_SetError(G780S_ERR_LOCKED);
            G780s_UpdateMaintenanceRegisters();
            return 0x03;
        }

        err = G780s_WriteConfigRegister(reg_addr, reg_value);
        if (err != G780S_ERR_NONE)
        {
            G780s_RecordBadWrite(reg_addr, reg_value);
            G780s_SetError(err);
            G780s_UpdateMaintenanceRegisters();
            return 0x03;
        }

        G780s_SetError(G780S_ERR_NONE);
        G780s_UpdateMaintenanceRegisters();
        return 0;
    }

    if (reg_addr == REG_RELAY_CTRL || reg_addr == REG_RELAY_CMD_BITS)
    {
        g_registers[reg_addr] = reg_value;
        return 0;
    }

    G780s_RecordBadWrite(reg_addr, reg_value);
    return 0x02;
}

/**
 * @brief 处理云端写单线圈请求，并映射到继电器按位命令寄存器。
 * @param coil_addr: 线圈地址
 * @param coil_value: 线圈目标状态，true 为置位，false 为清位
 * @param context: 业务上下文，当前未使用
 * @retval uint8_t: 0 表示成功，非 0 为 Modbus 异常码
 */
static uint8_t G780s_WriteSingleCoil(uint16_t coil_addr,
                                     bool coil_value,
                                     void *context)
{
    uint16_t bit_num;
    uint16_t current;

    (void)context;

    bit_num = (uint16_t)(coil_addr % 16U);
    if (bit_num > 15U)
    {
        return 0x02;
    }

    current = g_registers[REG_RELAY_CMD_BITS];
    if (coil_value)
    {
        current |= (uint16_t)(1u << bit_num);
    }
    else
    {
        current &= (uint16_t)~(1u << bit_num);
    }

    g_registers[REG_RELAY_CMD_BITS] = current;
    return 0;
}

/**
 * @brief 初始化 G780S 业务层、维护配置和底层 Modbus 从站引擎。
 * @param 无
 * @retval 无
 */
void G780s_Init(void)
{
    ModbusSlaveConfig config = {
        .slave_addr = MODBUS_SLAVE_ADDR,
        .context = NULL,
        .read_holding_registers = G780s_ReadRegisters,
        .read_input_registers = G780s_ReadRegisters,
        .write_single_register = G780s_WriteSingleRegister,
        .write_single_coil = G780s_WriteSingleCoil,
    };
    G780sRemoteConfig flash_config;
    uint8_t load_err;

    memset(g_registers, 0, sizeof(g_registers));
    Modbus_Slave_Init(&config);
    g_reset_reason = G780s_DetectResetReason();
    __HAL_RCC_CLEAR_RESET_FLAGS();
    G780s_InitPowerOnCounter();

    /* 先装入默认参数，再尝试用 Flash 中的持久化配置覆盖。 */
    G780s_SetDefaults(&g_active_config);
    g_staged_config = g_active_config;
    g_last_config_source = G780S_CFG_SOURCE_DEFAULT;
    G780s_SetError(G780S_ERR_NONE);
    g_last_command_result = G780S_ERR_NONE;

    load_err = G780s_LoadConfigFromFlash(&flash_config);
    if (load_err == G780S_ERR_NONE)
    {
        G780s_ApplyConfig(&flash_config, 1u);
        g_last_config_source = G780S_CFG_SOURCE_FLASH;
    }
    else
    {
        G780s_ApplyConfig(&g_active_config, 0u);
        G780s_SetError(load_err);
    }

    g_registers[REG_MAINT_UNLOCK] = 0;
    g_registers[REG_MAINT_COMMAND] = G780S_CMD_NONE;
    G780s_UpdateMaintenanceRegisters();

    printf("[G780s] Init OK (Addr=%d, USART3+PA5, layered RTU)\r\n", MODBUS_SLAVE_ADDR);
}

/**
 * @brief 在主循环中推进 G780S 相关任务处理。
 * @param 无
 * @retval 无
 */
void G780s_Process(void)
{
    /* 维护解锁窗口超时后自动重新上锁，避免设备长期暴露在可写状态。 */
    if (((g_maint_status & G780S_STATUS_UNLOCKED) != 0u) &&
        (HAL_GetTick() >= g_unlock_deadline))
    {
        G780s_LockMaintenance();
    }

    G780s_UpdateMaintenanceRegisters();
    Modbus_Slave_Process();
}

/**
 * @brief 兼容旧接口的字节接收入口，内部转发到通用 Modbus 从站引擎。
 * @param byte: 串口接收到的单个字节
 * @retval 无
 */
void G780s_RxCallback(uint8_t byte)
{
    Modbus_Slave_RxCallback(byte);
}

/**
 * @brief 刷新现场采集值到 G780S 寄存器镜像，供远端轮询读取。
 * @param data: 最新业务数据快照
 * @retval 无
 */
void G780s_UpdateData(const G780sSlaveData *data)
{
    if (data == NULL)
    {
        return;
    }

    __disable_irq();

    /* 前半段是现场数据寄存器区，后半段配置寄存器区由维护逻辑单独管理。 */
    g_registers[REG_PUSH_SEQ] = data->push_seq;

    g_registers[REG_PT100_CH1] = (uint16_t)data->pt100_ch[0];
    g_registers[REG_PT100_CH2] = (uint16_t)data->pt100_ch[1];
    g_registers[REG_PT100_CH3] = (uint16_t)data->pt100_ch[2];
    g_registers[REG_PT100_CH4] = (uint16_t)data->pt100_ch[3];

    g_registers[REG_WEIGHT_CH1_H] = (uint16_t)((data->weight_ch[0] >> 16) & 0xFFFF);
    g_registers[REG_WEIGHT_CH1_L] = (uint16_t)(data->weight_ch[0] & 0xFFFF);
    g_registers[REG_WEIGHT_CH2_H] = (uint16_t)((data->weight_ch[1] >> 16) & 0xFFFF);
    g_registers[REG_WEIGHT_CH2_L] = (uint16_t)(data->weight_ch[1] & 0xFFFF);
    g_registers[REG_WEIGHT_CH3_H] = (uint16_t)((data->weight_ch[2] >> 16) & 0xFFFF);
    g_registers[REG_WEIGHT_CH3_L] = (uint16_t)(data->weight_ch[2] & 0xFFFF);
    g_registers[REG_WEIGHT_CH4_H] = (uint16_t)((data->weight_ch[3] >> 16) & 0xFFFF);
    g_registers[REG_WEIGHT_CH4_L] = (uint16_t)(data->weight_ch[3] & 0xFFFF);

    g_registers[REG_FLOW_RATE] = data->flow_rate;
    g_registers[REG_FLOW_TOTAL_HIGH] = (uint16_t)((data->flow_total >> 16) & 0xFFFF);
    g_registers[REG_FLOW_TOTAL_LOW] = (uint16_t)(data->flow_total & 0xFFFF);

    g_registers[REG_RELAY_DO] = data->relay_do;
    g_registers[REG_RELAY_DI] = data->relay_di;
    g_registers[REG_RELAY_BITS] = data->relay_do;
    g_registers[REG_SYSTEM_STATUS] = data->status;

    __enable_irq();
}

/**
 * @brief 获取 G780S 对应的底层 UART 句柄。
 * @param 无
 * @retval UART_HandleTypeDef*: USART3 句柄指针
 */
UART_HandleTypeDef *G780s_GetHandle(void)
{
    return Modbus_Slave_GetHandle();
}

/**
 * @brief 读取继电器控制寄存器当前值。
 * @param 无
 * @retval uint16_t: 当前继电器控制寄存器值
 */
uint16_t G780s_GetRelayCtrl(void)
{
    return g_registers[REG_RELAY_CTRL];
}

/**
 * @brief 读取继电器按位命令寄存器当前值。
 * @param 无
 * @retval uint16_t: 当前继电器按位命令寄存器值
 */
uint16_t G780s_GetRelayBits(void)
{
    return g_registers[REG_RELAY_CMD_BITS];
}

/**
 * @brief 获取当前已生效的远程配置快照。
 * @param out_config: 输出配置结构体指针
 * @retval 无
 */
void G780s_GetActiveConfig(G780sRemoteConfig *out_config)
{
    if (out_config == NULL)
    {
        return;
    }

    __disable_irq();
    *out_config = g_active_config;
    __enable_irq();
}

/**
 * @brief 获取当前是否处于自动模式。
 * @param 无
 * @retval uint8_t: 1 表示自动模式，0 表示手动模式
 */
uint8_t G780s_IsAutoMode(void)
{
    return (g_active_config.control_mode == G780S_MODE_AUTO) ? 1u : 0u;
}

uint8_t G780s_ConsumeBootUpgradeRequest(void)
{
    uint8_t pending;

    __disable_irq();
    pending = g_boot_upgrade_pending;
    g_boot_upgrade_pending = 0u;
    __enable_irq();

    return pending;
}
