/**
 * @file    modbus_slave.c
 * @brief   Modbus RTU从站驱动实现
 * @note    USART2 + PD7方向控制
 */

#include "modbus_slave.h"
#include <string.h>
#include <stdio.h>

/*----------------------- 硬件定义 -----------------------*/
#define SLAVE_EN_PORT       GPIOD
#define SLAVE_EN_PIN        GPIO_PIN_7
#define SLAVE_TX_ENABLE()   HAL_GPIO_WritePin(SLAVE_EN_PORT, SLAVE_EN_PIN, GPIO_PIN_SET)
#define SLAVE_RX_ENABLE()   HAL_GPIO_WritePin(SLAVE_EN_PORT, SLAVE_EN_PIN, GPIO_PIN_RESET)

/*----------------------- 内部变量 -----------------------*/
static UART_HandleTypeDef g_huart2;

/* 接收缓冲区 */
static uint8_t g_rx_buf[MODBUS_RX_BUF_SIZE];
static volatile uint16_t g_rx_len = 0;
static volatile uint32_t g_last_rx_tick = 0;
static volatile uint8_t g_frame_ready = 0;

/* 发送缓冲区 */
static uint8_t g_tx_buf[MODBUS_TX_BUF_SIZE];

/* 寄存器数据 */
static uint16_t g_registers[MODBUS_REG_COUNT];

/*----------------------- CRC16计算 -----------------------*/
static uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (uint8_t i = 8; i != 0; i--)
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

/*----------------------- 初始化 -----------------------*/
void ModbusSlave_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    /* 使能时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();
    
    /* PD7 方向控制 */
    GPIO_InitStruct.Pin = SLAVE_EN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SLAVE_EN_PORT, &GPIO_InitStruct);
    SLAVE_RX_ENABLE();  /* 默认接收模式 */
    
    /* PA2 TX */
    GPIO_InitStruct.Pin = GPIO_PIN_2;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    /* PA3 RX */
    GPIO_InitStruct.Pin = GPIO_PIN_3;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
    
    /* USART2配置 */
    g_huart2.Instance = USART2;
    g_huart2.Init.BaudRate = MODBUS_BAUDRATE;
    g_huart2.Init.WordLength = UART_WORDLENGTH_8B;
    g_huart2.Init.StopBits = UART_STOPBITS_1;
    g_huart2.Init.Parity = UART_PARITY_NONE;
    g_huart2.Init.Mode = UART_MODE_TX_RX;
    g_huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_huart2);
    
    /* 使能接收中断 */
    __HAL_UART_ENABLE_IT(&g_huart2, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
    
    /* 清空缓冲区 */
    g_rx_len = 0;
    g_frame_ready = 0;
    memset(g_registers, 0, sizeof(g_registers));
    
    printf("[ModbusSlave] Init OK (Addr=%d, USART2+PD7)\r\n", MODBUS_SLAVE_ADDR);
}

/*----------------------- 接收回调 -----------------------*/
void ModbusSlave_RxCallback(uint8_t byte)
{
    if (g_rx_len < MODBUS_RX_BUF_SIZE)
    {
        g_rx_buf[g_rx_len++] = byte;
    }
    g_last_rx_tick = HAL_GetTick();
}

/*----------------------- 发送响应 -----------------------*/
static void SendResponse(uint8_t *data, uint16_t len)
{
    /* 添加CRC */
    uint16_t crc = Modbus_CRC16(data, len);
    data[len] = (uint8_t)(crc & 0xFF);
    data[len + 1] = (uint8_t)((crc >> 8) & 0xFF);
    len += 2;
    
    /* 切换到发送模式 */
    SLAVE_TX_ENABLE();
    for(volatile int i = 0; i < 500; i++);  /* 稳定延时 */
    
    /* 发送 */
    HAL_UART_Transmit(&g_huart2, data, len, 100);
    
    /* 等待发送完成 */
    while(__HAL_UART_GET_FLAG(&g_huart2, UART_FLAG_TC) == RESET);
    
    /* 切换回接收模式 */
    for(volatile int i = 0; i < 200; i++);
    SLAVE_RX_ENABLE();
    
#if 0  /* 调试输出 */
    printf("[Slave_TX] ");
    for(uint16_t i = 0; i < len; i++) printf("%02X ", data[i]);
    printf("\r\n");
#endif
}

