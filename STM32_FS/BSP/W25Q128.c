/**
 ****************************************************************************************************
 * @file        W25Q128.c
 * @brief       W25Q128 SPI NOR Flash 驱动实现
 *
 * 通信协议: SPI Mode 0 (CPOL=0, CPHA=0), MSB 优先, 8 位数据
 * 时钟速率: APB1 (36MHz) 4 分频 = 9MHz (W25Q128 支持最高 104MHz)
 * 片选策略: 软件控制 PB12, 每次事务前拉低、完成后拉高
 ****************************************************************************************************
 */
#include "W25Q128.h"
#include "delay.h"
#include <stdio.h>

/* ======================================================
 *  W25Qxx 指令码定义
 *  参考 W25Q128JV 数据手册 Table 1 (Instruction Set)
 * ====================================================== */
#define CMD_WRITE_ENABLE        0x06  /* 写使能: 每次编程/擦除前必须发送 */
#define CMD_WRITE_DISABLE       0x04  /* 写禁止 */
#define CMD_READ_STATUS1        0x05  /* 读状态寄存器 1 (含 WIP 位) */
#define CMD_READ_STATUS2        0x35  /* 读状态寄存器 2 */
#define CMD_WRITE_STATUS        0x01  /* 写状态寄存器 */
#define CMD_PAGE_PROGRAM        0x02  /* 页编程 (最多 256 字节) */
#define CMD_SECTOR_ERASE        0x20  /* 扇区擦除 (4KB) */
#define CMD_BLOCK32_ERASE       0x52  /* 32KB 块擦除 */
#define CMD_BLOCK64_ERASE       0xD8  /* 64KB 块擦除 */
#define CMD_CHIP_ERASE          0xC7  /* 全片擦除 */
#define CMD_READ_DATA           0x03  /* 标准读数据 (最高 33MHz) */
#define CMD_FAST_READ           0x0B  /* 快速读 (需 1 Dummy 字节, 最高 104MHz) */
#define CMD_JEDEC_ID            0x9F  /* 读 JEDEC ID (3 字节: 厂商+类型+容量) */
#define CMD_POWER_DOWN          0xB9  /* 进入掉电模式 (约 1μA 待机电流) */
#define CMD_RELEASE_POWER_DOWN  0xAB  /* 退出掉电模式并读取设备 ID */

/* 状态寄存器 1 位定义 */
#define STATUS1_WIP_BIT  0x01  /* Write In Progress: 1=正在执行写/擦操作, 需等待 */
#define STATUS1_WEL_BIT  0x02  /* Write Enable Latch: 1=已写使能 */

/* SPI2 句柄, 供本文件内所有函数使用 */
static SPI_HandleTypeDef g_spi2_handle;

static uint8_t W25Q_IsKnownJedec(uint32_t id)
{
    uint8_t manuf = (uint8_t)(id >> 16);
    uint16_t devid = (uint16_t)(id & 0xFFFFU);

    if (id == 0x000000U || id == 0xFFFFFFU)
    {
        return 0u;
    }

    /* 常见 128Mbit SPI NOR: Winbond / GigaDevice。 */
    if ((devid == W25Q_JEDEC_ID_W25Q128) &&
        (manuf == W25Q_JEDEC_MANUF_WB || manuf == 0xC8U))
    {
        return 1u;
    }

    return 0u;
}

/* ======================================================
 *  内部函数: SPI2 外设初始化
 * ====================================================== */

/**
 * @brief  初始化 SPI2 外设及相关 GPIO.
 * @note   引脚分配:
 *           PB13 -> SCK  (复用推挽输出)
 *           PB15 -> MOSI (复用推挽输出)
 *           PB14 -> MISO (浮空输入)
 *           PB12 -> CS   (普通推挽输出, 软件控制)
 */
