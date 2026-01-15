#include "Relay.h"
#include "RS485_Master.h"
#include <stdio.h>

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

uint8_t Relay_WriteCoil(uint8_t channel, uint8_t on)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];

    /* 通道范围 1-16，写单个线圈 FC05 */
    if (channel < 1 || channel > 16) return 1;
    uint16_t coil_addr = (uint16_t)(channel - 1);
    uint16_t coil_value = on ? RELAY_COIL_ON : RELAY_COIL_OFF;

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_COIL;
    tx_buf[2] = (uint8_t)(coil_addr >> 8);
    tx_buf[3] = (uint8_t)(coil_addr & 0xFF);
    tx_buf[4] = (uint8_t)(coil_value >> 8);
    tx_buf[5] = (uint8_t)(coil_value & 0xFF);

    uint16_t crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    RS485_MasterSend(tx_buf, sizeof(tx_buf));

    if (RS485_MasterReceive(rx_buf, 8, 500) != HAL_OK)
    {
        return 1;
    }

    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_WRITE_COIL)
    {
        return 1;
    }

    return 0;
}

uint8_t Relay_ReadCoil(uint8_t channel, uint8_t *state)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];

    /* 读单个线圈 FC01，返回 bit0 状态 */
    if (channel < 1 || channel > 16) return 1;
    uint16_t coil_addr = (uint16_t)(channel - 1);

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_COILS;
    tx_buf[2] = (uint8_t)(coil_addr >> 8);
    tx_buf[3] = (uint8_t)(coil_addr & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01;

    uint16_t crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    RS485_MasterSend(tx_buf, sizeof(tx_buf));

    if (RS485_MasterReceive(rx_buf, 6, 500) != HAL_OK)
    {
        return 1;
    }

    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_COILS)
    {
        return 1;
    }

    crc = Relay_CalcCrc(rx_buf, 4);
    if ((crc & 0xFF) != rx_buf[4] || ((crc >> 8) & 0xFF) != rx_buf[5])
    {
        return 1;
    }

    if (state != NULL)
    {
        *state = (rx_buf[3] & 0x01) ? 1 : 0;
    }

    return 0;
}

uint8_t Relay_ReadAllCoils(uint16_t *mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];

    /* 从地址 0 开始读 16 个线圈，返回 2 字节掩码 */
    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_COILS;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x10;

    uint16_t crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    RS485_MasterSend(tx_buf, sizeof(tx_buf));

    if (RS485_MasterReceive(rx_buf, 7, 500) != HAL_OK)
    {
        return 1;
    }

    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_COILS)
    {
        return 1;
    }

    crc = Relay_CalcCrc(rx_buf, 5);
    if ((crc & 0xFF) != rx_buf[5] || ((crc >> 8) & 0xFF) != rx_buf[6])
    {
        return 1;
    }

    if (mask != NULL)
    {
        *mask = ((uint16_t)rx_buf[4] << 8) | rx_buf[3];
    }

    return 0;
}

uint8_t Relay_ReadInputPack(uint16_t *mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[10];

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_INPUT;
    tx_buf[2] = (uint8_t)(RELAY_REG_INPUT_PACK >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_INPUT_PACK & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01;

    uint16_t crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    RS485_MasterSend(tx_buf, sizeof(tx_buf));

    HAL_StatusTypeDef ret = RS485_MasterReceive(rx_buf, 7, 500);

    if (ret != HAL_OK)
    {
        return 1;
    }

    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_INPUT)
    {
        return 1;
    }

    crc = Relay_CalcCrc(rx_buf, 5);
    if ((crc & 0xFF) != rx_buf[5] || ((crc >> 8) & 0xFF) != rx_buf[6])
    {
        return 1;
    }

    if (mask != NULL)
    {
        *mask = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    }

    return 0;
}

uint8_t Relay_BatchControl(uint8_t all_on)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t value = all_on ? 1 : 0;

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_REGISTER;
    tx_buf[2] = (uint8_t)(RELAY_REG_BATCH_CTRL >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_BATCH_CTRL & 0xFF);
    tx_buf[4] = (uint8_t)(value >> 8);
    tx_buf[5] = (uint8_t)(value & 0xFF);

    uint16_t crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    RS485_MasterSend(tx_buf, sizeof(tx_buf));

    if (RS485_MasterReceive(rx_buf, 8, 500) != HAL_OK)
    {
        return 1;
    }

    return 0;
}

uint8_t Relay_SetOutputMask(uint16_t mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];

    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_REGISTER;
    tx_buf[2] = (uint8_t)(RELAY_REG_OUTPUT_MASK >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_OUTPUT_MASK & 0xFF);
    tx_buf[4] = (uint8_t)(mask >> 8);
    tx_buf[5] = (uint8_t)(mask & 0xFF);

    uint16_t crc = Relay_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    RS485_MasterSend(tx_buf, sizeof(tx_buf));

    if (RS485_MasterReceive(rx_buf, 8, 500) != HAL_OK)
    {
        return 1;
    }

    return 0;
}

uint8_t Relay_ToggleOutput(uint8_t channel)
{
    uint8_t current_state = 0;

    if (Relay_ReadCoil(channel, &current_state) != 0)
    {
        return 1;
    }

    return Relay_WriteCoil(channel, current_state ? 0 : 1);
}
