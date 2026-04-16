#include "boot_main.h"

#include "boot_config.h"
#include "boot_flash.h"
#include "boot_protocol.h"
#include "boot_sha256.h"
#include "boot_verify.h"
#include "bootloader.h"

#include <stdio.h>
#include <string.h>

static BootloaderRuntime g_boot_runtime = {0};

/* 如果 App 已开启 IWDG，发生看门狗复位后在 Bootloader 侧也要持续喂狗，
 * 否则驻留升级时会被二次复位打断。 */
static void BootMain_RefreshWatchdog(void)
{
    IWDG->KR = 0xAAAAu;
}

static void BootMain_InitForceStayInput(void)
{
    GPIO_InitTypeDef gpio = {0};

    BOOT_FORCE_STAY_KEY_CLK_ENABLE();
    gpio.Pin = BOOT_FORCE_STAY_KEY_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(BOOT_FORCE_STAY_KEY_PORT, &gpio);
}

static uint8_t BootMain_IsForceStayKeyPressed(void)
{
    return (HAL_GPIO_ReadPin(BOOT_FORCE_STAY_KEY_PORT, BOOT_FORCE_STAY_KEY_PIN) ==
            BOOT_FORCE_STAY_KEY_ACTIVE_LEVEL) ? 1u : 0u;
}

static const char *BootMain_SlotName(uint16_t slot)
{
    if (slot == UPGRADE_SLOT_A)
    {
        return "A";
    }
    if (slot == UPGRADE_SLOT_B)
    {
        return "B";
    }

    return "?";
}

/* YMODEM 传输层只给出一个粗粒度状态，这里把它翻译成更贴近升级业务的错误码，
 * 方便状态页落盘和现场串口日志排障。 */
static uint16_t BootMain_MapYmodemStatusToError(COM_StatusTypeDef status)
{
    switch (status)
    {
        case COM_ABORT:
            return BOOT_ERR_BAD_FRAME;

        case COM_LIMIT:
            return BOOT_ERR_BAD_SIZE;

        case COM_DATA:
            return (g_boot_runtime.last_error != BOOT_ERR_NONE) ?
                g_boot_runtime.last_error : BOOT_ERR_FLASH_PROGRAM;

        case COM_TIMEOUT:
            return BOOT_ERR_BAD_FRAME;

        case COM_ERROR:
        default:
            return BOOT_ERR_PACKET_CRC;
    }
}

/* 统一把状态页加载失败的原因翻译成串口日志，避免启动日志里只看到模糊的“invalid”。 */
static void BootMain_LogStateLoadIssue(uint8_t load_status)
{
    switch (load_status)
    {
        case 2u:
            printf("[BOOT] state page empty, default state assumed\r\n");
            break;

        case 4u:
            printf("[BOOT] state page CRC16 invalid, fallback policy active\r\n");
            break;

        default:
            printf("[BOOT] state page invalid (code=%u), fallback policy active\r\n",
                   (unsigned int)load_status);
            break;
    }
}

static void BootMain_LogBootControlLoadIssue(uint8_t load_status)
{
    switch (load_status)
    {
        case 2u:
            printf("[BOOT] boot control empty, default slot policy assumed\r\n");
            break;

        case 4u:
            printf("[BOOT] boot control CRC16 invalid, rebuilding defaults\r\n");
            break;

        default:
            printf("[BOOT] boot control invalid (code=%u), rebuilding defaults\r\n",
                   (unsigned int)load_status);
            break;
    }
}

/* 最终校验失败时，把“失败发生在哪一层”打印清楚。
 * 这一步是联调时最关键的诊断入口，所以日志尽量直接暴露期望值与实算值。 */