static void W25Q_SPI_Init(void)
{
    GPIO_InitTypeDef gpio_init = {0};

    /* 使能 GPIOB 和 SPI2 时钟 */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();

    /* PB13 (SCK) 和 PB15 (MOSI): 复用推挽输出, 高速 */
    gpio_init.Pin   = GPIO_PIN_13 | GPIO_PIN_15;
    gpio_init.Mode  = GPIO_MODE_AF_PP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    /* PB14 (MISO): 浮空输入 (由 Flash 驱动数据线) */
    gpio_init.Pin   = GPIO_PIN_14;
    gpio_init.Mode  = GPIO_MODE_INPUT;
    gpio_init.Pull  = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &gpio_init);

    /* PB12 (CS): 普通推挽输出, 上拉, 初始化为高电平 (未选中) */
    gpio_init.Pin   = GPIO_PIN_12;
    gpio_init.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull  = GPIO_PULLUP;
    gpio_init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio_init);
    W25Q_CS_HIGH();  /* 确保 CS 初始为高, 防止上电时误触发 */

    /* 配置 SPI2:
     *   主机模式, 全双工, 8 位, SPI Mode 0 (CPOL=0 CPHA=0)
     *   软件 NSS, 4 分频 (9MHz), MSB 优先, 关闭 CRC */
    g_spi2_handle.Instance               = SPI2;
    g_spi2_handle.Init.Mode              = SPI_MODE_MASTER;
    g_spi2_handle.Init.Direction         = SPI_DIRECTION_2LINES;
    g_spi2_handle.Init.DataSize          = SPI_DATASIZE_8BIT;
    g_spi2_handle.Init.CLKPolarity       = SPI_POLARITY_LOW;   /* CPOL=0: 空闲时 SCK 低电平 */
    g_spi2_handle.Init.CLKPhase          = SPI_PHASE_1EDGE;    /* CPHA=0: 第一个时钟沿采样 */
    g_spi2_handle.Init.NSS               = SPI_NSS_SOFT;       /* 软件片选, 由 CS_LOW/HIGH 控制 */
    g_spi2_handle.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4; /* 36MHz / 4 = 9MHz */
    g_spi2_handle.Init.FirstBit          = SPI_FIRSTBIT_MSB;   /* MSB 优先 */
    g_spi2_handle.Init.TIMode            = SPI_TIMODE_DISABLE;
    g_spi2_handle.Init.CRCCalculation    = SPI_CRCCALCULATION_DISABLE;
    g_spi2_handle.Init.CRCPolynomial     = 7;
    HAL_SPI_Init(&g_spi2_handle);
    __HAL_SPI_ENABLE(&g_spi2_handle);  /* 使能 SPI2 外设 */
}

/* ======================================================
 *  内部函数: SPI 基础收发
 * ====================================================== */

/**
 * @brief  SPI 全双工收发 1 字节 (发送 tx 同时接收 rx).
 * @param  tx  待发送字节
 * @retval 接收到的字节 (读数据时发送 0xFF 作为 dummy)
 */
static uint8_t W25Q_SPI_RW(uint8_t tx)
{
    uint8_t rx = 0xFF;
    /* 超时 1000ms, 实际通信远快于此值 */
    HAL_SPI_TransmitReceive(&g_spi2_handle, &tx, &rx, 1, 1000);
    return rx;
}

/* ======================================================
 *  内部函数: 写使能
 * ====================================================== */

/**
 * @brief  发送写使能指令 (0x06).
 * @note   每次页编程或擦除前都必须先调用此函数, 否则操作被 Flash 忽略.
 *         写使能状态在操作完成后自动清除.
 */
static void W25Q_WriteEnable(void)
{
    W25Q_CS_LOW();
    W25Q_SPI_RW(CMD_WRITE_ENABLE);
    W25Q_CS_HIGH();
}

/* ======================================================
 *  公共 API 实现
 * ====================================================== */

uint8_t W25Q_ReadStatusReg1(void)
{
    uint8_t sr;
    W25Q_CS_LOW();
    W25Q_SPI_RW(CMD_READ_STATUS1);  /* 发送读状态 1 指令 */
    sr = W25Q_SPI_RW(0xFF);         /* 读取返回值 (发 dummy 字节) */
    W25Q_CS_HIGH();
    return sr;
}

