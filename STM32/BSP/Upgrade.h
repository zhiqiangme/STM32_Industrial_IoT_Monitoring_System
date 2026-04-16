#ifndef __UPGRADE_H
#define __UPGRADE_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define UPGRADE_BOOT_BASE_ADDR         0x08000000UL
#define UPGRADE_BOOT_MAX_SIZE          0x00008000UL
#define UPGRADE_FLASH_PAGE_SIZE        0x00000800UL

#define UPGRADE_SLOT_A                 0x0000u
#define UPGRADE_SLOT_B                 0x0001u
#define UPGRADE_SLOT_NONE              0xFFFFu
#define UPGRADE_SLOT_COUNT             2u

#define UPGRADE_SLOT_A_BASE_ADDR       0x08008000UL
#define UPGRADE_SLOT_B_BASE_ADDR       0x08043000UL
#define UPGRADE_SLOT_MAX_SIZE          0x0003B000UL

#define UPGRADE_BOOTCTRL_PAGE_ADDR     0x0807E000UL
#define UPGRADE_STATE_PAGE_ADDR        0x0807E800UL
#define UPGRADE_DIAG_PAGE_ADDR         0x0807F000UL
#define UPGRADE_CFG_PAGE_ADDR          0x0807F800UL

/* 兼容旧的单槽命名，历史接口默认映射到 A 槽。 */
#define UPGRADE_APP_BASE_ADDR          UPGRADE_SLOT_A_BASE_ADDR
#define UPGRADE_APP_MAX_SIZE           UPGRADE_SLOT_MAX_SIZE

#define UPGRADE_STATE_MAGIC            0x55504753UL  /* UPGS */
#define UPGRADE_STATE_VERSION          0x0002u
#define UPGRADE_STATE_VERSION_LEGACY   0x0001u
#define UPGRADE_IMAGE_SHA256_SIZE      32u

#define UPGRADE_BOOTCTRL_MAGIC         0x55424743UL  /* UBGC */
#define UPGRADE_BOOTCTRL_VERSION       0x0002u
#define UPGRADE_BOOTCTRL_VERSION_LEGACY 0x0001u

#define UPGRADE_SLOT_FLAG_PRESENT      0x0001u

#define UPGRADE_STATE_IDLE             0x0000u
#define UPGRADE_STATE_REQUESTED        0x0001u
#define UPGRADE_STATE_ERASING          0x0002u
#define UPGRADE_STATE_PROGRAMMING      0x0003u
#define UPGRADE_STATE_VERIFYING        0x0004u
#define UPGRADE_STATE_DONE             0x0005u
#define UPGRADE_STATE_FAILED           0x0006u

/* 当设备连续多次上电仍停留在“升级进行中”状态，且当前 App 仍然有效时，
 * Bootloader 会把本次升级判定为超时失败并回退到 App。 */
#define UPGRADE_ACTIVE_STATE_BOOT_LIMIT  3u
#define UPGRADE_PENDING_BOOT_LIMIT       3u
#define UPGRADE_WDG_RESET_LIMIT          3u

#define UPGRADE_REQUEST_SOURCE_NONE    0x0000u
#define UPGRADE_REQUEST_SOURCE_LOCAL   0x0001u
#define UPGRADE_REQUEST_SOURCE_G780S   0x0002u
#define UPGRADE_REQUEST_SOURCE_REMOTE  0x0003u

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint16_t state;
    uint16_t request_source;
    /* 由 App 或升级工具写入的目标版本号，Bootloader 不解释语义，只负责持久化。 */
    uint32_t target_fw_version;
    /* 以下三项共同描述“这次打算写入什么镜像”，供最终校验和重启后复核复用。 */
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t image_sha256[UPGRADE_IMAGE_SHA256_SIZE];
    /* written_bytes/last_ok_offset 用于掉线恢复和诊断当前写入进度。 */
    uint32_t written_bytes;
    uint32_t last_ok_offset;
    uint16_t error_code;
    /* 记录设备在“升级进行中”状态下已经连续启动了多少次，用于超时恢复。 */
    uint16_t active_boot_count;
    uint32_t reserved;
    uint16_t crc16;
} UpgradeStateImage;

typedef struct
{
    uint32_t target_fw_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t image_sha256[UPGRADE_IMAGE_SHA256_SIZE];
    uint16_t flags;
    uint16_t reserved;
} UpgradeSlotRecord;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t payload_size;
    uint16_t active_slot;
    uint16_t confirmed_slot;
    uint16_t pending_slot;
    uint16_t boot_attempts;
    /* 连续 IWDG 复位计数。达到阈值后，Bootloader 会触发故障恢复策略。 */
    uint16_t watchdog_reset_count;
    UpgradeSlotRecord slots[UPGRADE_SLOT_COUNT];
    uint16_t crc16;
} UpgradeBootControl;

/* 以下接口由 App 与 Bootloader 共用：
 * - App 用它写升级请求和保存升级状态
 * - Bootloader 用它校验、擦写 App 和完成跳转 */
uint16_t Upgrade_CRC16(const uint8_t *buf, uint16_t len);
void Upgrade_InitStateImage(UpgradeStateImage *image);
uint8_t Upgrade_LoadState(UpgradeStateImage *image);
uint8_t Upgrade_SaveState(const UpgradeStateImage *image);
uint8_t Upgrade_RequestBootMode(uint16_t request_source, uint32_t target_fw_version);
uint8_t Upgrade_ClearState(void);
void Upgrade_InitBootControl(UpgradeBootControl *control);
uint8_t Upgrade_LoadBootControl(UpgradeBootControl *control);
uint8_t Upgrade_SaveBootControl(const UpgradeBootControl *control);
/* 判断状态页里是否已经写入了有效的期望 SHA-256。
 * 旧版状态页没有该字段时，这个接口会返回 0，Bootloader 自动走兼容校验路径。 */
uint8_t Upgrade_StateHasImageSha256(const UpgradeStateImage *image);
uint8_t Upgrade_IsSlotIdValid(uint16_t slot);
uint16_t Upgrade_GetOtherSlot(uint16_t slot);
uint32_t Upgrade_GetSlotBaseAddress(uint16_t slot);
uint16_t Upgrade_ResolveSlotFromAddress(uint32_t address);
uint16_t Upgrade_GetRunningSlot(void);
void Upgrade_ClearSlotRecord(UpgradeSlotRecord *record);
void Upgrade_SetSlotRecord(UpgradeSlotRecord *record,
                           uint32_t target_fw_version,
                           uint32_t image_size,
                           uint32_t image_crc32,
                           const uint8_t *image_sha256);
uint8_t Upgrade_IsAppVectorValid(uint32_t app_base_addr);
uint8_t Upgrade_IsSlotVectorValid(uint16_t slot);
uint32_t Upgrade_CRC32_Calculate(const uint8_t *buf, uint32_t len, uint32_t seed);
uint32_t Upgrade_CRC32_CalculateFlash(uint32_t address, uint32_t len);
uint8_t Upgrade_EraseAppRegion(uint32_t image_size);
uint8_t Upgrade_EraseSlotRegion(uint16_t slot, uint32_t image_size);
uint8_t Upgrade_ProgramBytes(uint32_t address, const uint8_t *data, uint32_t len);
uint8_t Upgrade_ProgramSlotBytes(uint16_t slot, uint32_t offset, const uint8_t *data, uint32_t len);
void Upgrade_JumpToApplication(uint32_t app_base_addr);
void Upgrade_JumpToSlot(uint16_t slot);
uint8_t Upgrade_ConfirmRunningSlot(void);

#endif
