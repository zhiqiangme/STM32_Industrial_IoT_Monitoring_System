#include "bootloader.h"

#include "delay.h"
#include "LED.h"
#include "Upgrade.h"
#include "usart.h"
#include "ymodem.h"

#include "stm32f1xx_it.h"
#include <stdio.h>
#include <string.h>

#define BOOT_UART                     USART3
#define BOOT_485_EN_PORT              GPIOA
#define BOOT_485_EN_PIN               GPIO_PIN_5
#define BOOT_UART_TX_PORT             GPIOB
#define BOOT_UART_TX_PIN              GPIO_PIN_10
#define BOOT_UART_RX_PORT             GPIOB
#define BOOT_UART_RX_PIN              GPIO_PIN_11
#define BOOT_INFO_VERSION             0x0002u

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

static UART_HandleTypeDef g_boot_uart;
static UpgradeStateImage g_boot_state;
static uint16_t g_boot_last_error = BOOT_ERR_NONE;
static uint8_t g_boot_reset_pending = 0u;

/* 这些状态说明上次升级尚未结束，Bootloader 默认应继续驻留等待恢复。 */
static uint8_t Bootloader_IsUpgradeActiveState(uint16_t state)
{
    return (state == UPGRADE_STATE_REQUESTED ||
            state == UPGRADE_STATE_ERASING ||
            state == UPGRADE_STATE_PROGRAMMING ||
            state == UPGRADE_STATE_VERIFYING) ? 1u : 0u;
}

/* START 阶段尽量沿用 App 先前写入状态页的请求来源，避免把远程请求覆盖成 LOCAL。 */
static uint16_t Bootloader_ResolveRequestSource(void)
{
    UpgradeStateImage previous_state;

    if (Upgrade_LoadState(&previous_state) == 0u &&
        Bootloader_IsUpgradeActiveState(previous_state.state) != 0u &&
        previous_state.request_source != UPGRADE_REQUEST_SOURCE_NONE)
    {
        return previous_state.request_source;
    }

    return UPGRADE_REQUEST_SOURCE_LOCAL;
}

