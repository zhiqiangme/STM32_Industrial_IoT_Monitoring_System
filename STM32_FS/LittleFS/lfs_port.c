/**
 ****************************************************************************************************
 * @file        lfs_port.c
 * @brief       LittleFS 块设备适配层实现
 *
 * 本文件实现 LittleFS 所需的四个块设备回调函数:
 *   lfs_bd_read  -> W25Q_ReadData   (读取 Flash 数据)
 *   lfs_bd_prog  -> W25Q_WriteData  (编程 Flash 页)
 *   lfs_bd_erase -> W25Q_SectorErase (擦除 Flash 扇区)
 *   lfs_bd_sync  -> 无操作 (每次 prog/erase 内部已等待完成)
 *
 * 地址转换: Flash 物理地址 = LFS_FLASH_BASE_OFFSET + block * block_size + offset
 ****************************************************************************************************
 */
#include "lfs_port.h"
#include "W25Q128.h"
#include <stdio.h>
#include <string.h>

/* ======================================================
 *  LittleFS 块设备回调函数
 *  这四个函数由 LittleFS 核心在需要时自动调用, 上层代码不直接调用.
 * ====================================================== */

/**
 * @brief  块设备读回调: 从 Flash 指定块的指定偏移读取数据.
 * @param  c      LittleFS 配置指针 (本实现不使用)
 * @param  block  逻辑块号 (0 ~ block_count-1)
 * @param  off    块内字节偏移
 * @param  buffer 输出缓冲区
 * @param  size   读取字节数
 * @retval LFS_ERR_OK (0) 始终成功 (W25Q_ReadData 不会失败)
 */
static int lfs_bd_read(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, void *buffer, lfs_size_t size)
{
    (void)c;
    /* 将逻辑块号和块内偏移转换为 Flash 物理地址 */
    uint32_t addr = LFS_FLASH_BASE_OFFSET + block * LFS_PORT_BLOCK_SIZE + off;
    W25Q_ReadData(addr, (uint8_t *)buffer, size);
    return LFS_ERR_OK;
}

/**
 * @brief  块设备编程回调: 向 Flash 指定位置写入数据 (不含擦除).
 * @note   LittleFS 保证调用 prog 前已调用过 erase, 目标区域为 0xFF.
 *         LittleFS 保证 size <= prog_size (256B), 且地址和大小均对齐.
 * @param  c      LittleFS 配置指针
 * @param  block  逻辑块号
 * @param  off    块内字节偏移 (对齐到 prog_size)
 * @param  buffer 待写数据
 * @param  size   写入字节数
 * @retval LFS_ERR_OK
 */
static int lfs_bd_prog(const struct lfs_config *c, lfs_block_t block,
                       lfs_off_t off, const void *buffer, lfs_size_t size)
{
    (void)c;
    uint32_t addr = LFS_FLASH_BASE_OFFSET + block * LFS_PORT_BLOCK_SIZE + off;
    W25Q_WriteData(addr, (const uint8_t *)buffer, size);
    return LFS_ERR_OK;
}

/**
 * @brief  块设备擦除回调: 擦除一个逻辑块 (= 一个 4KB 扇区).
 * @note   擦除后块内所有字节变为 0xFF.
 *         擦除耗时约 45~400ms, 在此期间 CPU 被 W25Q_WaitBusy 占用.
 *         裸机单任务架构下, 擦除期间 Modbus 通信会暂停响应;
 *         建议在传感器采样间隙 (约 1.7s 空闲窗口) 执行擦除.
 * @param  c     LittleFS 配置指针
 * @param  block 逻辑块号 (LittleFS 保证不超过 block_count)
 * @retval LFS_ERR_OK
 */
static int lfs_bd_erase(const struct lfs_config *c, lfs_block_t block)
{
    (void)c;
    uint32_t addr = LFS_FLASH_BASE_OFFSET + block * LFS_PORT_BLOCK_SIZE;
    W25Q_SectorErase(addr);  /* 4KB 扇区擦除, 内部已等待完成 */
    return LFS_ERR_OK;
}

/**
 * @brief  块设备同步回调: 确保所有写操作已持久化到存储介质.
 * @note   本实现每次 prog/erase 操作后都调用 W25Q_WaitBusy 等待完成,
 *         因此 sync 无需额外操作, 直接返回 OK.
 * @retval LFS_ERR_OK
 */
static int lfs_bd_sync(const struct lfs_config *c)
{
    (void)c;
    return LFS_ERR_OK;
}

/* ======================================================
 *  静态缓冲区 (因定义了 LFS_NO_MALLOC, 所有缓冲由此提供)
 *  总占用 RAM: 256 (读) + 256 (写) + 32 (前瞻) = 544 字节
 * ====================================================== */
static uint8_t s_read_buf[LFS_PORT_CACHE_SIZE];       /* LittleFS 读缓存 */
static uint8_t s_prog_buf[LFS_PORT_CACHE_SIZE];       /* LittleFS 写缓存 */
static uint8_t s_lookahead_buf[LFS_PORT_LOOKAHEAD_SIZE]; /* 磨损均衡前瞻位图 */

