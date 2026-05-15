/**
 * @file    sys_thread.c
 * @brief   Hi3516CV610 线程封装模块实现
 */

#include <string.h>
#include <sys/prctl.h>
#include "sys_thread.h"
#include "sys_dbg.h"

td_s32 thread_create(thread_t *t, const td_char *name, td_u32 stack_size,
                     thread_fn_t fn, td_void *arg)
{
    pthread_attr_t  attr;
    td_s32          ret;

    if (t == TD_NULL || name == TD_NULL || fn == TD_NULL) {
        return TD_FAILURE;
    }

    pthread_attr_init(&attr);
    if (stack_size > 0) {
        pthread_attr_setstacksize(&attr, stack_size);
    }
    ret = pthread_create(&t->tid, &attr, fn, arg);
    pthread_attr_destroy(&attr);

    if (ret != 0) {
        DBG_ERROR("THREAD", "create '%s' failed: %d (%s)",
                  name, ret, strerror(ret));
        t->tid = 0;
        return TD_FAILURE;
    }

    return TD_SUCCESS;
}

td_void thread_join(thread_t t)
{
    if (t.tid) {
        pthread_join(t.tid, TD_NULL);
    }
}

td_void thread_set_name(const td_char *name)
{
    if (name != TD_NULL) {
        prctl(PR_SET_NAME, name, 0, 0, 0);
    }
}
