#include "Relay.h"
#include "BusService.h"
#include "Modbus_Master.h"
#include <stdio.h>

/**
 * @brief 计算继电器模块 Modbus RTU 帧的 CRC16。
 * @param buf: 参与 CRC 计算的数据缓冲区
 * @param len: 参与 CRC 计算的字节数
 * @retval 计算得到的 CRC16
 */
/* Modbus CRC16 计算，低字节在前 */
static uint16_t Relay_CalcCrc(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 0x0001)
            {
                crc >>= 1;
                crc ^= 0xA001;
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
 * @brief 写单个继电器线圈，控制指定通道吸合或断开。
 * @param channel: 继电器通道号，范围 1-16
 * @param on: 目标状态，1 为开，0 为关
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_WriteCoil(uint8_t channel, uint8_t on)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;

    /* 通道范围 1-16，写单个线圈 FC05 */
    if (channel < 1 || channel > 16) return 1;
    /* 继电器写单线圈期间独占主站总线。 */
    if (BusService_Lock(rt_tick_from_millisecond(1000)) != RT_EOK)
    {
        return 1;
    }
    uint16_t coil_addr = (uint16_t)(channel - 1);
    uint16_t coil_value = on ? RELAY_COIL_ON : RELAY_COIL_OFF;

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_COIL;
    tx_buf[2] = (uint8_t)(coil_addr >> 8);
    tx_buf[3] = (uint8_t)(coil_addr & 0xFF);
    tx_buf[4] = (uint8_t)(coil_value >> 8);
    tx_buf[5] = (uint8_t)(coil_value & 0xFF);

    crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    if (Modbus_MasterReceive(rx_buf, 8, 500) != HAL_OK)
    {
        BusService_Unlock();
        return 1;
    }

    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_WRITE_COIL)
    {
        BusService_Unlock();
        return 1;
    }

    BusService_Unlock();
    return 0;
}

/**
 * @brief 读取单个继电器线圈当前状态。
 * @param channel: 继电器通道号，范围 1-16
 * @param state: 输出状态指针，1 为开，0 为关
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_ReadCoil(uint8_t channel, uint8_t *state)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;

    /* 读单个线圈 FC01，返回 bit0 状态 */
    if (channel < 1 || channel > 16) return 1;
    /* 读单线圈用于按键/云端翻转前的状态确认。 */
    if (BusService_Lock(rt_tick_from_millisecond(1000)) != RT_EOK)
    {
        return 1;
    }
    uint16_t coil_addr = (uint16_t)(channel - 1);

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_COILS;
    tx_buf[2] = (uint8_t)(coil_addr >> 8);
    tx_buf[3] = (uint8_t)(coil_addr & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01;

    crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    if (Modbus_MasterReceive(rx_buf, 6, 500) != HAL_OK)
    {
        BusService_Unlock();
        return 1;
    }

    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_COILS)
    {
        BusService_Unlock();
        return 1;
    }

    crc = Relay_CalcCrc(rx_buf, 4);
    if ((crc & 0xFF) != rx_buf[4] || ((crc >> 8) & 0xFF) != rx_buf[5])
    {
        BusService_Unlock();
        return 1;
    }

    if (state != NULL)
    {
        *state = (rx_buf[3] & 0x01) ? 1 : 0;
    }

    BusService_Unlock();
    return 0;
}

/**
 * @brief 一次性读取全部 16 路继电器输出状态位。
 * @param mask: 输出位图指针，bit0-bit15 对应 CH1-CH16
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_ReadAllCoils(uint16_t *mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;

    /* 整包读取 16 路输出位图，避免和其他现场访问交叉。 */
    if (BusService_Lock(rt_tick_from_millisecond(1000)) != RT_EOK)
    {
        return 1;
    }

    /* 从地址 0 开始读 16 个线圈，返回 2 字节掩码 */
    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_COILS;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x10;

    crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    if (Modbus_MasterReceive(rx_buf, 7, 500) != HAL_OK)
    {
        BusService_Unlock();
        return 1;
    }

    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_COILS)
    {
        BusService_Unlock();
        return 1;
    }

    crc = Relay_CalcCrc(rx_buf, 5);
    if ((crc & 0xFF) != rx_buf[5] || ((crc >> 8) & 0xFF) != rx_buf[6])
    {
        BusService_Unlock();
        return 1;
    }

    if (mask != NULL)
    {
        *mask = ((uint16_t)rx_buf[4] << 8) | rx_buf[3];
    }

    BusService_Unlock();
    return 0;
}

