#include "Modbus_Slave.h"
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
static ModbusSlaveConfig g_config;

/* USART3 接收缓冲：中断逐字节写入，主循环按帧静默时间取整帧。 */
static uint8_t g_rx_buf[MODBUS_SLAVE_RX_BUFFER_SIZE];
static volatile uint16_t g_rx_len = 0;
static volatile uint32_t g_last_rx_tick = 0;
static volatile uint8_t g_frame_ready = 0;
/* 中断里发现溢出/异常时把当前帧标记为污染，主循环静默超时后整帧丢弃，
 * 防止把溢出后接上的字节误解释成新帧帧头并 CRC 巧合通过。 */
static volatile uint8_t g_rx_corrupted = 0;

/* 发送缓冲由协议层临时组织响应帧，末尾再统一补 CRC。 */
static uint8_t g_tx_buf[MODBUS_SLAVE_TX_BUFFER_SIZE];
static volatile uint32_t g_crc_error_count = 0;
static volatile uint32_t g_uart_error_count = 0;
static volatile uint32_t g_uart_ore_count = 0;
static volatile uint32_t g_uart_fe_count = 0;
static volatile uint32_t g_uart_ne_count = 0;
static volatile uint32_t g_rx_overflow_count = 0;

/**
 * @brief 计算 Modbus RTU CRC16。
 * @param buf: 参与 CRC 计算的数据缓冲区
 * @param len: 参与 CRC 计算的字节数
 * @retval 计算得到的 CRC16
 */
/* 通用 Modbus RTU CRC16，低字节在前。 */
static uint16_t Modbus_Slave_CRC16(const uint8_t *buf, uint16_t len)
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

/**
 * @brief 清空 USART3 接收缓冲和错误标志。
 * @param 无
 * @retval 无
 */
static void Modbus_Slave_ClearPendingRx(void)
{
    /* 发送完回包后清空 UART 残留，避免尾字节被误判为下一帧开头。 */
    while (__HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_RXNE) != RESET)
    {
        (void)(g_uart3_handle.Instance->DR & 0xFF);
    }

    if (__HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_ORE) != RESET)
    {
        __HAL_UART_CLEAR_OREFLAG(&g_uart3_handle);
    }
    if (__HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_FE) != RESET)
    {
        __HAL_UART_CLEAR_FEFLAG(&g_uart3_handle);
    }
    if (__HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_NE) != RESET)
    {
        __HAL_UART_CLEAR_NEFLAG(&g_uart3_handle);
    }
}

/**
 * @brief 发送一帧完整 Modbus RTU 响应，并负责 485 方向切换。
 * @param data: 待发送的数据区，函数内部会在末尾追加 CRC
 * @param len: 未包含 CRC 的数据长度
 * @retval 无
 */
static void Modbus_Slave_SendFrame(uint8_t *data, uint16_t len)
{
    uint16_t crc;

    if (len + 2U > MODBUS_SLAVE_TX_BUFFER_SIZE)
    {
        return;
    }

    crc = Modbus_Slave_CRC16(data, len);
    data[len] = (uint8_t)(crc & 0xFF);
    data[len + 1U] = (uint8_t)((crc >> 8) & 0xFF);
    len += 2U;

    /* 半双工 485：发送前拉高 DE/RE，发完并确认 TC 后再切回接收。 */
    MODBUS_SLAVE_TX_ENABLE();
    for (volatile int i = 0; i < 500; i++)
    {
    }

    (void)HAL_UART_Transmit(&g_uart3_handle, data, len, 100);
    while (__HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_TC) == RESET)
    {
    }

    for (volatile int i = 0; i < 200; i++)
    {
    }

    MODBUS_SLAVE_RX_ENABLE();
    Modbus_Slave_ClearPendingRx();
}

/**
 * @brief 发送标准 Modbus 异常响应。
 * @param fc: 原始功能码
 * @param exception_code: 异常码
 * @retval 无
 */