static void BootMain_LogVerificationFailure(uint16_t slot,
                                            uint32_t expected_crc32,
                                            const uint8_t expected_sha256[BOOT_SHA256_DIGEST_SIZE],
                                            uint32_t expected_size,
                                            uint16_t error_code,
                                            uint32_t calc_crc32,
                                            const uint8_t calc_sha256[BOOT_SHA256_DIGEST_SIZE])
{
    if (error_code == BOOT_ERR_VERIFY_CRC32)
    {
        printf("[BOOT] slot %s verify failed: CRC32 mismatch expect=0x%08lX calc=0x%08lX\r\n",
               BootMain_SlotName(slot),
               (unsigned long)expected_crc32,
               (unsigned long)calc_crc32);
        return;
    }

    if (error_code == BOOT_ERR_VERIFY_SHA256)
    {
        char expected_hex[BOOT_SHA256_HEX_LENGTH] = {0};
        char actual_hex[BOOT_SHA256_HEX_LENGTH] = {0};

        BootSha256_FormatHex(expected_sha256, expected_hex);
        BootSha256_FormatHex(calc_sha256, actual_hex);
        printf("[BOOT] slot %s verify failed: SHA256 mismatch\r\n",
               BootMain_SlotName(slot));
        printf("[BOOT]   expect=%s\r\n", expected_hex);
        printf("[BOOT]   calc  =%s\r\n", actual_hex);
        return;
    }

    if (error_code == BOOT_ERR_VECTOR_INVALID)
    {
        printf("[BOOT] slot %s verify failed: vector table invalid\r\n",
               BootMain_SlotName(slot));
        return;
    }

    if (error_code == BOOT_ERR_BAD_SIZE)
    {
        printf("[BOOT] slot %s verify failed: image size invalid (%lu)\r\n",
               BootMain_SlotName(slot),
               (unsigned long)expected_size);
        return;
    }

    printf("[BOOT] slot %s verify failed: err=0x%04X\r\n",
           BootMain_SlotName(slot),
           error_code);
}

static uint16_t BootMain_FindBestSlot(uint16_t exclude_slot)
{
    if (g_boot_runtime.boot_control.confirmed_slot != exclude_slot &&
        Upgrade_IsSlotIdValid(g_boot_runtime.boot_control.confirmed_slot) != 0u &&
        Upgrade_IsSlotVectorValid(g_boot_runtime.boot_control.confirmed_slot) != 0u)
    {
        return g_boot_runtime.boot_control.confirmed_slot;
    }

    if (g_boot_runtime.boot_control.active_slot != exclude_slot &&
        Upgrade_IsSlotIdValid(g_boot_runtime.boot_control.active_slot) != 0u &&
        Upgrade_IsSlotVectorValid(g_boot_runtime.boot_control.active_slot) != 0u)
    {
        return g_boot_runtime.boot_control.active_slot;
    }

    if (exclude_slot != UPGRADE_SLOT_A && Upgrade_IsSlotVectorValid(UPGRADE_SLOT_A) != 0u)
    {
        return UPGRADE_SLOT_A;
    }

    if (exclude_slot != UPGRADE_SLOT_B && Upgrade_IsSlotVectorValid(UPGRADE_SLOT_B) != 0u)
    {
        return UPGRADE_SLOT_B;
    }

    return UPGRADE_SLOT_NONE;
}

static void BootMain_UpdateWatchdogResetCounter(void)
{
    uint8_t was_iwdg_reset = (__HAL_RCC_GET_FLAG(RCC_FLAG_IWDGRST) != RESET) ? 1u : 0u;
    uint8_t changed = 0u;

    if (was_iwdg_reset != 0u)
    {
        if (g_boot_runtime.boot_control.watchdog_reset_count < 0xFFFFu)
        {
            g_boot_runtime.boot_control.watchdog_reset_count++;
            changed = 1u;
        }
    }
    else if (g_boot_runtime.boot_control.watchdog_reset_count != 0u)
    {
        g_boot_runtime.boot_control.watchdog_reset_count = 0u;
        changed = 1u;
    }

    __HAL_RCC_CLEAR_RESET_FLAGS();

    if (changed != 0u)
    {
        (void)BootFlash_SaveBootControl(&g_boot_runtime);
    }

    if (was_iwdg_reset != 0u)
    {
        printf("[BOOT] reset cause: IWDG (count=%u)\r\n",
               g_boot_runtime.boot_control.watchdog_reset_count);
    }
}

static uint8_t BootMain_CheckForceStayRequest(void)
{
    if (BootMain_IsForceStayKeyPressed() != 0u)
    {
        printf("[BOOT] force-stay key pressed, keep loader mode\r\n");
        return 1u;
    }

    return BootProtocol_CheckForceStayWindow(&g_boot_runtime, BOOT_FORCE_STAY_UART_WINDOW_MS);
}

