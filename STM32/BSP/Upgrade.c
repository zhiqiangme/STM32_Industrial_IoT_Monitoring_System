#include "Upgrade.h"

#include <stddef.h>
#include <string.h>

/* v1 状态页没有 SHA-256 字段。
 * 保留这个旧结构体是为了让升级后的 Bootloader 仍能读取历史状态页，
 * 避免设备一升级 Bootloader 就把旧状态页判成完全损坏。 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint16_t state;
    uint16_t request_source;
    uint32_t target_fw_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t written_bytes;
    uint32_t last_ok_offset;
    uint16_t error_code;
    uint16_t active_boot_count;
    uint32_t reserved;
    uint16_t crc16;
} UpgradeStateImageLegacy;

static uint16_t Upgrade_StatePayloadSize(void)
{
    /* payload_size 只覆盖真正参与版本演进的负载区，不包含 magic/version/crc16。
     * 这样状态页可以在不重排整个结构体头尾的情况下做兼容判断。 */
    return (uint16_t)(offsetof(UpgradeStateImage, crc16) -
                      offsetof(UpgradeStateImage, state));
}

static uint16_t Upgrade_StatePayloadSizeLegacy(void)
{
    return (uint16_t)(offsetof(UpgradeStateImageLegacy, crc16) -
                      offsetof(UpgradeStateImageLegacy, state));
}

/* Bootloader 协议帧和状态页都复用 Modbus 风格 CRC16，低字节在前。 */
uint16_t Upgrade_CRC16(const uint8_t *buf, uint16_t len)
{
    uint16_t crc = 0xFFFFu;

    if (buf == NULL)
    {
        return 0u;
    }

    for (uint16_t pos = 0; pos < len; pos++)
    {
        crc ^= (uint16_t)buf[pos];
        for (uint8_t i = 0; i < 8u; i++)
        {
            if ((crc & 0x0001u) != 0u)
            {
                crc >>= 1;
                crc ^= 0xA001u;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc;
}

/* 初始化一份默认状态页镜像。调用方再按需要覆写 state/image_size 等字段。 */
void Upgrade_InitStateImage(UpgradeStateImage *image)
{
    if (image == NULL)
    {
        return;
    }

    /* 先按 Flash 擦除态填满 0xFF，再覆写真正有意义的字段。
     * 这样状态页镜像在 RAM 中的布局更接近最终写入到 Flash 的内容。 */
    memset(image, 0xFF, sizeof(*image));
    image->magic = UPGRADE_STATE_MAGIC;
    image->version = UPGRADE_STATE_VERSION;
    image->payload_size = Upgrade_StatePayloadSize();
    image->state = UPGRADE_STATE_IDLE;
    image->request_source = UPGRADE_REQUEST_SOURCE_NONE;
    image->target_fw_version = 0u;
    image->image_size = 0u;
    image->image_crc32 = 0u;
    /* SHA-256 缺省置 0，表示当前状态页还没有“期望哈希”。
     * 旧镜像或兼容路径看到全 0 时，会退回到 legacy 的 CRC32 校验。 */
    memset(image->image_sha256, 0, sizeof(image->image_sha256));
    image->written_bytes = 0u;
    image->last_ok_offset = 0u;
    image->error_code = 0u;
    image->active_boot_count = 0u;
    image->reserved = 0u;
    image->crc16 = Upgrade_CRC16((const uint8_t *)image,
                                 (uint16_t)offsetof(UpgradeStateImage, crc16));
}

/* 状态页固定占用最后预留的 1 页 Flash，每次保存都整页擦写。 */
static uint8_t Upgrade_WriteStateImage(const UpgradeStateImage *image)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0;
    /* STM32F1 Flash 以半字编程，这里直接把状态页镜像按 16bit 顺序写入。 */
    const uint16_t *halfwords = (const uint16_t *)image;
    uint32_t halfword_count = sizeof(*image) / 2u;
    const UpgradeStateImage *flash_image = (const UpgradeStateImage *)UPGRADE_STATE_PAGE_ADDR;

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = UPGRADE_STATE_PAGE_ADDR;
    erase.NbPages = 1;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 1u;
    }

    for (uint32_t i = 0; i < halfword_count; i++)
    {
        uint32_t address = UPGRADE_STATE_PAGE_ADDR + i * 2u;
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address, halfwords[i]) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 2u;
        }
    }

    HAL_FLASH_Lock();

    /* 写完再整结构体回读一次，确保状态页真的落盘成功。 */
    return (memcmp(flash_image, image, sizeof(*image)) == 0) ? 0u : 3u;
}