/*----------------------- 发送异常响应 -----------------------*/
static void SendException(uint8_t fc, uint8_t exception_code)
{
    g_tx_buf[0] = MODBUS_SLAVE_ADDR;
    g_tx_buf[1] = fc | 0x80;  /* 功能码+0x80表示异常 */
    g_tx_buf[2] = exception_code;
    SendResponse(g_tx_buf, 3);
}

/*----------------------- 处理读输入寄存器 (FC04) -----------------------*/
static void HandleReadInputRegs(uint8_t *frame, uint16_t len)
{
    if (len < 8) return;
    
    uint16_t start_addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t reg_count = ((uint16_t)frame[4] << 8) | frame[5];
    
    /* 检查范围 */
    if (start_addr + reg_count > MODBUS_REG_COUNT || reg_count > 125)
    {
        SendException(FC_READ_INPUT_REGS, 0x02);  /* 非法数据地址 */
        return;
    }
    
    /* 构建响应 */
    g_tx_buf[0] = MODBUS_SLAVE_ADDR;
    g_tx_buf[1] = FC_READ_INPUT_REGS;
    g_tx_buf[2] = (uint8_t)(reg_count * 2);  /* 字节数 */
    
    uint16_t idx = 3;
    for (uint16_t i = 0; i < reg_count; i++)
    {
        uint16_t reg_val = g_registers[start_addr + i];
        g_tx_buf[idx++] = (uint8_t)(reg_val >> 8);    /* 高字节 */
        g_tx_buf[idx++] = (uint8_t)(reg_val & 0xFF);  /* 低字节 */
    }
    
    SendResponse(g_tx_buf, idx);
}

/*----------------------- 处理写单个寄存器 (FC06) -----------------------*/
static void HandleWriteSingleReg(uint8_t *frame, uint16_t len)
{
    if (len < 8) return;
    
    uint16_t reg_addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t reg_val = ((uint16_t)frame[4] << 8) | frame[5];
    
    /* 调试输出：打印收到的FC06请求 */
    printf("[FC06] Addr=%u Val=0x%04X (RAW: ", reg_addr, reg_val);
    for (uint16_t i = 0; i < len && i < 10; i++) printf("%02X ", frame[i]);
    printf(")\r\n");
    
    /* 只允许写入继电器控制寄存器 */
    if (reg_addr != REG_RELAY_CTRL && reg_addr != REG_RELAY_BITS)
    {
        printf("[FC06] Rejected: Addr %u not allowed\r\n", reg_addr);
        SendException(FC_WRITE_SINGLE_REG, 0x02);  /* 非法数据地址 */
        return;
    }
    
    /* 写入寄存器 */
    g_registers[reg_addr] = reg_val;
    
    if (reg_addr == REG_RELAY_CTRL)
        printf("[Slave] Write RELAY_CTRL = 0x%04X\r\n", reg_val);
    else
        printf("[Slave] Write RELAY_BITS = 0x%04X\r\n", reg_val);
    
    /* 响应 (原样返回请求帧) */
    memcpy(g_tx_buf, frame, 6);
    SendResponse(g_tx_buf, 6);
}

