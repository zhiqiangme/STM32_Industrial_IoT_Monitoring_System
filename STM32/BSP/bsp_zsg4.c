#include "bsp_zsg4.h"
#include "bsp_rs485.h"
#include <string.h>
#include <stdio.h>

/* Modbus RTU CRC16 计算 (same as PT100) */
static uint16_t Modbus_CRC16(uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (int pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (int i = 8; i != 0; i--)
        {
            if ((crc & 0x0001) != 0)
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
 * @brief  读取ZSG4指定通道重量
 * @param  channel: 通道号 (1-4)
 * @param  weight_g: 指向存储结果的int32指针 (单位: 克)
 * @retval 0:成功, 1:失败
 */
uint8_t ZSG4_Read_Weight_Channel(uint8_t channel, int32_t *weight_g)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[20];
    uint16_t crc;
    HAL_StatusTypeDef ret;
    uint16_t reg_addr;
    
    /* Calculate register address for channel (0-indexed) */
    if (channel < 1 || channel > 4) return 1;
    reg_addr = (channel - 1) * 2; // CH1=0x0000, CH2=0x0002, CH3=0x0004, CH4=0x0006
    
    /* 1. 组装Modbus请求帧: 02 03 [ADDR] 00 02 CRC_L CRC_H */
    tx_buf[0] = ZSG4_SLAVE_ID;
    tx_buf[1] = ZSG4_FUNC_READ;
    tx_buf[2] = (uint8_t)(reg_addr >> 8);
    tx_buf[3] = (uint8_t)(reg_addr & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x02; // Read 2 registers
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    printf("[ZSG4_DBG] CH%d TX: ", channel);
    for(int i=0; i<8; i++) printf("%02X ", tx_buf[i]);
    printf("\r\n");
    
    /* 2. 发送请求 */
    RS485_Send_Data(tx_buf, 8);
    
    /* 3. 接收响应 */
    ret = RS485_Receive_Data(rx_buf, 9, 500);
    
    if (ret != HAL_OK)
    {
        printf("[ZSG4_DBG] CH%d RX Timeout/Error (HAL=%d)\r\n", channel, ret);
        return 1;
    }
    
    printf("[ZSG4_DBG] CH%d RX: ", channel);
    for(int i=0; i<9; i++) printf("%02X ", rx_buf[i]);
    printf("\r\n");
    
    /* 4. 校验响应 */
    if (rx_buf[0] != ZSG4_SLAVE_ID || rx_buf[1] != ZSG4_FUNC_READ)
    {
        printf("[ZSG4_DBG] CH%d Invalid Addr/Func\r\n", channel);
        return 1;
    }
    
    crc = Modbus_CRC16(rx_buf, 7);
    uint8_t crc_l = (uint8_t)(crc & 0xFF);
    uint8_t crc_h = (uint8_t)((crc >> 8) & 0xFF);
    
    if (crc_l != rx_buf[7] || crc_h != rx_buf[8])
    {
        printf("[ZSG4_DBG] CH%d CRC Error\r\n", channel);
        return 1;
    }
    
    /* 5. 解析数据 */
    if (rx_buf[2] != 0x04)
    {
        printf("[ZSG4_DBG] CH%d Invalid byte count\r\n", channel);
        return 1;
    }
    
    /* Decode int32: high word first, then low word */
    uint16_t high_word = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    uint16_t low_word = ((uint16_t)rx_buf[5] << 8) | rx_buf[6];
    
    int32_t weight = ((int32_t)high_word << 16) | low_word;
    
    if (weight_g != NULL)
    {
        *weight_g = weight;
    }
    
    printf("[ZSG4_DBG] CH%d Weight: %ld g (raw: %04X %04X)\r\n", 
           channel, (long)weight, high_word, low_word);
    
    return 0;
}

/**
 * @brief  读取通道3重量 (快捷函数)
 */
uint8_t ZSG4_Read_Weight(int32_t *weight_g)
{
    return ZSG4_Read_Weight_Channel(3, weight_g);
}

/**
 * @brief  诊断: 扫描所有通道
 */
void ZSG4_Scan_All_Channels(void)
{
    printf("\r\n=== ZSG4 Channel Scan ===\r\n");
    for (uint8_t ch = 1; ch <= 4; ch++)
    {
        int32_t weight = 0;
        if (ZSG4_Read_Weight_Channel(ch, &weight) == 0)
        {
            printf("CH%d: %ld g\r\n", ch, (long)weight);
        }
        else
        {
            printf("CH%d: ERROR\r\n", ch);
        }
        HAL_Delay(100); // Small delay between channels
    }
    printf("=========================\r\n\r\n");
}

/**
 * @brief  去皮 - 通道3
 * @retval 0:成功, 1:失败
 */
uint8_t ZSG4_Tare_CH3(void)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;
    
    /* FC06: Write Single Register */
    tx_buf[0] = ZSG4_SLAVE_ID;
    tx_buf[1] = ZSG4_FUNC_WRITE;
    tx_buf[2] = (uint8_t)(ZSG4_REG_TARE >> 8);
    tx_buf[3] = (uint8_t)(ZSG4_REG_TARE & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = ZSG4_CH3_ACTION; // Write value 3 for Channel 3
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    printf("[ZSG4_DBG] Tare CH3 TX: ");
    for(int i=0; i<8; i++) printf("%02X ", tx_buf[i]);
    printf("\r\n");
    
    RS485_Send_Data(tx_buf, 8);
    
    /* Response is echo of request */
    if (RS485_Receive_Data(rx_buf, 8, 500) != HAL_OK)
    {
        printf("[ZSG4_DBG] Tare RX Timeout\r\n");
        return 1;
    }
    
    printf("[ZSG4_DBG] Tare OK\r\n");
    return 0;
}

/**
 * @brief  零点校准 - 通道3
 * @retval 0:成功, 1:失败
 */
uint8_t ZSG4_Zero_Cal_CH3(void)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;
    
    tx_buf[0] = ZSG4_SLAVE_ID;
    tx_buf[1] = ZSG4_FUNC_WRITE;
    tx_buf[2] = (uint8_t)(ZSG4_REG_ZERO_CAL >> 8);
    tx_buf[3] = (uint8_t)(ZSG4_REG_ZERO_CAL & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = ZSG4_CH3_ACTION;
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    printf("[ZSG4_DBG] Zero Cal CH3 TX: ");
    for(int i=0; i<8; i++) printf("%02X ", tx_buf[i]);
    printf("\r\n");
    
    RS485_Send_Data(tx_buf, 8);
    
    if (RS485_Receive_Data(rx_buf, 8, 500) != HAL_OK)
    {
        printf("[ZSG4_DBG] Zero Cal RX Timeout\r\n");
        return 1;
    }
    
    printf("[ZSG4_DBG] Zero Cal OK\r\n");
    return 0;
}