static void Modbus_Slave_SendException(uint8_t fc, uint8_t exception_code)
{
    /* 异常响应格式：功能码最高位置 1，数据区只带异常码。 */
    g_tx_buf[0] = g_config.slave_addr;
    g_tx_buf[1] = (uint8_t)(fc | 0x80U);
    g_tx_buf[2] = exception_code;
    Modbus_Slave_SendFrame(g_tx_buf, 3);
}

/**
 * @brief 处理读寄存器请求，并通过业务层回调获取寄存器内容。
 * @param frame: 完整请求帧缓冲区
 * @param len: 请求帧长度
 * @param fc: 当前功能码，支持 03/04
 * @retval 无
 */
static void Modbus_Slave_HandleRead(const uint8_t *frame, uint16_t len, uint8_t fc)
{
    ModbusSlaveReadRegsFn reader = NULL;
    uint16_t start_addr;
    uint16_t reg_count;
    uint16_t regs[125];
    uint16_t tx_idx = 3;
    uint8_t exception;

    if (len < 8)
    {
        return;
    }

    start_addr = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    reg_count = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);
    if (reg_count == 0U || reg_count > 125U)
    {
        Modbus_Slave_SendException(fc, 0x03);
        return;
    }

    /* 读保持寄存器和读输入寄存器共用同一套框架，业务层决定寄存器内容。 */
    reader = (fc == MODBUS_FC_READ_HOLD_REGS) ? g_config.read_holding_registers : g_config.read_input_registers;
    if (reader == NULL)
    {
        Modbus_Slave_SendException(fc, 0x01);
        return;
    }

    exception = reader(start_addr, reg_count, regs, g_config.context);
    if (exception != 0U)
    {
        Modbus_Slave_SendException(fc, exception);
        return;
    }

    g_tx_buf[0] = g_config.slave_addr;
    g_tx_buf[1] = fc;
    g_tx_buf[2] = (uint8_t)(reg_count * 2U);

    for (uint16_t i = 0; i < reg_count; i++)
    {
        g_tx_buf[tx_idx++] = (uint8_t)(regs[i] >> 8);
        g_tx_buf[tx_idx++] = (uint8_t)(regs[i] & 0xFF);
    }

    Modbus_Slave_SendFrame(g_tx_buf, tx_idx);
}

/**
 * @brief 处理写单个寄存器请求，并转发到业务层回调。
 * @param frame: 完整请求帧缓冲区
 * @param len: 请求帧长度
 * @retval 无
 */
static void Modbus_Slave_HandleWriteSingleReg(const uint8_t *frame, uint16_t len)
{
    uint16_t reg_addr;
    uint16_t reg_val;
    uint8_t exception;

    if (len < 8 || g_config.write_single_register == NULL)
    {
        Modbus_Slave_SendException(MODBUS_FC_WRITE_SINGLE_REG, 0x01);
        return;
    }

    reg_addr = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    reg_val = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);
    /* 具体寄存器是否合法由业务层判断，本层只负责协议编解码。 */
    exception = g_config.write_single_register(reg_addr, reg_val, g_config.context);
    if (exception != 0U)
    {
        Modbus_Slave_SendException(MODBUS_FC_WRITE_SINGLE_REG, exception);
        return;
    }

    memcpy(g_tx_buf, frame, 6);
    Modbus_Slave_SendFrame(g_tx_buf, 6);
}

/**
 * @brief 处理写单个线圈请求，并转发到业务层回调。
 * @param frame: 完整请求帧缓冲区
 * @param len: 请求帧长度
 * @retval 无
 */
static void Modbus_Slave_HandleWriteSingleCoil(const uint8_t *frame, uint16_t len)
{
    uint16_t coil_addr;
    uint16_t coil_val;
    uint8_t exception;

    if (len < 8 || g_config.write_single_coil == NULL)
    {
        Modbus_Slave_SendException(MODBUS_FC_WRITE_SINGLE_COIL, 0x01);
        return;
    }

    coil_addr = (uint16_t)(((uint16_t)frame[2] << 8) | frame[3]);
    coil_val = (uint16_t)(((uint16_t)frame[4] << 8) | frame[5]);

    /* 线圈写入按标准 Modbus 约定，只接受 0xFF00 / 0x0000。 */
    if (coil_val == 0xFF00U)
    {
        exception = g_config.write_single_coil(coil_addr, true, g_config.context);
    }
    else if (coil_val == 0x0000U)
    {
        exception = g_config.write_single_coil(coil_addr, false, g_config.context);
    }
    else
    {
        Modbus_Slave_SendException(MODBUS_FC_WRITE_SINGLE_COIL, 0x03);
        return;
    }

    if (exception != 0U)
    {
        Modbus_Slave_SendException(MODBUS_FC_WRITE_SINGLE_COIL, exception);
        return;
    }

    memcpy(g_tx_buf, frame, 6);
    Modbus_Slave_SendFrame(g_tx_buf, 6);
}