/*----------------------- 处理写单个线圈 (FC05) -----------------------*/
static void HandleWriteSingleCoil(uint8_t *frame, uint16_t len)
{
    if (len < 8) return;
    
    uint16_t coil_addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t coil_val = ((uint16_t)frame[4] << 8) | frame[5];
    
    /* 调试输出 */
    printf("[FC05] CoilAddr=%u Val=0x%04X (RAW: ", coil_addr, coil_val);
    for (uint16_t i = 0; i < len && i < 10; i++) printf("%02X ", frame[i]);
    printf(")\r\n");
    
    /* 计算寄存器地址和位号 */
    /* 云端保持寄存器4.21.0的线圈地址可能是: (21-1)*16+0=320 或其他格式 */
    /* 这里假设线圈地址直接对应位号 (0-15 对应 REG_RELAY_BITS 的 bit0-bit15) */
    
    uint16_t bit_num = coil_addr % 16;  /* 位号 0-15 */
    uint16_t reg_idx = coil_addr / 16;  /* 寄存器索引 */
    
    printf("[FC05] RegIdx=%u BitNum=%u\r\n", reg_idx, bit_num);
    
    /* 只允许控制 REG_RELAY_BITS 对应的线圈 */
    /* 假设 REG_RELAY_BITS(地址0x14=20) 对应线圈地址 20*16=320 到 320+15 */
    /* 或者简单地：线圈地址20.0-20.15对应位0-15 */
    
    /* 为了兼容性，我们接受任何线圈地址，直接作为位号 */
    if (bit_num > 15)
    {
        SendException(FC_WRITE_SINGLE_COIL, 0x02);
        return;
    }
    
    /* 读取当前寄存器值 */
    uint16_t current = g_registers[REG_RELAY_BITS];
    
    /* 修改对应位 */
    if (coil_val == 0xFF00)  /* ON */
        current |= (1 << bit_num);
    else if (coil_val == 0x0000)  /* OFF */
        current &= ~(1 << bit_num);
    else
    {
        SendException(FC_WRITE_SINGLE_COIL, 0x03);  /* 非法数据值 */
        return;
    }
    
    /* 写入寄存器 */
    g_registers[REG_RELAY_BITS] = current;
    printf("[FC05] RELAY_BITS bit%u -> %s, Now=0x%04X\r\n", 
           bit_num, (coil_val == 0xFF00) ? "ON" : "OFF", current);
    
    /* 响应 (原样返回请求帧) */
    memcpy(g_tx_buf, frame, 6);
    SendResponse(g_tx_buf, 6);
}

/*----------------------- 处理帧 -----------------------*/
static void ProcessFrame(void)
{
    if (g_rx_len < 4) return;  /* 最小帧长度 */
    
#if 1  /* 调试输出 */
    printf("[Slave_RX] ");
    for(uint16_t i = 0; i < g_rx_len; i++) printf("%02X ", g_rx_buf[i]);
    printf("\r\n");
#endif
    
    /* 检查地址 */
    if (g_rx_buf[0] != MODBUS_SLAVE_ADDR)
    {
        return;  /* 不是发给我的 */
    }
    
    /* 检查CRC */
    uint16_t crc_calc = Modbus_CRC16(g_rx_buf, g_rx_len - 2);
    uint16_t crc_recv = ((uint16_t)g_rx_buf[g_rx_len - 1] << 8) | g_rx_buf[g_rx_len - 2];
    if (crc_calc != crc_recv)
    {
        printf("[Slave] CRC Error\r\n");
        return;
    }
    
    /* 处理功能码 */
    uint8_t fc = g_rx_buf[1];
    switch (fc)
    {
        case FC_READ_INPUT_REGS:
        case FC_READ_HOLD_REGS:  /* 也支持FC03 */
            HandleReadInputRegs(g_rx_buf, g_rx_len);
            break;
        
        case FC_WRITE_SINGLE_REG:  /* 写单个寄存器 */
            HandleWriteSingleReg(g_rx_buf, g_rx_len);
            break;
        
        case FC_WRITE_SINGLE_COIL:  /* 写单个线圈 (云端按位控制) */
            HandleWriteSingleCoil(g_rx_buf, g_rx_len);
            break;
            
        default:
            SendException(fc, 0x01);  /* 非法功能码 */
            break;
    }
}

