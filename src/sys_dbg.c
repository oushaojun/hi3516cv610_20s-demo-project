/**
 * @file    sys_dbg.c
 * @brief   Hi3516CV610 调试日志模块实现
 *
 * 支持 6 级日志等级，ANSI 8 色终端，ERROR/FATAL 定向到 stderr。
 */

#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
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

/** 日志等级对应的 ANSI 颜色码 */
static const td_char *g_level_color[] = {
    [DBG_LVL_TRACE] = "\033[90m",     /* 灰色 */
    [DBG_LVL_DEBUG] = "\033[36m",     /* 青色 */
    [DBG_LVL_LOG]   = "\033[0m",      /* 无色 (终端默认) */
    [DBG_LVL_WARN]  = "\033[33m",     /* 黄色 */
    [DBG_LVL_ERROR] = "\033[31m",     /* 红色 */
    [DBG_LVL_FATAL] = "\033[1;31m",   /* 粗体红色 */
};

/** ANSI 颜色复位 */
#define COLOR_RESET  "\033[0m"

/** 从 __FILE__ 全路径中提取纯文件名 */
static const td_char *short_file(const td_char *path)
{
    const td_char *p = strrchr(path, '/');
    return p ? (p + 1) : path;
}

/* ====================================================================
 *  对外实现
 * ==================================================================== */

/** 生成当前时间戳 "YYYY-MM-DD HH:MM:SS.mmm" */
static td_void time_now_ms(td_char *out, td_s32 size)
{
    struct timeval  tv;
    struct tm       tm_info;
    time_t          now;

    gettimeofday(&tv, TD_NULL);
    now = tv.tv_sec;
    localtime_r(&now, &tm_info);

    (td_void)snprintf(out, size, "%04d-%02d-%02d %02d:%02d:%02d.%03lld",
                      tm_info.tm_year + 1900,
                      tm_info.tm_mon  + 1,
                      tm_info.tm_mday,
                      tm_info.tm_hour,
                      tm_info.tm_min,
                      tm_info.tm_sec,
                      (td_s64)(tv.tv_usec / 1000));
}

td_void _dbg_print(dbg_level_t lvl, const td_char *mod,
                    const td_char *func,
                    const td_char *file, td_s32 line,
                    const td_char *fmt, ...)
{
    va_list   args;
    td_char   buf[512];
    td_char   ts[24];
    FILE     *out;
    const td_char *color;
    const td_char *tag;

    if (lvl > DBG_LVL_FATAL) { return; }

    tag   = g_level_tag[lvl];
    color = g_level_color[lvl];
    time_now_ms(ts, sizeof(ts));

    /* ERROR / FATAL → stderr, 其余 → stdout */
    out = (lvl >= DBG_LVL_ERROR) ? stderr : stdout;

    /* 格式化用户消息到 buf */
    va_start(args, fmt);
    (td_void)vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    /* 输出: [LEVEL] TS func() file:line (module) message */
    fprintf(out, "%s[%s] %s %s() %s:%d (%s) %s" COLOR_RESET "\n",
            color, tag, ts, func, short_file(file), line, mod, buf);
}