static void BootMain_NormalizeBootControl(void)
{
    uint16_t best_slot;

    if (Upgrade_IsSlotIdValid(g_boot_runtime.boot_control.active_slot) == 0u)
    {
        g_boot_runtime.boot_control.active_slot = UPGRADE_SLOT_A;
    }

    if (Upgrade_IsSlotIdValid(g_boot_runtime.boot_control.confirmed_slot) == 0u)
    {
        g_boot_runtime.boot_control.confirmed_slot = g_boot_runtime.boot_control.active_slot;
    }

    if (Upgrade_IsSlotIdValid(g_boot_runtime.boot_control.pending_slot) == 0u)
    {
        g_boot_runtime.boot_control.pending_slot = UPGRADE_SLOT_NONE;
    }

    best_slot = BootMain_FindBestSlot(UPGRADE_SLOT_NONE);
    if (best_slot != UPGRADE_SLOT_NONE)
    {
        if (Upgrade_IsSlotVectorValid(g_boot_runtime.boot_control.active_slot) == 0u)
        {
            g_boot_runtime.boot_control.active_slot = best_slot;
        }
        if (Upgrade_IsSlotVectorValid(g_boot_runtime.boot_control.confirmed_slot) == 0u)
        {
            g_boot_runtime.boot_control.confirmed_slot = best_slot;
        }
    }
}

static void BootMain_MarkUpgradeFailed(uint16_t error_code)
{
    g_boot_runtime.state.state = UPGRADE_STATE_FAILED;
    g_boot_runtime.state.error_code = error_code;
    g_boot_runtime.state.active_boot_count = 0u;
    (void)Upgrade_SaveState(&g_boot_runtime.state);
}

static void BootMain_RollbackToSlot(uint16_t slot, uint16_t error_code)
{
    if (Upgrade_IsSlotIdValid(slot) == 0u)
    {
        return;
    }

    g_boot_runtime.boot_control.active_slot = slot;
    g_boot_runtime.boot_control.confirmed_slot = slot;
    g_boot_runtime.boot_control.pending_slot = UPGRADE_SLOT_NONE;
    g_boot_runtime.boot_control.boot_attempts = 0u;
    g_boot_runtime.boot_control.watchdog_reset_count = 0u;
    (void)BootFlash_SaveBootControl(&g_boot_runtime);

    BootMain_MarkUpgradeFailed(error_code);
    g_boot_runtime.boot_slot = slot;
}

/* 连续看门狗复位达到阈值后，优先回退到另一有效槽；
 * 如果没有可回退槽，则强制停留 Bootloader，避免继续跳入死循环 App。 */
static uint8_t BootMain_HandleWatchdogRecovery(void)
{
    uint16_t suspect_slot = UPGRADE_SLOT_NONE;
    uint16_t fallback_slot;

    if (g_boot_runtime.boot_control.watchdog_reset_count < UPGRADE_WDG_RESET_LIMIT)
    {
        return 0u;
    }

    if (Upgrade_IsSlotIdValid(g_boot_runtime.boot_control.pending_slot) != 0u)
    {
        suspect_slot = g_boot_runtime.boot_control.pending_slot;
    }
    else if (Upgrade_IsSlotIdValid(g_boot_runtime.boot_control.active_slot) != 0u)
    {
        suspect_slot = g_boot_runtime.boot_control.active_slot;
    }
    else if (Upgrade_IsSlotIdValid(g_boot_runtime.boot_control.confirmed_slot) != 0u)
    {
        suspect_slot = g_boot_runtime.boot_control.confirmed_slot;
    }

    fallback_slot = BootMain_FindBestSlot(suspect_slot);
    if (fallback_slot != UPGRADE_SLOT_NONE)
    {
        printf("[BOOT] watchdog reset count reached %u, rollback to slot %s\r\n",
               g_boot_runtime.boot_control.watchdog_reset_count,
               BootMain_SlotName(fallback_slot));
        BootMain_RollbackToSlot(fallback_slot, BOOT_ERR_WDG_RECOVERY);
        return 2u;
    }

    printf("[BOOT] watchdog reset count reached %u, no valid fallback slot, stay in loader\r\n",
           g_boot_runtime.boot_control.watchdog_reset_count);
    g_boot_runtime.boot_control.pending_slot = UPGRADE_SLOT_NONE;
    g_boot_runtime.boot_control.boot_attempts = 0u;
    g_boot_runtime.boot_control.watchdog_reset_count = 0u;
    (void)BootFlash_SaveBootControl(&g_boot_runtime);
    BootMain_MarkUpgradeFailed(BOOT_ERR_WDG_RECOVERY);

    return 1u;
}

