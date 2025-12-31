#include "bsp_pt100.h"
#include "bsp_rs485.h"
#include <string.h>
#include <stdio.h>

/* Modbus RTU CRC16 计算 */
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
    /* 注意：Modbus协议中CRC是低字节在前，高字节在后发送 */
    // 这里返回原生uint16，发送时拆分即可
    return crc;
}

/**
 * @brief  读取PT100温度 (通道1)
 * @param  temp_val: 指向存储结果的float指针
 * @retval 0:成功, 1:失败
 */
uint8_t PT100_Read_Temperature(float *temp_val)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[20]; // 预留足够空间
    uint16_t crc;
    HAL_StatusTypeDef ret;
    
    /* 1. 组装Modbus请求帧: 01 04 00 03 00 01 CRC_L CRC_H */
    tx_buf[0] = PT100_SLAVE_ID;
    tx_buf[1] = PT100_FUNC_READ;
    tx_buf[2] = (uint8_t)(PT100_REG_CH4 >> 8);
    tx_buf[3] = (uint8_t)(PT100_REG_CH4 & 0xFF);
    tx_buf[4] = 0x00; // 寄存器数量高
    tx_buf[5] = 0x01; // 寄存器数量低 (读1个寄存器)
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);      // CRC低字节
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF); // CRC高字节
    
    /* 调试输出 (保留,需要时取消注释)
    printf("[PT100_DBG] TX: ");
    for(int i=0; i<8; i++) printf("%02X ", tx_buf[i]);
    printf("\r\n");
    */
    
    /* 2. 发送请求 */
    RS485_Send_Data(tx_buf, 8);
    
    /* 3. 接收响应 */
    // 预期响应长度: Slave(1) + Func(1) + Len(1) + Data(2) + CRC(2) = 7字节
    ret = RS485_Receive_Data(rx_buf, 7, 500); // 增加超时到500ms
    
    if (ret != HAL_OK)
    {
        /* printf("[PT100_DBG] RX Timeout/Error (HAL=%d)\r\n", ret); */
        return 1; // 超时或错误
    }
    
    /* 调试输出 (保留,需要时取消注释)
    printf("[PT100_DBG] RX: ");
    for(int i=0; i<7; i++) printf("%02X ", rx_buf[i]);
    printf("\r\n");
    */
    
    /* 4. 校验响应 */
    // 检查地址和功能码
    if (rx_buf[0] != PT100_SLAVE_ID || rx_buf[1] != PT100_FUNC_READ)
    {
        /* printf("[PT100_DBG] Invalid Addr/Func\r\n"); */
        return 1; // 响应错误
    }
    
    // 检查CRC
    crc = Modbus_CRC16(rx_buf, 5); // 计算前5个字节的CRC
    uint8_t crc_l = (uint8_t)(crc & 0xFF);
    uint8_t crc_h = (uint8_t)((crc >> 8) & 0xFF);
    
    if (crc_l != rx_buf[5] || crc_h != rx_buf[6])
    {
        /* printf("[PT100_DBG] CRC Error\r\n"); */
        return 1; // CRC校验失败
    }
    
    /* 5. 解析数据 */
    // 字节数应为2
    if (rx_buf[2] != 0x02)
    {
        /* printf("[PT100_DBG] Invalid byte count\r\n"); */
        return 1; 
    }

    uint16_t raw_data = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    
    // 检查是否无效值 (0xFFFF)
    if (raw_data == 0xFFFF)
    {
        /* printf("[PT100_DBG] Sensor fault (0xFFFF)\r\n"); */
        return 1; 
    }
    
    // 解析: 符号(bit15) + 数值(bit14-0) * 0.1
    uint8_t is_negative = (raw_data & 0x8000) ? 1 : 0;
    uint16_t magnitude = raw_data & 0x7FFF;
    
    float val = (float)magnitude * 0.1f;
    if (is_negative)
    {
        val = -val;
    }
    
    if (temp_val != NULL)
    {
        *temp_val = val;
    }
    
    /* printf("[PT100_DBG] Temp OK: %.1f C\r\n", val); */
    
    return 0; // 成功
}
