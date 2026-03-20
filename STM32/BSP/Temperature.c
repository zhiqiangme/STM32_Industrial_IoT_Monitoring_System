#include "Temperature.h"
#include "Modbus_Master.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 计算 PT100 变送器 Modbus RTU 帧的 CRC16。
 * @param buf: 参与 CRC 计算的数据缓冲区
 * @param len: 参与 CRC 计算的字节数
 * @retval 计算得到的 CRC16
 */
/* Modbus CRC16 计算（低字节在前） */
static uint16_t PT100_CalcCrc(const uint8_t *buf, uint16_t len)
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
 * @brief 读取 PT100 变送器 CH4 温度并转换为摄氏度浮点数。
 * @param temp_val: 输出温度指针，单位为 ℃
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Temperature_Read(float *temp_val)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[20];

    /* 组帧：01 04 00 03 00 01 CRC_L CRC_H */
    tx_buf[0] = PT100_SLAVE_ID;
    tx_buf[1] = PT100_FUNC_READ;
    tx_buf[2] = (uint8_t)(PT100_REG_CH4 >> 8);
    tx_buf[3] = (uint8_t)(PT100_REG_CH4 & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01;

    uint16_t crc = PT100_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    /* 期望响应长度：ID + FC + Len + Data(2) + CRC(2) = 7 字节 */
    if (Modbus_MasterReceive(rx_buf, 7, 500) != HAL_OK)
    {
        return 1;
    }

    if (rx_buf[0] != PT100_SLAVE_ID || rx_buf[1] != PT100_FUNC_READ)
    {
        return 1;
    }

    crc = PT100_CalcCrc(rx_buf, 5);
    if (((crc & 0xFF) != rx_buf[5]) || (((crc >> 8) & 0xFF) != rx_buf[6]))
    {
        return 1;
    }

    if (rx_buf[2] != 0x02)
    {
        return 1;
    }

    uint16_t raw_data = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    if (raw_data == 0xFFFF)
    {
        return 1;
    }

    /* bit15 符号位，低 15 位为 0.1℃ 精度的幅值 */
    float val = (float)(raw_data & 0x7FFF) * 0.1f;
    if (raw_data & 0x8000)
    {
        val = -val;
    }

    if (temp_val != NULL)
    {
        *temp_val = val;
    }

    return 0;
}
