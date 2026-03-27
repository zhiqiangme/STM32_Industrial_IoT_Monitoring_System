#include "boot_protocol.h"

#include "LED.h"
#include "boot_config.h"
#include "boot_flash.h"
#include "boot_sha256.h"
#include "ymodem.h"

#include <stdio.h>

/* 485 收发切换：拉低 PA5 进入接收态。 */
static void BootProtocol_RxEnable(void)
{
    HAL_GPIO_WritePin(BOOT_485_EN_PORT, BOOT_485_EN_PIN, GPIO_PIN_RESET);
}

/* 485 收发切换：拉高 PA5 进入发送态。 */
static void BootProtocol_TxEnable(void)
{
    HAL_GPIO_WritePin(BOOT_485_EN_PORT, BOOT_485_EN_PIN, GPIO_PIN_SET);
}

/* 最底层发包函数，只负责 485 方向切换与串口发送。 */
static void BootProtocol_SendRaw(BootloaderRuntime *runtime, const uint8_t *data, uint16_t len)
{
    if (runtime == NULL || data == NULL || len == 0u)
    {
        return;
    }

    BootProtocol_TxEnable();
    for (volatile uint32_t i = 0; i < BOOT_RS485_TX_SETTLE_DELAY; i++)
    {
    }

    (void)HAL_UART_Transmit(&runtime->uart, (uint8_t *)data, len, 200);
    while (__HAL_UART_GET_FLAG(&runtime->uart, UART_FLAG_TC) == RESET)
    {
    }

    for (volatile uint32_t i = 0; i < BOOT_RS485_RX_SETTLE_DELAY; i++)
    {
    }
    BootProtocol_RxEnable();
}

static uint8_t BootProtocol_ReceiveByte(BootloaderRuntime *runtime, uint8_t *byte, uint32_t timeout_ms)
{
    if (runtime == NULL || byte == NULL)
    {
        return 0u;
    }

    if (HAL_UART_Receive(&runtime->uart, byte, 1u, timeout_ms) == HAL_OK)
    {
        return 1u;
    }

    return 0u;
}

static void BootProtocol_YmodemWriteBytes(const uint8_t *data, uint16_t len, void *context)
{
    BootProtocol_SendRaw((BootloaderRuntime *)context, data, len);
}

static uint8_t BootProtocol_YmodemReadByte(uint8_t *byte, uint32_t timeout_ms, void *context)
{
    uint32_t start_tick;
    static uint32_t s_last_led_tick = 0u;
    BootloaderRuntime *runtime = (BootloaderRuntime *)context;

    if (runtime == NULL)
    {
        return 0u;
    }

    start_tick = HAL_GetTick();
    if (s_last_led_tick == 0u)
    {
        s_last_led_tick = start_tick;
    }

    while ((HAL_GetTick() - start_tick) < timeout_ms)
    {
        if (BootProtocol_ReceiveByte(runtime, byte, BOOT_UART_READ_POLL_MS) != 0u)
        {
            return 1u;
        }

        if ((HAL_GetTick() - s_last_led_tick) >= BOOT_LED_TOGGLE_INTERVAL_MS)
        {
            s_last_led_tick = HAL_GetTick();
            LED_R_TOGGLE();
        }
    }

    return 0u;
}

static COM_StatusTypeDef BootProtocol_YmodemStart(const char *file_name,
                                                  uint32_t file_size,
                                                  uint32_t image_crc32,
                                                  uint32_t target_fw_version,
                                                  const uint8_t *image_sha256,
                                                  void *context)
{
    char sha256_hex[BOOT_SHA256_HEX_LENGTH] = {0};
    BootloaderRuntime *runtime = (BootloaderRuntime *)context;

    BootSha256_FormatHex(image_sha256, sha256_hex);
    printf("[BOOT] YMODEM header: file=%s size=%lu crc32=0x%08lX target=0x%08lX sha256=%s\r\n",
           (file_name != NULL) ? file_name : "(null)",
           (unsigned long)file_size,
           (unsigned long)image_crc32,
           (unsigned long)target_fw_version,
           sha256_hex);

    return BootFlash_BeginImage(runtime, file_size, image_crc32, target_fw_version, image_sha256);
}

