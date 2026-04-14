/**
 ****************************************************************************************************
 * @file        DataLogger.c
 * @brief       传感器数据与错误日志文件记录模块 (基于 LittleFS)
 *
 * 写入策略:
 *   传感器数据: 先写入 512 字节 RAM 缓冲, 缓冲满时自动刷盘.
 *               main.c 每 60 秒也会调用 DataLogger_Flush() 强制刷盘.
 *   错误日志:   立即写入 Flash 文件, 不经过缓冲, 避免掉电丢失.
 *
 * 磨损分析:
 *   以 2 秒采样周期计算, 每天约 43200 条记录, 每条约 60 字节 = 约 2.6MB/天.
 *   LittleFS 磨损均衡会将写入分散到所有 4080 个块 (4KB 各块).
 *   每天约 650 次块擦除 / 4080 块 ≈ 每块每年约 58 次擦除, 远低于额定 10 万次.
 ****************************************************************************************************
 */
#include "DataLogger.h"
#include "lfs_port.h"
#include "lfs.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

/* ======================================================
 *  文件路径常量
 * ====================================================== */
#define DATA_DIR    "/data"     /* 传感器数据目录 */
#define LOG_DIR     "/log"      /* 错误日志目录 */
#define META_FILE   "/meta.dat" /* 元数据文件 */

/* ======================================================
 *  缓冲区大小
 * ====================================================== */
#define CSV_BUF_SIZE   512  /* 传感器数据 RAM 缓冲 (约 6~8 条 CSV 记录) */
#define LINE_BUF_SIZE  160  /* 单行格式化缓冲 (足够容纳一条 CSV 或日志行) */

/* ======================================================
 *  元数据结构
 *  持久化到 /meta.dat, 每次启动加载, 修改后立即保存.
 * ====================================================== */
typedef struct {
    uint32_t magic;       /* 魔术字 0x4D4C4F47 ("MLOG"), 用于校验元数据有效性 */
    uint32_t boot_count;  /* 累计启动次数 (每次上电 +1) */
    uint32_t day_index;   /* 当前日序号 (从 1 开始递增, 无 RTC 时作为日期替代) */
    uint32_t reserved;    /* 保留字段, 预留给未来扩展 (如 RTC 日历日期) */
} Metadata;

#define META_MAGIC  0x4D4C4F47U  /* ASCII "MLOG" */

/* ======================================================
 *  模块内部状态
 * ====================================================== */
static int      s_initialized = 0;   /* 初始化标志: 0=未初始化, 1=已初始化 */
static Metadata s_meta;              /* 当前元数据 (内存副本) */

/* 传感器 CSV 写入缓冲区 */
static char     s_csv_buf[CSV_BUF_SIZE]; /* 积累多条 CSV 行的内存缓冲 */
static uint32_t s_csv_len = 0;           /* 缓冲区当前已用字节数 */

/* ======================================================
 *  LittleFS 文件操作所需的静态缓冲区
 *  原因: 定义了 LFS_NO_MALLOC 后, lfs_file_open() 不再可用,
 *        必须改用 lfs_file_opencfg() 并手动提供 cache_size 大小的缓冲.
 *  约束: 同一时刻只允许打开一个文件 (本模块设计如此).
 *        buffer 大小必须 >= lfs_config.cache_size (256B).
 * ====================================================== */
static uint8_t           s_file_buf[LFS_PORT_CACHE_SIZE]; /* 每次文件操作复用 */
static struct lfs_file_config s_file_cfg = {
    .buffer     = s_file_buf,  /* 指向静态缓冲区, 替代 malloc */
    .attrs      = NULL,
    .attr_count = 0,
};

/* ======================================================
 *  内部函数: 元数据读写
 * ====================================================== */

/**
 * @brief  从 Flash 加载元数据到内存.
 * @note   若文件不存在或魔术字不匹配, 则初始化为默认值 (首次使用).
 * @retval  0: 成功加载有效元数据
 * @retval -1: 使用了默认值 (首次使用或文件损坏)
 */