/* 一次完整 YMODEM 会话写完后的收尾逻辑。
 * 顺序固定为：
 * 1. 检查是否收满
 * 2. 状态切到 VERIFYING 并持久化
 * 3. 计算整包 CRC32 / SHA-256
 * 4. 结合向量表一起做最终放行
 * 5. 通过后把“待试运行槽”写入启动控制页，等待复位重新走启动路径 */
static uint8_t BootMain_FinalizeSession(const YmodemReceiveResult *result)
{
    uint32_t calc_crc32;
    uint16_t verify_error;
    uint8_t calc_sha256[BOOT_SHA256_DIGEST_SIZE] = {0};

    if (result == NULL)
    {
        BootFlash_MarkFailed(&g_boot_runtime, BOOT_ERR_BAD_FRAME);
        return 0u;
    }
    if (BootVerify_IsReceivedImageComplete(&g_boot_runtime, result) == 0u)
    {
        BootFlash_MarkFailed(&g_boot_runtime, BOOT_ERR_NOT_COMPLETE);
        return 0u;
    }
    if (BootFlash_MarkVerifying(&g_boot_runtime) == 0u)
    {
        BootFlash_MarkFailed(&g_boot_runtime, BOOT_ERR_VERIFY);
        return 0u;
    }

    /* CRC32 和 SHA-256 都在目标槽 Flash 上直接计算，避免大镜像搬到 RAM。 */
    calc_crc32 = BootVerify_CalculateProgrammedImageCrc32(&g_boot_runtime);
    BootVerify_CalculateProgrammedImageSha256(&g_boot_runtime, calc_sha256);

    printf("[BOOT] slot %s verify CRC32: expect=0x%08lX calc=0x%08lX\r\n",
           BootMain_SlotName(g_boot_runtime.transfer_slot),
           (unsigned long)g_boot_runtime.state.image_crc32,
           (unsigned long)calc_crc32);
    if (BootVerify_HasExpectedSha256(&g_boot_runtime) != 0u)
    {
        printf("[BOOT] slot %s verify SHA256: calculated, checking against state page\r\n",
               BootMain_SlotName(g_boot_runtime.transfer_slot));
    }
    else
    {
        printf("[BOOT] slot %s verify SHA256: missing in state page, fallback to legacy CRC32 path\r\n",
               BootMain_SlotName(g_boot_runtime.transfer_slot));
    }

    verify_error = BootVerify_ValidateProgrammedImage(&g_boot_runtime, calc_crc32, calc_sha256);
    if (verify_error != BOOT_ERR_NONE)
    {
        BootMain_LogVerificationFailure(g_boot_runtime.transfer_slot,
                                        g_boot_runtime.state.image_crc32,
                                        g_boot_runtime.state.image_sha256,
                                        g_boot_runtime.state.image_size,
                                        verify_error,
                                        calc_crc32,
                                        calc_sha256);
        BootFlash_MarkFailed(&g_boot_runtime, verify_error);
        return 0u;
    }
    if (BootFlash_MarkDone(&g_boot_runtime, calc_crc32) == 0u)
    {
        BootFlash_MarkFailed(&g_boot_runtime, BOOT_ERR_VERIFY);
        return 0u;
    }

    return 1u;
}

