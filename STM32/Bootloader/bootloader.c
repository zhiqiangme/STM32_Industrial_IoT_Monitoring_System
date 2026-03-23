#include "bootloader.h"

#include "delay.h"
#include "LED.h"
#include "Upgrade.h"
#include "usart.h"

#include "stm32f1xx_it.h"
#include <string.h>
#include <stdio.h>

#define BOOT_UART                     USART3
#define BOOT_UART_IRQn                USART3_IRQn
#define BOOT_485_EN_PORT              GPIOA
#define BOOT_485_EN_PIN               GPIO_PIN_5
#define BOOT_UART_TX_PORT             GPIOB
#define BOOT_UART_TX_PIN              GPIO_PIN_10
#define BOOT_UART_RX_PORT             GPIOB
#define BOOT_UART_RX_PIN              GPIO_PIN_11
#define BOOT_FRAME_SOF0               0x55u
#define BOOT_FRAME_SOF1               0xAAu
#define BOOT_MAX_PAYLOAD              256u
#define BOOT_RX_TIMEOUT_MS            20u
#define BOOT_INFO_VERSION             0x0001u
#define BOOT_PROTOCOL_VERSION         0x0001u

#define BOOT_CMD_GET_INFO             0x01u
#define BOOT_CMD_START                0x02u
#define BOOT_CMD_DATA                 0x03u
#define BOOT_CMD_END                  0x04u
#define BOOT_CMD_ABORT                0x05u
#define BOOT_CMD_QUERY_STATUS         0x06u

#define BOOT_RSP_ACK                  0x80u
#define BOOT_RSP_NACK                 0x81u
#define BOOT_RSP_INFO                 0x82u
#define BOOT_RSP_STATUS               0x83u

#define BOOT_ERR_NONE                 0x0000u
#define BOOT_ERR_BAD_FRAME            0x0001u
#define BOOT_ERR_BAD_CMD              0x0002u
#define BOOT_ERR_BAD_LENGTH           0x0003u
#define BOOT_ERR_BAD_STATE            0x0004u
#define BOOT_ERR_BAD_SIZE             0x0005u
#define BOOT_ERR_FLASH_ERASE          0x0006u
#define BOOT_ERR_FLASH_PROGRAM        0x0007u
#define BOOT_ERR_BAD_OFFSET           0x0008u
#define BOOT_ERR_VERIFY               0x0009u
#define BOOT_ERR_NOT_COMPLETE         0x000Au
#define BOOT_ERR_BAD_CRC              0x000Bu
#define BOOT_ERR_TIMEOUT_RECOVERY     0x000Cu

typedef struct
{
    uint8_t cmd;
    uint8_t seq;
    uint16_t len;
    uint8_t payload[BOOT_MAX_PAYLOAD];
} BootFrame;

static UART_HandleTypeDef g_boot_uart;
static UpgradeStateImage g_boot_state;
static uint16_t g_boot_last_error = BOOT_ERR_NONE;

/* 这些状态说明上次升级尚未结束，Bootloader 默认应继续驻留等待恢复。 */
static uint8_t Bootloader_IsUpgradeActiveState(uint16_t state)
{
    return (state == UPGRADE_STATE_REQUESTED ||
            state == UPGRADE_STATE_ERASING ||
            state == UPGRADE_STATE_PROGRAMMING ||
            state == UPGRADE_STATE_VERIFYING) ? 1u : 0u;
}

/* 485 收发切换：拉低 PA5 进入接收态。 */
static void Bootloader_RxEnable(void)
{
    HAL_GPIO_WritePin(BOOT_485_EN_PORT, BOOT_485_EN_PIN, GPIO_PIN_RESET);
}

/* 485 收发切换：拉高 PA5 进入发送态。 */
static void Bootloader_TxEnable(void)
{
    HAL_GPIO_WritePin(BOOT_485_EN_PORT, BOOT_485_EN_PIN, GPIO_PIN_SET);
}

/* 通过串口1输出启动横幅，便于现场判断当前固件与状态页地址。 */
static void Bootloader_PrintBanner(void)
{
    printf("\r\n[BOOT] STM32 Mill Bootloader\r\n");
    printf("[BOOT] APP base: 0x%08lX\r\n", (unsigned long)UPGRADE_APP_BASE_ADDR);
    printf("[BOOT] State page: 0x%08lX\r\n", (unsigned long)UPGRADE_STATE_PAGE_ADDR);
}

