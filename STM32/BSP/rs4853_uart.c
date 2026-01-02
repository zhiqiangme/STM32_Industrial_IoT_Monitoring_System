/**
 * @file    rs4853_uart.c
 * @brief   RS485-B上云链路驱动 (简化版,无DMA)
 * @note    USART3 + MAX485 (PF15)
 */

#include "rs4853_uart.h"
#include <string.h>
#include <stdio.h>

#define RS4853_EN_PORT      GPIOF
#define RS4853_EN_PIN       GPIO_PIN_15
#define RS4853_TX_ENABLE()  HAL_GPIO_WritePin(RS4853_EN_PORT, RS4853_EN_PIN, GPIO_PIN_SET)
#define RS4853_RX_ENABLE()  HAL_GPIO_WritePin(RS4853_EN_PORT, RS4853_EN_PIN, GPIO_PIN_RESET)

static UART_HandleTypeDef g_uart3_handle;
static volatile RS4853_State_t g_state = RS4853_STATE_IDLE;
static volatile uint32_t g_silence_start_tick = 0;

/* 发送缓冲 */
static uint8_t g_tx_buffer[RS4853_TX_BUF_SIZE];
static volatile uint16_t g_tx_len = 0;
static volatile uint8_t g_tx_pending = 0;  /* 有待发送数据 */

/* 接收缓冲 */
static uint8_t g_rx_buffer[RS4853_RX_BUF_SIZE];
static volatile uint16_t g_rx_head = 0;
static volatile uint16_t g_rx_tail = 0;

void RS4853_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();
    
    /* PF15 方向控制 */
    GPIO_InitStruct.Pin = RS4853_EN_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(RS4853_EN_PORT, &GPIO_InitStruct);
    RS4853_RX_ENABLE();
    
    /* PB10 TX */
    GPIO_InitStruct.Pin = GPIO_PIN_10;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    /* PB11 RX */
    GPIO_InitStruct.Pin = GPIO_PIN_11;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    /* USART3 - 简单配置,不使用DMA */
    g_uart3_handle.Instance = USART3;
    g_uart3_handle.Init.BaudRate = RS4853_BAUDRATE;
    g_uart3_handle.Init.WordLength = UART_WORDLENGTH_8B;
    g_uart3_handle.Init.StopBits = UART_STOPBITS_1;
    g_uart3_handle.Init.Parity = UART_PARITY_NONE;
    g_uart3_handle.Init.Mode = UART_MODE_TX_RX;
    g_uart3_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_uart3_handle.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_uart3_handle);
    
    g_state = RS4853_STATE_IDLE;
    g_tx_pending = 0;
    g_rx_head = 0;
    g_rx_tail = 0;
    
    printf("[RS4853] Init OK\r\n");
}

bool RS4853_SendAsync(const uint8_t* data, uint16_t len)
{
    if (g_state != RS4853_STATE_IDLE)
        return false;
    
    if (len == 0 || len > RS4853_TX_BUF_SIZE)
        return false;
    
    /* 复制到缓冲区,标记待发送 */
    memcpy(g_tx_buffer, data, len);
    g_tx_len = len;
    g_tx_pending = 1;
    
    return true;
}

void RS4853_Task(void)
{
    switch (g_state)
    {
        case RS4853_STATE_IDLE:
            /* 检查是否有待发送数据 */
            if (g_tx_pending)
            {
                g_tx_pending = 0;
                g_state = RS4853_STATE_SENDING;
                
                /* 切换到发送模式 */
                RS4853_TX_ENABLE();
                HAL_Delay(1);  /* 稳定延时 */
                
                /* 阻塞发送 */
                HAL_UART_Transmit(&g_uart3_handle, g_tx_buffer, g_tx_len, 2000);
                
                /* 等待TC */
                while(__HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_TC) == RESET);
                
                /* 切换回接收 */
                RS4853_RX_ENABLE();
                
                /* 进入静默 */
                g_silence_start_tick = HAL_GetTick();
                g_state = RS4853_STATE_POST_SILENCE;
            }
            break;
            
        case RS4853_STATE_SENDING:
            /* 不应到达这里(阻塞发送) */
            g_state = RS4853_STATE_IDLE;
            break;
            
        case RS4853_STATE_POST_SILENCE:
            if ((HAL_GetTick() - g_silence_start_tick) >= RS4853_SILENCE_MS)
            {
                g_state = RS4853_STATE_IDLE;
            }
            break;
    }
}

uint16_t RS4853_ReadRx(uint8_t* out, uint16_t maxlen)
{
    /* 暂不实现接收 */
    return 0;
}

RS4853_State_t RS4853_GetState(void)
{
    return g_state;
}

bool RS4853_IsIdle(void)
{
    return (g_state == RS4853_STATE_IDLE) && (g_tx_pending == 0);
}

void RS4853_TxCpltCallback(void) { }
void RS4853_RxCallback(uint8_t byte) { }
void RS4853_DMA_IRQHandler(void) { }
void RS4853_UART_IRQHandler(void) { }
