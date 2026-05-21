/**
 * @file    sys_system.c
 * @brief   系统监控与控制模块实现
 *
 * /proc 文件系统读写: fopen/fgets/sscanf 完成读, fopen/fprintf 完成写。
 * 嵌入式环境无 getprocsys/vm, 直接操作 /proc/sys/vm 伪文件。
 */

#include "sys_system.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* ====================================================================
 *  内部 helper: 读取 /proc/sys/vm/ 下的一个无符号整数
 * ==================================================================== */
static td_s32 read_vm_uint(const td_char *name, td_u32 *val)
{
    FILE *fp;
    td_char path[128];

    if (!name || !val) { return -1; }

    snprintf(path, sizeof(path), "/proc/sys/vm/%s", name);
    fp = fopen(path, "r");
    if (!fp) { return -1; }

    if (fscanf(fp, "%u", val) != 1) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/* ====================================================================
 *  内部 helper: 写入 /proc/sys/vm/ 下的一个无符号整数
 * ==================================================================== */
static td_s32 write_vm_uint(const td_char *name, td_u32 val)
{
    FILE *fp;
    td_char path[128];

    if (!name) { return -1; }

    snprintf(path, sizeof(path), "/proc/sys/vm/%s", name);
    fp = fopen(path, "w");
    if (!fp) { return -1; }

    if (fprintf(fp, "%u", val) < 0) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}

/* ====================================================================
 *  /proc/sys/vm/ 读写 API
 * ==================================================================== */

td_s32 sys_system_get_min_free_kbytes(td_u32 *val)
{
    return read_vm_uint("min_free_kbytes", val);
}

td_s32 sys_system_set_min_free_kbytes(td_u32 val)
{
    return write_vm_uint("min_free_kbytes", val);
}

td_s32 sys_system_get_admin_reserve_kbytes(td_u32 *val)
{
    return read_vm_uint("admin_reserve_kbytes", val);
}

td_s32 sys_system_set_admin_reserve_kbytes(td_u32 val)
{
    return write_vm_uint("admin_reserve_kbytes", val);
}

td_s32 sys_system_get_vfs_cache_pressure(td_u32 *val)
{
    return read_vm_uint("vfs_cache_pressure", val);
}

td_s32 sys_system_set_vfs_cache_pressure(td_u32 val)
{
    return write_vm_uint("vfs_cache_pressure", val);
}

/* ====================================================================
 *  可用内存查询
 * ==================================================================== */

td_s32 sys_system_get_mem_available(td_u32 *kb)
{
    FILE  *fp;
    td_char line[128];

    if (!kb) { return -1; }

    fp = fopen("/proc/meminfo", "r");
    if (!fp) { return -1; }

    *kb = 0;
    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%u", kb);
            break;
        }
    }
    fclose(fp);
    return (*kb > 0) ? 0 : -1;
}

/* ====================================================================
 *  CPU 使用率 — 两次 /proc/stat 采样取差值 (间隔 1 秒)
 * ==================================================================== */

td_s32 sys_system_get_cpu_usage(double *percent)
{
    td_u64 a[8], b[8];
    td_u64 total_a, idle_a, total_b, idle_b;
    td_u64 total_delta, idle_delta;
    FILE  *fp;
    td_char line[256];
    td_s32 n;

    if (!percent) { return -1; }

    /* 第一次采样 */
    fp = fopen("/proc/stat", "r");
    if (!fp) { return -1; }
    if (fgets(line, sizeof(line), fp) == NULL) { fclose(fp); return -1; }
    n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7]);
    fclose(fp);
    if (n < 4) { return -1; }

    usleep(1000000);  /* 间隔 1 秒 */

    /* 第二次采样 */
    fp = fopen("/proc/stat", "r");
    if (!fp) { return -1; }
    if (fgets(line, sizeof(line), fp) == NULL) { fclose(fp); return -1; }
    n = sscanf(line, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5], &b[6], &b[7]);
    fclose(fp);
    if (n < 4) { return -1; }

    total_a = a[0] + a[1] + a[2] + a[3] + a[4] + a[5] + a[6] + a[7];
    idle_a  = a[3] + a[4];  /* iowait 也算 idle */
    total_b = b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + b[6] + b[7];
    idle_b  = b[3] + b[4];

    total_delta = total_b - total_a;
    idle_delta  = idle_b  - idle_a;

    if (total_delta == 0) {
        *percent = 0.0;
    } else {
        *percent = 100.0 - ((double)idle_delta * 100.0 / (double)total_delta);
        if (*percent < 0.0) { *percent = 0.0; }
        if (*percent > 100.0) { *percent = 100.0; }
    }
    return 0;
}

/* ====================================================================
 *  drop_caches
 * ==================================================================== */

td_s32 sys_system_drop_cache(td_u32 mode)
{
    FILE *fp;

    if (mode < 1 || mode > 3) { return -1; }

    /* 先 sync 落盘, 避免丢数据 */
    sync();

    fp = fopen("/proc/sys/vm/drop_caches", "w");
    if (!fp) { return -1; }

    if (fprintf(fp, "%u", mode) < 0) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return 0;
}