/* Bootloader 自己占用 USART3 + PA5 这条 485 通道，和 App 运行态复用同一物理口。 */
static void Bootloader_UartInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    gpio.Pin = BOOT_485_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOOT_485_EN_PORT, &gpio);
    Bootloader_RxEnable();

    gpio.Pin = BOOT_UART_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOOT_UART_TX_PORT, &gpio);

    gpio.Pin = BOOT_UART_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BOOT_UART_RX_PORT, &gpio);

    g_boot_uart.Instance = BOOT_UART;
    g_boot_uart.Init.BaudRate = 115200;
    g_boot_uart.Init.WordLength = UART_WORDLENGTH_8B;
    g_boot_uart.Init.StopBits = UART_STOPBITS_1;
    g_boot_uart.Init.Parity = UART_PARITY_NONE;
    g_boot_uart.Init.Mode = UART_MODE_TX_RX;
    g_boot_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    g_boot_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&g_boot_uart);
}

/* 最底层发包函数，只负责 485 方向切换与串口发送。 */
static void Bootloader_SendRaw(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0u)
    {
        return;
    }

    Bootloader_TxEnable();
    for (volatile uint32_t i = 0; i < 500u; i++)
    {
    }

    (void)HAL_UART_Transmit(&g_boot_uart, (uint8_t *)data, len, 200);
    while (__HAL_UART_GET_FLAG(&g_boot_uart, UART_FLAG_TC) == RESET)
    {
    }

    for (volatile uint32_t i = 0; i < 200u; i++)
    {
    }
    Bootloader_RxEnable();
}

/* 统一封装 Bootloader 协议帧：SOF + CMD/SEQ/LEN + PAYLOAD + CRC16。 */
static void Bootloader_SendFrame(uint8_t cmd, uint8_t seq, const uint8_t *payload, uint16_t len)
{
    uint8_t frame[2u + 1u + 1u + 2u + BOOT_MAX_PAYLOAD + 2u];
    uint16_t crc;
    uint16_t idx = 0u;

    if (len > BOOT_MAX_PAYLOAD)
    {
        return;
    }

    frame[idx++] = BOOT_FRAME_SOF0;
    frame[idx++] = BOOT_FRAME_SOF1;
    frame[idx++] = cmd;
    frame[idx++] = seq;
    frame[idx++] = (uint8_t)(len & 0xFFu);
    frame[idx++] = (uint8_t)((len >> 8) & 0xFFu);

    if (payload != NULL && len > 0u)
    {
        memcpy(&frame[idx], payload, len);
        idx = (uint16_t)(idx + len);
    }

    crc = Upgrade_CRC16(&frame[2], (uint16_t)(idx - 2u));
    frame[idx++] = (uint8_t)(crc & 0xFFu);
    frame[idx++] = (uint8_t)((crc >> 8) & 0xFFu);
    Bootloader_SendRaw(frame, idx);
}

/* ACK 与 NACK 都带 2 字节错误码，便于上位机直接判断结果。 */
static void Bootloader_SendAck(uint8_t seq)
{
    uint8_t payload[2];

    payload[0] = (uint8_t)(g_boot_last_error & 0xFFu);
    payload[1] = (uint8_t)((g_boot_last_error >> 8) & 0xFFu);
    Bootloader_SendFrame(BOOT_RSP_ACK, seq, payload, sizeof(payload));
}

static void Bootloader_SendNack(uint8_t seq, uint16_t error)
{
    uint8_t payload[2];

    g_boot_last_error = error;
    payload[0] = (uint8_t)(error & 0xFFu);
    payload[1] = (uint8_t)((error >> 8) & 0xFFu);
    Bootloader_SendFrame(BOOT_RSP_NACK, seq, payload, sizeof(payload));
}