static int meta_load(void)
{
    lfs_t *lfs = LfsPort_Get();
    lfs_file_t file;

    /* 尝试打开元数据文件 (使用 opencfg 提供静态缓存, 因为 LFS_NO_MALLOC) */
    int err = lfs_file_opencfg(lfs, &file, META_FILE, LFS_O_RDONLY, &s_file_cfg);
    if (err)
    {
        /* 文件不存在 (首次启动), 使用默认初始值 */
        memset(&s_meta, 0, sizeof(s_meta));
        s_meta.magic     = META_MAGIC;
        s_meta.day_index = 1;   /* 日序号从 1 开始 */
        return -1;
    }

    Metadata tmp;
    lfs_ssize_t rd = lfs_file_read(lfs, &file, &tmp, sizeof(tmp));
    lfs_file_close(lfs, &file);

    /* 校验读取长度和魔术字 */
    if (rd == (lfs_ssize_t)sizeof(tmp) && tmp.magic == META_MAGIC)
    {
        s_meta = tmp;  /* 校验通过, 使用 Flash 中保存的值 */
        return 0;
    }

    /* 数据损坏: 重置为默认值 */
    memset(&s_meta, 0, sizeof(s_meta));
    s_meta.magic     = META_MAGIC;
    s_meta.day_index = 1;
    return -1;
}

/**
 * @brief  将内存中的元数据保存到 Flash (/meta.dat).
 * @note   每次修改 boot_count 或 day_index 后应立即调用.
 * @retval LittleFS 错误码 (0 = 成功)
 */
static int meta_save(void)
{
    lfs_t *lfs = LfsPort_Get();
    lfs_file_t file;

    /* 以覆盖模式打开 (TRUNC: 清空旧内容, CREAT: 不存在则创建) */
    int err = lfs_file_opencfg(lfs, &file, META_FILE,
                               LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC, &s_file_cfg);
    if (err) return err;

    lfs_file_write(lfs, &file, &s_meta, sizeof(s_meta));
    return lfs_file_close(lfs, &file);  /* close 时会刷新 LittleFS 内部缓存 */
}

/* ======================================================
 *  内部函数: 目录和路径工具
 * ====================================================== */

/**
 * @brief  确保目录存在, 若不存在则创建.
 * @param  path  目录路径
 */
static void ensure_dir(const char *path)
{
    lfs_t *lfs = LfsPort_Get();
    int err = lfs_mkdir(lfs, path);
    if (err && err != LFS_ERR_EXIST)
    {
        /* LFS_ERR_EXIST 是正常情况 (目录已存在), 其他错误才打印 */
        printf("[LOG] mkdir %s failed: %d\r\n", path, err);
    }
}

/**
 * @brief  根据日序号生成传感器数据文件路径.
 *         格式: /data/NNNNNN.csv (6 位十进制, 前置 0 补位)
 */
static void build_data_path(char *out, size_t n, uint32_t day)
{
    snprintf(out, n, "%s/%06lu.csv", DATA_DIR, (unsigned long)day);
}

/**
 * @brief  根据日序号生成错误日志文件路径.
 *         格式: /log/NNNNNN.log
 */
static void build_log_path(char *out, size_t n, uint32_t day)
{
    snprintf(out, n, "%s/%06lu.log", LOG_DIR, (unsigned long)day);
}

/* ======================================================
 *  公共 API: 初始化
 * ====================================================== */

int DataLogger_Init(void)
{
    if (s_initialized) return 0;  /* 防止重复初始化 */

    /* 第一步: 挂载 LittleFS 文件系统 */
    if (LfsPort_Init() != 0)
    {
        printf("[LOG] LFS init failed\r\n");
        return -1;
    }

    /* 第二步: 确保数据目录和日志目录存在 */
    ensure_dir(DATA_DIR);  /* /data */
    ensure_dir(LOG_DIR);   /* /log  */

    /* 第三步: 加载元数据, 递增启动计数, 保存 */
    meta_load();
    s_meta.boot_count++;   /* 每次上电 +1 */
    meta_save();

    printf("[LOG] Ready. boot=%lu day=%lu free=%lu KB\r\n",
           (unsigned long)s_meta.boot_count,
           (unsigned long)s_meta.day_index,
           (unsigned long)(DataLogger_GetFreeBytes() / 1024U));

    s_initialized = 1;

    /* 记录一条启动事件到日志文件 (立即写盘) */
    DataLogger_LogError("BOOT count=%lu", (unsigned long)s_meta.boot_count);
    return 0;
}

/* ======================================================
 *  内部函数: 将 CSV 缓冲区内容刷写到 Flash
 * ====================================================== */

