/**
 * @file    sys_uevent.c
 * @brief   Hi3516CV610 内核 uevent 监听模块实现
 *
 * 通过 netlink socket 订阅内核 KOBJ_UEVENT 组播,
 * 后台线程轮询接收 (poll, 1s 超时), 解析并回调注册的监听者。
 */

#include "sys_uevent.h"
#include "sys_dbg.h"
#include "sys_thread.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <linux/netlink.h>

#define MOD "UEVENT"

/* ====================================================================
 *  配置
 * ==================================================================== */

#define MAX_LISTENERS    8        /**< 最大回调注册数 */
#define UEVENT_BUF_SIZE  (4096)   /**< 单次 recv 缓冲区 */

/* ====================================================================
 *  内部类型
 * ==================================================================== */

typedef struct {
    uevent_cb_t  cb;
    td_void     *ctx;
} listener_t;

/* ====================================================================
 *  全局状态
 * ==================================================================== */

static listener_t       g_list[MAX_LISTENERS];
static td_s32           g_list_cnt  = 0;
static td_s32           g_sock      = -1;
static thread_t         g_thread;
static volatile td_bool g_running   = TD_FALSE;
static td_bool          g_inited    = TD_FALSE;

/* ---- 锁 (保护 g_list/g_list_cnt) ---- */
static pthread_mutex_t  g_lock = PTHREAD_MUTEX_INITIALIZER;

/* ====================================================================
 *  uevent 消息解析
 * ==================================================================== */

/**
 * @brief  解析内核 uevent 消息
 *
 * 内核格式: "ACTION@DEVPATH\0KEY=VALUE\0KEY=VALUE\0...\0"
 *
 * @param buf  raw 消息缓冲区
 * @param len  消息长度
 * @param ev   输出, 解析结果
 */
static td_void parse_uevent(const td_char *buf, td_s32 len, uevent_t *ev)
{
    const td_char *p;
    const td_char *end = buf + len;

    memset(ev, 0, sizeof(*ev));

    /* ---- 第一段: "action@devpath" ---- */
    {
        const td_char *at = memchr(buf, '@', (size_t)(end - buf));
        td_s32 alen;

        if (at != NULL) {
            alen = (td_s32)(at - buf);
            if (alen >= (td_s32)sizeof(ev->action)) {
                alen = (td_s32)sizeof(ev->action) - 1;
            }
            memcpy(ev->action, buf, (size_t)alen);
            ev->action[alen] = '\0';

            /* devpath (从 @ 后到 \0) */
            p = at + 1;
            {
                const td_char *nul = memchr(p, '\0', (size_t)(end - p));
                td_s32 dlen;
                if (nul != NULL) {
                    dlen = (td_s32)(nul - p);
                    if (dlen >= (td_s32)sizeof(ev->devpath)) {
                        dlen = (td_s32)sizeof(ev->devpath) - 1;
                    }
                    memcpy(ev->devpath, p, (size_t)dlen);
                    ev->devpath[dlen] = '\0';
                    p = nul + 1;
                } else {
                    p = end;
                }
            }
        } else {
            /* 无 @ 符号, 取第一个 \0 为止 */
            const td_char *nul = memchr(buf, '\0', (size_t)(end - buf));
            td_s32 alen;
            if (nul != NULL) {
                alen = (td_s32)(nul - buf);
                p = nul + 1;
            } else {
                alen = len;
                p = end;
            }
            if (alen >= (td_s32)sizeof(ev->action)) {
                alen = (td_s32)sizeof(ev->action) - 1;
            }
            memcpy(ev->action, buf, (size_t)alen);
            ev->action[alen] = '\0';
        }
    }

    /* ---- 后续 KEY=VALUE 对 ---- */
    while (p < end && *p != '\0') {
        const td_char *nul  = memchr(p, '\0', (size_t)(end - p));
        td_s32         klen;
        const td_char *eq;

        if (nul == NULL) { break; }
        klen = (td_s32)(nul - p);

        eq = memchr(p, '=', (size_t)klen);
        if (eq != NULL) {
            td_s32 key_len = (td_s32)(eq - p);
            td_s32 val_len = klen - key_len - 1;
            const td_char *val = eq + 1;

            if (key_len == 9 && strncmp(p, "SUBSYSTEM", 9) == 0) {
                td_s32 n = (val_len < (td_s32)sizeof(ev->subsystem) - 1)
                           ? val_len : (td_s32)sizeof(ev->subsystem) - 1;
                memcpy(ev->subsystem, val, (size_t)n);
                ev->subsystem[n] = '\0';
            } else if (key_len == 7 && strncmp(p, "DEVNAME", 7) == 0) {
                td_s32 n = (val_len < (td_s32)sizeof(ev->devname) - 1)
                           ? val_len : (td_s32)sizeof(ev->devname) - 1;
                memcpy(ev->devname, val, (size_t)n);
                ev->devname[n] = '\0';
            }
        }

        p = nul + 1;
    }

    /* devname 兜底: 若内核没提供 DEVNAME, 从 devpath 末尾提取 */
    if (ev->devname[0] == '\0' && ev->devpath[0] != '\0') {
        const td_char *last = strrchr(ev->devpath, '/');
        if (last != NULL) {
            snprintf(ev->devname, sizeof(ev->devname), "%s", last + 1);
        }
    }
}

