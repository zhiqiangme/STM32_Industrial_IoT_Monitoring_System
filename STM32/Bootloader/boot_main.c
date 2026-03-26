#include "boot_main.h"

#include "boot_config.h"
#include "boot_flash.h"
#include "boot_protocol.h"
#include "boot_sha256.h"
#include "boot_verify.h"
#include "bootloader.h"

#include <stdio.h>

static BootloaderRuntime g_boot_runtime = {0};

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

/* 最终校验失败时，把“失败发生在哪一层”打印清楚。
 * 这一步是联调时最关键的诊断入口，所以日志尽量直接暴露期望值与实算值。 */
static void BootMain_LogVerificationFailure(uint16_t error_code,
                                            uint32_t calc_crc32,
                                            const uint8_t calc_sha256[BOOT_SHA256_DIGEST_SIZE])
{
    if (error_code == BOOT_ERR_VERIFY_CRC32)
    {
        printf("[BOOT] verify failed: image CRC32 mismatch expect=0x%08lX calc=0x%08lX\r\n",
               (unsigned long)g_boot_runtime.state.image_crc32,
               (unsigned long)calc_crc32);
        return;
    }

    if (error_code == BOOT_ERR_VERIFY_SHA256)
    {
        char expected_sha256[BOOT_SHA256_HEX_LENGTH] = {0};
        char actual_sha256[BOOT_SHA256_HEX_LENGTH] = {0};

        BootSha256_FormatHex(g_boot_runtime.state.image_sha256, expected_sha256);
        BootSha256_FormatHex(calc_sha256, actual_sha256);
        printf("[BOOT] verify failed: image SHA256 mismatch\r\n");
        printf("[BOOT]   expect=%s\r\n", expected_sha256);
        printf("[BOOT]   calc  =%s\r\n", actual_sha256);
        return;
    }

    if (error_code == BOOT_ERR_VECTOR_INVALID)
    {
        printf("[BOOT] verify failed: app vector table invalid\r\n");
        return;
    }

    if (error_code == BOOT_ERR_BAD_SIZE)
    {
        printf("[BOOT] verify failed: image size invalid (%lu)\r\n",
               (unsigned long)g_boot_runtime.state.image_size);
        return;
    }

    printf("[BOOT] verify failed: err=0x%04X\r\n", error_code);
}

