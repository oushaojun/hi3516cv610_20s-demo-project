#include "video_dispatcher.h"
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define DESTROY_TIMEOUT_MS  3000

/* ========== 帧操作 ========== */
frame_t *frame_create(const uint8_t *data, size_t size, int64_t pts)
{
    frame_t *f = calloc(1, sizeof(*f));
    if (!f) return NULL;
    f->data = malloc(size);
    if (!f->data) {
        free(f);
        return NULL;
    }
    memcpy(f->data, data, size);
    f->size = size;
    f->pts  = pts;
    atomic_init(&f->refcount, 1);
    return f;
}

void frame_ref(frame_t *f)
{
    if (f) atomic_fetch_add(&f->refcount, 1);
}

void frame_unref(frame_t *f)
{
    if (!f) return;
    if (atomic_fetch_sub(&f->refcount, 1) == 1) {
        free(f->data);
        free(f);
    }
}

/* ========== 消费者内部队列节点 ========== */
typedef struct qnode {
    frame_t      *frame;
    struct qnode *next;
} qnode_t;

struct consumer_t {
    pthread_mutex_t mutex;
    pthread_cond_t  cond;

    qnode_t *head, *tail;
    int      count;
    int      max_len;
    bool     drop_old;
    bool     shutdown;          /**< 停止标志, consumer_get_frame 依此返回 NULL */
    uint64_t dropped_frames;    /**< 丢帧统计 */

    /* ---- 安全销毁相关 ---- */
    atomic_int thread_exited;   /**< 消费线程退出时置 1; consumer_destroy 等待此标志 */
};

/* ========== 消费者实现 ========== */
consumer_t *consumer_create(int max_len, bool drop_old)
{
    consumer_t *c = calloc(1, sizeof(*c));
    if (!c) return NULL;
    pthread_mutex_init(&c->mutex, NULL);
    pthread_cond_init(&c->cond, NULL);
    c->max_len   = max_len;
    c->drop_old  = drop_old;
    c->shutdown  = false;
    atomic_init(&c->thread_exited, 0);
    return c;
}

void consumer_stop(consumer_t *c)
{
    if (!c) return;
    pthread_mutex_lock(&c->mutex);
    c->shutdown = true;
    pthread_cond_broadcast(&c->cond);
    pthread_mutex_unlock(&c->mutex);
}

void consumer_mark_exited(consumer_t *c)
{
    if (!c) return;
    atomic_store(&c->thread_exited, 1);
}

void consumer_destroy(consumer_t *c)
{
    if (!c) return;

    /* 等待消费线程退出标志 (最多 3 秒) */
    int waited = 0;
    while (!atomic_load(&c->thread_exited) && waited < DESTROY_TIMEOUT_MS) {
        usleep(10000);
        waited += 10;
    }
    if (!atomic_load(&c->thread_exited)) {
        fprintf(stderr, "consumer_destroy: timeout %dms, force cleanup\n",
                DESTROY_TIMEOUT_MS);
    }

    /* thread_exited 置位后线程不再访问 mutex/cond, 安全清理 */
    pthread_mutex_lock(&c->mutex);
    qnode_t *node = c->head;
    while (node) {
        qnode_t *next = node->next;
        frame_unref(node->frame);
        free(node);
        node = next;
    }
    c->head = c->tail = NULL;
    c->count = 0;
    pthread_mutex_unlock(&c->mutex);

    pthread_mutex_destroy(&c->mutex);
    pthread_cond_destroy(&c->cond);
    free(c);
}

frame_t *consumer_get_frame(consumer_t *c)
{
    pthread_mutex_lock(&c->mutex);
    while (c->count == 0 && !c->shutdown) {
        pthread_cond_wait(&c->cond, &c->mutex);
    }
    if (c->shutdown) {
        pthread_mutex_unlock(&c->mutex);
        return NULL;        /* 通知调用者退出 */
    }
    /* 取出队首 */
    qnode_t *node = c->head;
    c->head = node->next;
    if (!c->head) c->tail = NULL;
    c->count--;
    frame_t *f = node->frame;
    free(node);
    pthread_mutex_unlock(&c->mutex);
    return f;
}

/* ========== 分发器实现 ========== */
void dispatcher_init(dispatcher_t *d)
{
    d->consumers = NULL;
    d->count     = 0;
    d->capacity  = 0;
    pthread_mutex_init(&d->lock, NULL);
}