/* GET_INFO 回包用于上位机确认分区、页大小、状态和最近错误。 */
static void Bootloader_SendInfo(uint8_t seq)
{
    uint8_t payload[24];
    uint16_t idx = 0u;
    uint32_t value32;
    uint16_t value16;

    value16 = BOOT_INFO_VERSION;
    memcpy(&payload[idx], &value16, sizeof(value16));
    idx += (uint16_t)sizeof(value16);

    value16 = BOOT_PROTOCOL_VERSION;
    memcpy(&payload[idx], &value16, sizeof(value16));
    idx += (uint16_t)sizeof(value16);

    value32 = UPGRADE_APP_BASE_ADDR;
    memcpy(&payload[idx], &value32, sizeof(value32));
    idx += (uint16_t)sizeof(value32);

    value32 = UPGRADE_APP_MAX_SIZE;
    memcpy(&payload[idx], &value32, sizeof(value32));
    idx += (uint16_t)sizeof(value32);

    value16 = 2048u;
    memcpy(&payload[idx], &value16, sizeof(value16));
    idx += (uint16_t)sizeof(value16);

    value16 = g_boot_state.state;
    memcpy(&payload[idx], &value16, sizeof(value16));
    idx += (uint16_t)sizeof(value16);

    value32 = g_boot_state.written_bytes;
    memcpy(&payload[idx], &value32, sizeof(value32));
    idx += (uint16_t)sizeof(value32);

    value16 = g_boot_last_error;
    memcpy(&payload[idx], &value16, sizeof(value16));
    idx += (uint16_t)sizeof(value16);

    value16 = 0u;
    memcpy(&payload[idx], &value16, sizeof(value16));
    idx += (uint16_t)sizeof(value16);

    Bootloader_SendFrame(BOOT_RSP_INFO, seq, payload, idx);
}

/* QUERY_STATUS 主要用于续传和现场排障：镜像大小、已写字节、最近成功偏移等。 */
static void Bootloader_SendStatus(uint8_t seq)
{
    uint8_t payload[20];
    uint16_t idx = 0u;

    payload[idx++] = (uint8_t)(g_boot_state.state & 0xFFu);
    payload[idx++] = (uint8_t)((g_boot_state.state >> 8) & 0xFFu);
    payload[idx++] = (uint8_t)(g_boot_last_error & 0xFFu);
    payload[idx++] = (uint8_t)((g_boot_last_error >> 8) & 0xFFu);

    memcpy(&payload[idx], &g_boot_state.image_size, sizeof(g_boot_state.image_size));
    idx += (uint16_t)sizeof(g_boot_state.image_size);
    memcpy(&payload[idx], &g_boot_state.written_bytes, sizeof(g_boot_state.written_bytes));
    idx += (uint16_t)sizeof(g_boot_state.written_bytes);
    memcpy(&payload[idx], &g_boot_state.last_ok_offset, sizeof(g_boot_state.last_ok_offset));
    idx += (uint16_t)sizeof(g_boot_state.last_ok_offset);
    memcpy(&payload[idx], &g_boot_state.image_crc32, sizeof(g_boot_state.image_crc32));
    idx += (uint16_t)sizeof(g_boot_state.image_crc32);

    Bootloader_SendFrame(BOOT_RSP_STATUS, seq, payload, idx);
}

/* Bootloader 协议采用阻塞接收；SOF 逐字节找头，其余字段整段收取。 */
static uint8_t Bootloader_ReceiveByte(uint8_t *byte, uint32_t timeout_ms)
{
    if (HAL_UART_Receive(&g_boot_uart, byte, 1u, timeout_ms) == HAL_OK)
    {
        return 1u;
    }

    return 0u;
}

