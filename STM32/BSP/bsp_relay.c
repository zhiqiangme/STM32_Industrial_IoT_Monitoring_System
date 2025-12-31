/**
 * @file    bsp_relay.c
 * @brief   16路继电器输入输出模块驱动实现
 * @note    Modbus RTU协议, 从站地址=2
 */

#include "bsp_relay.h"
#include "bsp_rs485.h"
#include <stdio.h>

/**
 * @brief  Modbus RTU CRC16校验计算
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
 * @brief  写单个线圈 (FC05) - 控制单个输出通道
 * @param  channel: 通道号 (1-16)
 * @param  on: 1=开启, 0=关闭
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Write_Coil(uint8_t channel, uint8_t on)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;
    uint16_t coil_addr;
    uint16_t coil_value;
    
    /* 检查通道范围 */
    if (channel < 1 || channel > 16) return 1;
    coil_addr = channel - 1;  /* 通道1对应地址0x0000 */
    coil_value = on ? RELAY_COIL_ON : RELAY_COIL_OFF;
    
    /* 组装请求帧: ID FC ADDR_H ADDR_L VAL_H VAL_L CRC_L CRC_H */
    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_COIL;
    tx_buf[2] = (uint8_t)(coil_addr >> 8);
    tx_buf[3] = (uint8_t)(coil_addr & 0xFF);
    tx_buf[4] = (uint8_t)(coil_value >> 8);
    tx_buf[5] = (uint8_t)(coil_value & 0xFF);
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    RS485_Send_Data(tx_buf, 8);
    
    /* 响应为请求的回显 */
    if (RS485_Receive_Data(rx_buf, 8, 500) != HAL_OK)
    {
        return 1;
    }
    
    /* 校验响应 */
    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_WRITE_COIL)
    {
        return 1;
    }
    
    return 0;
}

/**
 * @brief  读单个线圈状态 (FC01)
 * @param  channel: 通道号 (1-16)
 * @param  state: 输出状态指针 (0/1)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Read_Coil(uint8_t channel, uint8_t *state)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;
    uint16_t coil_addr;
    
    if (channel < 1 || channel > 16) return 1;
    coil_addr = channel - 1;
    
    /* 组装请求帧: 读1个线圈 */
    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_COILS;
    tx_buf[2] = (uint8_t)(coil_addr >> 8);
    tx_buf[3] = (uint8_t)(coil_addr & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01;  /* 读1个线圈 */
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    RS485_Send_Data(tx_buf, 8);
    
    /* 响应: ID(1) + FC(1) + Len(1) + Data(1) + CRC(2) = 6字节 */
    if (RS485_Receive_Data(rx_buf, 6, 500) != HAL_OK)
    {
        return 1;
    }
    
    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_COILS)
    {
        return 1;
    }
    
    /* CRC校验 */
    crc = Modbus_CRC16(rx_buf, 4);
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

/**
 * @brief  读所有输出线圈状态 (FC01, 16个)
 * @param  mask: 输出位掩码指针 (bit0=CH1 ... bit15=CH16)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Read_All_Coils(uint16_t *mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;
    
    /* 组装请求帧: 读16个线圈从地址0开始 */
    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_COILS;
    tx_buf[2] = 0x00;
    tx_buf[3] = 0x00;  /* 起始地址 0x0000 */
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x10;  /* 数量: 16 */
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    RS485_Send_Data(tx_buf, 8);
    
    /* 响应: ID(1) + FC(1) + Len(1) + Data(2) + CRC(2) = 7字节 */
    if (RS485_Receive_Data(rx_buf, 7, 500) != HAL_OK)
    {
        return 1;
    }
    
    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_COILS)
    {
        return 1;
    }
    
    /* CRC校验 */
    crc = Modbus_CRC16(rx_buf, 5);
    if ((crc & 0xFF) != rx_buf[5] || ((crc >> 8) & 0xFF) != rx_buf[6])
    {
        return 1;
    }
    
    if (mask != NULL)
    {
        /* 低字节在前 (CH1-8), 高字节在后 (CH9-16) */
        *mask = ((uint16_t)rx_buf[4] << 8) | rx_buf[3];
    }
    
    return 0;
}

