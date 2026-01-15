#include "RS485_Master.h"
#include <stdio.h>

UART_HandleTypeDef g_rs485_uart;

void RS485_MasterInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 打开 GPIO 与 USART3 时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    /* 方向控制引脚：PA5，默认接收 */
    gpio.Pin = RS485_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RS485_EN_PORT, &gpio);
    RS485_RX_MODE();

    /* USART3 TX: PB10 */
    gpio.Pin = RS485_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RS485_TX_PORT, &gpio);

    /* USART3 RX: PB11 */
    gpio.Pin = RS485_RX_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RS485_RX_PORT, &gpio);

    g_rs485_uart.Instance = USART3;
    g_rs485_uart.Init.BaudRate = 38400;
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
        printf("[485] Init OK (USART3+PA5, 38400)\r\n");
    }
}

void RS485_MasterSend(const uint8_t *buf, uint16_t len)
{
    RS485_TX_MODE();

    for (volatile int i = 0; i < 500; i++);

    HAL_UART_Transmit(&g_rs485_uart, (uint8_t *)buf, len, 100);

    while (__HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_TC) == RESET);

    for (volatile int i = 0; i < 500; i++);

    RS485_RX_MODE();
}

HAL_StatusTypeDef RS485_MasterReceive(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    HAL_StatusTypeDef status;

    __HAL_UART_CLEAR_OREFLAG(&g_rs485_uart);
    __HAL_UART_CLEAR_NEFLAG(&g_rs485_uart);
    __HAL_UART_CLEAR_FEFLAG(&g_rs485_uart);

    if (__HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_RXNE))
    {
        (void)g_rs485_uart.Instance->DR;
    }

    status = HAL_UART_Receive(&g_rs485_uart, buf, len, timeout);
    return status;
}