/* ====================================================================
 *  后台监听线程
 * ==================================================================== */

static td_void *uevent_thread(td_void *arg)
{
    td_char       buf[UEVENT_BUF_SIZE];
    td_s32        n;
    uevent_t      ev;

    (td_void)arg;
    DBG_LOG(MOD, "listener thread started");

    while (g_running) {
        struct pollfd pfd;

        pfd.fd     = g_sock;
        pfd.events = POLLIN;

        n = poll(&pfd, 1, 1000);   /* 1s 超时, 便于检测 g_running */
        if (n < 0) {
            if (errno == EINTR) { continue; }
            DBG_WARN(MOD, "poll error: %s", strerror(errno));
            break;
        }
        if (n == 0) { continue; }  /* 超时 */
        if (!(pfd.revents & POLLIN)) { continue; }

        n = (td_s32)recv(g_sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            if (errno == EAGAIN || errno == EINTR) { continue; }
            DBG_WARN(MOD, "recv error: %s (g_running=%d)",
                     strerror(errno), (td_s32)g_running);
            break;
        }
        buf[n] = '\0';

        /* 解析 */
        parse_uevent(buf, n, &ev);

        DBG_TRACE(MOD, "uevent: %s@%s sub=%s dev=%s",
                  ev.action, ev.devpath, ev.subsystem, ev.devname);

        /* 分发给所有注册回调 (快照模式: 复制列表后无锁回调) */
        {
            td_s32         i;
            td_s32         cnt;
            listener_t     snap[MAX_LISTENERS];

            pthread_mutex_lock(&g_lock);
            cnt = g_list_cnt;
            memcpy(snap, g_list, (size_t)cnt * sizeof(listener_t));
            pthread_mutex_unlock(&g_lock);

            for (i = 0; i < cnt; i++) {
                snap[i].cb(&ev, snap[i].ctx);
            }
        }
    }

    DBG_LOG(MOD, "listener thread exit");
    return TD_NULL;
}

/* ====================================================================
 *  生命周期
 * ==================================================================== */

