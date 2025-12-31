/**
 * @file    bsp_zsg4.c
 * @brief   ZSG4称重变送器驱动实现
 * @note    Modbus RTU协议, 从站地址=3, 通道3
 */

#include "bsp_zsg4.h"
#include "bsp_rs485.h"
#include <string.h>
#include <stdio.h>

/**
 * @brief  Modbus RTU CRC16校验计算
 * @param  buf: 数据缓冲区
 * @param  len: 数据长度
 * @retval CRC16值
 */
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
 * @brief  读取单个保持寄存器 (FC03, 读1个)
 * @param  reg_addr: 寄存器地址
 * @param  value: 输出值指针
 * @retval 0:成功, 1:失败
 */
uint8_t ZSG4_Read_Register(uint16_t reg_addr, uint16_t *value)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[10];
    uint16_t crc;
    
    /* 组装请求帧 */
    tx_buf[0] = ZSG4_SLAVE_ID;
    tx_buf[1] = ZSG4_FUNC_READ;
    tx_buf[2] = (uint8_t)(reg_addr >> 8);
    tx_buf[3] = (uint8_t)(reg_addr & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01; /* 读1个寄存器 */
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    RS485_Send_Data(tx_buf, 8);
    
    /* 响应: ID(1) + FC(1) + Len(1) + Data(2) + CRC(2) = 7字节 */
    if (RS485_Receive_Data(rx_buf, 7, 500) != HAL_OK)
    {
        return 1;
    }
    
    /* 校验地址和功能码 */
    if (rx_buf[0] != ZSG4_SLAVE_ID || rx_buf[1] != ZSG4_FUNC_READ)
    {
        return 1;
    }
    
    /* 校验CRC */
    crc = Modbus_CRC16(rx_buf, 5);
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
 * @brief  写入单个保持寄存器 (FC06)
 * @param  reg_addr: 寄存器地址
 * @param  value: 要写入的值
 * @retval 0:成功, 1:失败
 */
uint8_t ZSG4_Write_Register(uint16_t reg_addr, uint16_t value)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;
    
    /* 组装请求帧 */
    tx_buf[0] = ZSG4_SLAVE_ID;
    tx_buf[1] = ZSG4_FUNC_WRITE;
    tx_buf[2] = (uint8_t)(reg_addr >> 8);
    tx_buf[3] = (uint8_t)(reg_addr & 0xFF);
    tx_buf[4] = (uint8_t)(value >> 8);
    tx_buf[5] = (uint8_t)(value & 0xFF);
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    printf("[ZSG4] 写寄存器 0x%04X = 0x%04X\r\n", reg_addr, value);
    
    RS485_Send_Data(tx_buf, 8);
    
    /* 响应为请求的回显 */
    if (RS485_Receive_Data(rx_buf, 8, 500) != HAL_OK)
    {
        printf("[ZSG4] 写入超时\r\n");
        return 1;
    }
    
    printf("[ZSG4] 写入成功\r\n");
    return 0;
}

/**
 * @brief  读取并打印通道3配置参数
 */
void ZSG4_Read_CH3_Config(void)
{
    uint16_t val;
    uint16_t coeff_h, coeff_l;
    
    printf("\r\n=== ZSG4 通道3配置 ===\r\n");
    
    /* 跟踪零点范围 */
    if (ZSG4_Read_Register(ZSG4_REG_TRACK_ZERO_CH3, &val) == 0)
        printf("跟踪零点范围 (0x0038): %u\r\n", val);
    else
        printf("跟踪零点范围 (0x0038): 读取错误\r\n");
    
    HAL_Delay(50);
    
    /* 分度值 */
    if (ZSG4_Read_Register(ZSG4_REG_DIVISION_CH3, &val) == 0)
        printf("分度值 (0x0040): %u\r\n", val);
    else
        printf("分度值 (0x0040): 读取错误\r\n");
    
    HAL_Delay(50);
    
    /* 采样率 */
    if (ZSG4_Read_Register(ZSG4_REG_SAMPLE_RATE_CH3, &val) == 0)
        printf("采样率 (0x0045): %u (1=10Hz, 2=40Hz)\r\n", val);
    else
        printf("采样率 (0x0045): 读取错误\r\n");
    
    HAL_Delay(50);
    
    /* 修正系数 (float32 = 2个寄存器) */
    if (ZSG4_Read_Register(ZSG4_REG_COEFF_CH3, &coeff_h) == 0)
    {
        HAL_Delay(50);
        if (ZSG4_Read_Register(ZSG4_REG_COEFF_CH3 + 1, &coeff_l) == 0)
        {
            uint32_t coeff_raw = ((uint32_t)coeff_h << 16) | coeff_l;
            float coeff;
            memcpy(&coeff, &coeff_raw, sizeof(float));
            printf("修正系数 (0x0068-69): raw=0x%08lX, float=%.6f\r\n", 
                   (unsigned long)coeff_raw, coeff);
        }
    }
    else
    {
        printf("修正系数 (0x0068-69): 读取错误\r\n");
    }
    
    printf("======================\r\n\r\n");
}

/**
 * @brief  写入通道3默认配置
 */
