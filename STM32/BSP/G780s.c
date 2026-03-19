#include "G780s.h"
#include <stdio.h>
#include <string.h>

#define G780S_EN_PORT              GPIOA
#define G780S_EN_PIN               GPIO_PIN_5
#define G780S_TX_PORT              GPIOB
#define G780S_TX_PIN               GPIO_PIN_10
#define G780S_RX_PORT              GPIOB
#define G780S_RX_PIN               GPIO_PIN_11
#define G780S_TX_ENABLE()          HAL_GPIO_WritePin(G780S_EN_PORT, G780S_EN_PIN, GPIO_PIN_SET)
#define G780S_RX_ENABLE()          HAL_GPIO_WritePin(G780S_EN_PORT, G780S_EN_PIN, GPIO_PIN_RESET)

#define MODBUS_BAUDRATE            115200
#define MODBUS_RX_BUF_SIZE         64
#define MODBUS_TX_BUF_SIZE         64
#define MODBUS_FRAME_TIMEOUT_MS    10

#define FC_READ_INPUT_REGS         0x04
#define FC_READ_HOLD_REGS          0x03
#define FC_WRITE_SINGLE_REG        0x06
#define FC_WRITE_SINGLE_COIL       0x05

static UART_HandleTypeDef g_huart3;
static uint8_t g_rx_buf[MODBUS_RX_BUF_SIZE];
static volatile uint16_t g_rx_len = 0;
static volatile uint32_t g_last_rx_tick = 0;
static volatile uint8_t g_frame_ready = 0;

static uint8_t g_tx_buf[MODBUS_TX_BUF_SIZE];
static uint16_t g_registers[MODBUS_REG_COUNT];

/* Keep the last valid response around for debugging unexpected line noise. */
static uint8_t g_last_tx_len = 0;

static uint16_t Modbus_CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFF;

    for (uint16_t pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (uint8_t i = 0; i < 8; i++)
        {
            if (crc & 0x0001U)
            {
                crc >>= 1;
                crc ^= 0xA001U;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

static void G780s_ClearPendingRx(void)
{
    while (__HAL_UART_GET_FLAG(&g_huart3, UART_FLAG_RXNE) != RESET)
    {
        (void)(g_huart3.Instance->DR & 0xFF);
    }

    if (__HAL_UART_GET_FLAG(&g_huart3, UART_FLAG_ORE) != RESET)
    {
        __HAL_UART_CLEAR_OREFLAG(&g_huart3);
    }
    if (__HAL_UART_GET_FLAG(&g_huart3, UART_FLAG_FE) != RESET)
    {
        __HAL_UART_CLEAR_FEFLAG(&g_huart3);
    }
    if (__HAL_UART_GET_FLAG(&g_huart3, UART_FLAG_NE) != RESET)
    {
        __HAL_UART_CLEAR_NEFLAG(&g_huart3);
    }
}

static void G780s_SendResponse(uint8_t *data, uint16_t len)
{
    uint16_t crc;

    if (len + 2 > MODBUS_TX_BUF_SIZE)
    {
        return;
    }

    crc = Modbus_CRC16(data, len);
    data[len] = (uint8_t)(crc & 0xFF);
    data[len + 1] = (uint8_t)((crc >> 8) & 0xFF);
    len += 2;

    memcpy(g_tx_buf, data, len);
    g_last_tx_len = (uint8_t)len;

    G780S_TX_ENABLE();
    for (volatile int i = 0; i < 500; i++)
    {
    }

    (void)HAL_UART_Transmit(&g_huart3, g_tx_buf, len, 100);
    while (__HAL_UART_GET_FLAG(&g_huart3, UART_FLAG_TC) == RESET)
    {
    }

    for (volatile int i = 0; i < 200; i++)
    {
    }
    G780S_RX_ENABLE();
    G780s_ClearPendingRx();
}

static void G780s_SendException(uint8_t fc, uint8_t exception_code)
{
    uint8_t frame[5];

    frame[0] = MODBUS_SLAVE_ADDR;
    frame[1] = (uint8_t)(fc | 0x80U);
    frame[2] = exception_code;
    G780s_SendResponse(frame, 3);
}

static void G780s_HandleReadRegs(const uint8_t *frame, uint16_t len, uint8_t fc)
{
    uint16_t start_addr;
    uint16_t reg_count;
    uint16_t idx = 3;

    if (len < 8)
    {
        return;
    }

    start_addr = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    reg_count = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);

    if (reg_count == 0 || reg_count > 125 || (uint32_t)start_addr + reg_count > MODBUS_REG_COUNT)
    {
        G780s_SendException(fc, 0x02);
        return;
    }

    g_tx_buf[0] = MODBUS_SLAVE_ADDR;
    g_tx_buf[1] = fc;
    g_tx_buf[2] = (uint8_t)(reg_count * 2U);

    for (uint16_t i = 0; i < reg_count; i++)
    {
        uint16_t reg_val = g_registers[start_addr + i];
        g_tx_buf[idx++] = (uint8_t)(reg_val >> 8);
        g_tx_buf[idx++] = (uint8_t)(reg_val & 0xFF);
    }

    G780s_SendResponse(g_tx_buf, idx);
}

static void G780s_HandleWriteSingleReg(const uint8_t *frame, uint16_t len)
{
    uint16_t reg_addr;
    uint16_t reg_val;

    if (len < 8)
    {
        return;
    }

    reg_addr = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    reg_val = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);

    if (reg_addr != REG_RELAY_CTRL && reg_addr != REG_RELAY_CMD_BITS)
    {
        G780s_SendException(FC_WRITE_SINGLE_REG, 0x02);
        return;
    }

    g_registers[reg_addr] = reg_val;
    memcpy(g_tx_buf, frame, 6);
    G780s_SendResponse(g_tx_buf, 6);
}

static void G780s_HandleWriteSingleCoil(const uint8_t *frame, uint16_t len)
{
    uint16_t coil_addr;
    uint16_t coil_val;
    uint16_t bit_num;
    uint16_t current;

    if (len < 8)
    {
        return;
    }

    coil_addr = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    coil_val = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);
    bit_num = (uint16_t)(coil_addr % 16U);

    if (bit_num > 15U)
    {
        G780s_SendException(FC_WRITE_SINGLE_COIL, 0x02);
        return;
    }

    current = g_registers[REG_RELAY_CMD_BITS];

    if (coil_val == 0xFF00U)
    {
        current |= (uint16_t)(1u << bit_num);
    }
    else if (coil_val == 0x0000U)
    {
        current &= (uint16_t)~(1u << bit_num);
    }
    else
    {
        G780s_SendException(FC_WRITE_SINGLE_COIL, 0x03);
        return;
    }

    g_registers[REG_RELAY_CMD_BITS] = current;
    memcpy(g_tx_buf, frame, 6);
    G780s_SendResponse(g_tx_buf, 6);
}