int W25Q_WaitBusy(uint32_t timeout_ms)
{
    if (timeout_ms == 0) timeout_ms = 5000;  /* 默认 5 秒超时 */

    uint32_t start = HAL_GetTick();
    /* 持续轮询 WIP 位直到为 0 (操作完成) 或超时 */
    while (W25Q_ReadStatusReg1() & STATUS1_WIP_BIT)
    {
        if ((HAL_GetTick() - start) > timeout_ms)
            return -1;  /* 超时, 可能硬件故障 */
    }
    return 0;
}

uint32_t W25Q_ReadJedecID(void)
{
    uint32_t id = 0;
    W25Q_CS_LOW();
    W25Q_SPI_RW(CMD_JEDEC_ID);                        /* 发送 JEDEC ID 读取指令 */
    id  = (uint32_t)W25Q_SPI_RW(0xFF) << 16;          /* 第 1 字节: 厂商 ID (如 0xEF=Winbond) */
    id |= (uint32_t)W25Q_SPI_RW(0xFF) << 8;           /* 第 2 字节: 内存类型 (如 0x40) */
    id |= (uint32_t)W25Q_SPI_RW(0xFF);                /* 第 3 字节: 容量 (如 0x18=128Mbit) */
    W25Q_CS_HIGH();
    return id;  /* W25Q128 完整 ID = 0xEF4018 */
}

int W25Q_Init(void)
{
    uint32_t id_first;
    uint32_t id_second;
    uint8_t manuf;
    uint16_t devid;

    W25Q_SPI_Init();  /* 初始化 SPI2 和 GPIO */

    /* 发送唤醒指令: 若芯片处于掉电模式 (Power-Down) 则先唤醒
     * 唤醒后需等待 tRES1 ≥ 3μs, 这里保守等待 50μs */
    W25Q_CS_LOW();
    W25Q_SPI_RW(CMD_RELEASE_POWER_DOWN);
    W25Q_CS_HIGH();
    delay_us(50);

    /* 连续读取两次 JEDEC ID，过滤共享 SPI 总线或片选异常导致的毛刺值。 */
    id_first  = W25Q_ReadJedecID();
    id_second = W25Q_ReadJedecID();
    manuf = (uint8_t)(id_second >> 16);              /* 厂商 ID */
    devid = (uint16_t)(id_second & 0xFFFFU);         /* 设备 ID */

    printf("[W25Q] JEDEC ID = 0x%06lX / 0x%06lX\r\n",
           (unsigned long)id_first, (unsigned long)id_second);

    if (id_first != id_second)
    {
        printf("[W25Q] ERROR: JEDEC ID unstable, check CS/SPI bus sharing\r\n");
        return -1;
    }

    if (W25Q_IsKnownJedec(id_second) == 0u)
    {
        printf("[W25Q] ERROR: Unsupported or invalid flash ID "
               "(manuf=0x%02X, dev=0x%04X)\r\n",
               manuf, devid);
        return -1;
    }

    printf("[W25Q] Flash ready (manuf=0x%02X, dev=0x%04X)\r\n", manuf, devid);
    return 0;
}

void W25Q_ReadData(uint32_t addr, uint8_t *buf, uint32_t len)
{
    W25Q_CS_LOW();
    W25Q_SPI_RW(CMD_READ_DATA);               /* 标准读命令 (0x03) */
    W25Q_SPI_RW((uint8_t)(addr >> 16));       /* 24 位地址: 高 8 位 */
    W25Q_SPI_RW((uint8_t)(addr >> 8));        /* 24 位地址: 中 8 位 */
    W25Q_SPI_RW((uint8_t)(addr));             /* 24 位地址: 低 8 位 */
    /* 连续读取: Flash 会自动递增内部地址指针 */
    for (uint32_t i = 0; i < len; i++)
        buf[i] = W25Q_SPI_RW(0xFF);
    W25Q_CS_HIGH();
}

