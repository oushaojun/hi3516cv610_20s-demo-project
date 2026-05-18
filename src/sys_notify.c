/**
 * @file    sys_notify.c
 * @brief   Hi3516CV610 发布-订阅通知模块实现
 *
 * 设计要点:
 *   - 固定容量订阅表 (MAX_SUBSCRIBERS), 无动态分配
 *   - publish 时只遍历 key 匹配 + mask 命中 的订阅者
 *   - 严格 N * O(1): 每次遍历 = 一次整数比较 + 一次位与
 */

#include "sys_notify.h"
#include "sys_dbg.h"
#include <pthread.h>
#include <string.h>

/* ====================================================================
 *  配置
 * ==================================================================== */

#define MAX_SUBSCRIBERS  32       /**< 最大订阅者数 */

#define MOD "NOTIFY"

/* ====================================================================
 *  内部类型
 * ==================================================================== */

typedef struct {
    sys_notify_key_e  key;       /**< 事件大类 */
    td_u32            mask;      /**< 关心的子事件位掩码 */
    sys_notify_cb     cb;        /**< 回调函数 */
    td_void          *ctx;       /**< 回调上下文 */
} subscriber_t;

/* ====================================================================
 *  全局状态
 * ==================================================================== */

static subscriber_t    g_subs[MAX_SUBSCRIBERS];
static td_s32          g_sub_cnt = 0;
static pthread_mutex_t g_lock    = PTHREAD_MUTEX_INITIALIZER;
static td_bool         g_inited  = TD_FALSE;

/* ====================================================================
 *  生命周期
 * ==================================================================== */

td_s32 sys_notify_init(td_void)
{
    td_s32 ret;

    if (g_inited) {
        DBG_WARN(MOD, "already initialized");
        return TD_SUCCESS;
    }

    ret = pthread_mutex_init(&g_lock, NULL);
    if (ret != 0) {
        DBG_ERROR(MOD, "mutex init failed: %d", ret);
        return TD_FAILURE;
    }

    g_sub_cnt = 0;
    memset(g_subs, 0, sizeof(g_subs));
    g_inited = TD_TRUE;

    DBG_LOG(MOD, "initialized (max_subs=%d)", MAX_SUBSCRIBERS);
    return TD_SUCCESS;
}

td_void sys_notify_deinit(td_void)
{
    if (!g_inited) { return; }

    pthread_mutex_lock(&g_lock);
    {
        g_sub_cnt = 0;
        memset(g_subs, 0, sizeof(g_subs));
        g_inited = TD_FALSE;
    }
    pthread_mutex_unlock(&g_lock);

    pthread_mutex_destroy(&g_lock);

    DBG_LOG(MOD, "deinitialized");
}

/* ====================================================================
 *  订阅
 * ==================================================================== */

td_s32 sys_notify_subscribe(sys_notify_key_e key, td_u32 mask,
                             sys_notify_cb cb, td_void *ctx)
{
    td_s32 ret = TD_FAILURE;

    if (cb == NULL) {
        DBG_ERROR(MOD, "subscribe: cb is NULL");
        return TD_FAILURE;
    }
    if (key >= SYS_NOTIFY_MAX) {
        DBG_ERROR(MOD, "subscribe: key %d out of range", (td_s32)key);
        return TD_FAILURE;
    }

    pthread_mutex_lock(&g_lock);
    {
        if (g_sub_cnt >= MAX_SUBSCRIBERS) {
            DBG_WARN(MOD, "subscribe: table full (max=%d)", MAX_SUBSCRIBERS);
            goto out;
        }

        g_subs[g_sub_cnt].key = key;
        g_subs[g_sub_cnt].mask = mask;
        g_subs[g_sub_cnt].cb = cb;
        g_subs[g_sub_cnt].ctx = ctx;
        g_sub_cnt++;

        DBG_LOG(MOD, "subscribe: key=%d mask=0x%x cb=%p (total=%d)",
                (td_s32)key, mask, (td_void *)cb, g_sub_cnt);
        ret = TD_SUCCESS;
    }
out:
    pthread_mutex_unlock(&g_lock);
    return ret;
}

/* ====================================================================
 *  取消订阅
 * ==================================================================== */

td_s32 sys_notify_unsubscribe(sys_notify_key_e key, td_u32 mask,
                               sys_notify_cb cb, td_void *ctx)
{
    td_s32 removed = 0;
    td_s32 i;

    pthread_mutex_lock(&g_lock);
    {
        for (i = 0; i < g_sub_cnt; ) {
            if (g_subs[i].key  == key &&
                g_subs[i].mask == mask &&
                g_subs[i].cb   == cb &&
                g_subs[i].ctx  == ctx) {

                /* 将最后一个复制到当前位置 (O(1) 删除) */
                if (i < g_sub_cnt - 1) {
                    g_subs[i] = g_subs[g_sub_cnt - 1];
                }
                g_sub_cnt--;
                removed++;
                /* 不递增 i, 因为可能新复制过来的也需要匹配 */
            } else {
                i++;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);

    if (removed > 0) {
        DBG_LOG(MOD, "unsubscribe: key=%d mask=0x%x cb=%p (removed=%d left=%d)",
                (td_s32)key, mask, (td_void *)cb, removed, g_sub_cnt);
        return TD_SUCCESS;
    }

    DBG_WARN(MOD, "unsubscribe: key=%d mask=0x%x cb=%p not found",
             (td_s32)key, mask, (td_void *)cb);
    return TD_FAILURE;
}

/* ====================================================================
 *  发布
 * ==================================================================== */

td_void sys_notify_publish(sys_notify_key_e key, td_u32 event, td_void *data)
{
    td_s32 i;
    td_s32 fired = 0;

    pthread_mutex_lock(&g_lock);
    {
        for (i = 0; i < g_sub_cnt; i++) {
            if (g_subs[i].key != key) { continue; }

            /* mask 为 0 表示订阅该 key 下所有事件 */
            if (g_subs[i].mask != 0 && !(g_subs[i].mask & event)) {
                continue;
            }

            g_subs[i].cb(g_subs[i].ctx, g_subs[i].key, event, data);
            fired++;
        }
    }
    pthread_mutex_unlock(&g_lock);

    DBG_TRACE(MOD, "publish: key=%d event=0x%x fired=%d", (td_s32)key, event, fired);
}