td_s32 sys_uevent_init(td_void)
{
    td_s32             ret;
    struct sockaddr_nl sa;

    if (g_inited) {
        DBG_WARN(MOD, "already initialized");
        return TD_SUCCESS;
    }

    /* 1. 创建 netlink socket */
    g_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_KOBJECT_UEVENT);
    if (g_sock < 0) {
        DBG_ERROR(MOD, "netlink socket failed: %s", strerror(errno));
        return TD_FAILURE;
    }

    /* 2. 增大接收缓冲区 (避免事件突发丢包) */
    {
        td_s32 rcvbuf = 256 * 1024;  /* 256 KB */
        (td_void)setsockopt(g_sock, SOL_SOCKET, SO_RCVBUF,
                            &rcvbuf, sizeof(rcvbuf));
    }

    /* 3. bind — 订阅内核 uevent 组播 */
    memset(&sa, 0, sizeof(sa));
    sa.nl_family = AF_NETLINK;
    sa.nl_groups = 1;                /* kernel multicast group */
    sa.nl_pid    = getpid();

    ret = bind(g_sock, (struct sockaddr *)&sa, sizeof(sa));
    if (ret < 0) {
        DBG_ERROR(MOD, "bind failed: %s", strerror(errno));
        close(g_sock);
        g_sock = -1;
        return TD_FAILURE;
    }

    /* 4. 启动监听线程 */
    g_running  = TD_TRUE;
    g_list_cnt = 0;
    memset(g_list, 0, sizeof(g_list));

    ret = thread_create(&g_thread, "uevent_listen", 8192,
                        uevent_thread, TD_NULL);
    if (ret != TD_SUCCESS) {
        DBG_ERROR(MOD, "thread create failed");
        close(g_sock);
        g_sock = -1;
        return TD_FAILURE;
    }

    g_inited = TD_TRUE;
    DBG_LOG(MOD, "initialized (sock=%d)", g_sock);
    return TD_SUCCESS;
}

td_void sys_uevent_deinit(td_void)
{
    if (!g_inited) { return; }

    /* 通知线程退出 */
    g_running = TD_FALSE;

    /* 等待线程结束 (最多 2s) */
    thread_join(g_thread);

    /* 清理 socket */
    if (g_sock >= 0) {
        close(g_sock);
        g_sock = -1;
    }

    /* 清空回调列表 */
    pthread_mutex_lock(&g_lock);
    {
        g_list_cnt = 0;
        memset(g_list, 0, sizeof(g_list));
        g_inited = TD_FALSE;
    }
    pthread_mutex_unlock(&g_lock);

    DBG_LOG(MOD, "deinitialized");
}

/* ====================================================================
 *  回调注册 / 注销
 * ==================================================================== */

td_s32 sys_uevent_register(uevent_cb_t cb, td_void *ctx)
{
    td_s32 ret = TD_FAILURE;

    if (cb == NULL) {
        DBG_ERROR(MOD, "register: cb is NULL");
        return TD_FAILURE;
    }

    pthread_mutex_lock(&g_lock);
    {
        if (g_list_cnt >= MAX_LISTENERS) {
            DBG_WARN(MOD, "register: table full (max=%d)", MAX_LISTENERS);
            goto out;
        }

        g_list[g_list_cnt].cb  = cb;
        g_list[g_list_cnt].ctx = ctx;
        g_list_cnt++;

        DBG_LOG(MOD, "register: cb=%p ctx=%p (total=%d)",
                (td_void *)cb, ctx, g_list_cnt);
        ret = TD_SUCCESS;
    }
out:
    pthread_mutex_unlock(&g_lock);
    return ret;
}

td_s32 sys_uevent_unregister(uevent_cb_t cb, td_void *ctx)
{
    td_s32 removed = 0;
    td_s32 i;

    pthread_mutex_lock(&g_lock);
    {
        for (i = 0; i < g_list_cnt; ) {
            if (g_list[i].cb  == cb &&
                g_list[i].ctx == ctx) {
                if (i < g_list_cnt - 1) {
                    g_list[i] = g_list[g_list_cnt - 1];
                }
                g_list_cnt--;
                removed++;
            } else {
                i++;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);

    if (removed > 0) {
        DBG_LOG(MOD, "unregister: cb=%p ctx=%p (removed=%d left=%d)",
                (td_void *)cb, ctx, removed, g_list_cnt);
        return TD_SUCCESS;
    }

    DBG_WARN(MOD, "unregister: cb=%p ctx=%p not found",
             (td_void *)cb, ctx);
    return TD_FAILURE;
}