void W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    if (len == 0) return;
    if (len > W25Q_PAGE_SIZE) len = W25Q_PAGE_SIZE;  /* 限制最大 256 字节 */

    W25Q_WriteEnable();   /* 写入前必须先发写使能指令 */
    W25Q_CS_LOW();
    W25Q_SPI_RW(CMD_PAGE_PROGRAM);            /* 页编程命令 (0x02) */
    W25Q_SPI_RW((uint8_t)(addr >> 16));       /* 目标地址高字节 */
    W25Q_SPI_RW((uint8_t)(addr >> 8));        /* 目标地址中字节 */
    W25Q_SPI_RW((uint8_t)(addr));             /* 目标地址低字节 */
    for (uint16_t i = 0; i < len; i++)
        W25Q_SPI_RW(buf[i]);                  /* 逐字节写入数据 */
    W25Q_CS_HIGH();

    /* 等待页编程完成. 典型时间 0.7ms, 最大 3ms */
    W25Q_WaitBusy(10);
}

void W25Q_WriteData(uint32_t addr, const uint8_t *buf, uint32_t len)
{
    /* 将跨页写入拆分为多次 PageProgram:
     * 每次写从 addr 到当前页末尾, 或写完剩余数据 */
    while (len > 0)
    {
        uint32_t page_off = addr % W25Q_PAGE_SIZE;       /* addr 在当前页内的偏移 */
        uint32_t chunk    = W25Q_PAGE_SIZE - page_off;   /* 本次最多可写字节数 */
        if (chunk > len) chunk = len;                    /* 不超过剩余数据量 */

        W25Q_PageProgram(addr, buf, (uint16_t)chunk);

        addr += chunk;   /* 地址前移 */
        buf  += chunk;   /* 数据指针前移 */
        len  -= chunk;   /* 剩余长度减少 */
    }
}

/**
 * @brief  内部通用擦除函数: 发送擦除指令并等待完成.
 * @param  cmd         擦除指令码 (扇区/块32/块64)
 * @param  addr        已对齐的目标地址
 * @param  timeout_ms  等待超时毫秒数
 */
static void W25Q_EraseCmd(uint8_t cmd, uint32_t addr, uint32_t timeout_ms)
{
    W25Q_WriteEnable();   /* 擦除前必须写使能 */
    W25Q_CS_LOW();
    W25Q_SPI_RW(cmd);                          /* 擦除命令 */
    W25Q_SPI_RW((uint8_t)(addr >> 16));        /* 目标地址高字节 */
    W25Q_SPI_RW((uint8_t)(addr >> 8));         /* 目标地址中字节 */
    W25Q_SPI_RW((uint8_t)(addr));              /* 目标地址低字节 */
    W25Q_CS_HIGH();
    /* 等待擦除完成 (扇区擦除典型 45ms, 最长 400ms) */
    W25Q_WaitBusy(timeout_ms);
}

void W25Q_SectorErase(uint32_t addr)
{
    /* 将地址向下对齐到扇区边界 (4KB = 0x1000) */
    W25Q_EraseCmd(CMD_SECTOR_ERASE, addr & ~(W25Q_SECTOR_SIZE - 1U), 1000);
}

void W25Q_Block32Erase(uint32_t addr)
{
    /* 将地址向下对齐到 32KB 块边界 */
    W25Q_EraseCmd(CMD_BLOCK32_ERASE, addr & ~(W25Q_BLOCK32_SIZE - 1U), 3000);
}

void W25Q_Block64Erase(uint32_t addr)
{
    /* 将地址向下对齐到 64KB 块边界 */
    W25Q_EraseCmd(CMD_BLOCK64_ERASE, addr & ~(W25Q_BLOCK64_SIZE - 1U), 4000);
}

void W25Q_ChipErase(void)
{
    W25Q_WriteEnable();
    W25Q_CS_LOW();
    W25Q_SPI_RW(CMD_CHIP_ERASE);   /* 全片擦除命令, 无需地址 */
    W25Q_CS_HIGH();
    /* W25Q128 全片擦除典型时间 40s, 最长约 200s */
    W25Q_WaitBusy(120000);
}
