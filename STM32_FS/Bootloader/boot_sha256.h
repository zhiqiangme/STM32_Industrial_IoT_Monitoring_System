#ifndef __BOOT_SHA256_H
#define __BOOT_SHA256_H

#include <stdint.h>

#define BOOT_SHA256_BLOCK_SIZE   64u
#define BOOT_SHA256_DIGEST_SIZE  32u
#define BOOT_SHA256_HEX_LENGTH   (BOOT_SHA256_DIGEST_SIZE * 2u + 1u)

typedef struct
{
    uint32_t state[8];
    uint64_t total_len;
    uint32_t block_len;
    uint8_t block[BOOT_SHA256_BLOCK_SIZE];
} BootSha256Context;

void BootSha256_Init(BootSha256Context *context);
void BootSha256_Update(BootSha256Context *context, const uint8_t *data, uint32_t len);
void BootSha256_Final(BootSha256Context *context, uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);
void BootSha256_Calculate(const uint8_t *data, uint32_t len, uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);
void BootSha256_CalculateFlash(uint32_t address, uint32_t len, uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);
void BootSha256_FormatHex(const uint8_t digest[BOOT_SHA256_DIGEST_SIZE], char output[BOOT_SHA256_HEX_LENGTH]);

#endif
