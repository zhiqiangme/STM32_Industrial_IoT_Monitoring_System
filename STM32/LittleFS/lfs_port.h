/**
 ****************************************************************************************************
 * @file        lfs_port.h
 * @brief       LittleFS 块设备适配层 (Block Device Port) 头文件
 *
 * 本模块将 W25Q128 SPI Flash 封装为 LittleFS 所需的块设备接口,
 * 并提供挂载/卸载/格式化等高层 API.
 *
 * 层次结构 (从上到下):
 *   DataLogger (应用层)
 *     └── LfsPort_Get() / lfs_file_open() / lfs_file_write() ...
 *         └── LittleFS 核心 (lfs.c)
 *             └── lfs_bd_read / lfs_bd_prog / lfs_bd_erase / lfs_bd_sync  ← 本模块实现
 *                 └── W25Q_ReadData / W25Q_WriteData / W25Q_SectorErase   ← W25Q128 驱动
 *                     └── SPI2 硬件
 *
 * Flash 分区布局 (W25Q128, 16MB):
 *   地址范围              大小    用途
 *   0x000000~0x00FFFF    64KB   保留区 (可用于 OTA 暂存、KV 存储等)
 *   0x010000~0xFFFFFF   ~16MB   LittleFS 文件系统分区
 ****************************************************************************************************
 */
#ifndef __LFS_PORT_H
#define __LFS_PORT_H

#include "lfs.h"

/* ======================================================
 *  Flash 分区地址
 * ====================================================== */
/** LittleFS 分区在 Flash 中的起始偏移地址 (跳过前 64KB 保留区) */
#define LFS_FLASH_BASE_OFFSET   0x00010000U

/** LittleFS 分区可用总字节数 (~15.9MB) */
#define LFS_FLASH_SIZE          (16U * 1024U * 1024U - LFS_FLASH_BASE_OFFSET)

/* ======================================================
 *  LittleFS 块设备参数
 *  这些参数必须与底层 Flash 的物理特性匹配
 * ====================================================== */
/** 单次读取最小粒度 (字节). 设为 256 以匹配 Flash 页大小, 提升缓存命中率 */
#define LFS_PORT_READ_SIZE      256U

/** 单次编程最小粒度 (字节). 必须 = Flash 页大小 (256B) */
#define LFS_PORT_PROG_SIZE      256U

/** 擦除块大小 (字节). 必须 = Flash 最小擦除单元 (4KB 扇区) */
#define LFS_PORT_BLOCK_SIZE     4096U

/** 文件系统总块数 = LittleFS 分区大小 / 块大小 = 约 4080 块 */
#define LFS_PORT_BLOCK_COUNT    (LFS_FLASH_SIZE / LFS_PORT_BLOCK_SIZE)

/** 读写缓存大小 (字节). 必须是 read_size 和 prog_size 的倍数.
 *  LittleFS 内部维护两个此大小的缓存 (读缓存 + 写缓存), 共占 512B RAM */
#define LFS_PORT_CACHE_SIZE     256U

/** 前瞻分配缓冲区大小 (字节). 每字节对应 8 个块的空闲位图.
 *  32 字节 = 256 块的前瞻范围, 用于磨损均衡算法 */
#define LFS_PORT_LOOKAHEAD_SIZE 32U

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  初始化 W25Q128 驱动并挂载 LittleFS 文件系统.
 * @note   挂载失败时, 仅当分区首部看起来是全 0xFF 的空白态才自动格式化一次.
 *         若分区已有内容但挂载失败, 直接返回错误, 避免在总线异常时误擦写.
 *         格式化会清除分区内所有数据, 但不影响保留区 (前 64KB).
 *         已挂载时重复调用直接返回 0, 不执行任何操作.
 * @retval  0: 挂载成功
 * @retval -1: W25Q128 初始化失败 (SPI 通信异常)
 * @retval -2: 文件系统损坏或挂载失败, 且分区不是空白态
 * @retval -3: 空白分区格式化失败
 * @retval -4: 格式化后重新挂载仍失败
 */
int LfsPort_Init(void);

/**
 * @brief  卸载 LittleFS 文件系统, 刷新所有缓存到 Flash.
 * @note   卸载后不可再调用 lfs_file_*() 等文件操作函数.
 *         若需重新使用, 再次调用 LfsPort_Init().
 * @retval LittleFS 错误码 (0 = 成功)
 */
int LfsPort_Deinit(void);

/**
 * @brief  获取已挂载的 LittleFS 文件系统句柄.
 * @note   DataLogger 等上层模块通过此函数获取句柄后调用 lfs_file_open() 等 API.
 *         未挂载时返回 NULL, 上层应做判空保护.
 * @retval 已挂载: 指向内部 lfs_t 实例的指针
 * @retval 未挂载: NULL
 */
lfs_t *LfsPort_Get(void);

/**
 * @brief  强制格式化 LittleFS 分区.
 * @warning 此操作会擦除分区内所有文件和目录, 不可恢复!
 *          仅用于调试、恢复损坏文件系统或首次部署初始化.
 *          格式化后需调用 LfsPort_Init() 重新挂载.
 * @retval LittleFS 错误码 (0 = 成功)
 */
int LfsPort_Format(void);

#ifdef __cplusplus
}
#endif

#endif /* __LFS_PORT_H */