/**
 * @brief  读输入位打包寄存器 (FC04)
 * @param  mask: 输入位掩码指针 (bit0=CH1 ... bit15=CH16)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Read_Input_Pack(uint16_t *mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[10];
    uint16_t crc;
    
    /* 组装请求帧: FC04 读1个输入寄存器 0x0032 */
    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_READ_INPUT;
    tx_buf[2] = (uint8_t)(RELAY_REG_INPUT_PACK >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_INPUT_PACK & 0xFF);
    tx_buf[4] = 0x00;
    tx_buf[5] = 0x01;  /* 读1个寄存器 */
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    /* 调试输出 (保留,需要时取消注释)
    printf("[RELAY_DBG] TX: ");
    for(int i=0; i<8; i++) printf("%02X ", tx_buf[i]);
    printf("\r\n");
    */
    
    RS485_Send_Data(tx_buf, 8);
    
    /* 响应: ID(1) + FC(1) + Len(1) + Data(2) + CRC(2) = 7字节 */
    HAL_StatusTypeDef ret = RS485_Receive_Data(rx_buf, 7, 500);
    
    if (ret != HAL_OK)
    {
        /* printf("[RELAY_DBG] RX超时 (ret=%d)\r\n", ret); */
        return 1;
    }
    
    /* 调试输出 (保留,需要时取消注释)
    printf("[RELAY_DBG] RX: ");
    for(int i=0; i<7; i++) printf("%02X ", rx_buf[i]);
    printf("\r\n");
    */
    
    if (rx_buf[0] != RELAY_SLAVE_ID || rx_buf[1] != RELAY_FC_READ_INPUT)
    {
        /* printf("[RELAY_DBG] 地址或功能码错误\r\n"); */
        return 1;
    }
    
    /* CRC校验 */
    crc = Modbus_CRC16(rx_buf, 5);
    if ((crc & 0xFF) != rx_buf[5] || ((crc >> 8) & 0xFF) != rx_buf[6])
    {
        /* printf("[RELAY_DBG] CRC错误\r\n"); */
        return 1;
    }
    
    if (mask != NULL)
    {
        *mask = ((uint16_t)rx_buf[3] << 8) | rx_buf[4];
    }
    
    return 0;
}

/**
 * @brief  批量控制所有输出 (FC06)
 * @param  all_on: 1=全部打开, 0=全部关闭
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Batch_Control(uint8_t all_on)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;
    uint16_t value = all_on ? 1 : 0;
    
    /* 组装请求帧: FC06 写寄存器 0x0034 */
    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_REGISTER;
    tx_buf[2] = (uint8_t)(RELAY_REG_BATCH_CTRL >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_BATCH_CTRL & 0xFF);
    tx_buf[4] = (uint8_t)(value >> 8);
    tx_buf[5] = (uint8_t)(value & 0xFF);
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    RS485_Send_Data(tx_buf, 8);
    
    if (RS485_Receive_Data(rx_buf, 8, 500) != HAL_OK)
    {
        return 1;
    }
    
    return 0;
}

/**
 * @brief  设置输出位掩码 (FC06)
 * @param  mask: 输出位掩码 (bit0=CH1 ... bit15=CH16)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Set_Output_Mask(uint16_t mask)
{
    uint8_t tx_buf[8];
    uint8_t rx_buf[8];
    uint16_t crc;
    
    /* 组装请求帧: FC06 写寄存器 0x0035 */
    tx_buf[0] = RELAY_SLAVE_ID;
    tx_buf[1] = RELAY_FC_WRITE_REGISTER;
    tx_buf[2] = (uint8_t)(RELAY_REG_OUTPUT_MASK >> 8);
    tx_buf[3] = (uint8_t)(RELAY_REG_OUTPUT_MASK & 0xFF);
    tx_buf[4] = (uint8_t)(mask >> 8);
    tx_buf[5] = (uint8_t)(mask & 0xFF);
    
    crc = Modbus_CRC16(tx_buf, 6);
    tx_buf[6] = (uint8_t)(crc & 0xFF);
    tx_buf[7] = (uint8_t)((crc >> 8) & 0xFF);
    
    RS485_Send_Data(tx_buf, 8);
    
    if (RS485_Receive_Data(rx_buf, 8, 500) != HAL_OK)
    {
        return 1;
    }
    
    return 0;
}

/**
 * @brief  翻转指定通道输出状态
 * @param  channel: 通道号 (1-16)
 * @retval 0:成功, 1:失败
 */
uint8_t Relay_Toggle_Output(uint8_t channel)
{
    uint8_t current_state = 0;
    
    /* 读取当前状态 */
    if (Relay_Read_Coil(channel, &current_state) != 0)
    {
        return 1;
    }
    
    /* 写入相反状态 */
    return Relay_Write_Coil(channel, current_state ? 0 : 1);
}
