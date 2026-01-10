#include "485.h"
#include <stdio.h>

UART_HandleTypeDef huart_485;

void RS485_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    /* 使能时钟 */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    
    /* PA5 方向控制 */
    GPIO_InitStruct.Pin = RS485_EN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_PULLDOWN;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RS485_EN_PORT, &GPIO_InitStruct);
    RS485_RX_MODE();  /* 默认接收模式 */
    
    /* PB10 TX */
    GPIO_InitStruct.Pin = RS485_TX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RS485_TX_PORT, &GPIO_InitStruct);
    
    /* PB11 RX */
    GPIO_InitStruct.Pin = RS485_RX_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(RS485_RX_PORT, &GPIO_InitStruct);
    
    /* USART3 配置 */
    huart_485.Instance = USART3;
    huart_485.Init.BaudRate = 38400;
    huart_485.Init.WordLength = UART_WORDLENGTH_8B;
    huart_485.Init.StopBits = UART_STOPBITS_1;
    huart_485.Init.Parity = UART_PARITY_NONE;
    huart_485.Init.Mode = UART_MODE_TX_RX;
    huart_485.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart_485.Init.OverSampling = UART_OVERSAMPLING_16;
    
    if (HAL_UART_Init(&huart_485) != HAL_OK)
    {
        printf("[485] Init FAILED!\r\n");
    }
    else
    {
        printf("[485] Init OK (USART3+PA5, 38400)\r\n");
    }
}

void RS485_SendData(uint8_t *buf, uint16_t len)
{
    RS485_TX_MODE();
    
    /* 延时让电平稳定 */
    for(volatile int i = 0; i < 500; i++);
    
    /* 发送数据 */
    HAL_UART_Transmit(&huart_485, buf, len, 100);
    
    /* 等待发送完成 */
    while(__HAL_UART_GET_FLAG(&huart_485, UART_FLAG_TC) == RESET);
    
    /* 延时确保数据完全发出 */
    for(volatile int i = 0; i < 500; i++);
    
    RS485_RX_MODE();
    
#if 0  /* 调试输出 */
    printf("[485_TX] ");
    for(uint16_t i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\r\n");
#endif
}

HAL_StatusTypeDef RS485_ReceiveData(uint8_t *buf, uint16_t len, uint32_t timeout)
{
    HAL_StatusTypeDef status;
    
    /* 清除错误标志 */
    __HAL_UART_CLEAR_OREFLAG(&huart_485);
    __HAL_UART_CLEAR_NEFLAG(&huart_485);
    __HAL_UART_CLEAR_FEFLAG(&huart_485);
    
    /* 清空RX缓冲 */
    if(__HAL_UART_GET_FLAG(&huart_485, UART_FLAG_RXNE))
    {
        (void)huart_485.Instance->DR;
    }
    
    status = HAL_UART_Receive(&huart_485, buf, len, timeout);
    
#if 0  /* 调试输出 */
    if(status == HAL_OK)
    {
        printf("[485_RX] ");
        for(uint16_t i = 0; i < len; i++) printf("%02X ", buf[i]);
        printf("\r\n");
    }
    else if(status == HAL_TIMEOUT)
    {
        printf("[485_RX] TIMEOUT (%lums)\r\n", timeout);
    }
    else
    {
        printf("[485_RX] ERROR (%d)\r\n", status);
    }
#endif
    
    return status;
}