void dispatcher_destroy(dispatcher_t *d)
{
    pthread_mutex_lock(&d->lock);

    /* 1. 通知所有消费者停止 */
    for (int i = 0; i < d->count; i++) {
        consumer_stop(d->consumers[i]);
    }

    /*
     * 2. 逐个销毁 (consumer_destroy 内部会等待 thread_exited,
     *    消费线程在收到 shutdown 后应置 thread_exited=1 再退出)
     */
    for (int i = 0; i < d->count; i++) {
        consumer_destroy(d->consumers[i]);
    }

    if(d->consumers){
        free(d->consumers);
        d->consumers = NULL;
    }
    d->count     = 0;
    d->capacity  = 0;

    pthread_mutex_unlock(&d->lock);
    pthread_mutex_destroy(&d->lock);
}

consumer_t *dispatcher_add_consumer(dispatcher_t *d,
                                     int max_len, bool drop_old)
{
    consumer_t *c = consumer_create(max_len, drop_old);
    if (!c) return NULL;

    pthread_mutex_lock(&d->lock);

    /* 扩容 */
    if (d->count == d->capacity) {
        int new_cap = d->capacity == 0 ? 4 : d->capacity * 2;
        consumer_t **tmp = realloc(d->consumers,
                                   new_cap * sizeof(consumer_t *));
        if (!tmp) {
            pthread_mutex_unlock(&d->lock);
            consumer_destroy(c);
            return NULL;
        }
        d->consumers = tmp;
        d->capacity  = new_cap;
    }

    d->consumers[d->count++] = c;
    pthread_mutex_unlock(&d->lock);
    return c;
}

void dispatcher_remove_consumer(dispatcher_t *d, consumer_t *c)
{
    if (!c) return;

    pthread_mutex_lock(&d->lock);
    for (int i = 0; i < d->count; i++) {
        if (d->consumers[i] == c) {
            /* compact: 最后元素移到当前位置 */
            d->count--;
            if (i < d->count) {
                d->consumers[i] = d->consumers[d->count];
            }
            break;
        }
    }
    pthread_mutex_unlock(&d->lock);

    consumer_destroy(c);
}

void dispatcher_dispatch(dispatcher_t *d, frame_t *frame)
{
    pthread_mutex_lock(&d->lock);
    int n = d->count;

    /* 没有消费者: 直接释放帧 */
    if (n == 0) {
        pthread_mutex_unlock(&d->lock);
        free(frame->data);
        free(frame);
        return;
    }

    atomic_store(&frame->refcount, n);

    for (int i = 0; i < n; i++) {
        consumer_t *c = d->consumers[i];

        pthread_mutex_lock(&c->mutex);

        /* shutdown 的消费者不投递, 减引用 */
        if (c->shutdown) {
            frame_unref(frame);
            pthread_mutex_unlock(&c->mutex);
            continue;
        }

        if (c->max_len == 0 || c->count < c->max_len) {
            /* 队列未满, 正常入队 */
            qnode_t *node = malloc(sizeof(*node));
            if (!node) {
                frame_unref(frame);
                c->dropped_frames++;
                pthread_mutex_unlock(&c->mutex);
                continue;
            }
            node->frame = frame;
            node->next  = NULL;
            if (c->tail) c->tail->next = node;
            else         c->head       = node;
            c->tail = node;
            c->count++;
            pthread_cond_signal(&c->cond);
        } else {
            /* 队列满, 根据策略丢弃 */
            c->dropped_frames++;
            if (c->drop_old) {
                /* 丢最旧, 入新 */
                qnode_t *old = c->head;
                c->head = old->next;
                if (!c->head) c->tail = NULL;
                frame_unref(old->frame);
                free(old);
                c->count--;

                qnode_t *node = malloc(sizeof(*node));
                if (node) {
                    node->frame = frame;
                    node->next  = NULL;
                    if (c->tail) c->tail->next = node;
                    else         c->head       = node;
                    c->tail = node;
                    c->count++;
                    pthread_cond_signal(&c->cond);
                } else {
                    frame_unref(frame);  /* malloc 失败, 丢新帧 */
                }
            } else {
                /* 丢最新 */
                frame_unref(frame);
            }
        }
        pthread_mutex_unlock(&c->mutex);
    }

    pthread_mutex_unlock(&d->lock);
}
