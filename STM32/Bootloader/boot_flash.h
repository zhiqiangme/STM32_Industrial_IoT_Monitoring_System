#ifndef __BOOT_FLASH_H
#define __BOOT_FLASH_H

#include "bootloader.h"
#include "ymodem.h"

#include <stdint.h>

uint8_t BootFlash_LoadState(BootloaderRuntime *runtime);
void BootFlash_InitState(BootloaderRuntime *runtime);
uint8_t BootFlash_IsUpgradeActiveState(uint16_t state);
uint16_t BootFlash_ResolveRequestSource(void);
uint32_t BootFlash_ResolveTargetVersion(uint32_t requested_target_fw_version);
void BootFlash_MarkFailed(BootloaderRuntime *runtime, uint16_t error_code);
uint8_t BootFlash_SaveStateThrottled(BootloaderRuntime *runtime, uint8_t force_save);
COM_StatusTypeDef BootFlash_BeginImage(BootloaderRuntime *runtime,
                                       uint32_t file_size,
                                       uint32_t image_crc32,
                                       uint32_t target_fw_version,
                                       const uint8_t *image_sha256);
COM_StatusTypeDef BootFlash_WriteImageData(BootloaderRuntime *runtime,
                                           uint32_t offset,
                                           const uint8_t *data,
                                           uint32_t len);
uint8_t BootFlash_MarkVerifying(BootloaderRuntime *runtime);
uint8_t BootFlash_MarkDone(BootloaderRuntime *runtime, uint32_t image_crc32);

#endif