/* 这里使用标准 CRC32(Poly 0xEDB88320)，供整包镜像完整性校验。 */
uint32_t Upgrade_CRC32_Calculate(const uint8_t *buf, uint32_t len, uint32_t seed)
{
    uint32_t crc = ~seed;

    if (buf == NULL)
    {
        return ~crc;
    }

    for (uint32_t i = 0; i < len; i++)
    {
        crc ^= buf[i];
        for (uint32_t j = 0; j < 8u; j++)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (crc >> 1) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return ~crc;
}

/* 直接对 App Flash 区做 CRC32，避免把整包搬到 RAM。 */
uint32_t Upgrade_CRC32_CalculateFlash(uint32_t address, uint32_t len)
{
    uint32_t crc = 0u;
    const uint8_t *ptr = (const uint8_t *)address;

    return Upgrade_CRC32_Calculate(ptr, len, crc);
}

/* 从状态页加载升级状态；如果整页为空，则返回一份默认 IDLE 镜像。 */
uint8_t Upgrade_LoadState(UpgradeStateImage *image)
{
    const UpgradeStateImage *flash_image = (const UpgradeStateImage *)UPGRADE_STATE_PAGE_ADDR;
    uint16_t crc_calc;

    if (image == NULL)
    {
        return 1u;
    }

    /* 整页擦除态说明状态页从未写过，此时返回默认 IDLE 镜像，不视为硬错误。 */
    if (flash_image->magic == 0xFFFFFFFFUL && flash_image->version == 0xFFFFu)
    {
        Upgrade_InitStateImage(image);
        return 2u;
    }

    if (flash_image->magic != UPGRADE_STATE_MAGIC)
    {
        return 3u;
    }

    /* 优先按当前结构体版本读取。
     * 只要 payload_size 和 CRC16 对得上，就直接把整页镜像搬进 RAM。 */
    if (flash_image->version == UPGRADE_STATE_VERSION &&
        flash_image->payload_size == Upgrade_StatePayloadSize())
    {
        crc_calc = Upgrade_CRC16((const uint8_t *)flash_image,
                                 (uint16_t)offsetof(UpgradeStateImage, crc16));
        if (crc_calc != flash_image->crc16)
        {
            return 4u;
        }

        *image = *flash_image;
        return 0u;
    }

    /* 如果是旧版状态页，则把旧字段逐项迁移到新版结构体。
     * 新增的 SHA-256 字段会保持为默认 0，后续逻辑据此走兼容分支。 */
    if (flash_image->version == UPGRADE_STATE_VERSION_LEGACY &&
        flash_image->payload_size == Upgrade_StatePayloadSizeLegacy())
    {
        const UpgradeStateImageLegacy *legacy_image = (const UpgradeStateImageLegacy *)UPGRADE_STATE_PAGE_ADDR;

        crc_calc = Upgrade_CRC16((const uint8_t *)legacy_image,
                                 (uint16_t)offsetof(UpgradeStateImageLegacy, crc16));
        if (crc_calc != legacy_image->crc16)
        {
            return 4u;
        }

        Upgrade_InitStateImage(image);
        image->state = legacy_image->state;
        image->request_source = legacy_image->request_source;
        image->target_fw_version = legacy_image->target_fw_version;
        image->image_size = legacy_image->image_size;
        image->image_crc32 = legacy_image->image_crc32;
        image->written_bytes = legacy_image->written_bytes;
        image->last_ok_offset = legacy_image->last_ok_offset;
        image->error_code = legacy_image->error_code;
        image->active_boot_count = legacy_image->active_boot_count;
        image->reserved = legacy_image->reserved;
        return 0u;
    }

    return 3u;
}

/* 保存状态页前统一重算头字段和 CRC，避免调用方漏填。 */
uint8_t Upgrade_SaveState(const UpgradeStateImage *image)
{
    UpgradeStateImage copy;

    if (image == NULL)
    {
        return 1u;
    }

    copy = *image;
    copy.magic = UPGRADE_STATE_MAGIC;
    copy.version = UPGRADE_STATE_VERSION;
    copy.payload_size = Upgrade_StatePayloadSize();
    copy.crc16 = Upgrade_CRC16((const uint8_t *)&copy,
                               (uint16_t)offsetof(UpgradeStateImage, crc16));

    return Upgrade_WriteStateImage(&copy);
}

/* App 侧请求进入 Bootloader 时调用：写 REQUESTED 状态并记录来源。 */
uint8_t Upgrade_RequestBootMode(uint16_t request_source, uint32_t target_fw_version)
{
    UpgradeStateImage image;

    Upgrade_InitStateImage(&image);
    image.state = UPGRADE_STATE_REQUESTED;
    image.request_source = request_source;
    image.target_fw_version = target_fw_version;

    return Upgrade_SaveState(&image);
}

/* 清空升级状态页，等价于把状态恢复为 IDLE。 */
uint8_t Upgrade_ClearState(void)
{
    UpgradeStateImage image;

    Upgrade_InitStateImage(&image);
    return Upgrade_SaveState(&image);
}

uint8_t Upgrade_StateHasImageSha256(const UpgradeStateImage *image)
{
    if (image == NULL)
    {
        return 0u;
    }

    /* 当前实现把“不是全 0”视为状态页中已存在期望 SHA-256。
     * 这样不用再额外增加一个标志位，兼容成本最低。 */
    for (uint32_t i = 0u; i < UPGRADE_IMAGE_SHA256_SIZE; i++)
    {
        if (image->image_sha256[i] != 0u)
        {
            return 1u;
        }
    }

    return 0u;
}

/* 只做最小有效性检查：栈顶必须落在 SRAM，复位向量必须落在 App 区内。 */
uint8_t Upgrade_IsAppVectorValid(uint32_t app_base_addr)
{
    uint32_t app_stack = *(__IO uint32_t *)app_base_addr;
    uint32_t app_reset = *(__IO uint32_t *)(app_base_addr + 4u);

    if ((app_stack & 0x2FFE0000u) != 0x20000000u)
    {
        return 0u;
    }
    if (app_reset < app_base_addr || app_reset >= (app_base_addr + UPGRADE_APP_MAX_SIZE))
    {
        return 0u;
    }

    return 1u;
}

/* 擦除 App 区前半部分时，只按镜像大小计算需要擦多少页，不碰参数页与诊断页。 */
uint8_t Upgrade_EraseAppRegion(uint32_t image_size)
{
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t page_error = 0u;
    uint32_t erase_size = image_size;
    uint32_t page_count;

    if (erase_size == 0u || erase_size > UPGRADE_APP_MAX_SIZE)
    {
        return 1u;
    }

    page_count = (erase_size + 2047u) / 2048u;

    HAL_FLASH_Unlock();

    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = UPGRADE_APP_BASE_ADDR;
    erase.NbPages = page_count;

    if (HAL_FLASHEx_Erase(&erase, &page_error) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return 2u;
    }

    HAL_FLASH_Lock();
    return 0u;
}

/* 以半字为单位写入 App 数据，并在写后逐字节回读确认。 */
uint8_t Upgrade_ProgramBytes(uint32_t address, const uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint16_t halfword;

    if (data == NULL || len == 0u)
    {
        return 1u;
    }
    if (address < UPGRADE_APP_BASE_ADDR ||
        (address + len) > (UPGRADE_APP_BASE_ADDR + UPGRADE_APP_MAX_SIZE))
    {
        return 2u;
    }

    HAL_FLASH_Unlock();

    for (i = 0u; i < len; i += 2u)
    {
        halfword = data[i];
        if ((i + 1u) < len)
        {
            halfword |= (uint16_t)((uint16_t)data[i + 1u] << 8);
        }
        else
        {
            halfword |= 0xFF00u;
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, address + i, halfword) != HAL_OK)
        {
            HAL_FLASH_Lock();
            return 3u;
        }
    }

    HAL_FLASH_Lock();

    for (i = 0u; i < len; i++)
    {
        if (*(__IO uint8_t *)(address + i) != data[i])
        {
            return 4u;
        }
    }

    return 0u;
}

/* 从 Bootloader 跳转到 App 的标准流程：
 * 1. 关闭 SysTick 与中断
 * 2. 反初始化 HAL / RCC
 * 3. 切换 VTOR 与 MSP
 * 4. 重新开中断后进入 App ResetHandler */
void Upgrade_JumpToApplication(uint32_t app_base_addr)
{
    uint32_t app_stack = *(__IO uint32_t *)app_base_addr;
    uint32_t app_reset = *(__IO uint32_t *)(app_base_addr + 4u);
    void (*app_entry)(void) = (void (*)(void))app_reset;

    __disable_irq();

    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL = 0u;

    for (uint32_t i = 0u; i < 8u; i++)
    {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }

    HAL_RCC_DeInit();
    HAL_DeInit();

    SCB->VTOR = app_base_addr;
    __set_MSP(app_stack);
    __DSB();
    __ISB();
    __enable_irq();

    app_entry();

    while (1)
    {
    }
}
