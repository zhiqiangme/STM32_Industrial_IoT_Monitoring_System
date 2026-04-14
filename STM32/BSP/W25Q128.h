/**
 ****************************************************************************************************
 * @file        W25Q128.h
 * @brief       W25Q128 SPI NOR Flash 驱动头文件 (容量 16MB)
 *
 * 硬件连接 (SPI2 总线):
 *   PB12 -> /CS   片选信号, 软件控制, 低电平有效
 *   PB13 -> SCK   时钟
 *   PB14 -> MISO  主入从出 (W25Q128 SO 引脚)
 *   PB15 -> MOSI  主出从入 (W25Q128 SI 引脚)
 *   3V3  -> WP#   写保护引脚拉高, 禁用硬件写保护
 *   3V3  -> HOLD# 暂停引脚拉高, 禁用 HOLD 功能
 *
 * 注意: SPI2 总线与 NRF24L01 无线模块共用.
 *       两个设备通过各自的 CS 引脚独立选片, 互不干扰.
 *       访问 W25Q128 时需保证 NRF24L01 的 CS 处于高电平 (未选中).
 ****************************************************************************************************
 */
#ifndef __W25Q128_H
#define __W25Q128_H

#include "sys.h"

/* ======================================================
 *  芯片几何参数
 * ====================================================== */
#define W25Q_TOTAL_SIZE     (16U * 1024U * 1024U)   /* 总容量: 16 MB (128 Mbit) */
#define W25Q_PAGE_SIZE      256U                    /* 页大小: 256 字节 (页编程最大单次写入量) */
#define W25Q_SECTOR_SIZE    4096U                   /* 扇区大小: 4 KB (最小擦除单元) */
#define W25Q_BLOCK32_SIZE   (32U * 1024U)           /* 32KB 块大小 */
#define W25Q_BLOCK64_SIZE   (64U * 1024U)           /* 64KB 块大小 */
#define W25Q_SECTOR_COUNT   (W25Q_TOTAL_SIZE / W25Q_SECTOR_SIZE) /* 总扇区数: 4096 */

/* ======================================================
 *  JEDEC 厂商/设备 ID
 *  读取方式: 发送 0x9F 指令后连续读 3 字节
 *  格式: [23:16]=厂商ID  [15:8]=内存类型  [7:0]=容量
 * ====================================================== */
#define W25Q_JEDEC_MANUF_WB    0xEFU   /* Winbond 厂商 ID */
#define W25Q_JEDEC_ID_W25Q128  0x4018U /* W25Q128 设备 ID (16MB), 国产 GD25Q128 与此相同 */
#define W25Q_JEDEC_ID_W25Q64   0x4017U /* W25Q64  设备 ID (8MB), 供参考 */

/* ======================================================
 *  片选 (CS) 引脚定义与操作宏
 *  CS 低电平: 选中芯片, 开始通信
 *  CS 高电平: 取消选中, 释放 SPI 总线
 * ====================================================== */
#define W25Q_CS_PORT  GPIOB
#define W25Q_CS_PIN   GPIO_PIN_12

/* 拉低 CS 选中 W25Q128 */
#define W25Q_CS_LOW()   HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_RESET)
/* 拉高 CS 取消选中, 释放总线供 NRF24L01 或其他设备使用 */
#define W25Q_CS_HIGH()  HAL_GPIO_WritePin(W25Q_CS_PORT, W25Q_CS_PIN, GPIO_PIN_SET)

/* ======================================================
 *  公共 API
 * ====================================================== */

/**
 * @brief  初始化 SPI2 外设及 CS 引脚, 发送唤醒指令后读取 JEDEC ID 验证芯片.
 * @note   首次上电或从掉电模式恢复时需调用此函数.
 *         若 JEDEC ID 为全 0 或全 F, 说明 SPI 通信异常, 返回错误.
 * @retval  0: 成功 (ID 有效)
 * @retval -1: 失败 (无应答或 SPI 异常)
 */
int W25Q_Init(void);

/**
 * @brief  读取 3 字节 JEDEC ID.
 * @note   返回值格式: bit[23:16]=厂商ID  bit[15:0]=设备ID
 *         例: W25Q128 返回 0xEF4018
 * @retval 24 位 ID, 高 8 位为厂商 ID, 低 16 位为设备 ID
 */
uint32_t W25Q_ReadJedecID(void);

/**
 * @brief  从指定地址读取任意长度的数据.
 * @note   可跨页、跨扇区读取, 无需对齐. 使用 0x03 慢速读指令 (最高 33MHz).
 * @param  addr  Flash 内绝对地址 (0x000000 ~ 0xFFFFFF)
 * @param  buf   接收缓冲区指针
 * @param  len   读取字节数
 */
void W25Q_ReadData(uint32_t addr, uint8_t *buf, uint32_t len);

/**
 * @brief  页编程: 向已擦除区域写入最多 256 字节数据.
 * @note   写入范围不得跨页边界 (256 字节对齐). 若 len 超过剩余页空间,
 *         多余字节会回卷到页首覆盖已写内容, 请勿依赖此行为.
 *         调用前必须确保目标地址已擦除 (全为 0xFF).
 * @param  addr  目标地址 (建议页对齐)
 * @param  buf   待写数据
 * @param  len   写入字节数 (≤ 256)
 */
void W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint16_t len);

/**
 * @brief  跨页写入: 向任意地址写入任意长度数据.
 * @note   内部自动切分为多次 PageProgram 调用, 不做擦除操作.
 *         调用前需确保目标区域已擦除.
 * @param  addr  起始地址
 * @param  buf   待写数据
 * @param  len   写入总字节数
 */
void W25Q_WriteData(uint32_t addr, const uint8_t *buf, uint32_t len);

/**
 * @brief  擦除 4KB 扇区 (最小擦除单元).
 * @note   擦除后该扇区内所有字节变为 0xFF. 擦除耗时约 45~400ms.
 *         addr 无需对齐到扇区首地址, 函数内部自动向下对齐.
 * @param  addr  目标扇区内的任意地址
 */
void W25Q_SectorErase(uint32_t addr);

/**
 * @brief  擦除 32KB 块. 耗时约 120~1600ms.
 * @param  addr  目标块内的任意地址
 */
void W25Q_Block32Erase(uint32_t addr);

/**
 * @brief  擦除 64KB 块. 耗时约 150~2000ms.
 * @param  addr  目标块内的任意地址
 */
void W25Q_Block64Erase(uint32_t addr);

/**
 * @brief  全片擦除. 耗时极长 (W25Q128 约 40~200 秒), 谨慎使用.
 */
void W25Q_ChipErase(void);

/**
 * @brief  读取状态寄存器 1 (SR1).
 * @note   SR1 的 bit0 为 WIP (Write In Progress), 为 1 表示正在执行写/擦操作.
 * @retval SR1 寄存器值
 */
uint8_t W25Q_ReadStatusReg1(void);

/**
 * @brief  轮询等待芯片空闲 (WIP 位清零).
 * @note   页编程/扇区擦除发出指令后必须调用此函数等待完成.
 *         在裸机轮询期间 CPU 不会执行其他任务, 注意对实时性的影响.
 * @param  timeout_ms  最大等待时间 (毫秒). 传入 0 使用默认值 5000ms.
 * @retval  0: 正常完成
 * @retval -1: 超时 (可能硬件故障)
 */
int W25Q_WaitBusy(uint32_t timeout_ms);

#endif /* __W25Q128_H */