static uint8_t Bootloader_ReadFrame(BootFrame *frame)
{
    uint8_t byte = 0u;
    uint8_t header[4];
    uint16_t rx_crc;
    uint16_t calc_crc;

    if (frame == NULL)
    {
        return 0u;
    }

    /* 先同步帧头 0x55 0xAA，再收剩余头字段和负载。 */
    while (1)
    {
        if (Bootloader_ReceiveByte(&byte, BOOT_RX_TIMEOUT_MS) == 0u)
        {
            return 0u;
        }
        if (byte == BOOT_FRAME_SOF0)
        {
            break;
        }
    }

    if (Bootloader_ReceiveByte(&byte, BOOT_RX_TIMEOUT_MS) == 0u || byte != BOOT_FRAME_SOF1)
    {
        g_boot_last_error = BOOT_ERR_BAD_FRAME;
        return 0u;
    }

    if (HAL_UART_Receive(&g_boot_uart, header, sizeof(header), 100u) != HAL_OK)
    {
        g_boot_last_error = BOOT_ERR_BAD_FRAME;
        return 0u;
    }

    frame->cmd = header[0];
    frame->seq = header[1];
    frame->len = (uint16_t)((uint16_t)header[2] | ((uint16_t)header[3] << 8));

    if (frame->len > BOOT_MAX_PAYLOAD)
    {
        g_boot_last_error = BOOT_ERR_BAD_LENGTH;
        return 0u;
    }

    if (frame->len > 0u && HAL_UART_Receive(&g_boot_uart, frame->payload, frame->len, 100u) != HAL_OK)
    {
        g_boot_last_error = BOOT_ERR_BAD_FRAME;
        return 0u;
    }

    if (HAL_UART_Receive(&g_boot_uart, (uint8_t *)&rx_crc, sizeof(rx_crc), 100u) != HAL_OK)
    {
        g_boot_last_error = BOOT_ERR_BAD_FRAME;
        return 0u;
    }

    {
        uint8_t crc_buf[1u + 1u + 2u + BOOT_MAX_PAYLOAD];
        uint16_t crc_len = 0u;
        crc_buf[crc_len++] = frame->cmd;
        crc_buf[crc_len++] = frame->seq;
        crc_buf[crc_len++] = (uint8_t)(frame->len & 0xFFu);
        crc_buf[crc_len++] = (uint8_t)((frame->len >> 8) & 0xFFu);
        if (frame->len > 0u)
        {
            memcpy(&crc_buf[crc_len], frame->payload, frame->len);
            crc_len = (uint16_t)(crc_len + frame->len);
        }
        calc_crc = Upgrade_CRC16(crc_buf, crc_len);
    }

    /* 只对 CMD/SEQ/LEN/PAYLOAD 做 CRC，不包含 SOF。 */
    if (calc_crc != rx_crc)
    {
        g_boot_last_error = BOOT_ERR_BAD_CRC;
        return 0u;
    }

    return 1u;
}

/* 避免每包都刷状态页，默认每写满 2KB 页边界或最后一包时再持久化一次。 */
static uint8_t Bootloader_SaveStateThrottled(uint8_t force_save)
{
    if (force_save != 0u || (g_boot_state.written_bytes % 2048u) == 0u)
    {
        return Upgrade_SaveState(&g_boot_state);
    }

    return 0u;
}

/* START 表示上位机正式发起一次升级：记录镜像元数据并擦除 App 区。 */
static void Bootloader_HandleStart(const BootFrame *frame)
{
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t target_fw_version;
    uint8_t err;

    if (frame->len != 12u)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_LENGTH);
        return;
    }

    memcpy(&image_size, &frame->payload[0], sizeof(image_size));
    memcpy(&image_crc32, &frame->payload[4], sizeof(image_crc32));
    memcpy(&target_fw_version, &frame->payload[8], sizeof(target_fw_version));

    if (image_size == 0u || image_size > UPGRADE_APP_MAX_SIZE)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_SIZE);
        return;
    }

    Upgrade_InitStateImage(&g_boot_state);
    g_boot_state.state = UPGRADE_STATE_ERASING;
    g_boot_state.request_source = UPGRADE_REQUEST_SOURCE_LOCAL;
    g_boot_state.target_fw_version = target_fw_version;
    g_boot_state.image_size = image_size;
    g_boot_state.image_crc32 = image_crc32;
    g_boot_state.written_bytes = 0u;
    g_boot_state.last_ok_offset = 0u;
    g_boot_state.error_code = BOOT_ERR_NONE;

    if (Upgrade_SaveState(&g_boot_state) != 0u)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_FLASH_ERASE);
        return;
    }

    err = Upgrade_EraseAppRegion(image_size);
    if (err != 0u)
    {
        g_boot_state.state = UPGRADE_STATE_FAILED;
        g_boot_state.error_code = BOOT_ERR_FLASH_ERASE;
        Upgrade_SaveState(&g_boot_state);
        Bootloader_SendNack(frame->seq, BOOT_ERR_FLASH_ERASE);
        return;
    }

    g_boot_state.state = UPGRADE_STATE_PROGRAMMING;
    g_boot_state.error_code = BOOT_ERR_NONE;
    Upgrade_SaveState(&g_boot_state);
    g_boot_last_error = BOOT_ERR_NONE;
    Bootloader_SendAck(frame->seq);
}