/**
 * @brief 校验并分发当前缓存中的一帧 Modbus RTU 请求。
 * @param 无
 * @retval 无
 */
static void Modbus_Slave_ProcessFrame(void)
{
    uint16_t crc_calc;
    uint16_t crc_recv;
    uint8_t fc;

    if (g_rx_len < 4U)
    {
        return;
    }

    /* 先做地址和 CRC 过滤，只有合法完整帧才进入功能码分发。 */
    if (g_rx_buf[0] != g_config.slave_addr)
    {
        return;
    }

    crc_calc = Modbus_Slave_CRC16(g_rx_buf, (uint16_t)(g_rx_len - 2U));
    crc_recv = (uint16_t)(((uint16_t)g_rx_buf[g_rx_len - 1U] << 8) | g_rx_buf[g_rx_len - 2U]);
    if (crc_calc != crc_recv)
    {
        g_crc_error_count++;
        return;
    }

    fc = g_rx_buf[1];
    switch (fc)
    {
        case MODBUS_FC_READ_HOLD_REGS:
        case MODBUS_FC_READ_INPUT_REGS:
            Modbus_Slave_HandleRead(g_rx_buf, g_rx_len, fc);
            break;

        case MODBUS_FC_WRITE_SINGLE_REG:
            Modbus_Slave_HandleWriteSingleReg(g_rx_buf, g_rx_len);
            break;

        case MODBUS_FC_WRITE_SINGLE_COIL:
            Modbus_Slave_HandleWriteSingleCoil(g_rx_buf, g_rx_len);
            break;

        default:
            Modbus_Slave_SendException(fc, 0x01);
            break;
    }
}

/**
 * @brief 初始化通用 Modbus 从站引擎，并绑定业务层回调。
 * @param config: 从站配置，包括地址、读写回调和业务上下文
 * @retval 无
 */
void Modbus_Slave_Init(const ModbusSlaveConfig *config)
{
    GPIO_InitTypeDef gpio = {0};

    if (config == NULL)
    {
        return;
    }

    /* 这层固定负责 USART3 + PA5 的 485 从站通道，业务层只注册回调。 */
    g_config = *config;

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    gpio.Pin = MODBUS_SLAVE_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MODBUS_SLAVE_EN_PORT, &gpio);
    MODBUS_SLAVE_RX_ENABLE();

    gpio.Pin = MODBUS_SLAVE_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(MODBUS_SLAVE_TX_PORT, &gpio);

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

    /* 打开 RXNE 中断，收到的每个字节先进入软件缓冲，再由主循环判帧。 */
    __HAL_UART_ENABLE_IT(&g_uart3_handle, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART3_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART3_IRQn);

    g_rx_len = 0;
    g_last_rx_tick = 0;
    g_frame_ready = 0;
    g_rx_corrupted = 0u;
    memset(g_rx_buf, 0, sizeof(g_rx_buf));
    memset(g_tx_buf, 0, sizeof(g_tx_buf));
    g_crc_error_count = 0;
    g_uart_error_count = 0;
    g_uart_ore_count = 0;
    g_uart_fe_count = 0;
    g_uart_ne_count = 0;
    g_rx_overflow_count = 0;
    Modbus_Slave_ClearPendingRx();
}

/**
 * @brief 在主循环中推进从站状态机，依据静默时间判定完整帧。
 * @param 无
 * @retval 无
 */
