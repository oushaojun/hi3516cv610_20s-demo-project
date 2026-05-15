/**
 * @file    dbg.h
 * @brief   Hi3516CV610 调试日志模块 — syslog 后端 + stdout 降级
 *
 * 默认通过 syslog() 写入 syslogd 环形缓冲，用 logread 查看。
 * 若 syslogd 启动失败，自动降级为 stdout/stderr 带 ANSI 颜色输出。
 *
 * 用法:
 *   dbg_init();                        // 初始化
 *   DBG_LOG("APP", "VB init OK");
 *   DBG_WARN("MEDIA", "fail: 0x%x", ret);
 *   dbg_deinit();                      // 清理
 *
 * logread 输出格式:
 *   May 15 20:07:28 user.info hi3516_project[963]: 2026-05-15 20:07:28.456 [LOG] func() file:line (MOD) message
 *
 * stdout 降级格式:
 *   [LOG] 2026-05-15 20:07:28.456 func() file:line (MOD) message   (带 ANSI 颜色)
 *
 * 强制 stdout 模式 (跳过 syslogd): 取消下面这行的注释即可
 *   #define DBG_FORCE_STDOUT
 */

#ifndef SYS_DBG_H
#define SYS_DBG_H

#include <stdio.h>
#include "ot_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ====================================================================
 *  日志等级
 * ==================================================================== */

typedef enum {
    DBG_LVL_TRACE = 0,  /**< 最详细跟踪信息 (灰色) */
    DBG_LVL_DEBUG,       /**< 调试信息 (青色) */
    DBG_LVL_LOG,         /**< 常规运行信息 (无色) */
    DBG_LVL_WARN,        /**< 警告 (黄色) */
    DBG_LVL_ERROR,       /**< 错误 (红色) */
    DBG_LVL_FATAL,       /**< 致命错误 (粗体红色) */
} dbg_level_t;

/* ====================================================================
 *  便捷宏 (自动捕获 __FILE__ / __LINE__)
 * ==================================================================== */

#define DBG_TRACE(mod, fmt, ...) \
    _dbg_print(DBG_LVL_TRACE, mod, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DBG_DEBUG(mod, fmt, ...) \
    _dbg_print(DBG_LVL_DEBUG, mod, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DBG_LOG(mod, fmt, ...) \
    _dbg_print(DBG_LVL_LOG,  mod, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DBG_WARN(mod, fmt, ...) \
    _dbg_print(DBG_LVL_WARN, mod, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DBG_ERROR(mod, fmt, ...) \
    _dbg_print(DBG_LVL_ERROR, mod, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define DBG_FATAL(mod, fmt, ...) \
    _dbg_print(DBG_LVL_FATAL, mod, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

/* ====================================================================
 *  底层实现
 * ==================================================================== */

/**
 * @brief  带等级/模块/文件/行号的结构化日志输出
 *
 * syslog 格式: 2026-05-15 20:07:28.456 [LEVEL] func() file:line (MOD) message
 * stdout 格式: [LEVEL] 2026-05-15 20:07:28.456 func() file:line (MOD) message (带颜色)
 *
 * @param lvl   日志等级
 * @param mod   模块名 (如 "APP" "MEDIA" "IR")
 * @param func  函数名 (宏自动填入 __func__)
 * @param file  源码文件名 (宏自动填入 __FILE__)
 * @param line  行号 (宏自动填入 __LINE__)
 * @param fmt   printf 格式串
 * @param ...   可变参数
 */
td_void _dbg_print(dbg_level_t lvl, const td_char *mod,
                    const td_char *func,
                    const td_char *file, td_s32 line,
                    const td_char *fmt, ...);

/* ====================================================================
 *  生命周期
 * ==================================================================== */

/**
 * @brief  初始化日志系统，启动 syslogd 并 openlog
 * @return TD_SUCCESS
 */
td_s32 dbg_init(td_void);

/**
 * @brief  清理日志系统，closelog
 */
td_void dbg_deinit(td_void);


/** 是否强制使用 stdout 模式 (跳过 syslogd) */
#define DBG_FORCE_STDOUT 0


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* SYS_DBG_H */