/* DATA 要求严格顺序写入，不允许乱序；写入后按节流策略保存状态页。 */
static void Bootloader_HandleData(const BootFrame *frame)
{
    uint32_t offset;
    uint16_t chunk_len;
    uint8_t err;

    if (frame->len < 6u)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_LENGTH);
        return;
    }
    if (g_boot_state.state != UPGRADE_STATE_PROGRAMMING)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_STATE);
        return;
    }

    memcpy(&offset, &frame->payload[0], sizeof(offset));
    memcpy(&chunk_len, &frame->payload[4], sizeof(chunk_len));

    if (frame->len != (uint16_t)(6u + chunk_len) || chunk_len == 0u)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_LENGTH);
        return;
    }
    if (offset != g_boot_state.written_bytes)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_OFFSET);
        return;
    }
    if ((offset + chunk_len) > g_boot_state.image_size)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_SIZE);
        return;
    }

    err = Upgrade_ProgramBytes(UPGRADE_APP_BASE_ADDR + offset, &frame->payload[6], chunk_len);
    if (err != 0u)
    {
        g_boot_state.state = UPGRADE_STATE_FAILED;
        g_boot_state.error_code = BOOT_ERR_FLASH_PROGRAM;
        Upgrade_SaveState(&g_boot_state);
        Bootloader_SendNack(frame->seq, BOOT_ERR_FLASH_PROGRAM);
        return;
    }

    g_boot_state.written_bytes = offset + chunk_len;
    g_boot_state.last_ok_offset = offset + chunk_len;
    g_boot_state.error_code = BOOT_ERR_NONE;
    if (Bootloader_SaveStateThrottled((g_boot_state.written_bytes == g_boot_state.image_size) ? 1u : 0u) != 0u)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_FLASH_PROGRAM);
        return;
    }

    g_boot_last_error = BOOT_ERR_NONE;
    Bootloader_SendAck(frame->seq);
}

/* END 负责最终收口：校验长度、计算 CRC32、检查向量表，再把状态写成 DONE。 */
static void Bootloader_HandleEnd(const BootFrame *frame)
{
    uint32_t calc_crc32;

    if (frame->len != 0u)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_LENGTH);
        return;
    }
    if (g_boot_state.state != UPGRADE_STATE_PROGRAMMING)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_STATE);
        return;
    }
    if (g_boot_state.written_bytes != g_boot_state.image_size)
    {
        Bootloader_SendNack(frame->seq, BOOT_ERR_NOT_COMPLETE);
        return;
    }

    g_boot_state.state = UPGRADE_STATE_VERIFYING;
    g_boot_state.error_code = BOOT_ERR_NONE;
    Upgrade_SaveState(&g_boot_state);

    calc_crc32 = Upgrade_CRC32_CalculateFlash(UPGRADE_APP_BASE_ADDR, g_boot_state.image_size);
    if (calc_crc32 != g_boot_state.image_crc32 || Upgrade_IsAppVectorValid(UPGRADE_APP_BASE_ADDR) == 0u)
    {
        g_boot_state.state = UPGRADE_STATE_FAILED;
        g_boot_state.error_code = BOOT_ERR_VERIFY;
        Upgrade_SaveState(&g_boot_state);
        Bootloader_SendNack(frame->seq, BOOT_ERR_VERIFY);
        return;
    }

    g_boot_state.state = UPGRADE_STATE_DONE;
    g_boot_state.error_code = BOOT_ERR_NONE;
    Upgrade_SaveState(&g_boot_state);
    g_boot_last_error = BOOT_ERR_NONE;
    Bootloader_SendAck(frame->seq);
}

/* ABORT 不回滚旧镜像，只把本次升级标记为 FAILED，继续留在 Bootloader。 */
static void Bootloader_HandleAbort(const BootFrame *frame)
{
    Upgrade_InitStateImage(&g_boot_state);
    g_boot_state.state = UPGRADE_STATE_FAILED;
    g_boot_state.error_code = BOOT_ERR_NONE;
    Upgrade_SaveState(&g_boot_state);
    g_boot_last_error = BOOT_ERR_NONE;
    Bootloader_SendAck(frame->seq);
}