/**
 * @brief  将 s_csv_buf 中积累的 CSV 数据追加写入当天的数据文件.
 * @note   若为新建文件则自动写入 CSV 表头.
 *         写入完成后清空缓冲区 (s_csv_len = 0).
 * @retval  0: 成功 (或缓冲区本来为空)
 * @retval <0: 文件操作错误
 */
static int flush_csv_buffer(void)
{
    if (s_csv_len == 0) return 0;  /* 缓冲区为空, 无需刷写 */

    lfs_t *lfs = LfsPort_Get();
    if (!lfs) return -1;

    /* 构建当天的数据文件路径 */
    char path[32];
    build_data_path(path, sizeof(path), s_meta.day_index);

    /* 以追加模式打开文件 (O_APPEND: 写入时自动定位到文件末尾) */
    lfs_file_t file;
    int err = lfs_file_opencfg(lfs, &file, path,
                               LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND, &s_file_cfg);
    if (err)
    {
        printf("[LOG] open %s failed: %d\r\n", path, err);
        s_csv_len = 0;  /* 清空缓冲, 避免下次继续累积损坏数据 */
        return err;
    }

    /* 若文件是新建的 (大小为 0), 先写入 CSV 表头行 */
    lfs_soff_t size = lfs_file_size(lfs, &file);
    if (size == 0)
    {
        static const char header[] =
            "uptime_s,temp_x10,weight_g,flow_rate_x100,flow_total_x1000,relay_do,relay_di\r\n";
        lfs_file_write(lfs, &file, header, sizeof(header) - 1);
    }

    /* 将缓冲区内容追加写入文件 */
    lfs_file_write(lfs, &file, s_csv_buf, s_csv_len);
    lfs_file_close(lfs, &file);  /* close 触发 LittleFS 内部缓存刷新 */

    s_csv_len = 0;  /* 重置缓冲区指针 */
    return 0;
}

/* ======================================================
 *  公共 API: 传感器数据记录
 * ====================================================== */

int DataLogger_LogSensor(const SensorSnapshot *snap)
{
    if (!s_initialized || !snap) return -1;

    /* 将浮点数值转换为整数格式, 避免浮点 printf 的代码膨胀
     * 温度: ×10 (如 25.6°C -> 256)
     * 流量: ×100 (如 1.23 L/min -> 123)
     * 总量: ×1000 (如 45.678 L -> 45678) */
    char line[LINE_BUF_SIZE];
    int n = snprintf(line, sizeof(line),
                     "%lu,%d,%ld,%d,%lu,0x%04X,0x%04X\r\n",
                     (unsigned long)snap->uptime_s,
                     (int)(snap->temperature * 10.0f),
                     (long)snap->weight,
                     (int)(snap->flow_rate * 100.0f),
                     (unsigned long)(snap->flow_total * 1000.0f),
                     (unsigned)snap->relay_do,
                     (unsigned)snap->relay_di);
    if (n <= 0) return -2;
    if (n >= (int)sizeof(line)) n = sizeof(line) - 1;  /* 截断保护 */

    /* 若新行放不下, 先刷盘腾出空间 */
    if (s_csv_len + (uint32_t)n >= CSV_BUF_SIZE)
    {
        flush_csv_buffer();
    }

    /* 将新行追加到缓冲区 */
    memcpy(&s_csv_buf[s_csv_len], line, n);
    s_csv_len += n;
    return 0;
}

int DataLogger_Flush(void)
{
    return flush_csv_buffer();
}

/* ======================================================
 *  公共 API: 错误/事件日志
 * ====================================================== */

int DataLogger_LogError(const char *fmt, ...)
{
    if (!s_initialized) return -1;
    lfs_t *lfs = LfsPort_Get();
    if (!lfs) return -2;

    char line[LINE_BUF_SIZE];

    /* 写入时间戳前缀: "[运行秒数] " */
    int head = snprintf(line, sizeof(line), "[%lu] ",
                        (unsigned long)(HAL_GetTick() / 1000U));
    if (head < 0 || head >= (int)sizeof(line)) return -3;

    /* 格式化用户消息 (追加到时间戳后面) */
    va_list ap;
    va_start(ap, fmt);
    int body = vsnprintf(&line[head], sizeof(line) - head - 2, fmt, ap);
    va_end(ap);

    if (body < 0) return -4;

    /* 计算总长度并添加 \r\n 行尾 */
    int total = head + body;
    if (total > (int)sizeof(line) - 2) total = sizeof(line) - 2;
    line[total++] = '\r';
    line[total++] = '\n';

    /* 构建当天的日志文件路径并追加写入 */
    char path[32];
    build_log_path(path, sizeof(path), s_meta.day_index);

    lfs_file_t file;
    int err = lfs_file_opencfg(lfs, &file, path,
                               LFS_O_WRONLY | LFS_O_CREAT | LFS_O_APPEND, &s_file_cfg);
    if (err) return err;

    lfs_file_write(lfs, &file, line, total);
    lfs_file_close(lfs, &file);  /* 立即刷盘, 确保日志不因掉电丢失 */
    return 0;
}

