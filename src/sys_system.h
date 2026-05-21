/**
 * @file    sys_system.h
 * @brief   系统监控与控制模块 — /proc/sys/vm 读写 + 内存/CPU 查询 + drop_caches
 *
 * 提供:
 *   - min_free_kbytes / admin_reserve_kbytes / vfs_cache_pressure 的 get/set
 *   - 可用内存 (MemAvailable) 查询
 *   - CPU 使用率 (两次 /proc/stat 采样取差值)
 *   - drop_caches 触发 (1=pagecache, 2=dentries+inodes, 3=all)
 */

#ifndef SYS_SYSTEM_H
#define SYS_SYSTEM_H

#include "ot_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 *  /proc/sys/vm/ 读写
 * ==================================================================== */

/**
 * @brief  读取 /proc/sys/vm/min_free_kbytes
 * @param  val  输出: 值 (KB)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_get_min_free_kbytes(td_u32 *val);

/**
 * @brief  写入 /proc/sys/vm/min_free_kbytes
 * @param  val  目标值 (KB)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_set_min_free_kbytes(td_u32 val);

/**
 * @brief  读取 /proc/sys/vm/admin_reserve_kbytes
 * @param  val  输出: 值 (KB)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_get_admin_reserve_kbytes(td_u32 *val);

/**
 * @brief  写入 /proc/sys/vm/admin_reserve_kbytes
 * @param  val  目标值 (KB)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_set_admin_reserve_kbytes(td_u32 val);

/**
 * @brief  读取 /proc/sys/vm/vfs_cache_pressure
 * @param  val  输出: 值 (百分比)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_get_vfs_cache_pressure(td_u32 *val);

/**
 * @brief  写入 /proc/sys/vm/vfs_cache_pressure
 * @param  val  目标值 (百分比)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_set_vfs_cache_pressure(td_u32 val);

/* ====================================================================
 *  系统状态查询
 * ==================================================================== */

/**
 * @brief  查询可用内存 (从 /proc/meminfo 读取 MemAvailable)
 * @param  kb  输出: 可用内存 (KB)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_get_mem_available(td_u32 *kb);

/**
 * @brief  查询 CPU 使用率 (两次 /proc/stat 采样, 间隔 1 秒取差值)
 *
 * 内部 usleep(1000000) 阻塞 1 秒采样。
 * CPU 空闲时返回接近 0%, 满载返回接近 100%.
 *
 * @param  percent  输出: CPU 使用率 (0.0 ~ 100.0)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_get_cpu_usage(double *percent);

/* ====================================================================
 *  drop_caches
 * ==================================================================== */

/**
 * @brief  释放内核缓存 (写入 /proc/sys/vm/drop_caches)
 * @param  mode  1=pagecache, 2=dentries+inodes, 3=all (先 sync 后 drop)
 * @return 0 成功, -1 失败
 */
td_s32 sys_system_drop_cache(td_u32 mode);

#ifdef __cplusplus
}
#endif

#endif /* SYS_SYSTEM_H */