/**
 * @brief 读取继电器模块的输入打包寄存器。
 * @param mask: 输出输入位图指针
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_ReadInputPack(uint16_t *mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[10];
    uint16_t crc;
    HAL_StatusTypeDef ret;

    /* DI 打包寄存器读取也放在同一互斥模型下。 */
    if (BusService_Lock(rt_tick_from_millisecond(1000)) != RT_EOK)
    {
        return 1;
    }

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_INPUT;
    tx_buf[2] = (uint8_t)(RELAY_REG_INPUT_PACK >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_INPUT_PACK & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01;

    crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    ret = Modbus_MasterReceive(rx_buf, 7, 500);

    if (ret != HAL_OK)
    {
        BusService_Unlock();
        return 1;
    }

    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_INPUT)
    {
        BusService_Unlock();
        return 1;
    }

    crc = Relay_CalcCrc(rx_buf, 5);
    if ((crc & 0xFF) != rx_buf[5] || ((crc >> 8) & 0xFF) != rx_buf[6])
    {
        BusService_Unlock();
        return 1;
    }

    if (mask != NULL)
    {
        *mask = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    }

    BusService_Unlock();
    return 0;
}

/**
 * @brief 批量控制全部输出通道全开或全关。
 * @param all_on: 1 表示全开，0 表示全关
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_BatchControl(uint8_t all_on)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t value = all_on ? 1 : 0;
    uint16_t crc;

    /* 批量全开/全关属于控制命令，同样串行化。 */
    if (BusService_Lock(rt_tick_from_millisecond(1000)) != RT_EOK)
    {
        return 1;
    }

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_REGISTER;
    tx_buf[2] = (uint8_t)(RELAY_REG_BATCH_CTRL >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_BATCH_CTRL & 0xFF);
    tx_buf[4] = (uint8_t)(value >> 8);
    tx_buf[5] = (uint8_t)(value & 0xFF);

    crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    if (Modbus_MasterReceive(rx_buf, 8, 500) != HAL_OK)
    {
        BusService_Unlock();
        return 1;
    }

    BusService_Unlock();
    return 0;
}

/**
 * @brief 直接写入输出位图寄存器，按位控制多路继电器输出。
 * @param mask: 目标输出位图
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_SetOutputMask(uint16_t mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;

    /* 云端按位写输出时直接覆盖整个位图。 */
    if (BusService_Lock(rt_tick_from_millisecond(1000)) != RT_EOK)
    {
        return 1;
    }

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_REGISTER;
    tx_buf[2] = (uint8_t)(RELAY_REG_OUTPUT_MASK >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_OUTPUT_MASK & 0xFF);
    tx_buf[4] = (uint8_t)(mask >> 8);
    tx_buf[5] = (uint8_t)(mask & 0xFF);

    crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    if (Modbus_MasterReceive(rx_buf, 8, 500) != HAL_OK)
    {
        BusService_Unlock();
        return 1;
    }

    BusService_Unlock();
    return 0;
}

/**
 * @brief 翻转指定通道的继电器输出状态。
 * @param channel: 继电器通道号，范围 1-16
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Relay_ToggleOutput(uint8_t channel)
{
    uint8_t current_state = 0;
    uint8_t status;

    /* 翻转动作需要把“读当前状态 + 写新状态”包成一个原子事务。 */
    if (BusService_Lock(rt_tick_from_millisecond(1000)) != RT_EOK)
    {
        return 1;
    }

    if (Relay_ReadCoil(channel, &current_state) != 0)
    {
        BusService_Unlock();
        return 1;
    }

    status = Relay_WriteCoil(channel, current_state ? 0u : 1u);
    BusService_Unlock();
    return status;
}
