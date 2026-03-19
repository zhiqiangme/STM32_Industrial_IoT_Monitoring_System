#include "Modbus_Slave.h"
#include <stdio.h>
#include <string.h>

#define MODBUS_SLAVE_UART         USART3
#define MODBUS_SLAVE_EN_PORT      GPIOA
#define MODBUS_SLAVE_EN_PIN       GPIO_PIN_5
#define MODBUS_SLAVE_TX_PORT      GPIOB
#define MODBUS_SLAVE_TX_PIN       GPIO_PIN_10
#define MODBUS_SLAVE_RX_PORT      GPIOB
#define MODBUS_SLAVE_RX_PIN       GPIO_PIN_11
#define MODBUS_SLAVE_TX_ENABLE()  HAL_GPIO_WritePin(MODBUS_SLAVE_EN_PORT, MODBUS_SLAVE_EN_PIN, GPIO_PIN_SET)
#define MODBUS_SLAVE_RX_ENABLE()  HAL_GPIO_WritePin(MODBUS_SLAVE_EN_PORT, MODBUS_SLAVE_EN_PIN, GPIO_PIN_RESET)

static UART_HandleTypeDef g_uart3_handle;
static volatile ModbusSlaveState g_state = MODBUS_SLAVE_STATE_IDLE;
static volatile uint32_t g_silence_start_tick = 0;

static uint8_t g_tx_buffer[MODBUS_SLAVE_TX_BUFFER_SIZE];
static volatile uint16_t g_tx_len = 0;
static volatile uint8_t g_tx_pending = 0;

static uint8_t g_rx_buffer[MODBUS_SLAVE_RX_BUFFER_SIZE];
static volatile uint16_t g_rx_head = 0;
static volatile uint16_t g_rx_tail = 0;

static uint32_t Modbus_SlaveResolveTimeout(int32_t timeout_ms)
{
    if (timeout_ms < 0)
    {
        return HAL_MAX_DELAY;
    }

    return (uint32_t)timeout_ms;
}

static HAL_StatusTypeDef Modbus_SlaveTransmitFrame(const uint8_t *data, uint16_t len, uint32_t timeout)
{
    if (data == NULL || len == 0)
    {
        return HAL_ERROR;
    }

    MODBUS_SLAVE_TX_ENABLE();
    HAL_Delay(1);

    HAL_StatusTypeDef status = HAL_UART_Transmit(&g_uart3_handle, (uint8_t *)data, len, timeout);
    if (status == HAL_OK)
    {
        while (__HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_TC) == RESET);
    }

    MODBUS_SLAVE_RX_ENABLE();
    return status;
}

void Modbus_Slave_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 使能 GPIO/USART3 时钟并配置引脚 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    /* Direction control on PA5 */
    gpio.Pin = MODBUS_SLAVE_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    MODBUS_SLAVE_RX_ENABLE();
    HAL_GPIO_Init(MODBUS_SLAVE_EN_PORT, &gpio);

    /* PB10 TX */
    gpio.Pin = MODBUS_SLAVE_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MODBUS_SLAVE_TX_PORT, &gpio);

    /* PB11 RX */
    gpio.Pin = MODBUS_SLAVE_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MODBUS_SLAVE_RX_PORT, &gpio);

    g_uart3_handle.Instance = MODBUS_SLAVE_UART;
    g_uart3_handle.Init.BaudRate = MODBUS_SLAVE_BAUDRATE;
    g_uart3_handle.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart3_handle.Init.StopBits = UART_STOPBITS_1;
    g_uart3_handle.Init.Parity = UART_PARITY_NONE;
    g_uart3_handle.Init.Mode = UART_MODE_TX_RX;
    g_uart3_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart3_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_uart3_handle);

    g_state = MODBUS_SLAVE_STATE_IDLE;
    g_tx_pending = 0;
    g_rx_head = 0;
    g_rx_tail = 0;

    printf("[Modbus3] Init OK (USART3+PA5)\r\n");
}

