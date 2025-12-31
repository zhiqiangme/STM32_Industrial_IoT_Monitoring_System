#include "bsp_rs485.h"

UART_HandleTypeDef RS485_UART_Handle;

/**
  * @brief  初始化RS485底层 (USART2, 38400, 8N1)
  * @param  None
  * @retval None
  */
void RS485_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* 1. 开启时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_USART2_CLK_ENABLE();

    /* 2. 配置GPIO PD7 (DE/RE) */
    GPIO_InitStruct.Pin = RS485_RE_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RS485_RE_GPIO_PORT, &GPIO_InitStruct);
    
    /* 默认接收模式 */
    RS485_RX_ENABLE();

    /* 3. 配置USART2 TX (PA2) */
    GPIO_InitStruct.Pin = RS485_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RS485_TX_GPIO_PORT, &GPIO_InitStruct);

    /* 4. 配置USART2 RX (PA3) */
    GPIO_InitStruct.Pin = RS485_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(RS485_RX_GPIO_PORT, &GPIO_InitStruct);

    /* 5. 配置UART参数: 38400, 8, N, 1 */
    RS485_UART_Handle.Instance = USART2;
    RS485_UART_Handle.Init.BaudRate = 38400;
    RS485_UART_Handle.Init.WordLength = UART_WORDLENGTH_8B;
    RS485_UART_Handle.Init.StopBits = UART_STOPBITS_1;
    RS485_UART_Handle.Init.Parity = UART_PARITY_NONE;
    RS485_UART_Handle.Init.Mode = UART_MODE_TX_RX;
    RS485_UART_Handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    RS485_UART_Handle.Init.OverSampling = UART_OVERSAMPLING_16;
    
    if (HAL_UART_Init(&RS485_UART_Handle) != HAL_OK)
    {
        // Initialization Error
    }
}

/**
  * @brief  RS485发送数据
  * @param  buf: 数据指针
  * @param  len: 数据长度
  * @retval None
  */
void RS485_Send_Data(uint8_t *buf, uint16_t len)
{
    /* 切换为发送模式 */
    RS485_TX_ENABLE();
    
    /* 延时确保DE/RE电平稳定 */
    for(volatile int i=0; i<200; i++); 

    HAL_UART_Transmit(&RS485_UART_Handle, buf, len, 100);

    /* 等待发送完成 - 确保最后一个字节完全发出 */
    while(__HAL_UART_GET_FLAG(&RS485_UART_Handle, UART_FLAG_TC) == RESET);

    /* 切换回接收模式 */
    RS485_RX_ENABLE();
    
    /* 延时确保切换到接收模式 */
    for(volatile int i=0; i<200; i++);
}

/**
  * @brief  RS485接收数据
  * @param  buf: 接收缓冲区
  * @param  len: 需要接收的长度
  * @param  timeout: 超时时间(ms)
  * @retval HAL状态
  */
HAL_StatusTypeDef RS485_Receive_Data(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    /* Clear any error flags */
    __HAL_UART_CLEAR_OREFLAG(&RS485_UART_Handle);
    __HAL_UART_CLEAR_NEFLAG(&RS485_UART_Handle);
    __HAL_UART_CLEAR_FEFLAG(&RS485_UART_Handle);
    
    /* Flush RX buffer by reading DR if RXNE is set */
    if(__HAL_UART_GET_FLAG(&RS485_UART_Handle, UART_FLAG_RXNE))
    {
        (void)RS485_UART_Handle.Instance->DR;
    }
    
    return HAL_UART_Receive(&RS485_UART_Handle, buf, len, timeout);
}
