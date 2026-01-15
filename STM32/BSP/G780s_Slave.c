#include "G780s_Slave.h"
#include <stdio.h>
#include <string.h>

#define SLAVE_EN_PORT       GPIOD
#define SLAVE_EN_PIN        GPIO_PIN_7
#define SLAVE_TX_ENABLE()   HAL_GPIO_WritePin(SLAVE_EN_PORT, SLAVE_EN_PIN, GPIO_PIN_SET)
#define SLAVE_RX_ENABLE()   HAL_GPIO_WritePin(SLAVE_EN_PORT, SLAVE_EN_PIN, GPIO_PIN_RESET)

static UART_HandleTypeDef g_huart2;

static uint8_t g_rx_buf[MODBUS_RX_BUF_SIZE];
static volatile uint16_t g_rx_len = 0;
static volatile uint32_t g_last_rx_tick = 0;
static volatile uint8_t g_frame_ready = 0;

static uint8_t g_tx_buf[MODBUS_TX_BUF_SIZE];
static uint16_t g_registers[MODBUS_REG_COUNT];

/* 计算 Modbus CRC16：多项式 0xA001，低字节在前 */
static uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len)
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

void G780sSlave_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    /* 方向控制引脚：PD7，默认接收 */
    gpio.Pin = SLAVE_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SLAVE_EN_PORT, &gpio);
    SLAVE_RX_ENABLE();

    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* USART2 工作参数：115200 8N1，无流控 */
    g_huart2.Instance = USART2;
    g_huart2.Init.BaudRate = MODBUS_BAUDRATE;
    g_huart2.Init.WordLength = UART_WORDLENGTH_8B;
    g_huart2.Init.StopBits = UART_STOPBITS_1;
    g_huart2.Init.Parity = UART_PARITY_NONE;
    g_huart2.Init.Mode = UART_MODE_TX_RX;
    g_huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_huart2);

    __HAL_UART_ENABLE_IT(&g_huart2, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART2_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);

    g_rx_len = 0;
    g_frame_ready = 0;
    memset(g_registers, 0, sizeof(g_registers));

    printf("[G780sSlave] Init OK (Addr=%d, USART2+PD7)\r\n", MODBUS_SLAVE_ADDR);
}

void G780sSlave_RxCallback(uint8_t byte)
{
    if (g_rx_len < MODBUS_RX_BUF_SIZE)
    {
        g_rx_buf[g_rx_len++] = byte;
    }
    g_last_rx_tick = HAL_GetTick();
}

static void SendResponse(uint8_t *data, uint16_t len)
{
    /* 发送前追加 CRC，再切换到发送模式并等待 TC */
    uint16_t crc = Modbus_CRC16(data, len);
    data[len] = (uint8_t)(crc & 0xFF);
    data[len + 1] = (uint8_t)((crc >> 8) & 0xFF);
    len += 2;

    SLAVE_TX_ENABLE();
    for (volatile int i = 0; i < 500; i++);

    HAL_UART_Transmit(&g_huart2, data, len, 100);

    while (__HAL_UART_GET_FLAG(&g_huart2, UART_FLAG_TC) == RESET);

    for (volatile int i = 0; i < 200; i++);
    SLAVE_RX_ENABLE();
}

static void SendException(uint8_t fc, uint8_t exception_code)
{
    g_tx_buf[0] = MODBUS_SLAVE_ADDR;
    g_tx_buf[1] = fc | 0x80;
    g_tx_buf[2] = exception_code;
    SendResponse(g_tx_buf, 3);
}

static void HandleReadInputRegs(const uint8_t *frame, uint16_t len)
{
    if (len < 8) return;

    uint16_t start_addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t reg_count = ((uint16_t)frame[4] << 8) | frame[5];

    if (start_addr + reg_count > MODBUS_REG_COUNT || reg_count > 125)
    {
        SendException(FC_READ_INPUT_REGS, 0x02);
        return;
    }

    g_tx_buf[0] = MODBUS_SLAVE_ADDR;
    g_tx_buf[1] = FC_READ_INPUT_REGS;
    g_tx_buf[2] = (uint8_t)(reg_count * 2);

    uint16_t idx = 3;
    for (uint16_t i = 0; i < reg_count; i++)
    {
        uint16_t reg_val = g_registers[start_addr + i];
        g_tx_buf[idx++] = (uint8_t)(reg_val >> 8);
        g_tx_buf[idx++] = (uint8_t)(reg_val & 0xFF);
    }

    SendResponse(g_tx_buf, idx);
}

