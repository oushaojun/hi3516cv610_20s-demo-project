/**
 * @file    sys_uevent.h
 * @brief   Hi3516CV610 内核 uevent 监听模块
 *
 * 通过 netlink socket 订阅内核 KOBJ_UEVENT 广播,
 * 将原始 uevent 消息解析后分发给已注册的回调函数。
 *
 * 本模块是纯粹的 "事件源 → 回调" 转发器，不感知具体设备类型。
 * 上层模块 (如 SD 卡、USB 热插拔) 通过注册回调来消费事件。
 *
 * 用法:
 *   sys_uevent_init();
 *
 *   // 注册 SD 卡监听回调
 *   sys_uevent_register(sd_card_ev_handler, ctx);
 *
 *   ...
 *
 *   sys_uevent_unregister(sd_card_ev_handler, ctx);
 *   sys_uevent_deinit();
 *
 * 线程模型:
 *   内部创建后台线程 (recvfrom 阻塞 + 1s 超时), 支持干净退出。
 *   回调在后台线程上下文中执行，不应长时间阻塞。
 */

#ifndef SYS_UEVENT_H
#define SYS_UEVENT_H

#include "ot_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ====================================================================
 *  uevent 事件结构
 * ==================================================================== */

/** 单条 uevent 的解析结果 */
typedef struct {
    td_char  action[32];      /**< "add" / "remove" / "change" / "move" */
    td_char  devpath[256];    /**< 内核设备路径, e.g. "/devices/.../mmcblk0" */
    td_char  subsystem[64];   /**< 子系统, e.g. "block" "usb" */
    td_char  devname[64];     /**< 设备名, e.g. "mmcblk0" "mmcblk0p1" */
} uevent_t;

/* ====================================================================
 *  回调类型
 * ==================================================================== */

/**
 * @brief  uevent 回调
 *
 * 在后台线程中调用, 不应长时间阻塞.
 *
 * @param  ev  解析后的 uevent 事件 (栈变量, 回调返回后失效)
 * @param  ctx 注册时传入的上下文
 */
typedef td_void (*uevent_cb_t)(const uevent_t *ev, td_void *ctx);

/* ====================================================================
 *  生命周期
 * ==================================================================== */

/**
 * @brief  初始化 uevent 监听, 创建 netlink socket 并启动后台线程
 * @return TD_SUCCESS / TD_FAILURE
 */
td_s32 sys_uevent_init(td_void);

/**
 * @brief  停止后台线程, 关闭 socket, 清空回调列表
 */
td_void sys_uevent_deinit(td_void);

/* ====================================================================
 *  回调注册 / 注销
 * ==================================================================== */

/**
 * @brief  注册 uevent 回调
 * @param  cb   回调函数 (不可为 NULL)
 * @param  ctx  上下文 (可为 NULL)
 * @return TD_SUCCESS / TD_FAILURE (表已满)
 */
td_s32 sys_uevent_register(uevent_cb_t cb, td_void *ctx);

/**
 * @brief  注销 uevent 回调 (匹配 cb + ctx)
 * @return TD_SUCCESS / TD_FAILURE (未找到)
 */
td_s32 sys_uevent_unregister(uevent_cb_t cb, td_void *ctx);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* SYS_UEVENT_H */
