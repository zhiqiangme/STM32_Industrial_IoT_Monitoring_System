#include "boot_sha256.h"

#include <string.h>

/* 这是项目内嵌的轻量 SHA-256 实现。
 * 设计目标不是通用密码库，而是：
 * - 体积可控
 * - 不依赖 malloc
 * - 能直接对 STM32 Flash 做流式计算
 * - 足够支撑 Bootloader 的整包完整性校验 */
static const uint32_t k_boot_sha256_round_constants[64] =
{
    0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
    0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
    0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
    0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
    0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
    0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
    0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
    0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
    0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
    0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
    0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
    0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
    0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
    0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
    0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
    0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
};

static uint32_t BootSha256_RotateRight(uint32_t value, uint32_t shift)
{
    return (value >> shift) | (value << (32u - shift));
}

static uint32_t BootSha256_Choose(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (~x & z);
}

static uint32_t BootSha256_Majority(uint32_t x, uint32_t y, uint32_t z)
{
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t BootSha256_Sigma0(uint32_t value)
{
    return BootSha256_RotateRight(value, 2u) ^
           BootSha256_RotateRight(value, 13u) ^
           BootSha256_RotateRight(value, 22u);
}

static uint32_t BootSha256_Sigma1(uint32_t value)
{
    return BootSha256_RotateRight(value, 6u) ^
           BootSha256_RotateRight(value, 11u) ^
           BootSha256_RotateRight(value, 25u);
}

static uint32_t BootSha256_Gamma0(uint32_t value)
{
    return BootSha256_RotateRight(value, 7u) ^
           BootSha256_RotateRight(value, 18u) ^
           (value >> 3u);
}

static uint32_t BootSha256_Gamma1(uint32_t value)
{
    return BootSha256_RotateRight(value, 17u) ^
           BootSha256_RotateRight(value, 19u) ^
           (value >> 10u);
}

static void BootSha256_ProcessBlock(BootSha256Context *context, const uint8_t block[BOOT_SHA256_BLOCK_SIZE])
{
    uint32_t schedule[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;

    /* SHA-256 先把 64 字节输入块拆成 16 个大端 32bit 字，再扩展成 64 轮消息调度表。 */
    for (uint32_t i = 0u; i < 16u; i++)
    {
        schedule[i] = ((uint32_t)block[i * 4u] << 24) |
                      ((uint32_t)block[i * 4u + 1u] << 16) |
                      ((uint32_t)block[i * 4u + 2u] << 8) |
                      ((uint32_t)block[i * 4u + 3u]);
    }

    for (uint32_t i = 16u; i < 64u; i++)
    {
        schedule[i] = BootSha256_Gamma1(schedule[i - 2u]) +
                      schedule[i - 7u] +
                      BootSha256_Gamma0(schedule[i - 15u]) +
                      schedule[i - 16u];
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (uint32_t i = 0u; i < 64u; i++)
    {
        uint32_t temp1 = h + BootSha256_Sigma1(e) + BootSha256_Choose(e, f, g) +
                         k_boot_sha256_round_constants[i] + schedule[i];
        uint32_t temp2 = BootSha256_Sigma0(a) + BootSha256_Majority(a, b, c);

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void BootSha256_Init(BootSha256Context *context)
{
    if (context == NULL)
    {
        return;
    }

    memset(context, 0, sizeof(*context));
    context->state[0] = 0x6A09E667UL;
    context->state[1] = 0xBB67AE85UL;
    context->state[2] = 0x3C6EF372UL;
    context->state[3] = 0xA54FF53AUL;
    context->state[4] = 0x510E527FUL;
    context->state[5] = 0x9B05688CUL;
    context->state[6] = 0x1F83D9ABUL;
    context->state[7] = 0x5BE0CD19UL;
}

void BootSha256_Update(BootSha256Context *context, const uint8_t *data, uint32_t len)
{
    if ((context == NULL) || (data == NULL) || (len == 0u))
    {
        return;
    }

    context->total_len += len;

    while (len > 0u)
    {
        uint32_t copy_len = BOOT_SHA256_BLOCK_SIZE - context->block_len;

        if (copy_len > len)
        {
            copy_len = len;
        }

        memcpy(&context->block[context->block_len], data, copy_len);
        context->block_len += copy_len;
        data += copy_len;
        len -= copy_len;

        /* 只要暂存块满 64 字节，就立刻做一轮压缩。
         * 这样可以边读 Flash 边算，不要求调用方一次性提供整包数据。 */
        if (context->block_len == BOOT_SHA256_BLOCK_SIZE)
        {
            BootSha256_ProcessBlock(context, context->block);
            context->block_len = 0u;
        }
    }
}

void BootSha256_Final(BootSha256Context *context, uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    uint64_t total_bits;

    if ((context == NULL) || (digest == NULL))
    {
        return;
    }

    /* SHA-256 的收尾步骤：
     * 1. 追加 0x80
     * 2. 补 0 直到末尾只剩 8 字节
     * 3. 在最后 8 字节写入原始消息长度（bit） */
    total_bits = context->total_len * 8u;
    context->block[context->block_len++] = 0x80u;

    if (context->block_len > 56u)
    {
        while (context->block_len < BOOT_SHA256_BLOCK_SIZE)
        {
            context->block[context->block_len++] = 0u;
        }

        BootSha256_ProcessBlock(context, context->block);
        context->block_len = 0u;
    }

    while (context->block_len < 56u)
    {
        context->block[context->block_len++] = 0u;
    }

    for (uint32_t i = 0u; i < 8u; i++)
    {
        context->block[56u + i] = (uint8_t)(total_bits >> (56u - i * 8u));
    }

    BootSha256_ProcessBlock(context, context->block);

    for (uint32_t i = 0u; i < 8u; i++)
    {
        digest[i * 4u] = (uint8_t)(context->state[i] >> 24);
        digest[i * 4u + 1u] = (uint8_t)(context->state[i] >> 16);
        digest[i * 4u + 2u] = (uint8_t)(context->state[i] >> 8);
        digest[i * 4u + 3u] = (uint8_t)(context->state[i]);
    }
}

void BootSha256_Calculate(const uint8_t *data, uint32_t len, uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    BootSha256Context context;

    BootSha256_Init(&context);
    BootSha256_Update(&context, data, len);
    BootSha256_Final(&context, digest);
}

void BootSha256_CalculateFlash(uint32_t address, uint32_t len, uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    BootSha256Context context;
    const uint8_t *flash_ptr = (const uint8_t *)address;

    BootSha256_Init(&context);
    /* 分块从 Flash 读取并累计到哈希上下文中。
     * 这里的 256 字节只是实现上的折中值，既不占太多栈，也不会让函数调用过于频繁。 */
    while (len > 0u)
    {
        uint32_t chunk_len = (len > 256u) ? 256u : len;

        BootSha256_Update(&context, flash_ptr, chunk_len);
        flash_ptr += chunk_len;
        len -= chunk_len;
    }
    BootSha256_Final(&context, digest);
}

void BootSha256_FormatHex(const uint8_t digest[BOOT_SHA256_DIGEST_SIZE], char output[BOOT_SHA256_HEX_LENGTH])
{
    static const char k_hex_digits[] = "0123456789abcdef";

    if ((digest == NULL) || (output == NULL))
    {
        return;
    }

    /* 现场日志里更容易看十六进制字符串，所以提供一个小格式化助手。 */
    for (uint32_t i = 0u; i < BOOT_SHA256_DIGEST_SIZE; i++)
    {
        output[i * 2u] = k_hex_digits[(digest[i] >> 4) & 0x0Fu];
        output[i * 2u + 1u] = k_hex_digits[digest[i] & 0x0Fu];
    }

    output[BOOT_SHA256_HEX_LENGTH - 1u] = '\0';
}