/* 如果升级头里没显式传目标版本，则保留 App 阶段已经写入状态页的值。 */
static uint32_t Bootloader_ResolveTargetVersion(uint32_t requested_target_fw_version)
{
    UpgradeStateImage previous_state;

    if (requested_target_fw_version != 0u)
    {
        return requested_target_fw_version;
    }

    if (Upgrade_LoadState(&previous_state) == 0u &&
        Bootloader_IsUpgradeActiveState(previous_state.state) != 0u)
    {
        return previous_state.target_fw_version;
    }

    return 0u;
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
    printf("[BOOT] boot_ver: 0x%04X\r\n", BOOT_INFO_VERSION);
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

/* YMODEM 只需要逐字节阻塞读取。 */
static uint8_t Bootloader_ReceiveByte(uint8_t *byte, uint32_t timeout_ms)
{
    if (HAL_UART_Receive(&g_boot_uart, byte, 1u, timeout_ms) == HAL_OK)
    {
        return 1u;
    }

    return 0u;
}

static void Bootloader_MarkFailed(uint16_t error_code)
{
    if (g_boot_state.magic != UPGRADE_STATE_MAGIC)
    {
        Upgrade_InitStateImage(&g_boot_state);
    }

    g_boot_state.state = UPGRADE_STATE_FAILED;
    g_boot_state.error_code = error_code;
    g_boot_state.active_boot_count = 0u;
    (void)Upgrade_SaveState(&g_boot_state);
    g_boot_last_error = error_code;
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

static void Bootloader_YmodemWriteBytes(const uint8_t *data, uint16_t len, void *context)
{
    (void)context;
    Bootloader_SendRaw(data, len);
}

static uint8_t Bootloader_YmodemReadByte(uint8_t *byte, uint32_t timeout_ms, void *context)
{
    uint32_t start_tick;
    static uint32_t s_last_led_tick = 0u;

    (void)context;

    start_tick = HAL_GetTick();
    if (s_last_led_tick == 0u)
    {
        s_last_led_tick = start_tick;
    }

    while ((HAL_GetTick() - start_tick) < timeout_ms)
    {
        if (Bootloader_ReceiveByte(byte, 20u) != 0u)
        {
            return 1u;
        }

        if ((HAL_GetTick() - s_last_led_tick) >= 200u)
        {
            s_last_led_tick = HAL_GetTick();
            LED_R_TOGGLE();
        }
    }

    return 0u;
}

static COM_StatusTypeDef Bootloader_YmodemStart(const char *file_name,
                                                uint32_t file_size,
                                                uint32_t image_crc32,
                                                uint32_t target_fw_version,
                                                void *context)
{
    uint8_t err;

    (void)context;

    printf("[BOOT] YMODEM header: file=%s size=%lu crc32=0x%08lX target=0x%08lX\r\n",
           (file_name != NULL) ? file_name : "(null)",
           (unsigned long)file_size,
           (unsigned long)image_crc32,
           (unsigned long)target_fw_version);

    if (file_size == 0u || file_size > UPGRADE_APP_MAX_SIZE)
    {
        Bootloader_MarkFailed(BOOT_ERR_BAD_SIZE);
        return COM_LIMIT;
    }

    Upgrade_InitStateImage(&g_boot_state);
    g_boot_state.state = UPGRADE_STATE_ERASING;
    g_boot_state.request_source = Bootloader_ResolveRequestSource();
    g_boot_state.target_fw_version = Bootloader_ResolveTargetVersion(target_fw_version);
    g_boot_state.image_size = file_size;
    g_boot_state.image_crc32 = image_crc32;
    g_boot_state.written_bytes = 0u;
    g_boot_state.last_ok_offset = 0u;
    g_boot_state.error_code = BOOT_ERR_NONE;
    g_boot_state.active_boot_count = 0u;

    if (Upgrade_SaveState(&g_boot_state) != 0u)
    {
        Bootloader_MarkFailed(BOOT_ERR_FLASH_ERASE);
        return COM_DATA;
    }

    err = Upgrade_EraseAppRegion(file_size);
    if (err != 0u)
    {
        Bootloader_MarkFailed(BOOT_ERR_FLASH_ERASE);
        return COM_DATA;
    }

    g_boot_state.state = UPGRADE_STATE_PROGRAMMING;
    g_boot_state.error_code = BOOT_ERR_NONE;
    g_boot_state.active_boot_count = 0u;
    if (Upgrade_SaveState(&g_boot_state) != 0u)
    {
        Bootloader_MarkFailed(BOOT_ERR_FLASH_ERASE);
        return COM_DATA;
    }

    g_boot_last_error = BOOT_ERR_NONE;
    return COM_OK;
}

static COM_StatusTypeDef Bootloader_YmodemData(uint32_t offset,
                                               const uint8_t *data,
                                               uint32_t len,
                                               void *context)
{
    uint8_t err;

    (void)context;

    if (len == 0u || data == NULL)
    {
        Bootloader_MarkFailed(BOOT_ERR_BAD_LENGTH);
        return COM_DATA;
    }
    if (g_boot_state.state != UPGRADE_STATE_PROGRAMMING)
    {
        Bootloader_MarkFailed(BOOT_ERR_BAD_STATE);
        return COM_DATA;
    }
    if (offset != g_boot_state.written_bytes)
    {
        Bootloader_MarkFailed(BOOT_ERR_BAD_OFFSET);
        return COM_DATA;
    }
    if ((offset + len) > g_boot_state.image_size)
    {
        Bootloader_MarkFailed(BOOT_ERR_BAD_SIZE);
        return COM_DATA;
    }

    err = Upgrade_ProgramBytes(UPGRADE_APP_BASE_ADDR + offset, data, len);
    if (err != 0u)
    {
        Bootloader_MarkFailed(BOOT_ERR_FLASH_PROGRAM);
        return COM_DATA;
    }

    g_boot_state.written_bytes = offset + len;
    g_boot_state.last_ok_offset = offset + len;
    g_boot_state.error_code = BOOT_ERR_NONE;
    g_boot_state.active_boot_count = 0u;
    if (Bootloader_SaveStateThrottled((g_boot_state.written_bytes == g_boot_state.image_size) ? 1u : 0u) != 0u)
    {
        Bootloader_MarkFailed(BOOT_ERR_FLASH_PROGRAM);
        return COM_DATA;
    }

    g_boot_last_error = BOOT_ERR_NONE;
    return COM_OK;
}

/* 收包完成后做最终收口：校验长度、计算 CRC32、检查向量表，再把状态写成 DONE。 */
static uint8_t Bootloader_FinalizeYmodemImage(const YmodemReceiveResult *result)
{
    uint32_t calc_crc32;

    if (result == NULL)
    {
        Bootloader_MarkFailed(BOOT_ERR_BAD_FRAME);
        return 0u;
    }
    if (g_boot_state.state != UPGRADE_STATE_PROGRAMMING)
    {
        Bootloader_MarkFailed(BOOT_ERR_BAD_STATE);
        return 0u;
    }
    if (g_boot_state.written_bytes != g_boot_state.image_size ||
        result->bytes_received != result->file_size)
    {
        Bootloader_MarkFailed(BOOT_ERR_NOT_COMPLETE);
        return 0u;
    }

    g_boot_state.state = UPGRADE_STATE_VERIFYING;
    g_boot_state.error_code = BOOT_ERR_NONE;
    g_boot_state.active_boot_count = 0u;
    (void)Upgrade_SaveState(&g_boot_state);

    calc_crc32 = Upgrade_CRC32_CalculateFlash(UPGRADE_APP_BASE_ADDR, g_boot_state.image_size);
    if ((g_boot_state.image_crc32 != 0u && calc_crc32 != g_boot_state.image_crc32) ||
        Upgrade_IsAppVectorValid(UPGRADE_APP_BASE_ADDR) == 0u)
    {
        Bootloader_MarkFailed(BOOT_ERR_VERIFY);
        return 0u;
    }

    g_boot_state.state = UPGRADE_STATE_DONE;
    g_boot_state.image_crc32 = calc_crc32;
    g_boot_state.error_code = BOOT_ERR_NONE;
    g_boot_state.active_boot_count = 0u;
    if (Upgrade_SaveState(&g_boot_state) != 0u)
    {
        Bootloader_MarkFailed(BOOT_ERR_VERIFY);
        return 0u;
    }

    g_boot_last_error = BOOT_ERR_NONE;
    return 1u;
}

static void Bootloader_RunYmodemSession(void)
{
    const YmodemReceiveConfig config =
    {
        .read_byte = Bootloader_YmodemReadByte,
        .write_bytes = Bootloader_YmodemWriteBytes,
        .io_context = NULL,
        .on_start = Bootloader_YmodemStart,
        .on_data = Bootloader_YmodemData,
        .user_context = NULL
    };
    YmodemReceiveResult result;
    COM_StatusTypeDef status;

    status = Ymodem_Receive(&config, &result);
    if (status == COM_OK)
    {
        if (result.file_size == 0u)
        {
            g_boot_last_error = BOOT_ERR_NONE;
            return;
        }

        if (Bootloader_FinalizeYmodemImage(&result) != 0u)
        {
            printf("[BOOT] YMODEM done: size=%lu crc32=0x%08lX\r\n",
                   (unsigned long)result.file_size,
                   (unsigned long)g_boot_state.image_crc32);
            g_boot_reset_pending = 1u;
        }
        return;
    }

    if (g_boot_last_error == BOOT_ERR_NONE)
    {
        Bootloader_MarkFailed((status == COM_ABORT) ? BOOT_ERR_BAD_FRAME : BOOT_ERR_BAD_CRC);
    }

    printf("[BOOT] YMODEM failed: status=%u err=0x%04X written=%lu\r\n",
           (unsigned int)status,
           g_boot_last_error,
           (unsigned long)g_boot_state.written_bytes);
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
 * 3. 驻留时进入 YMODEM 接收会话，并用红灯表示当前处于 Bootloader */
void Bootloader_Run(void)
{
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

    printf("[BOOT] staying in loader, YMODEM receiver ready\r\n");
    printf("[BOOT] waiting on USART3 115200 8N1\r\n");

    while (1)
    {
        Bootloader_RunYmodemSession();

        if (g_boot_reset_pending != 0u)
        {
            printf("[BOOT] upgrade done, reset to re-enter startup path\r\n");
            HAL_Delay(50);
            HAL_NVIC_SystemReset();
        }
    }
}

void Bootloader_Usart3IrqHandler(void)
{
    HAL_UART_IRQHandler(&g_boot_uart);
}
