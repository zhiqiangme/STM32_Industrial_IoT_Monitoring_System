#ifndef __BOOT_VERIFY_H
#define __BOOT_VERIFY_H

#include "boot_sha256.h"
#include "bootloader.h"
#include "ymodem.h"

#include <stdint.h>

uint8_t BootVerify_IsReceivedImageComplete(const BootloaderRuntime *runtime,
                                          const YmodemReceiveResult *result);
uint32_t BootVerify_CalculateProgrammedImageCrc32(const BootloaderRuntime *runtime);
void BootVerify_CalculateProgrammedImageSha256(const BootloaderRuntime *runtime,
                                               uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);
uint8_t BootVerify_HasExpectedSha256(const BootloaderRuntime *runtime);
uint16_t BootVerify_ValidateProgrammedImage(const BootloaderRuntime *runtime,
                                            uint32_t calc_crc32,
                                            const uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);
uint16_t BootVerify_ValidateStoredSlot(uint16_t slot,
                                       const UpgradeSlotRecord *record,
                                       uint32_t *calc_crc32,
                                       uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);

#endif
