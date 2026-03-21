#include "G780s.h"
#include "Modbus_Slave.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static uint16_t g_registers[MODBUS_REG_COUNT];
static G780sRemoteConfig g_active_config;
static G780sRemoteConfig g_staged_config;
static uint32_t g_unlock_deadline = 0;
static uint16_t g_maint_status = 0;
static uint16_t g_last_error = G780S_ERR_NONE;

#define G780S_CFG_FLASH_PAGE_ADDR   0x0807F800UL
#define G780S_UNLOCK_WINDOW_MS      30000UL
#define G780S_CFG_MAGIC             0x47584346UL  /* GXCF */

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    G780sRemoteConfig config;
    uint16_t crc16;
} G780sConfigImage;

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
    config->sequence = 1UL;
}

/**
 * @brief 校验远程配置参数范围是否合法。
 * @param config: 待校验的配置结构体指针
 * @retval uint8_t: 0 表示合法，非 0 表示对应的维护错误码
 */
static uint8_t G780s_ValidateConfig(const G780sRemoteConfig *config)
{
    if (config == NULL)
    {
        return G780S_ERR_INVALID_VALUE;
    }

    if (config->sensor_period_ms < 200 || config->sensor_period_ms > 60000)
    {
        return G780S_ERR_INVALID_VALUE;
    }
    if (config->flow_sample_period_ms < 100 || config->flow_sample_period_ms > 10000)
    {
        return G780S_ERR_INVALID_VALUE;
    }
    if (config->di_debounce_ms < 10 || config->di_debounce_ms > 5000)
    {
        return G780S_ERR_INVALID_VALUE;
    }
    if (config->temp_change_threshold_x10 < 1 || config->temp_change_threshold_x10 > 500)
    {
        return G780S_ERR_INVALID_VALUE;
    }
    if (config->pulses_per_liter_x100 < 100 || config->pulses_per_liter_x100 > 10000000UL)
    {
        return G780S_ERR_INVALID_VALUE;
    }
    if (config->hz_per_lpm_x100 < 10 || config->hz_per_lpm_x100 > 1000000UL)
    {
        return G780S_ERR_INVALID_VALUE;
    }

    return G780S_ERR_NONE;
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
}

/**
 * @brief 刷新维护状态相关寄存器，供远端轮询查看。
 * @param 无
 * @retval 无
 */
static void G780s_UpdateMaintenanceRegisters(void)
{
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
        return G780S_ERR_INVALID_VALUE;
    }

    config->sensor_period_ms = g_registers[REG_CFG_SENSOR_PERIOD_MS];
    config->flow_sample_period_ms = g_registers[REG_CFG_FLOW_SAMPLE_MS];
    config->di_debounce_ms = g_registers[REG_CFG_DI_DEBOUNCE_MS];
    config->temp_change_threshold_x10 = g_registers[REG_CFG_TEMP_THRESHOLD_X10];
    config->pulses_per_liter_x100 = ((uint32_t)g_registers[REG_CFG_PPL_X100_H] << 16) |
                                    g_registers[REG_CFG_PPL_X100_L];
    config->hz_per_lpm_x100 = ((uint32_t)g_registers[REG_CFG_HZ_PER_LPM_X100_H] << 16) |
                              g_registers[REG_CFG_HZ_PER_LPM_X100_L];
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
        return G780S_ERR_INVALID_VALUE;
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
        return G780S_ERR_INVALID_VALUE;
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
    return (reg_addr >= REG_CFG_SENSOR_PERIOD_MS && reg_addr <= REG_CFG_HZ_PER_LPM_X100_L) ? 1u : 0u;
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
                g_maint_status |= G780S_STATUS_SAVE_OK;
            }
            break;

        case G780S_CMD_CLEAR_ERROR:
            G780s_SetError(G780S_ERR_NONE);
            break;

        default:
            err = G780S_ERR_BAD_COMMAND;
            break;
    }

    g_registers[REG_MAINT_COMMAND] = G780S_CMD_NONE;
    G780s_UpdateMaintenanceRegisters();
    return err;
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

        G780s_SetError(G780S_ERR_INVALID_VALUE);
        return 0x03;
    }

    if (reg_addr == REG_MAINT_COMMAND)
    {
        /* 维护命令寄存器是一次性触发型写入，执行完成后自动清零。 */
        g_registers[REG_MAINT_COMMAND] = reg_value;
        err = G780s_HandleMaintenanceCommand(reg_value);
        if (err != G780S_ERR_NONE)
        {
            G780s_SetError(err);
            G780s_UpdateMaintenanceRegisters();
            return (err == G780S_ERR_LOCKED) ? 0x03 : 0x04;
        }

        G780s_SetError(G780S_ERR_NONE);
        G780s_UpdateMaintenanceRegisters();
        return 0;
    }

    if (G780s_IsConfigRegister(reg_addr) != 0u)
    {
        /* 参数区写入只更新暂存值，不直接写 Flash。 */
        if ((g_maint_status & G780S_STATUS_UNLOCKED) == 0u)
        {
            G780s_SetError(G780S_ERR_LOCKED);
            G780s_UpdateMaintenanceRegisters();
            return 0x03;
        }

        g_registers[reg_addr] = reg_value;
        g_maint_status |= G780S_STATUS_STAGED_DIRTY;
        g_maint_status &= (uint16_t)~G780S_STATUS_SAVE_OK;
        G780s_SetError(G780S_ERR_NONE);
        G780s_UpdateMaintenanceRegisters();
        return 0;
    }

    if (reg_addr == REG_RELAY_CTRL || reg_addr == REG_RELAY_CMD_BITS)
    {
        g_registers[reg_addr] = reg_value;
        return 0;
    }

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

    /* 先装入默认参数，再尝试用 Flash 中的持久化配置覆盖。 */
    G780s_SetDefaults(&g_active_config);
    g_staged_config = g_active_config;
    G780s_SetError(G780S_ERR_NONE);

    load_err = G780s_LoadConfigFromFlash(&flash_config);
    if (load_err == G780S_ERR_NONE)
    {
        G780s_ApplyConfig(&flash_config, 1u);
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