void Modbus_Slave_Process(void)
{
    /* RTU 没有长度字段，靠帧间静默时间判断一帧结束。 */
    if (g_rx_len > 0U && !g_frame_ready)
    {
        if ((HAL_GetTick() - g_last_rx_tick) >= MODBUS_SLAVE_FRAME_TIMEOUT_MS)
        {
            g_frame_ready = 1;
        }
    }

    if (g_frame_ready)
    {
        if (g_rx_corrupted == 0u)
        {
            Modbus_Slave_ProcessFrame();
        }
        g_rx_len = 0;
        g_frame_ready = 0;
        g_rx_corrupted = 0u;
    }
}

/**
 * @brief USART3 接收中断回调，把收到的单个字节压入帧缓存。
 * @param byte: 刚收到的 1 个字节
 * @retval 无
 */
void Modbus_Slave_RxCallback(uint8_t byte)
{
    /* 中断里只做轻量缓存，不做协议解析，避免占用过长中断时间。 */
    if (g_rx_len < MODBUS_SLAVE_RX_BUFFER_SIZE)
    {
        g_rx_buf[g_rx_len++] = byte;
    }
    else
    {
        /* 缓冲区溢出：保留 g_rx_len 让超时仍能判帧结束，
         * 但置脏标志，主循环静默后整帧丢弃，避免把后续字节误当下一帧帧头。 */
        g_rx_overflow_count++;
        g_rx_corrupted = 1u;
    }

    g_last_rx_tick = HAL_GetTick();
}

/**
 * @brief 获取 Modbus 从站底层 UART 句柄，供中断服务函数使用。
 * @param 无
 * @retval UART_HandleTypeDef*: USART3 句柄指针
 */
UART_HandleTypeDef *Modbus_Slave_GetHandle(void)
{
    return &g_uart3_handle;
}

void Modbus_Slave_SendRawBytes(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0u)
    {
        return;
    }

    /* 半双工 485：和 Modbus 应答完全一致的方向切换序列，
       否则透传 JSON 不会真正出现在总线上，G780S 永远收不到。 */
    MODBUS_SLAVE_TX_ENABLE();
    for (volatile int i = 0; i < 500; i++)
    {
    }

    (void)HAL_UART_Transmit(&g_uart3_handle, (uint8_t *)data, len, 300);
    while (__HAL_UART_GET_FLAG(&g_uart3_handle, UART_FLAG_TC) == RESET)
    {
    }

    for (volatile int i = 0; i < 200; i++)
    {
    }

    MODBUS_SLAVE_RX_ENABLE();
    Modbus_Slave_ClearPendingRx();
}

/**
 * @brief 通知从站引擎记录一次 UART 异常。
 * @param 无
 * @retval 无
 */
void Modbus_Slave_NotifyUartOverrun(void)
{
    g_uart_ore_count++;
    g_uart_error_count++;
    g_rx_corrupted = 1u;
}

void Modbus_Slave_NotifyUartFrameError(void)
{
    g_uart_fe_count++;
    g_uart_error_count++;
    g_rx_corrupted = 1u;
}

void Modbus_Slave_NotifyUartNoiseError(void)
{
    g_uart_ne_count++;
    g_uart_error_count++;
    g_rx_corrupted = 1u;
}

/**
 * @brief 获取 Modbus CRC 错误累计次数。
 * @param 无
 * @retval uint32_t: CRC 错误次数
 */
uint32_t Modbus_Slave_GetCrcErrorCount(void)
{
    return g_crc_error_count;
}

/**
 * @brief 获取 UART 异常累计次数。
 * @param 无
 * @retval uint32_t: UART 异常次数
 */
uint32_t Modbus_Slave_GetUartErrorCount(void)
{
    return g_uart_error_count;
}

uint32_t Modbus_Slave_GetUartOverrunCount(void)
{
    return g_uart_ore_count;
}

uint32_t Modbus_Slave_GetUartFrameErrorCount(void)
{
    return g_uart_fe_count;
}

uint32_t Modbus_Slave_GetUartNoiseErrorCount(void)
{
    return g_uart_ne_count;
}

uint32_t Modbus_Slave_GetRxOverflowCount(void)
{
    return g_rx_overflow_count;
}