bool Modbus_Slave_SendAsync(const uint8_t *data, uint16_t len)
{
    /* 仅在空闲时接受新的帧请求 */
    if (g_state != MODBUS_SLAVE_STATE_IDLE)
    {
        return false;
    }

    if (data == NULL || len == 0 || len > MODBUS_SLAVE_TX_BUFFER_SIZE)
    {
        return false;
    }

    memcpy(g_tx_buffer, data, len);
    g_tx_len = len;
    g_tx_pending = 1;
    return true;
}

void Modbus_Slave_Process(void)
{
    /* 简单双状态机：Idle -> Sending -> PostSilence -> Idle */
    switch (g_state)
    {
        case MODBUS_SLAVE_STATE_IDLE:
            if (g_tx_pending)
            {
                g_tx_pending = 0;
                g_state = MODBUS_SLAVE_STATE_SENDING;

                Modbus_SlaveTransmitFrame(g_tx_buffer, g_tx_len, 2000);
                g_silence_start_tick = HAL_GetTick();
                g_state = MODBUS_SLAVE_STATE_POST_SILENCE;
            }
            break;

        case MODBUS_SLAVE_STATE_SENDING:
            g_state = MODBUS_SLAVE_STATE_IDLE;
            break;

        case MODBUS_SLAVE_STATE_POST_SILENCE:
            if ((HAL_GetTick() - g_silence_start_tick) >= MODBUS_SLAVE_SILENCE_MS)
            {
                g_state = MODBUS_SLAVE_STATE_IDLE;
            }
            break;
    }
}

uint16_t Modbus_Slave_ReadRx(uint8_t *out, uint16_t maxlen)
{
    if (out == NULL || maxlen == 0)
    {
        return 0;
    }

    uint16_t count = 0;
    while (g_rx_tail != g_rx_head && count < maxlen)
    {
        out[count++] = g_rx_buffer[g_rx_tail];
        g_rx_tail = (uint16_t)((g_rx_tail + 1) % MODBUS_SLAVE_RX_BUFFER_SIZE);
    }

    return count;
}

ModbusSlaveState Modbus_Slave_GetState(void)
{
    return g_state;
}

bool Modbus_Slave_IsIdle(void)
{
    return (g_state == MODBUS_SLAVE_STATE_IDLE) && (g_tx_pending == 0);
}

void Modbus_Slave_TxCpltCallback(void)
{
}

void Modbus_Slave_RxCallback(uint8_t byte)
{
    /* 简单环形缓冲写入，留出 1 字节空间避免 head==tail 冲突 */
    uint16_t next_head = (uint16_t)((g_rx_head + 1) % MODBUS_SLAVE_RX_BUFFER_SIZE);
    if (next_head != g_rx_tail)
    {
        g_rx_buffer[g_rx_head] = byte;
        g_rx_head = next_head;
    }
}

UART_HandleTypeDef *Modbus_Slave_GetHandle(void)
{
    return &g_uart3_handle;
}

int32_t Modbus_Slave_NMBS_Read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    (void)arg;

    if (buf == NULL || count == 0)
    {
        return -1;
    }

    MODBUS_SLAVE_RX_ENABLE();

    if (byte_timeout_ms == 0)
    {
        uint16_t rx_count = 0;

        while (rx_count < count && __HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_RXNE) != RESET)
        {
            buf[rx_count++] = (uint8_t)(g_uart3_handle.Instance->DR & 0xFF);
        }

        return (int32_t)rx_count;
    }

    switch (HAL_UART_Receive(&g_uart3_handle, buf, count, Modbus_SlaveResolveTimeout(byte_timeout_ms)))
    {
        case HAL_OK:
            return (int32_t)count;

        case HAL_TIMEOUT:
            return 0;

        default:
            return -1;
    }
}

int32_t Modbus_Slave_NMBS_Write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    (void)arg;

    switch (Modbus_SlaveTransmitFrame(buf, count, Modbus_SlaveResolveTimeout(byte_timeout_ms)))
    {
        case HAL_OK:
            return (int32_t)count;

        case HAL_TIMEOUT:
            return 0;

        default:
            return -1;
    }
}
