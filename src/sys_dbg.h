/**
 * @file    dbg.h
 * @brief   Hi3516CV610 调试日志模块
 *
 * 支持 6 级日志，带颜色、文件名、行号、模块名。
 *
 * 用法:
 *   DBG_LOG("APP", "VB init OK\n");
 *   DBG_WARN("MEDIA", "get_rc_param failed: 0x%x\n", ret);
 *   DBG_ERROR("IR", "ss_mpi_isp_ir_auto failed: 0x%x\n", ret);
 *
 * 颜色 (ANSI):
 *   TRACE: 灰色  DEBUG: 青色  LOG: 无色  WARN: 黄色  ERROR: 红色  FATAL: 粗红
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
 * 格式: [LEVEL] YYYY-MM-DD HH:MM:SS.mmm func() file:line (module) message
 * 颜色: ANSI escape 序列, ERROR/FATAL 定向到 stderr
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

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* SYS_DBG_H */
