/**
 * @file    sys_notify.h
 * @brief   Hi3516CV610 发布-订阅通知模块
 *
 * 轻量级事件总线:
 *   - 发布者按 key (enum) + event (bitmask) 发布消息
 *   - 订阅者按 key + mask 注册回调, 只有匹配位的事件才会触发
 *   - 临界区内路由匹配为 O(subscriber_count), 单次 & 操作
 *
 * 用法:
 *   sys_notify_init();
 *
 *   sys_notify_subscribe(SYS_NOTIFY_VENC,
 *                        SYS_NOTIFY_VENC_START | SYS_NOTIFY_VENC_STOP,
 *                        my_callback, ctx);
 *
 *   sys_notify_publish(SYS_NOTIFY_VENC, SYS_NOTIFY_VENC_START, &info);
 *
 *   sys_notify_unsubscribe(SYS_NOTIFY_VENC,
 *                          SYS_NOTIFY_VENC_START | SYS_NOTIFY_VENC_STOP,
 *                          my_callback, ctx);
 *
 *   sys_notify_deinit();
 *
 * 线程安全: publish/subscribe/unsubscribe 由内部 mutex 保护
 * 注意: 回调在 publish 调用者线程中同步执行, 且锁未释放时不可重入 publish
 */

#ifndef SYS_NOTIFY_H
#define SYS_NOTIFY_H

#include "ot_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ====================================================================
 *  Event Key 枚举
 * ==================================================================== */

/** 事件大类 (发布/订阅的 topic) */
typedef enum {
    SYS_NOTIFY_VENC     = 0,  /**< 编码通道事件 */
    SYS_NOTIFY_IRCUT    = 1,  /**< 红外滤光片事件 */
    SYS_NOTIFY_SYSTEM   = 2,  /**< 系统状态事件 */
    SYS_NOTIFY_DISCOVER = 3,  /**< 设备发现事件 */

    SYS_NOTIFY_MAX             /**< 仅作上限, 勿用于发布/订阅 */
} sys_notify_key_e;

/* ====================================================================
 *  Sub-event Bitmask
 * ==================================================================== */

/** SYS_NOTIFY_VENC — 编码通道子事件 */
#define SYS_NOTIFY_VENC_START            (1 << 0)  /**< 通道启动 */
#define SYS_NOTIFY_VENC_STOP             (1 << 1)  /**< 通道停止 */
#define SYS_NOTIFY_VENC_FRAME_DROP       (1 << 2)  /**< 丢帧 */
#define SYS_NOTIFY_VENC_KEY_FRAME        (1 << 3)  /**< IDR 帧 */
#define SYS_NOTIFY_VENC_BITRATE_CHG      (1 << 4)  /**< 码率/RC 变化 */

/** SYS_NOTIFY_IRCUT — 红外滤光片子事件 */
#define SYS_NOTIFY_IRCUT_CUT             (1 << 0)  /**< 红外滤光片切入 (夜) */
#define SYS_NOTIFY_IRCUT_OPEN            (1 << 1)  /**< 红外滤光片切出 (昼) */

/** SYS_NOTIFY_SYSTEM — 系统状态子事件 */
#define SYS_NOTIFY_SYSTEM_CPU_HIGH       (1 << 0)  /**< CPU 占用过高 */
#define SYS_NOTIFY_SYSTEM_MEM_LOW        (1 << 1)  /**< 可用内存过低 */
#define SYS_NOTIFY_SYSTEM_TEMP_HIGH      (1 << 2)  /**< 芯片温度过高 */
#define SYS_NOTIFY_SYSTEM_SD_INSERT      (1 << 3)  /**< SD 卡插入并挂载成功 */
#define SYS_NOTIFY_SYSTEM_SD_REMOVE      (1 << 4)  /**< SD 卡拔出并卸载成功 */

/** SYS_NOTIFY_DISCOVER — 设备发现子事件 */
#define SYS_NOTIFY_DISCOVER_QUERY        (1 << 0)  /**< 收到客户端查询 */
#define SYS_NOTIFY_DISCOVER_RESP         (1 << 1)  /**< 已回复客户端 */

/* ====================================================================
 *  回调类型
 * ==================================================================== */

/**
 * @brief 通知回调
 *
 * 在 publish() 调用者线程中同步执行, 不能长时间阻塞.
 *
 * @param ctx    订阅时传入的上下文
 * @param key    触发的事件大类
 * @param event  本次发布的 event 位 (通常只有 1 位置位, 也可能复合)
 * @param data   发布者传入的附加数据 (可为 NULL)
 */
typedef td_void (*sys_notify_cb)(td_void *ctx, sys_notify_key_e key,
                                  td_u32 event, td_void *data);

/* ====================================================================
 *  生命周期
 * ==================================================================== */

/**
 * @brief  初始化通知模块
 * @return TD_SUCCESS / TD_FAILURE
 */
td_s32 sys_notify_init(td_void);

/**
 * @brief  反初始化, 清空所有订阅
 */
td_void sys_notify_deinit(td_void);

/* ====================================================================
 *  订阅 / 取消
 * ==================================================================== */

/**
 * @brief  订阅事件
 *
 * @param  key   事件大类
 * @param  mask  关心的子事件位掩码 (0 = 订阅该 key 下所有事件)
 * @param  cb    回调 (不可为 NULL)
 * @param  ctx   传递给回调的上下文 (可为 NULL)
 * @return TD_SUCCESS / TD_FAILURE (订阅表已满)
 */
td_s32 sys_notify_subscribe(sys_notify_key_e key, td_u32 mask,
                             sys_notify_cb cb, td_void *ctx);

/**
 * @brief  取消订阅
 *
 * 匹配 (key, mask, cb, ctx) 完全相同的条目并移除.
 *
 * @return TD_SUCCESS / TD_FAILURE (未找到匹配项)
 */
td_s32 sys_notify_unsubscribe(sys_notify_key_e key, td_u32 mask,
                               sys_notify_cb cb, td_void *ctx);

/* ====================================================================
 *  发布
 * ==================================================================== */

/**
 * @brief  发布事件 — 回调所有匹配该 key 且 event 命中 mask 的订阅者
 *
 * @param key   事件大类
 * @param event 子事件位 (可多位同时发布, 如 VENC_START | VENC_KEY_FRAME)
 * @param data  附加数据 (可为 NULL)
 */
td_void sys_notify_publish(sys_notify_key_e key, td_u32 event, td_void *data);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* SYS_NOTIFY_H */
