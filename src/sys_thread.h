/**
 * @file    thread.h
 * @brief   Hi3516CV610 线程封装模块
 *
 * 对 pthread_create / pthread_join / prctl 的统一封装。
 * 创建时传入线程名和栈大小，内部自动设置 prctl 名称。
 *
 * 用法:
 *   thread_t  thr;
 *   thread_create(&thr, "my_thread", 32768, my_func, NULL);
 *   ...
 *   thread_join(thr);
 */

#ifndef SYS_THREAD_H
#define SYS_THREAD_H

#include <pthread.h>
#include "ot_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ====================================================================
 *  类型
 * ==================================================================== */

typedef td_void *(*thread_fn_t)(td_void *arg);

typedef struct {
    pthread_t tid;
} thread_t;

/* ====================================================================
 *  接口
 * ==================================================================== */

/**
 * @brief  创建线程并设置名称和栈大小
 * @param  t          传出, 线程句柄
 * @param  name       线程名 (prctl, 最多 15 字符)
 * @param  stack_size 栈大小 (字节, 0=系统默认)
 * @param  fn         线程入口函数
 * @param  arg        传递给入口函数的参数
 * @return TD_SUCCESS / TD_FAILURE
 */
td_s32 thread_create(thread_t *t, const td_char *name, td_u32 stack_size,
                     thread_fn_t fn, td_void *arg);

/**
 * @brief  等待线程结束
 * @param  t  线程句柄 (由 thread_create 返回)
 */
td_void thread_join(thread_t t);

/**
 * @brief  设置当前线程名 (在线程函数开头调用)
 * @param  name  线程名 (prctl, 最多 15 字符)
 */
td_void thread_set_name(const td_char *name);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* THREAD_H */