static int LfsPort_IsPartitionBlank(void)
{
    uint8_t probe0[16];
    uint8_t probe1[16];

    memset(probe0, 0, sizeof(probe0));
    memset(probe1, 0, sizeof(probe1));

    W25Q_ReadData(LFS_FLASH_BASE_OFFSET, probe0, sizeof(probe0));
    W25Q_ReadData(LFS_FLASH_BASE_OFFSET + LFS_PORT_BLOCK_SIZE, probe1, sizeof(probe1));

    for (uint32_t i = 0; i < sizeof(probe0); i++)
    {
        if (probe0[i] != 0xFFu || probe1[i] != 0xFFu)
        {
            return 0;
        }
    }

    return 1;
}

/**
 * @brief  LittleFS 配置结构体 (静态常量, 整个生命周期不变).
 * @note   所有 .xxx_buffer 字段指向静态数组, 避免动态内存分配.
 */
static const struct lfs_config s_lfs_cfg = {
    /* 块设备操作回调 */
    .read  = lfs_bd_read,
    .prog  = lfs_bd_prog,
    .erase = lfs_bd_erase,
    .sync  = lfs_bd_sync,

    /* 块设备几何参数 (必须与 Flash 物理特性匹配) */
    .read_size      = LFS_PORT_READ_SIZE,    /* 最小读粒度: 256B */
    .prog_size      = LFS_PORT_PROG_SIZE,    /* 最小编程粒度: 256B (= Flash 页大小) */
    .block_size     = LFS_PORT_BLOCK_SIZE,   /* 块大小: 4096B (= Flash 扇区大小) */
    .block_count    = LFS_PORT_BLOCK_COUNT,  /* 总块数: ~4080 */
    .block_cycles   = 500,                   /* 磨损均衡触发阈值: 每块擦除 500 次后触发迁移
                                              * NOR Flash 额定 10 万次擦除, 500 较保守 */
    .cache_size     = LFS_PORT_CACHE_SIZE,   /* 缓存大小: 256B (必须 >= read/prog_size) */
    .lookahead_size = LFS_PORT_LOOKAHEAD_SIZE, /* 前瞻大小: 32B = 覆盖 256 个块的空闲位图 */

    /* 静态缓冲区指针 (替代动态 malloc) */
    .read_buffer      = s_read_buf,
    .prog_buffer      = s_prog_buf,
    .lookahead_buffer = s_lookahead_buf,

    /* 以下参数保持默认值 (0 = 使用 LittleFS 内置默认) */
    .name_max = 0,   /* 文件名最大长度, 默认 255 */
    .file_max = 0,   /* 文件最大大小, 默认 2GB */
    .attr_max = 0,   /* 自定义属性最大大小, 默认 1022B */
};

/* LittleFS 文件系统实例 (占 RAM 约 80 字节) */
static lfs_t s_lfs;

/* 挂载状态标志: 0=未挂载, 1=已挂载 */
static int s_mounted = 0;

/* ======================================================
 *  公共 API 实现
 * ====================================================== */

int LfsPort_Format(void)
{
    /* lfs_format 会遍历所有块执行擦除和写入初始元数据, 耗时较长 */
    int err = lfs_format(&s_lfs, &s_lfs_cfg);
    if (err)
        printf("[LFS] Format failed: %d\r\n", err);
    else
        printf("[LFS] Format OK (all data erased)\r\n");
    return err;
}

int LfsPort_Init(void)
{
    /* 防止重复挂载 */
    if (s_mounted) return 0;

    /* 第一步: 初始化 SPI Flash 驱动, 验证芯片 ID */
    if (W25Q_Init() != 0)
    {
        printf("[LFS] W25Q128 init failed\r\n");
        return -1;
    }

    /* 第二步: 尝试挂载已有文件系统
     * 首次使用时 Flash 内无有效的 LittleFS 元数据, mount 会返回错误 */
    int err = lfs_mount(&s_lfs, &s_lfs_cfg);
    if (err)
    {
        /* 只在分区明显为空白时自动格式化，避免 SPI 总线异常时误擦写。 */
        if (LfsPort_IsPartitionBlank() == 0)
        {
            printf("[LFS] Mount failed (err=%d), partition not blank, skip auto-format\r\n", err);
            return -2;
        }

        printf("[LFS] Blank partition detected, formatting...\r\n");
        err = lfs_format(&s_lfs, &s_lfs_cfg);
        if (err)
        {
            printf("[LFS] Format failed: %d\r\n", err);
            return -3;
        }

        /* 格式化后再次挂载 */
        err = lfs_mount(&s_lfs, &s_lfs_cfg);
        if (err)
        {
            printf("[LFS] Remount after format failed: %d\r\n", err);
            return -4;
        }
    }

    s_mounted = 1;
    printf("[LFS] Mounted: %u blocks x %u B = %lu KB available\r\n",
           (unsigned)LFS_PORT_BLOCK_COUNT,
           (unsigned)LFS_PORT_BLOCK_SIZE,
           (unsigned long)(LFS_FLASH_SIZE / 1024U));
    return 0;
}

int LfsPort_Deinit(void)
{
    if (!s_mounted) return 0;
    /* lfs_unmount 会将所有未写入的缓存数据刷到 Flash */
    int err = lfs_unmount(&s_lfs);
    s_mounted = 0;
    return err;
}

lfs_t *LfsPort_Get(void)
{
    /* 未挂载时返回 NULL, 调用方需做判空处理 */
    return s_mounted ? &s_lfs : NULL;
}
