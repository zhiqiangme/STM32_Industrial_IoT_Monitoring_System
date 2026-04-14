#include "Weight.h"
#include "Modbus_Master.h"
#include <stdio.h>
#include <string.h>

/**
 * @brief 计算称重模块 Modbus RTU 帧的 CRC16。
 * @param buf: 参与 CRC 计算的数据缓冲区
 * @param len: 参与 CRC 计算的字节数
 * @retval 计算得到的 CRC16
 */
/* Modbus CRC16 计算，低字节在前 */
static uint16_t Weight_CalcCrc(const uint8_t *buf, uint16_t len)
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
 * @brief 读取称重模块的单个保持寄存器。
 * @param reg_addr: 目标寄存器地址
 * @param value: 输出寄存器值指针
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Weight_ReadRegister(uint16_t reg_addr, uint16_t *value)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[10];

    tx_buf[0] = WEIGHT_SLAVE_ID;
    tx_buf[1] = WEIGHT_FUNC_READ;
    tx_buf[2] = (uint8_t)(reg_addr >> 8);
    tx_buf[3] = (uint8_t)(reg_addr & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01;

    uint16_t crc = Weight_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    if (Modbus_MasterReceive(rx_buf, 7, 500) != HAL_OK)
    {
        return 1;
    }

    if (rx_buf[0] != WEIGHT_SLAVE_ID || rx_buf[1] != WEIGHT_FUNC_READ)
    {
        return 1;
    }

    crc = Weight_CalcCrc(rx_buf, 5);
    if ((crc & 0xFF) != rx_buf[5] || ((crc >> 8) & 0xFF) != rx_buf[6])
    {
        return 1;
    }

    if (value != NULL)
    {
        *value = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    }

    return 0;
}

/**
 * @brief 向称重模块写入单个保持寄存器。
 * @param reg_addr: 目标寄存器地址
 * @param value: 待写入的寄存器值
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Weight_WriteRegister(uint16_t reg_addr, uint16_t value)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];

    tx_buf[0] = WEIGHT_SLAVE_ID;
    tx_buf[1] = WEIGHT_FUNC_WRITE;
    tx_buf[2] = (uint8_t)(reg_addr >> 8);
    tx_buf[3] = (uint8_t)(reg_addr & 0xFF);
    tx_buf[4] = (uint8_t)(value >> 8);
    tx_buf[5] = (uint8_t)(value & 0xFF);

    uint16_t crc = Weight_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    printf("[Weight] Write reg 0x%04X = 0x%04X\r\n", reg_addr, value);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    if (Modbus_MasterReceive(rx_buf, 8, 500) != HAL_OK)
    {
        printf("[Weight] Write timeout\r\n");
        return 1;
    }

    printf("[Weight] Write OK\r\n");
    return 0;
}

/**
 * @brief 读取并打印 CH3 的关键配置项。
 * @param 无
 * @retval 无
 */
void Weight_ReadCh3Config(void)
{
    uint16_t val;
    uint16_t coeff_h = 0;
    uint16_t coeff_l = 0;

    printf("\r\n=== Weight CH3 Config ===\r\n");

    if (Weight_ReadRegister(WEIGHT_REG_TRACK_ZERO_CH3, &val) == 0)
        printf("Track zero (0x0038): %u\r\n", val);
    else
        printf("Track zero (0x0038): read failed\r\n");

    HAL_Delay(50);

    if (Weight_ReadRegister(WEIGHT_REG_DIVISION_CH3, &val) == 0)
        printf("Division (0x0040): %u\r\n", val);
    else
        printf("Division (0x0040): read failed\r\n");

    HAL_Delay(50);

    if (Weight_ReadRegister(WEIGHT_REG_SAMPLE_RATE_CH3, &val) == 0)
        printf("Sample rate (0x0045): %u (1=10Hz, 2=40Hz)\r\n", val);
    else
        printf("Sample rate (0x0045): read failed\r\n");

    HAL_Delay(50);

    if (Weight_ReadRegister(WEIGHT_REG_COEFF_CH3, &coeff_h) == 0)
    {
        HAL_Delay(50);
        if (Weight_ReadRegister(WEIGHT_REG_COEFF_CH3 + 1, &coeff_l) == 0)
        {
            uint32_t coeff_raw = ((uint32_t)coeff_h << 16) | coeff_l;
            float coeff;
            memcpy(&coeff, &coeff_raw, sizeof(float));
            printf("Coefficient (0x0068-69): raw=0x%08lX, float=%.6f\r\n",
                   (unsigned long)coeff_raw, coeff);
        }
    }
    else
    {
        printf("Coefficient (0x0068-69): read failed\r\n");
    }

    printf("======================\r\n\r\n");
}

/**
 * @brief 把 CH3 的常用默认参数写回称重模块。
 * @param 无
 * @retval 无
 */