static void HandleWriteSingleReg(const uint8_t *frame, uint16_t len)
{
    if (len < 8) return;

    uint16_t reg_addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t reg_val = ((uint16_t)frame[4] << 8) | frame[5];

    printf("[FC06] Addr=%u Val=0x%04X\r\n", reg_addr, reg_val);

    /* 仅允许写继电器控制寄存器 */
    if (reg_addr != REG_RELAY_CTRL && reg_addr != REG_RELAY_BITS)
    {
        SendException(FC_WRITE_SINGLE_REG, 0x02);
        return;
    }

    g_registers[reg_addr] = reg_val;

    memcpy(g_tx_buf, frame, 6);
    SendResponse(g_tx_buf, 6);
}

static void HandleWriteSingleCoil(const uint8_t *frame, uint16_t len)
{
    if (len < 8) return;

    uint16_t coil_addr = ((uint16_t)frame[2] << 8) | frame[3];
    uint16_t coil_val = ((uint16_t)frame[4] << 8) | frame[5];

    /* 将线圈地址直接映射到 0-15 位 */
    uint16_t bit_num = coil_addr % 16;

    if (bit_num > 15)
    {
        SendException(FC_WRITE_SINGLE_COIL, 0x02);
        return;
    }

    uint16_t current = g_registers[REG_RELAY_BITS];

    if (coil_val == 0xFF00)
    {
        current |= (1u << bit_num);
    }
    else if (coil_val == 0x0000)
    {
        current &= ~(1u << bit_num);
    }
    else
    {
        SendException(FC_WRITE_SINGLE_COIL, 0x03);
        return;
    }

    g_registers[REG_RELAY_BITS] = current;

    memcpy(g_tx_buf, frame, 6);
    SendResponse(g_tx_buf, 6);
}

static void ProcessFrame(void)
{
    if (g_rx_len < 4) return;

    printf("[Slave_RX] ");
    for (uint16_t i = 0; i < g_rx_len; i++) printf("%02X ", g_rx_buf[i]);
    printf("\r\n");

    if (g_rx_buf[0] != MODBUS_SLAVE_ADDR)
    {
        return;
    }

    uint16_t crc_calc = Modbus_CRC16(g_rx_buf, g_rx_len - 2);
    uint16_t crc_recv = ((uint16_t)g_rx_buf[g_rx_len - 1] << 8) | g_rx_buf[g_rx_len - 2];
    if (crc_calc != crc_recv)
    {
        printf("[Slave] CRC Error\r\n");
        return;
    }

    uint8_t fc = g_rx_buf[1];
    switch (fc)
    {
        case FC_READ_INPUT_REGS:
        case FC_READ_HOLD_REGS:
            HandleReadInputRegs(g_rx_buf, g_rx_len);
            break;

        case FC_WRITE_SINGLE_REG:
            HandleWriteSingleReg(g_rx_buf, g_rx_len);
            break;

        case FC_WRITE_SINGLE_COIL:
            HandleWriteSingleCoil(g_rx_buf, g_rx_len);
            break;

        default:
            SendException(fc, 0x01);
            break;
    }
}

void G780sSlave_Process(void)
{
    if (g_rx_len > 0 && !g_frame_ready)
    {
        if ((HAL_GetTick() - g_last_rx_tick) >= MODBUS_FRAME_TIMEOUT_MS)
        {
            g_frame_ready = 1;
        }
    }

    if (g_frame_ready)
    {
        ProcessFrame();
        g_rx_len = 0;
        g_frame_ready = 0;
    }
}

void G780sSlave_UpdateData(const G780sSlaveData *data)
{
    if (data == NULL) return;

    __disable_irq();

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
    g_registers[REG_FLOW_TOTAL_LOW] = (uint16_t)(data->flow_total & 0xFFFF);
    g_registers[REG_FLOW_TOTAL_HIGH] = (uint16_t)((data->flow_total >> 16) & 0xFFFF);

    g_registers[REG_RELAY_DO] = data->relay_do;
    g_registers[REG_RELAY_DI] = data->relay_di;

    g_registers[REG_SYSTEM_STATUS] = data->status;

    __enable_irq();
}

UART_HandleTypeDef *G780sSlave_GetHandle(void)
{
    return &g_huart2;
}

uint16_t G780sSlave_GetRelayCtrl(void)
{
    return g_registers[REG_RELAY_CTRL];
}

uint16_t G780sSlave_GetRelayBits(void)
{
    return g_registers[REG_RELAY_BITS];
}
