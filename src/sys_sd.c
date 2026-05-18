/**
 * @file    sys_sd.c
 * @brief   Hi3516CV610 SD 卡热插拔管理模块实现
 *
 * 监听 uevent 中 subsystem=="block" && devname=="mmcblk0p1" 的事件,
 * add → mount /mnt/sdcard, remove → umount /mnt/sdcard.
 * 状态变化时通过 sys_notify 发布 SD_INSERT / SD_REMOVE 事件.
 */

#include "sys_sd.h"
#include "sys_dbg.h"
#include "sys_notify.h"
#include "sys_uevent.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mount.h>
#include <sys/stat.h>

#define MOD "SD"

/* ====================================================================
 *  常量
 * ==================================================================== */

#define SD_DEVICE       "/dev/mmcblk0p1"
#define SD_MOUNT_POINT  "/mnt/sdcard"
#define SD_FS_TYPE      "vfat"

/* mount 重试 (设备节点可能稍晚于 uevent 出现) */
#define MOUNT_RETRY_MAX    10
#define MOUNT_RETRY_US     50000   /* 50ms */

/* ====================================================================
 *  全局状态
 * ==================================================================== */

static volatile td_bool g_mounted = TD_FALSE;
static td_bool          g_inited  = TD_FALSE;

/* ====================================================================
 *  内部辅助
 * ==================================================================== */

/**
 * @brief  解析 /proc/mounts, 判断 /mnt/sdcard 是否已挂载
 */
static td_bool check_mounted(td_void)
{
    FILE  *fp;
    td_char line[512];
    td_bool found = TD_FALSE;

    fp = fopen("/proc/mounts", "r");
    if (fp == NULL) {
        DBG_WARN(MOD, "cannot open /proc/mounts");
        return TD_FALSE;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        td_char dev[128], mnt[128];
        if (sscanf(line, "%127s %127s", dev, mnt) == 2) {
            if (strcmp(mnt, SD_MOUNT_POINT) == 0) {
                found = TD_TRUE;
                break;
            }
        }
    }
    fclose(fp);
    return found;
}

/**
 * @brief  挂载 SD 卡 (含重试)
 * @return TD_SUCCESS / TD_FAILURE
 */
static td_s32 do_mount(td_void)
{
    td_s32 i;
    td_s32 ret;

    /* 创建挂载目录 (幂等) */
    mkdir(SD_MOUNT_POINT, 0755);

    for (i = 0; i < MOUNT_RETRY_MAX; i++) {
        ret = mount(SD_DEVICE, SD_MOUNT_POINT, SD_FS_TYPE, MS_NOATIME, TD_NULL);
        if (ret == 0) {
            g_mounted = TD_TRUE;
            DBG_LOG(MOD, "mounted %s -> %s", SD_DEVICE, SD_MOUNT_POINT);
            return TD_SUCCESS;
        }
        if (errno == ENOENT || errno == ENXIO) {
            /* 设备节点尚未就绪 */
            usleep(MOUNT_RETRY_US);
            continue;
        }
        /* 其他错误 (如已挂载) 不重试 */
        break;
    }

    DBG_WARN(MOD, "mount %s failed: %s (retry=%d)",
             SD_DEVICE, strerror(errno), i);
    return TD_FAILURE;
}

/**
 * @brief  卸载 SD 卡
 * @return TD_SUCCESS / TD_FAILURE
 */
static td_s32 do_umount(td_void)
{
    td_s32 ret;

    ret = umount(SD_MOUNT_POINT);
    if (ret == 0) {
        g_mounted = TD_FALSE;
        DBG_LOG(MOD, "umounted %s", SD_MOUNT_POINT);
        return TD_SUCCESS;
    }

    /* EBUSY: 有进程仍在使用挂载点, 可考虑稍后重试 */
    DBG_WARN(MOD, "umount %s failed: %s", SD_MOUNT_POINT, strerror(errno));
    return TD_FAILURE;
}

/* ====================================================================
 *  uevent 回调
 * ==================================================================== */

static td_void on_uevent(const uevent_t *ev, td_void *ctx)
{
    (td_void)ctx;

    /* 只关心 block 子系统中名为 mmcblk0p1 的设备 */
    if (strcmp(ev->subsystem, "block") != 0) { return; }
    if (strcmp(ev->devname, "mmcblk0p1") != 0) { return; }

    if (strcmp(ev->action, "add") == 0) {
        if (g_mounted) {
            DBG_WARN(MOD, "add: already mounted, skip");
            return;
        }
        if (do_mount() == TD_SUCCESS) {
            sys_notify_publish(SYS_NOTIFY_SYSTEM, SYS_NOTIFY_SYSTEM_SD_INSERT,
                               TD_NULL);
        }

    } else if (strcmp(ev->action, "remove") == 0) {
        if (!g_mounted) {
            DBG_WARN(MOD, "remove: not mounted, skip");
            return;
        }
        if (do_umount() == TD_SUCCESS) {
            sys_notify_publish(SYS_NOTIFY_SYSTEM, SYS_NOTIFY_SYSTEM_SD_REMOVE,
                               TD_NULL);
        }
    }
}

/* ====================================================================
 *  生命周期
 * ==================================================================== */

td_s32 sys_sd_init(td_void)
{
    td_s32 ret;

    if (g_inited) {
        DBG_WARN(MOD, "already initialized");
        return TD_SUCCESS;
    }

    /* 1. 检测当前挂载状态 */
    g_mounted = check_mounted();
    DBG_LOG(MOD, "init: mounted=%s", g_mounted ? "yes" : "no");
    if (g_mounted) {
        sys_notify_publish(SYS_NOTIFY_SYSTEM, SYS_NOTIFY_SYSTEM_SD_INSERT,
                           TD_NULL);
    } else {
        /* 卡已插入但未挂载 (如刚开机 mdev 未处理) */
        if (access(SD_DEVICE, F_OK) == 0) {
            DBG_LOG(MOD, "init: device exists but not mounted, try mount");
            if (do_mount() == TD_SUCCESS) {
                sys_notify_publish(SYS_NOTIFY_SYSTEM,
                                   SYS_NOTIFY_SYSTEM_SD_INSERT, TD_NULL);
            }
        }
    }

    /* 2. 注册 uevent 回调 */
    ret = sys_uevent_register(on_uevent, TD_NULL);
    if (ret != TD_SUCCESS) {
        DBG_ERROR(MOD, "uevent register failed");
        return TD_FAILURE;
    }

    g_inited = TD_TRUE;
    return TD_SUCCESS;
}

td_void sys_sd_deinit(td_void)
{
    if (!g_inited) { return; }

    sys_uevent_unregister(on_uevent, TD_NULL);
    g_inited = TD_FALSE;

    DBG_LOG(MOD, "deinitialized");
}

/* ====================================================================
 *  状态查询
 * ==================================================================== */

td_bool sys_sd_is_mounted(td_void)
{
    return g_mounted;
}