static void G780s_ProcessFrame(void)
{
    uint16_t crc_calc;
    uint16_t crc_recv;
    uint8_t fc;

    if (g_rx_len < 4)
    {
        return;
    }

    if (g_rx_buf[0] != MODBUS_SLAVE_ADDR)
    {
        return;
    }

    crc_calc = Modbus_CRC16(g_rx_buf, (uint16_t)(g_rx_len - 2));
    crc_recv = (uint16_t)(((uint16_t)g_rx_buf[g_rx_len - 1] << 8) | g_rx_buf[g_rx_len - 2]);
    if (crc_calc != crc_recv)
    {
        return;
    }

    fc = g_rx_buf[1];
    switch (fc)
    {
        case FC_READ_INPUT_REGS:
        case FC_READ_HOLD_REGS:
            G780s_HandleReadRegs(g_rx_buf, g_rx_len, fc);
            break;

        case FC_WRITE_SINGLE_REG:
            G780s_HandleWriteSingleReg(g_rx_buf, g_rx_len);
            break;

        case FC_WRITE_SINGLE_COIL:
            G780s_HandleWriteSingleCoil(g_rx_buf, g_rx_len);
            break;

        default:
            G780s_SendException(fc, 0x01);
            break;
    }
}

void G780s_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    gpio.Pin = G780S_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(G780S_EN_PORT, &gpio);
    G780S_RX_ENABLE();

    gpio.Pin = G780S_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(G780S_TX_PORT, &gpio);

    gpio.Pin = G780S_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(G780S_RX_PORT, &gpio);

    g_huart3.Instance = USART3;
    g_huart3.Init.BaudRate = MODBUS_BAUDRATE;
    g_huart3.Init.WordLength = UART_WORDLENGTH_8B;
    g_huart3.Init.StopBits = UART_STOPBITS_1;
    g_huart3.Init.Parity = UART_PARITY_NONE;
    g_huart3.Init.Mode = UART_MODE_TX_RX;
    g_huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_huart3);

    __HAL_UART_ENABLE_IT(&g_huart3, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);

    g_rx_len = 0;
    g_frame_ready = 0;
    g_last_rx_tick = 0;
    g_last_tx_len = 0;
    memset(g_registers, 0, sizeof(g_registers));
    G780s_ClearPendingRx();

    printf("[G780s] Init OK (Addr=%d, USART3+PA5, manual RTU)\r\n", MODBUS_SLAVE_ADDR);
}

void G780s_RxCallback(uint8_t byte)
{
    if (g_rx_len < MODBUS_RX_BUF_SIZE)
    {
        g_rx_buf[g_rx_len++] = byte;
    }
    else
    {
        g_rx_len = 0;
    }

    g_last_rx_tick = HAL_GetTick();
}

void G780s_Process(void)
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
        G780s_ProcessFrame();
        g_rx_len = 0;
        g_frame_ready = 0;
    }
}

void G780s_UpdateData(const G780sSlaveData *data)
{
    if (data == NULL)
    {
        return;
    }

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
    g_registers[REG_FLOW_TOTAL_HIGH] = (uint16_t)((data->flow_total >> 16) & 0xFFFF);
    g_registers[REG_FLOW_TOTAL_LOW] = (uint16_t)(data->flow_total & 0xFFFF);

    g_registers[REG_RELAY_DO] = data->relay_do;
    g_registers[REG_RELAY_DI] = data->relay_di;
    g_registers[REG_RELAY_BITS] = data->relay_do;
    g_registers[REG_SYSTEM_STATUS] = data->status;

    __enable_irq();
}

UART_HandleTypeDef *G780s_GetHandle(void)
{
    return &g_huart3;
}

uint16_t G780s_GetRelayCtrl(void)
{
    return g_registers[REG_RELAY_CTRL];
}

uint16_t G780s_GetRelayBits(void)
{
    return g_registers[REG_RELAY_CMD_BITS];
}
