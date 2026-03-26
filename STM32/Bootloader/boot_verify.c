#include "boot_verify.h"

#include "boot_config.h"

#include <string.h>

/* 统一封装“整包 CRC32 + 可选 SHA256 + 向量表”三层放行判断。
 * 无论是刚升级完的最终校验，还是下次上电时的启动前复核，都走这一个入口，
 * 这样两条路径的放行标准就不会漂移。 */
static uint16_t BootVerify_ValidateDigestsAndVector(const BootloaderRuntime *runtime,
                                                    uint32_t calc_crc32,
                                                    const uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    if (runtime == NULL)
    {
        return BOOT_ERR_VERIFY;
    }

    if (runtime->state.image_size == 0u || runtime->state.image_size > UPGRADE_APP_MAX_SIZE)
    {
        return BOOT_ERR_BAD_SIZE;
    }

    if (calc_crc32 != runtime->state.image_crc32)
    {
        return BOOT_ERR_VERIFY_CRC32;
    }

    /* 只有状态页里确实存在期望 SHA-256 时，才把它纳入硬校验。
     * 这给旧版无 SHA-256 状态页保留了兼容空间。 */
    if (BootVerify_HasExpectedSha256(runtime) != 0u)
    {
        if ((digest == NULL) ||
            (memcmp(digest, runtime->state.image_sha256, BOOT_SHA256_DIGEST_SIZE) != 0))
        {
            return BOOT_ERR_VERIFY_SHA256;
        }
    }

    if (Upgrade_IsAppVectorValid(UPGRADE_APP_BASE_ADDR) == 0u)
    {
        return BOOT_ERR_VECTOR_INVALID;
    }

    return BOOT_ERR_NONE;
}

uint8_t BootVerify_IsReceivedImageComplete(const BootloaderRuntime *runtime,
                                           const YmodemReceiveResult *result)
{
    if (runtime == NULL || result == NULL)
    {
        return 0u;
    }

    if (runtime->state.state != UPGRADE_STATE_PROGRAMMING)
    {
        return 0u;
    }

    if (runtime->state.written_bytes != runtime->state.image_size)
    {
        return 0u;
    }

    /* written_bytes 代表 Flash 写入进度，bytes_received 代表 YMODEM 接收进度。
     * 两个值都对上，才说明“收到了”和“写进去了”这两件事同时成立。 */
    return (result->bytes_received == result->file_size) ? 1u : 0u;
}

uint32_t BootVerify_CalculateProgrammedImageCrc32(const BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return 0u;
    }

    return Upgrade_CRC32_CalculateFlash(UPGRADE_APP_BASE_ADDR, runtime->state.image_size);
}

void BootVerify_CalculateProgrammedImageSha256(const BootloaderRuntime *runtime,
                                               uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    if (runtime == NULL)
    {
        return;
    }

    /* 直接在 Flash 上流式计算哈希，避免整包拷贝到 RAM。 */
    BootSha256_CalculateFlash(UPGRADE_APP_BASE_ADDR, runtime->state.image_size, digest);
}

uint8_t BootVerify_HasExpectedSha256(const BootloaderRuntime *runtime)
{
    if (runtime == NULL)
    {
        return 0u;
    }

    return Upgrade_StateHasImageSha256(&runtime->state);
}

uint16_t BootVerify_ValidateProgrammedImage(const BootloaderRuntime *runtime,
                                            uint32_t calc_crc32,
                                            const uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    return BootVerify_ValidateDigestsAndVector(runtime, calc_crc32, digest);
}

uint16_t BootVerify_ValidateStoredImage(const BootloaderRuntime *runtime,
                                        uint32_t *calc_crc32,
                                        uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    uint32_t local_crc32;

    if (runtime == NULL)
    {
        return BOOT_ERR_VERIFY;
    }

    /* 启动前复核时，先重算当前 Flash 中的整包摘要，再跟状态页里的期望值比对。 */
    local_crc32 = BootVerify_CalculateProgrammedImageCrc32(runtime);
    if (calc_crc32 != NULL)
    {
        *calc_crc32 = local_crc32;
    }

    if (digest != NULL)
    {
        BootVerify_CalculateProgrammedImageSha256(runtime, digest);
    }

    return BootVerify_ValidateDigestsAndVector(runtime, local_crc32, digest);
}