static uint8_t BootMain_HandlePendingSlot(void)
{
    uint16_t pending_slot = g_boot_runtime.boot_control.pending_slot;
    const UpgradeSlotRecord *record;
    uint32_t calc_crc32 = 0u;
    uint16_t verify_error;
    uint16_t fallback_slot;
    uint8_t calc_sha256[BOOT_SHA256_DIGEST_SIZE] = {0};

    if (Upgrade_IsSlotIdValid(pending_slot) == 0u)
    {
        return 1u;
    }

    record = &g_boot_runtime.boot_control.slots[pending_slot];
    verify_error = BootVerify_ValidateStoredSlot(pending_slot, record, &calc_crc32, calc_sha256);
    if (verify_error != BOOT_ERR_NONE)
    {
        BootMain_LogVerificationFailure(pending_slot,
                                        record->image_crc32,
                                        record->image_sha256,
                                        record->image_size,
                                        verify_error,
                                        calc_crc32,
                                        calc_sha256);
        fallback_slot = BootMain_FindBestSlot(pending_slot);
        if (fallback_slot != UPGRADE_SLOT_NONE)
        {
            printf("[BOOT] pending slot %s invalid, rollback to slot %s\r\n",
                   BootMain_SlotName(pending_slot),
                   BootMain_SlotName(fallback_slot));
            BootMain_RollbackToSlot(fallback_slot, verify_error);
            return 0u;
        }

        BootMain_MarkUpgradeFailed(verify_error);
        return 1u;
    }

    if (g_boot_runtime.boot_control.boot_attempts >= UPGRADE_PENDING_BOOT_LIMIT)
    {
        fallback_slot = BootMain_FindBestSlot(pending_slot);
        if (fallback_slot != UPGRADE_SLOT_NONE)
        {
            printf("[BOOT] pending slot %s not confirmed after %u boots, rollback to slot %s\r\n",
                   BootMain_SlotName(pending_slot),
                   (unsigned int)g_boot_runtime.boot_control.boot_attempts,
                   BootMain_SlotName(fallback_slot));
            BootMain_RollbackToSlot(fallback_slot, BOOT_ERR_TIMEOUT_RECOVERY);
            return 0u;
        }

        BootMain_MarkUpgradeFailed(BOOT_ERR_TIMEOUT_RECOVERY);
        return 1u;
    }

    g_boot_runtime.boot_control.active_slot = pending_slot;
    if (g_boot_runtime.boot_control.boot_attempts < 0xFFFFu)
    {
        g_boot_runtime.boot_control.boot_attempts++;
    }
    if (BootFlash_SaveBootControl(&g_boot_runtime) != 0u)
    {
        return 1u;
    }

    printf("[BOOT] pending slot %s verified, trial boot #%u\r\n",
           BootMain_SlotName(pending_slot),
           (unsigned int)g_boot_runtime.boot_control.boot_attempts);
    g_boot_runtime.boot_slot = pending_slot;
    return 0u;
}

uint8_t BootMain_ShouldStayInLoader(void)
{
    uint8_t state_status;
    uint8_t boot_ctrl_status;
    uint8_t watchdog_recovery_action;
    uint16_t boot_slot;
    UpgradeBootControl original_boot_control;

    state_status = BootFlash_LoadState(&g_boot_runtime);
    if (state_status != 0u)
    {
        BootFlash_InitState(&g_boot_runtime);
        BootMain_LogStateLoadIssue(state_status);
    }

    boot_ctrl_status = BootFlash_LoadBootControl(&g_boot_runtime);
    if (boot_ctrl_status != 0u)
    {
        BootFlash_InitBootControl(&g_boot_runtime);
        BootMain_LogBootControlLoadIssue(boot_ctrl_status);
    }
    original_boot_control = g_boot_runtime.boot_control;
    BootMain_NormalizeBootControl();
    if (memcmp(&original_boot_control, &g_boot_runtime.boot_control, sizeof(original_boot_control)) != 0)
    {
        (void)BootFlash_SaveBootControl(&g_boot_runtime);
    }
    BootMain_UpdateWatchdogResetCounter();

    if (BootFlash_IsUpgradeActiveState(g_boot_runtime.state.state) != 0u)
    {
        /* 只要状态页还处于“升级进行中”，默认就继续留在 Bootloader。
         * 这样中途掉电、掉线、工具崩溃后都能重新发完整镜像恢复。 */
        boot_slot = BootMain_FindBestSlot(UPGRADE_SLOT_NONE);

        if (g_boot_runtime.state.active_boot_count < 0xFFFFu)
        {
            g_boot_runtime.state.active_boot_count++;
            (void)Upgrade_SaveState(&g_boot_runtime.state);
        }

        /* 如果连续多次上电都卡在升级态，且仍有稳定槽可用，
         * 就把这次升级判成超时失败，让设备自动回退到稳定槽。 */
        if (boot_slot != UPGRADE_SLOT_NONE &&
            g_boot_runtime.state.active_boot_count >= UPGRADE_ACTIVE_STATE_BOOT_LIMIT)
        {
            printf("[BOOT] upgrade state timeout, fallback to slot %s\r\n",
                   BootMain_SlotName(boot_slot));
            BootMain_RollbackToSlot(boot_slot, BOOT_ERR_TIMEOUT_RECOVERY);
            return 0u;
        }

        return 1u;
    }

    watchdog_recovery_action = BootMain_HandleWatchdogRecovery();
    if (watchdog_recovery_action == 1u)
    {
        return 1u;
    }
    if (watchdog_recovery_action == 2u)
    {
        return 0u;
    }

    if (BootMain_CheckForceStayRequest() != 0u)
    {
        return 1u;
    }

    if (g_boot_runtime.boot_control.pending_slot != UPGRADE_SLOT_NONE)
    {
        return BootMain_HandlePendingSlot();
    }

    boot_slot = BootMain_FindBestSlot(UPGRADE_SLOT_NONE);
    if (boot_slot == UPGRADE_SLOT_NONE)
    {
        return 1u;
    }

    if (g_boot_runtime.boot_control.active_slot != boot_slot ||
        g_boot_runtime.boot_control.confirmed_slot != boot_slot)
    {
        g_boot_runtime.boot_control.active_slot = boot_slot;
        g_boot_runtime.boot_control.confirmed_slot = boot_slot;
        g_boot_runtime.boot_control.boot_attempts = 0u;
        (void)BootFlash_SaveBootControl(&g_boot_runtime);
    }

    g_boot_runtime.boot_slot = boot_slot;
    return 0u;
}