/*----------------------- 任务处理 -----------------------*/
void ModbusSlave_Task(void)
{
    /* 检测帧结束 (3.5字符时间，约10ms) */
    if (g_rx_len > 0 && !g_frame_ready)
    {
        if ((HAL_GetTick() - g_last_rx_tick) >= MODBUS_FRAME_TIMEOUT_MS)
        {
            g_frame_ready = 1;
        }
    }
    
    /* 处理完整帧 */
    if (g_frame_ready)
    {
        ProcessFrame();
        
        /* 清空接收缓冲 */
        g_rx_len = 0;
        g_frame_ready = 0;
    }
}

/*----------------------- 更新寄存器数据 -----------------------*/
void ModbusSlave_UpdateData(const ModbusSlaveData_t *data)
{
    if (data == NULL) return;
    
    __disable_irq();
    
    /* 上报序号 */
    g_registers[REG_PUSH_SEQ] = data->push_seq;
    
    /* PT100温度 */
    g_registers[REG_PT100_CH1] = (uint16_t)data->pt100_ch[0];
    g_registers[REG_PT100_CH2] = (uint16_t)data->pt100_ch[1];
    g_registers[REG_PT100_CH3] = (uint16_t)data->pt100_ch[2];
    g_registers[REG_PT100_CH4] = (uint16_t)data->pt100_ch[3];
    
    /* ZSG4称重 (4通道, 32位大端模式) */
    g_registers[REG_ZSG4_CH1_H] = (uint16_t)((data->zsg4_ch[0] >> 16) & 0xFFFF);
    g_registers[REG_ZSG4_CH1_L] = (uint16_t)(data->zsg4_ch[0] & 0xFFFF);
    g_registers[REG_ZSG4_CH2_H] = (uint16_t)((data->zsg4_ch[1] >> 16) & 0xFFFF);
    g_registers[REG_ZSG4_CH2_L] = (uint16_t)(data->zsg4_ch[1] & 0xFFFF);
    g_registers[REG_ZSG4_CH3_H] = (uint16_t)((data->zsg4_ch[2] >> 16) & 0xFFFF);
    g_registers[REG_ZSG4_CH3_L] = (uint16_t)(data->zsg4_ch[2] & 0xFFFF);
    g_registers[REG_ZSG4_CH4_H] = (uint16_t)((data->zsg4_ch[3] >> 16) & 0xFFFF);
    g_registers[REG_ZSG4_CH4_L] = (uint16_t)(data->zsg4_ch[3] & 0xFFFF);
    
    /* 流量 */
    g_registers[REG_FLOW_RATE] = data->flow_rate;
    g_registers[REG_FLOW_TOTAL_LOW] = (uint16_t)(data->flow_total & 0xFFFF);
    g_registers[REG_FLOW_TOTAL_HIGH] = (uint16_t)((data->flow_total >> 16) & 0xFFFF);
    
    /* 继电器 (16位位图) */
    g_registers[REG_RELAY_DO] = data->relay_do;
    g_registers[REG_RELAY_DI] = data->relay_di;
    
    /* 状态 */
    g_registers[REG_SYSTEM_STATUS] = data->status;
    
    __enable_irq();
}

/*----------------------- 获取句柄 -----------------------*/
UART_HandleTypeDef* ModbusSlave_GetHandle(void)
{
    return &g_huart2;
}

/*----------------------- 获取继电器控制值 -----------------------*/
uint16_t ModbusSlave_GetRelayCtrl(void)
{
    return g_registers[REG_RELAY_CTRL];
}

/*----------------------- 获取继电器按位控制值 -----------------------*/
uint16_t ModbusSlave_GetRelayBits(void)
{
    return g_registers[REG_RELAY_BITS];
}