/* 协议层分发入口。 */
static void Bootloader_HandleFrame(const BootFrame *frame)
{
    switch (frame->cmd)
    {
        case BOOT_CMD_GET_INFO:
            Bootloader_SendInfo(frame->seq);
            break;

        case BOOT_CMD_START:
            Bootloader_HandleStart(frame);
            break;

        case BOOT_CMD_DATA:
            Bootloader_HandleData(frame);
            break;

        case BOOT_CMD_END:
            Bootloader_HandleEnd(frame);
            break;

        case BOOT_CMD_ABORT:
            Bootloader_HandleAbort(frame);
            break;

        case BOOT_CMD_QUERY_STATUS:
            Bootloader_SendStatus(frame->seq);
            break;

        default:
            Bootloader_SendNack(frame->seq, BOOT_ERR_BAD_CMD);
            break;
    }
}

/* 启动决策：
 * - DONE 且镜像一致：允许跳 App
 * - FAILED 且当前 App 仍有效：允许跳 App
 * - REQUESTED/ERASING/PROGRAMMING/VERIFYING：默认继续驻留
 * - 但如果连续多次上电仍卡在这些状态，且 App 依旧有效，则自动判为超时失败并回 App */
uint8_t Bootloader_ShouldStayInLoader(void)
{
    if (Upgrade_LoadState(&g_boot_state) == 0u)
    {
        if (Bootloader_IsUpgradeActiveState(g_boot_state.state) != 0u)
        {
            uint8_t app_valid = Upgrade_IsAppVectorValid(UPGRADE_APP_BASE_ADDR);

            if (g_boot_state.active_boot_count < 0xFFFFu)
            {
                g_boot_state.active_boot_count++;
                Upgrade_SaveState(&g_boot_state);
            }

            if (app_valid != 0u && g_boot_state.active_boot_count >= UPGRADE_ACTIVE_STATE_BOOT_LIMIT)
            {
                g_boot_state.state = UPGRADE_STATE_FAILED;
                g_boot_state.error_code = BOOT_ERR_TIMEOUT_RECOVERY;
                g_boot_state.active_boot_count = 0u;
                Upgrade_SaveState(&g_boot_state);
                return 0u;
            }

            return 1u;
        }
        if (g_boot_state.state == UPGRADE_STATE_DONE)
        {
            if (g_boot_state.image_size == 0u ||
                g_boot_state.image_size > UPGRADE_APP_MAX_SIZE ||
                Upgrade_CRC32_CalculateFlash(UPGRADE_APP_BASE_ADDR, g_boot_state.image_size) != g_boot_state.image_crc32)
            {
                g_boot_state.state = UPGRADE_STATE_FAILED;
                g_boot_state.error_code = BOOT_ERR_VERIFY;
                g_boot_state.active_boot_count = 0u;
                Upgrade_SaveState(&g_boot_state);
                return 1u;
            }
        }
    }
    else
    {
        Upgrade_InitStateImage(&g_boot_state);
    }

    return (Upgrade_IsAppVectorValid(UPGRADE_APP_BASE_ADDR) == 0u) ? 1u : 0u;
}

/* Bootloader 主循环：
 * 1. 打印状态
 * 2. 决定驻留还是跳 App
 * 3. 驻留时持续处理升级协议，并用红灯快闪表示当前处于 Bootloader */
void Bootloader_Run(void)
{
    BootFrame frame;
    uint32_t last_tick = 0u;

    Bootloader_PrintBanner();
    Bootloader_UartInit();

    if (Upgrade_LoadState(&g_boot_state) == 0u)
    {
        printf("[BOOT] state=%u source=%u err=%u written=%lu\r\n",
               g_boot_state.state,
               g_boot_state.request_source,
               g_boot_state.error_code,
               (unsigned long)g_boot_state.written_bytes);
    }
    else
    {
        printf("[BOOT] state page empty or invalid, fallback policy active\r\n");
    }

    if (Bootloader_ShouldStayInLoader() == 0u)
    {
        printf("[BOOT] valid app found, jump now\r\n");
        HAL_Delay(10);
        Upgrade_JumpToApplication(UPGRADE_APP_BASE_ADDR);
    }

    printf("[BOOT] staying in loader, upgrade protocol ready\r\n");
    printf("[BOOT] waiting on USART3 115200 8N1\r\n");

    while (1)
    {
        if (Bootloader_ReadFrame(&frame) != 0u)
        {
            Bootloader_HandleFrame(&frame);
        }

        if ((HAL_GetTick() - last_tick) >= 100u)
        {
            last_tick = HAL_GetTick();
            LED_R_TOGGLE();
        }
    }
}

void Bootloader_Usart3IrqHandler(void)
{
    HAL_UART_IRQHandler(&g_boot_uart);
}