void ZSG4_Write_CH3_Defaults(void)
{
    printf("\r\n=== 写入ZSG4通道3默认配置 ===\r\n");
    
    /* 采样率 = 1 (10Hz) */
    printf("设置采样率 = 1 (10Hz)...\r\n");
    ZSG4_Write_Register(ZSG4_REG_SAMPLE_RATE_CH3, 1);
    HAL_Delay(100);
    
    /* 分度值 = 1 */
    printf("设置分度值 = 1...\r\n");
    ZSG4_Write_Register(ZSG4_REG_DIVISION_CH3, 1);
    HAL_Delay(100);
    
    /* 跟踪零点范围 = 0 (禁用自动归零) */
    printf("设置跟踪零点 = 0...\r\n");
    ZSG4_Write_Register(ZSG4_REG_TRACK_ZERO_CH3, 0);
    HAL_Delay(100);
    
    printf("==============================\r\n\r\n");
}

/**
 * @brief  设置通道3修正系数 (float32 = 2个寄存器)
 * @param  coeff: 修正系数 (1.0 = 1:1比例, 显示原始AD值)
 */
void ZSG4_Write_CH3_Coefficient(float coeff)
{
    uint32_t raw;
    memcpy(&raw, &coeff, sizeof(float));
    
    uint16_t high_word = (uint16_t)(raw >> 16);
    uint16_t low_word = (uint16_t)(raw & 0xFFFF);
    
    printf("[ZSG4] 设置通道3修正系数 = %.6f (raw=0x%08lX)\r\n", coeff, (unsigned long)raw);
    printf("[ZSG4] 写入高字 0x%04X 到 0x0068\r\n", high_word);
    ZSG4_Write_Register(ZSG4_REG_COEFF_CH3, high_word);
    HAL_Delay(100);
    
    printf("[ZSG4] 写入低字 0x%04X 到 0x0069\r\n", low_word);
    ZSG4_Write_Register(ZSG4_REG_COEFF_CH3 + 1, low_word);
    HAL_Delay(100);
}

/**
 * @brief  读取指定通道重量
 * @param  channel: 通道号 (1-4)
 * @param  weight_g: 输出重量值指针 (单位: 克)
 * @retval 0:成功, 1:失败
 */
uint8_t ZSG4_Read_Weight_Channel(uint8_t channel, int32_t *weight_g)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[20];
    uint16_t crc;
    HAL_StatusTypeDef ret;
    uint16_t reg_addr;
    
    /* 检查通道范围 */
    if (channel < 1 || channel > 4) return 1;
    reg_addr = (channel - 1) * 2; /* 每通道2个寄存器 */
    
    /* 组装请求帧 */
    tx_buf[0] = ZSG4_SLAVE_ID;
    tx_buf[1] = ZSG4_FUNC_READ;
    tx_buf[2] = (uint8_t)(reg_addr >> 8);
    tx_buf[3] = (uint8_t)(reg_addr & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x02; /* 读2个寄存器 */
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    RS485_Send_Data(tx_buf, 8);
    
    /* 响应: ID(1) + FC(1) + Len(1) + Data(4) + CRC(2) = 9字节 */
    ret = RS485_Receive_Data(rx_buf, 9, 500);
    
    if (ret != HAL_OK) return 1;
    if (rx_buf[0] != ZSG4_SLAVE_ID || rx_buf[1] != ZSG4_FUNC_READ) return 1;
    
    /* 校验CRC */
    crc = Modbus_CRC16(rx_buf, 7);
    if ((crc & 0xFF) != rx_buf[7] || ((crc >> 8) & 0xFF) != rx_buf[8]) return 1;
    if (rx_buf[2] != 0x04) return 1;
    
    /* 解析int32 (ABCD大端: 高字在前) */
    uint16_t high_word = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    uint16_t low_word = ((uint16_t)rx_buf[5] << 8) | rx_buf[6];
    
    int32_t weight = ((int32_t)high_word << 16) | low_word;
    
    if (weight_g != NULL) *weight_g = weight;
    
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
 * @brief  扫描所有通道 (诊断用)
 */
void ZSG4_Scan_All_Channels(void)
{
    printf("\r\n=== ZSG4 通道扫描 ===\r\n");
    for (uint8_t ch = 1; ch <= 4; ch++)
    {
        int32_t weight = 0;
        if (ZSG4_Read_Weight_Channel(ch, &weight) == 0)
            printf("通道%d: %ld g\r\n", ch, (long)weight);
        else
            printf("通道%d: 错误\r\n", ch);
        HAL_Delay(100);
    }
    printf("=====================\r\n\r\n");
}

/**
 * @brief  通道3去皮 (将当前重量设为零)
 */
uint8_t ZSG4_Tare_CH3(void)
{
    printf("[ZSG4] 通道3去皮...\r\n");
    return ZSG4_Write_Register(ZSG4_REG_TARE, ZSG4_CH3_ACTION);
}

/**
 * @brief  通道3零点校准
 */
uint8_t ZSG4_Zero_Cal_CH3(void)
{
    printf("[ZSG4] 通道3零点校准...\r\n");
    return ZSG4_Write_Register(ZSG4_REG_ZERO_CAL, ZSG4_CH3_ACTION);
}