static COM_StatusTypeDef BootProtocol_YmodemData(uint32_t offset,
                                                 const uint8_t *data,
                                                 uint32_t len,
                                                 void *context)
{
    return BootFlash_WriteImageData((BootloaderRuntime *)context, offset, data, len);
}

void BootProtocol_PrintBanner(void)
{
    printf("\r\n[BOOT] STM32 Mill Bootloader\r\n");
    printf("[BOOT] boot_ver: 0x%04X\r\n", BOOT_INFO_VERSION);
    printf("[BOOT] slot A: 0x%08lX size=0x%05lX\r\n",
           (unsigned long)UPGRADE_SLOT_A_BASE_ADDR,
           (unsigned long)UPGRADE_SLOT_MAX_SIZE);
    printf("[BOOT] slot B: 0x%08lX size=0x%05lX\r\n",
           (unsigned long)UPGRADE_SLOT_B_BASE_ADDR,
           (unsigned long)UPGRADE_SLOT_MAX_SIZE);
    printf("[BOOT] bootctl : 0x%08lX\r\n", (unsigned long)UPGRADE_BOOTCTRL_PAGE_ADDR);
    printf("[BOOT] State page: 0x%08lX\r\n", (unsigned long)UPGRADE_STATE_PAGE_ADDR);
}

/* Bootloader 自己占用 USART3 + PA5 这条 485 通道，和 App 运行态复用同一物理口。 */
void BootProtocol_InitUart(BootloaderRuntime *runtime)
{
    GPIO_InitTypeDef gpio = {0};

    if (runtime == NULL)
    {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_USART3_CLK_ENABLE();

    gpio.Pin = BOOT_485_EN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOOT_485_EN_PORT, &gpio);
    BootProtocol_RxEnable();

    gpio.Pin = BOOT_UART_TX_PIN;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOOT_UART_TX_PORT, &gpio);

    gpio.Pin = BOOT_UART_RX_PIN;
    gpio.Mode = GPIO_MODE_AF_INPUT;
    gpio.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(BOOT_UART_RX_PORT, &gpio);

    runtime->uart.Instance = BOOT_UART;
    runtime->uart.Init.BaudRate = 115200;
    runtime->uart.Init.WordLength = UART_WORDLENGTH_8B;
    runtime->uart.Init.StopBits = UART_STOPBITS_1;
    runtime->uart.Init.Parity = UART_PARITY_NONE;
    runtime->uart.Init.Mode = UART_MODE_TX_RX;
    runtime->uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    runtime->uart.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&runtime->uart);
}

void BootProtocol_PrintState(const BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return;
    }

    printf("[BOOT] state=%u source=%u err=%u written=%lu\r\n",
           runtime->state.state,
           runtime->state.request_source,
           runtime->state.error_code,
           (unsigned long)runtime->state.written_bytes);
    printf("[BOOT] slots: active=%u confirmed=%u pending=%u attempts=%u transfer=%u\r\n",
           runtime->boot_control.active_slot,
           runtime->boot_control.confirmed_slot,
           runtime->boot_control.pending_slot,
           runtime->boot_control.boot_attempts,
           runtime->transfer_slot);
}

void BootProtocol_PrintWaitingMessage(void)
{
    printf("[BOOT] staying in loader, YMODEM receiver ready\r\n");
    printf("[BOOT] waiting on USART3 115200 8N1\r\n");
}

COM_StatusTypeDef BootProtocol_RunYmodemSession(BootloaderRuntime *runtime, YmodemReceiveResult *result)
{
    const YmodemReceiveConfig config =
    {
        .read_byte = BootProtocol_YmodemReadByte,
        .write_bytes = BootProtocol_YmodemWriteBytes,
        .io_context = runtime,
        .on_start = BootProtocol_YmodemStart,
        .on_data = BootProtocol_YmodemData,
        .user_context = runtime
    };

    return Ymodem_Receive(&config, result);
}

UART_HandleTypeDef *BootProtocol_GetUartHandle(BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return NULL;
    }

    return &runtime->uart;
}
