#include "RS485_Slave.h"
#include <stdio.h>
#include <string.h>

#define RS485_SLAVE_EN_PORT      GPIOD
#define RS485_SLAVE_EN_PIN       GPIO_PIN_7
#define RS485_SLAVE_TX_ENABLE()  HAL_GPIO_WritePin(RS485_SLAVE_EN_PORT, RS485_SLAVE_EN_PIN, GPIO_PIN_SET)
#define RS485_SLAVE_RX_ENABLE()  HAL_GPIO_WritePin(RS485_SLAVE_EN_PORT, RS485_SLAVE_EN_PIN, GPIO_PIN_RESET)

static UART_HandleTypeDef g_uart2_handle;
static volatile RS485SlaveState g_state = RS485_SLAVE_STATE_IDLE;
static volatile uint32_t g_silence_start_tick = 0;

static uint8_t g_tx_buffer[RS485_SLAVE_TX_BUFFER_SIZE];
static volatile uint16_t g_tx_len = 0;
static volatile uint8_t g_tx_pending = 0;

static uint8_t g_rx_buffer[RS485_SLAVE_RX_BUFFER_SIZE];
static volatile uint16_t g_rx_head = 0;
static volatile uint16_t g_rx_tail = 0;

void RS485_Slave_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 使能 GPIO/USART2 时钟并配置引脚 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    /* Direction control on PD7 */
    gpio.Pin = RS485_SLAVE_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    RS485_SLAVE_RX_ENABLE();
    HAL_GPIO_Init(RS485_SLAVE_EN_PORT, &gpio);

    /* PA2 TX */
    gpio.Pin = GPIO_PIN_2;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* PA3 RX */
    gpio.Pin = GPIO_PIN_3;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &gpio);

    g_uart2_handle.Instance = USART2;
    g_uart2_handle.Init.BaudRate = RS485_SLAVE_BAUDRATE;
    g_uart2_handle.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart2_handle.Init.StopBits = UART_STOPBITS_1;
    g_uart2_handle.Init.Parity = UART_PARITY_NONE;
    g_uart2_handle.Init.Mode = UART_MODE_TX_RX;
    g_uart2_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart2_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_uart2_handle);

    g_state = RS485_SLAVE_STATE_IDLE;
    g_tx_pending = 0;
    g_rx_head = 0;
    g_rx_tail = 0;

    printf("[RS4853] Init OK (USART2)\r\n");
}

bool RS485_Slave_SendAsync(const uint8_t *data, uint16_t len)
{
    /* 仅在空闲时接受新的帧请求 */
    if (g_state != RS485_SLAVE_STATE_IDLE)
    {
        return false;
    }

    if (data == NULL || len == 0 || len > RS485_SLAVE_TX_BUFFER_SIZE)
    {
        return false;
    }

    memcpy(g_tx_buffer, data, len);
    g_tx_len = len;
    g_tx_pending = 1;
    return true;
}

void RS485_Slave_Process(void)
{
    /* 简单双状态机：Idle -> Sending -> PostSilence -> Idle */
    switch (g_state)
    {
        case RS485_SLAVE_STATE_IDLE:
            if (g_tx_pending)
            {
                g_tx_pending = 0;
                g_state = RS485_SLAVE_STATE_SENDING;

                RS485_SLAVE_TX_ENABLE();
                HAL_Delay(1);

                HAL_UART_Transmit(&g_uart2_handle, g_tx_buffer, g_tx_len, 2000);
                while (__HAL_UART_GET_FLAG(&g_uart2_handle, UART_FLAG_TC) == RESET);

                RS485_SLAVE_RX_ENABLE();
                g_silence_start_tick = HAL_GetTick();
                g_state = RS485_SLAVE_STATE_POST_SILENCE;
            }
            break;

        case RS485_SLAVE_STATE_SENDING:
            g_state = RS485_SLAVE_STATE_IDLE;
            break;

        case RS485_SLAVE_STATE_POST_SILENCE:
            if ((HAL_GetTick() - g_silence_start_tick) >= RS485_SLAVE_SILENCE_MS)
            {
                g_state = RS485_SLAVE_STATE_IDLE;
            }
            break;
    }
}

uint16_t RS485_Slave_ReadRx(uint8_t *out, uint16_t maxlen)
{
    if (out == NULL || maxlen == 0)
    {
        return 0;
    }

    uint16_t count = 0;
    while (g_rx_tail != g_rx_head && count < maxlen)
    {
        out[count++] = g_rx_buffer[g_rx_tail];
        g_rx_tail = (uint16_t)((g_rx_tail + 1) % RS485_SLAVE_RX_BUFFER_SIZE);
    }

    return count;
}

RS485SlaveState RS485_Slave_GetState(void)
{
    return g_state;
}

bool RS485_Slave_IsIdle(void)
{
    return (g_state == RS485_SLAVE_STATE_IDLE) && (g_tx_pending == 0);
}

void RS485_Slave_TxCpltCallback(void)
{
}

void RS485_Slave_RxCallback(uint8_t byte)
{
    /* 简单环形缓冲写入，留出 1 字节空间避免 head==tail 冲突 */
    uint16_t next_head = (uint16_t)((g_rx_head + 1) % RS485_SLAVE_RX_BUFFER_SIZE);
    if (next_head != g_rx_tail)
    {
        g_rx_buffer[g_rx_head] = byte;
        g_rx_head = next_head;
    }
}

UART_HandleTypeDef *RS485_Slave_GetHandle(void)
{
    return &g_uart2_handle;
}