void BootMain_Run(void)
{
    YmodemReceiveResult result;
    COM_StatusTypeDef status;

    g_boot_runtime.boot_slot = UPGRADE_SLOT_NONE;
    g_boot_runtime.transfer_slot = UPGRADE_SLOT_NONE;
    BootProtocol_PrintBanner();
    BootMain_InitForceStayInput();
    BootProtocol_InitUart(&g_boot_runtime);

    if (BootMain_ShouldStayInLoader() == 0u)
    {
        printf("[BOOT] valid slot %s found, jump now\r\n",
               BootMain_SlotName(g_boot_runtime.boot_slot));
        HAL_Delay(10);
        Upgrade_JumpToSlot(g_boot_runtime.boot_slot);
    }

    BootProtocol_PrintState(&g_boot_runtime);
    BootProtocol_PrintWaitingMessage();

    while (1)
    {
        BootMain_RefreshWatchdog();

        /* 这里每轮都等待一次完整 YMODEM 会话。
         * 成功则做最终校验并触发复位，失败则保持驻留，允许 PC 端重新发起。 */
        status = BootProtocol_RunYmodemSession(&g_boot_runtime, &result);
        if (status == COM_OK)
        {
            if (result.file_size == 0u)
            {
                g_boot_runtime.last_error = BOOT_ERR_NONE;
            }
            else if (BootMain_FinalizeSession(&result) != 0u)
            {
                printf("[BOOT] YMODEM done: slot=%s size=%lu crc32=0x%08lX\r\n",
                       BootMain_SlotName(g_boot_runtime.transfer_slot),
                       (unsigned long)result.file_size,
                       (unsigned long)g_boot_runtime.state.image_crc32);
                g_boot_runtime.reset_pending = 1u;
            }
        }
        else
        {
            if (g_boot_runtime.last_error == BOOT_ERR_NONE)
            {
                BootFlash_MarkFailed(&g_boot_runtime, BootMain_MapYmodemStatusToError(status));
            }

            printf("[BOOT] YMODEM failed: status=%u err=0x%04X written=%lu\r\n",
                   (unsigned int)status,
                   g_boot_runtime.last_error,
                   (unsigned long)g_boot_runtime.state.written_bytes);
        }

        if (g_boot_runtime.reset_pending != 0u)
        {
            printf("[BOOT] upgrade done, reset to re-enter startup path\r\n");
            HAL_Delay(50);
            HAL_NVIC_SystemReset();
        }
    }
}

void BootMain_Usart3IrqHandler(void)
{
    HAL_UART_IRQHandler(BootProtocol_GetUartHandle(&g_boot_runtime));
}