/* 一次完整 YMODEM 会话写完后的收尾逻辑。
 * 顺序固定为：
 * 1. 检查是否收满
 * 2. 状态切到 VERIFYING 并持久化
 * 3. 计算整包 CRC32 / SHA-256
 * 4. 结合向量表一起做最终放行
 * 5. 通过后写 DONE，等待复位重新走启动路径 */
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

    /* CRC32 和 SHA-256 都在 App Flash 上直接计算，避免大镜像搬到 RAM。 */
    calc_crc32 = BootVerify_CalculateProgrammedImageCrc32(&g_boot_runtime);
    BootVerify_CalculateProgrammedImageSha256(&g_boot_runtime, calc_sha256);

    printf("[BOOT] verify CRC32: expect=0x%08lX calc=0x%08lX\r\n",
           (unsigned long)g_boot_runtime.state.image_crc32,
           (unsigned long)calc_crc32);
    if (BootVerify_HasExpectedSha256(&g_boot_runtime) != 0u)
    {
        printf("[BOOT] verify SHA256: calculated, checking against state page\r\n");
    }
    else
    {
        printf("[BOOT] verify SHA256: missing in state page, fallback to legacy CRC32 path\r\n");
    }

    verify_error = BootVerify_ValidateProgrammedImage(&g_boot_runtime, calc_crc32, calc_sha256);
    if (verify_error != BOOT_ERR_NONE)
    {
        BootMain_LogVerificationFailure(verify_error, calc_crc32, calc_sha256);
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

uint8_t BootMain_ShouldStayInLoader(void)
{
    uint8_t load_status = BootFlash_LoadState(&g_boot_runtime);

    if (load_status == 0u)
    {
        if (BootFlash_IsUpgradeActiveState(g_boot_runtime.state.state) != 0u)
        {
            /* 只要状态页还处于“升级进行中”，默认就继续留在 Bootloader。
             * 这样中途掉电、掉线、工具崩溃后都能重新发完整镜像恢复。 */
            uint8_t app_valid = Upgrade_IsAppVectorValid(UPGRADE_APP_BASE_ADDR);

            if (g_boot_runtime.state.active_boot_count < 0xFFFFu)
            {
                g_boot_runtime.state.active_boot_count++;
                Upgrade_SaveState(&g_boot_runtime.state);
            }

            /* 如果连续多次上电都卡在升级态，且旧 App 仍然有效，
             * 就把这次升级判成超时失败，让设备自动回退到 App。 */
            if (app_valid != 0u &&
                g_boot_runtime.state.active_boot_count >= UPGRADE_ACTIVE_STATE_BOOT_LIMIT)
            {
                g_boot_runtime.state.state = UPGRADE_STATE_FAILED;
                g_boot_runtime.state.error_code = BOOT_ERR_TIMEOUT_RECOVERY;
                g_boot_runtime.state.active_boot_count = 0u;
                Upgrade_SaveState(&g_boot_runtime.state);
                return 0u;
            }

            return 1u;
        }
        if (g_boot_runtime.state.state == UPGRADE_STATE_DONE)
        {
            /* DONE 不是“永远信任”。
             * 当前阶段的策略是：只要状态页显示 DONE，每次上电都重新做一遍
             * CRC32 + SHA256 + 向量表复核，确认 Flash 中的 App 没有被破坏。 */
            uint32_t calc_crc32 = 0u;
            uint16_t verify_error;
            uint8_t calc_sha256[BOOT_SHA256_DIGEST_SIZE] = {0};

            verify_error = BootVerify_ValidateStoredImage(&g_boot_runtime, &calc_crc32, calc_sha256);
            if (verify_error != BOOT_ERR_NONE)
            {
                g_boot_runtime.state.state = UPGRADE_STATE_FAILED;
                g_boot_runtime.state.error_code = verify_error;
                g_boot_runtime.state.active_boot_count = 0u;
                Upgrade_SaveState(&g_boot_runtime.state);
                BootMain_LogVerificationFailure(verify_error, calc_crc32, calc_sha256);
                return 1u;
            }

            if (BootVerify_HasExpectedSha256(&g_boot_runtime) != 0u)
            {
                printf("[BOOT] startup recheck: CRC32 + SHA256 + vector OK\r\n");
            }
            else
            {
                printf("[BOOT] startup recheck: legacy CRC32 + vector OK (no SHA256 in state page)\r\n");
            }
        }
    }
    else
    {
        BootFlash_InitState(&g_boot_runtime);
        BootMain_LogStateLoadIssue(load_status);
    }

    return (Upgrade_IsAppVectorValid(UPGRADE_APP_BASE_ADDR) == 0u) ? 1u : 0u;
}

void BootMain_Run(void)
{
    YmodemReceiveResult result;
    COM_StatusTypeDef status;
    uint8_t load_status;

    BootProtocol_PrintBanner();
    BootProtocol_InitUart(&g_boot_runtime);

    load_status = BootFlash_LoadState(&g_boot_runtime);
    if (load_status == 0u)
    {
        BootProtocol_PrintState(&g_boot_runtime);
    }

    /* Bootloader 启动后总是先执行自己的启动决策：
     * - 状态页无升级请求且 App 有效：直接跳 App
     * - 状态页显示升级进行中 / DONE 复核失败 / App 无效：继续留在 Bootloader */
    if (BootMain_ShouldStayInLoader() == 0u)
    {
        printf("[BOOT] valid app found, jump now\r\n");
        HAL_Delay(10);
        Upgrade_JumpToApplication(UPGRADE_APP_BASE_ADDR);
    }

    BootProtocol_PrintWaitingMessage();

    while (1)
    {
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
                printf("[BOOT] YMODEM done: size=%lu crc32=0x%08lX\r\n",
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
