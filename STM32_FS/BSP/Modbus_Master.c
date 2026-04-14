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

/**
 * @brief 切换 RS485 为发送模式。
 * @param 无
 * @retval 无
 */
/* 通过 485 方向控制脚切到发送态，供主站发起轮询命令。 */
static void Modbus_MasterSetTxMode(void)
{
    HAL_GPIO_WritePin(MODBUS_MASTER_EN_PORT, MODBUS_MASTER_EN_PIN, GPIO_PIN_SET);
}

/**
 * @brief 切换 RS485 为接收模式。
 * @param 无
 * @retval 无
 */
/* 切回接收态，等待传感器/从站回包。 */
static void Modbus_MasterSetRxMode(void)
{
    HAL_GPIO_WritePin(MODBUS_MASTER_EN_PORT, MODBUS_MASTER_EN_PIN, GPIO_PIN_RESET);
}

/**
 * @brief 将 nanoMODBUS 的超时参数转换为 HAL 使用的超时值。
 * @param timeout_ms: 毫秒超时，负数表示无限等待
 * @retval 转换后的 uint32_t 超时值
 */
/* nanoMODBUS 使用 int32_t 超时参数，这里统一换算到 HAL 需要的 uint32_t。 */
static uint32_t Modbus_MasterResolveTimeout(int32_t timeout_ms)
{
    if (timeout_ms < 0)
    {
        return HAL_MAX_DELAY;
    }

    return (uint32_t)timeout_ms;
}

/**
 * @brief 清除 USART2 接收错误标志和残留字节。
 * @param 无
 * @retval 无
 */
static void Modbus_MasterClearRxFlags(void)
{
    /* 每次读之前先清错误与残留字节，避免把上一帧尾巴当成本帧开头。 */
    __HAL_UART_CLEAR_OREFLAG(&g_rs485_uart);
    __HAL_UART_CLEAR_NEFLAG(&g_rs485_uart);
    __HAL_UART_CLEAR_FEFLAG(&g_rs485_uart);

    while (__HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_RXNE) != RESET)
    {
        (void)g_rs485_uart.Instance->DR;
    }
}

/**
 * @brief 初始化总线 1 的 Modbus 主站串口与 485 方向控制。
 * @param 无
 * @retval 无
 */
void Modbus_MasterInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* USART2 + PD7 作为总线 1 主站，轮询 PT100 / 称重 / 继电器等外设。 */
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

/**
 * @brief 通过 USART2 发送一帧 Modbus 主站请求。
 * @param buf: 待发送的数据缓冲区
 * @param len: 待发送的字节数
 * @retval 无
 */
void Modbus_MasterSend(const uint8_t *buf, uint16_t len)
{
    if (buf == NULL || len == 0)
    {
        return;
    }

    /* 半双工 485 必须先切发送态，再留一点硬件建立时间。 */
    Modbus_MasterSetTxMode();

    for (volatile int i = 0; i < 500; i++);

    HAL_UART_Transmit(&g_rs485_uart, (uint8_t *)buf, len, 100);

    /* 等待移位寄存器彻底发空，避免最后一个字节还没出线就切回接收。 */
    while (__HAL_UART_GET_FLAG(&g_rs485_uart, UART_FLAG_TC) == RESET);

    for (volatile int i = 0; i < 500; i++);

    Modbus_MasterSetRxMode();
}

/**
 * @brief 从 USART2 同步接收指定长度的从站响应。
 * @param buf: 接收缓冲区
 * @param len: 期望接收的字节数
 * @param timeout: HAL 接收超时，单位毫秒
 * @retval HAL_StatusTypeDef: HAL_OK/HAL_TIMEOUT/HAL_ERROR
 */
HAL_StatusTypeDef Modbus_MasterReceive(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    if (buf == NULL || len == 0)
    {
        return HAL_ERROR;
    }

    /* 主站读回包前统一清状态，尽量降低粘包和假超时。 */
    Modbus_MasterSetRxMode();
    Modbus_MasterClearRxFlags();
    return HAL_UART_Receive(&g_rs485_uart, buf, len, timeout);
}

/**
 * @brief nanoMODBUS 读适配函数，从主站串口读取指定数量字节。
 * @param buf: 输出缓冲区
 * @param count: 期望读取的字节数
 * @param byte_timeout_ms: 字节超时，0 表示仅取当前 FIFO 中已有字节
 * @param arg: 适配层保留参数，当前未使用
 * @retval >0 表示读取到的字节数，0 表示超时，<0 表示错误
 */
int32_t Modbus_Master_NMBS_Read(uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    (void)arg;

    if (buf == NULL || count == 0)
    {
        return -1;
    }

    /* 这里是 nanoMODBUS 的底层读适配：它要字节流，我们从 UART 同步取。 */
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

/**
 * @brief nanoMODBUS 写适配函数，把协议栈组织好的请求帧发到总线。
 * @param buf: 待发送缓冲区
 * @param count: 待发送字节数
 * @param byte_timeout_ms: 发送超时，单位毫秒
 * @param arg: 适配层保留参数，当前未使用
 * @retval >0 表示发送的字节数，0 表示超时，<0 表示错误
 */
int32_t Modbus_Master_NMBS_Write(const uint8_t *buf, uint16_t count, int32_t byte_timeout_ms, void *arg)
{
    (void)arg;

    if (buf == NULL || count == 0)
    {
        return -1;
    }

    /* 这里是 nanoMODBUS 的底层写适配：协议栈组织好帧，本层只负责发出去。 */
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