/* ======================================================
 *  公共 API: 日切 (切换到新的一天)
 * ====================================================== */

int DataLogger_RollDay(void)
{
    if (!s_initialized) return -1;

    /* 先将当前缓冲数据写入当天文件 */
    flush_csv_buffer();

    /* 日序号 +1, 后续数据写入新文件 */
    s_meta.day_index++;
    return meta_save();  /* 持久化新的日序号 */
}

/* ======================================================
 *  内部函数: 清理指定目录中的过期文件
 * ====================================================== */

/**
 * @brief  遍历目录, 删除日序号小于 keep_from_day 的文件.
 * @note   文件名格式为 NNNNNN.xxx, 直接解析前缀数字得到日序号.
 * @param  dir           目录路径 ("/data" 或 "/log")
 * @param  keep_from_day 保留起始日序号 (小于此值的文件被删除)
 * @retval 成功删除的文件数; -1 表示目录打开失败
 */
static int cleanup_dir(const char *dir, uint32_t keep_from_day)
{
    lfs_t *lfs = LfsPort_Get();
    lfs_dir_t d;

    if (lfs_dir_open(lfs, &d, dir) != LFS_ERR_OK) return -1;

    struct lfs_info info;
    int removed = 0;
    /* lfs_dir_read 返回 > 0 表示读到了一个有效条目 */
    while (lfs_dir_read(lfs, &d, &info) > 0)
    {
        if (info.type != LFS_TYPE_REG) continue;  /* 跳过子目录 */

        /* 解析文件名: "NNNNNN.ext" -> 取前缀数字作为日序号 */
        uint32_t day = (uint32_t)strtoul(info.name, NULL, 10);
        if (day == 0 || day >= keep_from_day) continue;  /* 0 无效 / 在保留范围内 */

        /* 拼接完整路径并删除 */
        char full[48];
        snprintf(full, sizeof(full), "%s/%s", dir, info.name);
        if (lfs_remove(lfs, full) == LFS_ERR_OK) removed++;
    }
    lfs_dir_close(lfs, &d);
    return removed;
}

/* ======================================================
 *  公共 API: 清理过期文件
 * ====================================================== */

int DataLogger_Cleanup(uint32_t keep_days)
{
    if (!s_initialized) return -1;

    /* 保留天数为 0 或超出历史范围, 不执行删除 */
    if (keep_days == 0 || keep_days >= s_meta.day_index) return 0;

    /* 计算需要保留的最小日序号 */
    uint32_t keep_from = s_meta.day_index - keep_days;

    int n1 = cleanup_dir(DATA_DIR, keep_from);  /* 清理数据文件 */
    int n2 = cleanup_dir(LOG_DIR,  keep_from);  /* 清理日志文件 */

    printf("[LOG] Cleanup: %d data + %d log files removed (kept %lu days)\r\n",
           n1, n2, (unsigned long)keep_days);
    return n1 + n2;
}

/* ======================================================
 *  公共 API: 查询可用空间
 * ====================================================== */

uint32_t DataLogger_GetFreeBytes(void)
{
    lfs_t *lfs = LfsPort_Get();
    if (!lfs) return 0;

    /* lfs_fs_size 返回已使用的块数 */
    lfs_ssize_t used = lfs_fs_size(lfs);
    if (used < 0) return 0;

    uint32_t total_bytes = LFS_PORT_BLOCK_COUNT * LFS_PORT_BLOCK_SIZE;
    uint32_t used_bytes  = (uint32_t)used * LFS_PORT_BLOCK_SIZE;

    return (total_bytes > used_bytes) ? (total_bytes - used_bytes) : 0;
}