void Weight_WriteCh3Defaults(void)
{
    printf("\r\n=== Set Weight CH3 defaults ===\r\n");

    printf("Sample rate = 1 (10Hz)...\r\n");
    Weight_WriteRegister(WEIGHT_REG_SAMPLE_RATE_CH3, 1);
    HAL_Delay(100);

    printf("Division = 1...\r\n");
    Weight_WriteRegister(WEIGHT_REG_DIVISION_CH3, 1);
    HAL_Delay(100);

    printf("Track zero = 0 (disable auto zero)...\r\n");
    Weight_WriteRegister(WEIGHT_REG_TRACK_ZERO_CH3, 0);
    HAL_Delay(100);

    printf("================================\r\n\r\n");
}

/**
 * @brief 设置 CH3 的浮点校准系数，按两个 16 位寄存器写入。
 * @param coeff: 待写入的浮点校准系数
 * @retval 无
 */
void Weight_WriteCh3Coefficient(float coeff)
{
    uint32_t raw;
    memcpy(&raw, &coeff, sizeof(float));

    uint16_t high_word = (uint16_t)(raw >> 16);
    uint16_t low_word = (uint16_t)(raw & 0xFFFF);

    printf("[Weight] Set CH3 coefficient = %.6f (raw=0x%08lX)\r\n", coeff, (unsigned long)raw);
    printf("[Weight] Write high word 0x%04X to 0x0068\r\n", high_word);
    Weight_WriteRegister(WEIGHT_REG_COEFF_CH3, high_word);
    HAL_Delay(100);

    printf("[Weight] Write low word 0x%04X to 0x0069\r\n", low_word);
    Weight_WriteRegister(WEIGHT_REG_COEFF_CH3 + 1, low_word);
    HAL_Delay(100);
}

/**
 * @brief 读取指定称重通道的 32 位重量值。
 * @param channel: 通道号，范围 1-4
 * @param weight_g: 输出重量值指针，单位为 g
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Weight_ReadChannel(uint8_t channel, int32_t *weight_g)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[20];

    /* channel 取值 1-4，每个通道占两个寄存器 (ABCD) */
    if (channel < 1 || channel > 4)
    {
        return 1;
    }

    uint16_t reg_addr = (uint16_t)((channel - 1u) * 2u);

    tx_buf[0] = WEIGHT_SLAVE_ID;
    tx_buf[1] = WEIGHT_FUNC_READ;
    tx_buf[2] = (uint8_t)(reg_addr >> 8);
    tx_buf[3] = (uint8_t)(reg_addr & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x02;

    uint16_t crc = Weight_CalcCrc(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);

    Modbus_MasterSend(tx_buf, sizeof(tx_buf));

    if (Modbus_MasterReceive(rx_buf, 9, 500) != HAL_OK)
    {
        return 1;
    }
    if (rx_buf[0] != WEIGHT_SLAVE_ID || rx_buf[1] != WEIGHT_FUNC_READ)
    {
        return 1;
    }

    crc = Weight_CalcCrc(rx_buf, 7);
    if ((crc & 0xFF) != rx_buf[7] || ((crc >> 8) & 0xFF) != rx_buf[8])
    {
        return 1;
    }
    if (rx_buf[2] != 0x04)
    {
        return 1;
    }

    uint16_t high_word = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    uint16_t low_word = ((uint16_t)rx_buf[5] << 8) | rx_buf[6];
    int32_t weight = ((int32_t)high_word << 16) | low_word;

    if (weight_g != NULL)
    {
        *weight_g = weight;
    }

    return 0;
}

/**
 * @brief 读取默认使用的 CH3 重量值。
 * @param weight_g: 输出重量值指针，单位为 g
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Weight_Read(int32_t *weight_g)
{
    return Weight_ReadChannel(3, weight_g);
}

/**
 * @brief 轮询读取并打印全部 4 个称重通道的重量值。
 * @param 无
 * @retval 无
 */
void Weight_ScanAllChannels(void)
{
    printf("\r\n=== Weight Channel Scan ===\r\n");
    for (uint8_t ch = 1; ch <= 4; ch++)
    {
        int32_t weight = 0;
        if (Weight_ReadChannel(ch, &weight) == 0)
        {
            printf("CH%u: %ld g\r\n", ch, (long)weight);
        }
        else
        {
            printf("CH%u: read error\r\n", ch);
        }
        HAL_Delay(100);
    }
    printf("=========================\r\n\r\n");
}

/**
 * @brief 对 CH3 执行去皮操作。
 * @param 无
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Weight_TareCh3(void)
{
    printf("[Weight] Tare CH3...\r\n");
    return Weight_WriteRegister(WEIGHT_REG_TARE, WEIGHT_CH3_ACTION);
}

/**
 * @brief 对 CH3 执行零点校准操作。
 * @param 无
 * @retval uint8_t: 0 成功，1 失败
 */
uint8_t Weight_ZeroCalibrateCh3(void)
{
    printf("[Weight] Zero calibration CH3...\r\n");
    return Weight_WriteRegister(WEIGHT_REG_ZERO_CAL, WEIGHT_CH3_ACTION);
}
