#include "Modbus_Master.h"
#include <stdio.h>

#define MODBUS_MASTER_UART              USART2
#define MODBUS_MASTER_BAUDRATE          38400U
#define MODBUS_MASTER_EN_PORT           GPIOD
#define MODBUS_MASTER_EN_PIN            GPIO_PIN_7
#define MODBUS_MASTER_TX_PORT           GPIOA
#define MODBUS_MASTER_TX_PIN            GPIO_PIN_2
#define MODBUS_MASTER_RX_PORT           GPIOA
#define MODBUS_MASTER_RX_PIN            GPIO_PIN_3

UART_HandleTypeDef g_rs485_uart;

static void Modbus_MasterSetTxMode(void)
{
    HAL_GPIO_WritePin(MODBUS_MASTER_EN_PORT, MODBUS_MASTER_EN_PIN, GPIO_PIN_SET);
}

static void Modbus_MasterSetRxMode(void)
{
    HAL_GPIO_WritePin(MODBUS_MASTER_EN_PORT, MODBUS_MASTER_EN_PIN, GPIO_PIN_RESET);
}

static uint32_t Modbus_MasterResolveTimeout(int32_t timeout_ms)
{
    if (timeout_ms < 0)
    {
        return HAL_MAX_DELAY;
    }

    return (uint32_t)timeout_ms;
}

static void Modbus_MasterClearRxFlags(void)
{
    __HAL_UART_CLEAR_OREFLAG(&g_rs485_uart);
    __HAL_UART_CLEAR_NEFLAG(&g_rs485_uart);
    __HAL_UART_CLEAR_FEFLAG(&g_rs485_uart);

    while (__HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_RXNE) != RESET)
    {
        (void)g_rs485_uart.Instance->DR;
    }
}

void Modbus_MasterInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 打开 GPIO 与 USART2 时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    /* 方向控制引脚：PD7，默认接收 */
    gpio.Pin = MODBUS_MASTER_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MODBUS_MASTER_EN_PORT, &gpio);
    Modbus_MasterSetRxMode();

    /* USART2 TX: PA2 */
    gpio.Pin = MODBUS_MASTER_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MODBUS_MASTER_TX_PORT, &gpio);

    /* USART2 RX: PA3 */
    gpio.Pin = MODBUS_MASTER_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(MODBUS_MASTER_RX_PORT, &gpio);

    g_rs485_uart.Instance = MODBUS_MASTER_UART;
    g_rs485_uart.Init.BaudRate = MODBUS_MASTER_BAUDRATE;
    g_rs485_uart.Init.WordLength = UART_WORDLENGTH_8B;
    g_rs485_uart.Init.StopBits = UART_STOPBITS_1;
    g_rs485_uart.Init.Parity = UART_PARITY_NONE;
    g_rs485_uart.Init.Mode = UART_MODE_TX_RX;
    g_rs485_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_rs485_uart.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&g_rs485_uart) != HAL_OK)
    {
        printf("[485] Init FAILED!\r\n");
    }
    else
    {
        printf("[485] Init OK (USART2+PD7, 38400)\r\n");
    }
}

void Modbus_MasterSend(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0)
    {
        return;
    }

    Modbus_MasterSetTxMode();

    for (volatile int i = 0; i < 500; i++);

    HAL_UART_Transmit(&g_rs485_uart, (uint8_t *)buf, len, 100);

    while (__HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_TC) == RESET);

    for (volatile int i = 0; i < 500; i++);

    Modbus_MasterSetRxMode();
}

HAL_StatusTypeDef Modbus_MasterReceive(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    if (buf == NULL || len == 0)
    {
        return HAL_ERROR;
    }

    Modbus_MasterSetRxMode();
    Modbus_MasterClearRxFlags();
    return HAL_UART_Receive(&g_rs485_uart, buf, len, timeout);
}

int32_t Modbus_Master_NMBS_Read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    (void)arg;

    if (buf == NULL || count == 0)
    {
        return -1;
    }

    Modbus_MasterSetRxMode();

    if (byte_timeout_ms == 0)
    {
        uint16_t rx_count = 0;

        while (rx_count < count && __HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_RXNE) != RESET)
        {
            buf[rx_count++] = (uint8_t)(g_rs485_uart.Instance->DR & 0xFF);
        }

        return (int32_t)rx_count;
    }

    switch (HAL_UART_Receive(&g_rs485_uart, buf, count, Modbus_MasterResolveTimeout(byte_timeout_ms)))
    {
        case HAL_OK:
            return (int32_t)count;

        case HAL_TIMEOUT:
            return 0;

        default:
            return -1;
    }
}

int32_t Modbus_Master_NMBS_Write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    (void)arg;

    if (buf == NULL || count == 0)
    {
        return -1;
    }

    Modbus_MasterSetTxMode();

    HAL_StatusTypeDef status = HAL_UART_Transmit(&g_rs485_uart, (uint8_t *)buf, count,
                                                 Modbus_MasterResolveTimeout(byte_timeout_ms));

    if (status == HAL_OK)
    {
        while (__HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_TC) == RESET);
        Modbus_MasterSetRxMode();
        return (int32_t)count;
    }

    Modbus_MasterSetRxMode();
    return (status == HAL_TIMEOUT) ? 0 : -1;
}
