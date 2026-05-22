/**
 * @file    sys_dbg.c
 * @brief   Hi3516CV610 调试日志模块实现 — syslog 后端 + stdout 降级
 *
 * 默认通过 syslog() 写入 syslogd，用 logread 查看。
 * 若 syslogd 启动失败，自动降级为 stdout/stderr 带 ANSI 颜色输出。
 */

#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <syslog.h>
#include "sys_dbg.h"

/* ====================================================================
 *  内部辅助
 * ==================================================================== */

/** 日志等级 5 字符固定宽标签 */
static const td_char *g_level_tag[] = {
    [DBG_LVL_TRACE] = "TRACE",
    [DBG_LVL_DEBUG] = "DEBUG",
    [DBG_LVL_LOG]   = "LOG",
    [DBG_LVL_WARN]  = "WARN",
    [DBG_LVL_ERROR] = "ERROR",
    [DBG_LVL_FATAL] = "FATAL",
};

/** 日志等级 → syslog priority */
static const td_s32 g_syslog_prio[] = {
    [DBG_LVL_TRACE] = LOG_DEBUG,
    [DBG_LVL_DEBUG] = LOG_DEBUG,
    [DBG_LVL_LOG]   = LOG_INFO,
    [DBG_LVL_WARN]  = LOG_WARNING,
    [DBG_LVL_ERROR] = LOG_ERR,
    [DBG_LVL_FATAL] = LOG_CRIT,
};

/** 日志等级对应的 ANSI 颜色码 (stdout 降级用) */
static const td_char *g_level_color[] = {
    [DBG_LVL_TRACE] = "\033[90m",     /* 灰色 */
    [DBG_LVL_DEBUG] = "\033[36m",     /* 青色 */
    [DBG_LVL_LOG]   = "\033[0m",      /* 无色 */
    [DBG_LVL_WARN]  = "\033[33m",     /* 黄色 */
    [DBG_LVL_ERROR] = "\033[31m",     /* 红色 */
    [DBG_LVL_FATAL] = "\033[1;31m",   /* 粗体红色 */
};

#define COLOR_RESET  "\033[0m"

/** syslog 是否可用 (TD_TRUE=走syslog, TD_FALSE=走stdout) */
static td_bool g_use_syslog = TD_FALSE;

/** 从 __FILE__ 全路径中提取纯文件名 */
static const td_char *short_file(const td_char *path)
{
    const td_char *p = strrchr(path, '/');
    return p ? (p + 1) : path;
}

/** 生成当前时间戳 "YYYY-MM-DD HH:MM:SS.mmm" */
static td_void time_now_ms(td_char *out, td_s32 size)
{
    struct timeval  tv;
    struct tm       tm_info;
    time_t          now;

    gettimeofday(&tv, TD_NULL);
    now = tv.tv_sec;
    localtime_r(&now, &tm_info);

    (td_void)snprintf(out, (size_t)size, "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                      tm_info.tm_year + 1900,
                      tm_info.tm_mon  + 1,
                      tm_info.tm_mday,
                      tm_info.tm_hour,
                      tm_info.tm_min,
                      tm_info.tm_sec,
                      (td_s64)(tv.tv_usec / 1000));
}

/* ====================================================================
 *  生命周期
 * ==================================================================== */

td_s32 dbg_init(td_void)
{
#if !DBG_FORCE_STDOUT
    /* 尝试启动 syslogd (-C64: 32KB 环形缓冲) */
    system("syslogd -C32");

    /* 轮询等待 /dev/log 就绪 (最多 1 秒), 避免 100ms 不够用 */
    {
        td_s32 retry;
        for (retry = 0; retry < 20; retry++) {
            if (access("/dev/log", F_OK) == 0) { break; }
            usleep(50000);  /* 50ms */
        }
    }

    /* 检查 syslogd 是否就绪 */
    if (access("/dev/log", F_OK) == 0) {
        /* socket 文件可能刚创建, syslogd 还没进主循环, 等 200ms */
        usleep(200000);

        openlog("hi3516_project", LOG_PID | LOG_NDELAY, LOG_USER);

        /* 启动 logread 后台实时输出到终端
         * fork+setsid 放到独立 session, Ctrl+C 打不到它 */
        {
            pid_t pid = fork();
            if (pid == 0) {
                setsid();
                execl("/sbin/logread", "logread", "-f", (td_char *)TD_NULL);
                _exit(1);
            }
            /*wait for logread to start*/
            usleep(50000);
        }

        g_use_syslog = TD_TRUE;
        return TD_SUCCESS;
    }
    /* syslogd 启动失败，降级 */
#endif

    /* stdout 模式: 带 ANSI 颜色输出 */
    return TD_SUCCESS;
}

td_void dbg_deinit(td_void)
{
    if (!g_use_syslog) { return; }

    closelog();

    /* 等 logread 把环形缓冲里最后几条日志刷出来 */
    sleep(1);

    /* 停止 logread 和 syslogd */
    system("killall logread 2>/dev/null");
    system("killall syslogd 2>/dev/null");
}

/* ====================================================================
 *  对外实现
 * ==================================================================== */

td_void _dbg_print(dbg_level_t lvl, const td_char *mod,
                    const td_char *func,
                    const td_char *file, td_s32 line,
                    const td_char *fmt, ...)
{
    va_list   args;
    td_char   buf[512];
    td_char   ts[24];

    if (lvl > DBG_LVL_FATAL) { return; }

    /* 格式化用户消息到 buf */
    va_start(args, fmt);
    (td_void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    time_now_ms(ts, sizeof(ts));

    if (g_use_syslog) {
        /* syslog: TS [LEVEL] func() file:line (module) message */
        syslog(g_syslog_prio[lvl], "%s [%s] %s() %s:%d (%s) %s",
               ts, g_level_tag[lvl], func, short_file(file), line, mod, buf);
    } else {
        /* stdout 降级: 带 ANSI 颜色, ERROR/FATAL → stderr */
        FILE *out = (lvl >= DBG_LVL_ERROR) ? stderr : stdout;
        fprintf(out, "%s[%s] %s %s() %s:%d (%s) %s" COLOR_RESET "\n",
                g_level_color[lvl], g_level_tag[lvl], ts, func,
                short_file(file), line, mod, buf);
    }
}
