#ifndef __UPGRADE_H
#define __UPGRADE_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define UPGRADE_BOOT_BASE_ADDR         0x08000000UL
#define UPGRADE_APP_BASE_ADDR          0x08008000UL
#define UPGRADE_APP_MAX_SIZE           0x00076800UL
#define UPGRADE_STATE_PAGE_ADDR        0x0807E800UL
#define UPGRADE_DIAG_PAGE_ADDR         0x0807F000UL
#define UPGRADE_CFG_PAGE_ADDR          0x0807F800UL

#define UPGRADE_STATE_MAGIC            0x55504753UL  /* UPGS */
#define UPGRADE_STATE_VERSION          0x0001u

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
    uint32_t target_fw_version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t written_bytes;
    uint32_t last_ok_offset;
    uint16_t error_code;
    /* 记录设备在“升级进行中”状态下已经连续启动了多少次，用于超时恢复。 */
    uint16_t active_boot_count;
    uint32_t reserved;
    uint16_t crc16;
} UpgradeStateImage;

/* 以下接口由 App 与 Bootloader 共用：
 * - App 用它写升级请求和保存升级状态
 * - Bootloader 用它校验、擦写 App 和完成跳转 */
uint16_t Upgrade_CRC16(const uint8_t *buf, uint16_t len);
void Upgrade_InitStateImage(UpgradeStateImage *image);
uint8_t Upgrade_LoadState(UpgradeStateImage *image);
uint8_t Upgrade_SaveState(const UpgradeStateImage *image);
uint8_t Upgrade_RequestBootMode(uint16_t request_source, uint32_t target_fw_version);
uint8_t Upgrade_ClearState(void);
uint8_t Upgrade_IsAppVectorValid(uint32_t app_base_addr);
uint32_t Upgrade_CRC32_Calculate(const uint8_t *buf, uint32_t len, uint32_t seed);
uint32_t Upgrade_CRC32_CalculateFlash(uint32_t address, uint32_t len);
uint8_t Upgrade_EraseAppRegion(uint32_t image_size);
uint8_t Upgrade_ProgramBytes(uint32_t address, const uint8_t *data, uint32_t len);
void Upgrade_JumpToApplication(uint32_t app_base_addr);

#endif
