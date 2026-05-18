/**
 * @file    sys_sd.h
 * @brief   Hi3516CV610 SD 卡热插拔管理模块
 *
 * 通过 sys_uevent 监听 /dev/mmcblk0p1 的插入/拔出,
 * 自动执行 mount/umount 到 /mnt/sdcard,
 * 并通过 sys_notify 发布 SYS_NOTIFY_SYSTEM_SD_INSERT / _REMOVE 事件.
 *
 * 用法:
 *   sys_sd_init();                       // 检测当前状态 + 注册 uevent 回调
 *   if (sys_sd_is_mounted()) { ... }     // 查询挂载状态
 *   sys_sd_deinit();                     // 注销回调
 */

#ifndef SYS_SD_H
#define SYS_SD_H

#include "ot_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ====================================================================
 *  生命周期
 * ==================================================================== */

/**
 * @brief  初始化 SD 卡管理
 *
 * 1. 检测 /dev/mmcblk0p1 是否已挂载到 /mnt/sdcard
 * 2. 注册 uevent 回调, 监听 mmcblk0p1 事件
 *
 * @return TD_SUCCESS / TD_FAILURE
 */
td_s32 sys_sd_init(td_void);

/**
 * @brief  反初始化, 注销 uevent 回调
 */
td_void sys_sd_deinit(td_void);

/* ====================================================================
 *  状态查询
 * ==================================================================== */

/**
 * @brief  查询 SD 卡当前是否已挂载
 * @return TD_TRUE / TD_FALSE
 */
td_bool sys_sd_is_mounted(td_void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* SYS_SD_H */
