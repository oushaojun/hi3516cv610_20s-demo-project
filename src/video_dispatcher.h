#ifndef VIDEO_DISPATCHER_H
#define VIDEO_DISPATCHER_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 帧 ---------- */
typedef struct {
    uint8_t    *data;
    size_t      size;
    int64_t     pts;
    atomic_int  refcount;   /**< 引用计数, 归零时自动释放 */
} frame_t;

/**
 * @brief  创建帧 (深拷贝数据), 初始 refcount=1
 */
frame_t *frame_create(const uint8_t *data, size_t size, int64_t pts);

/** @brief  增加引用计数 */
void frame_ref(frame_t *f);

/** @brief  减少引用计数, 归零时释放 data 和 frame 本身 */
void frame_unref(frame_t *f);

/* ---------- 消费者 ---------- */
typedef struct consumer_t consumer_t;

/**
 * @brief  创建消费者 (不加入任何分发器)
 * @param  max_len  队列最大长度, 0 = 无限制
 * @param  drop_old true=队列满时丢弃最旧帧, false=丢弃最新帧
 */
consumer_t *consumer_create(int max_len, bool drop_old);

/**
 * @brief  阻塞获取一帧
 * @return 帧指针; 若返回 NULL 说明消费者已 shutdown, 调用者应退出循环
 * @note   返回的帧用完必须 frame_unref
 */
frame_t *consumer_get_frame(consumer_t *c);

/**
 * @brief  通知消费者停止, 使 consumer_get_frame 返回 NULL
 * @note   调用后应等待消费线程退出, 再调用 consumer_destroy
 */
void consumer_stop(consumer_t *c);

/**
 * @brief  释放消费者内部资源
 * @note   调用前必须已调过 consumer_stop, 且消费线程已退出。
 *         内部会等待 thread_exited 标志 (最多 3s), 超时则强制清理。
 */
void consumer_destroy(consumer_t *c);

/**
 * @brief  消费线程退出时调用, 通知 consumer_destroy 可安全清理
 * @note   必须在 consumer_get_frame 返回 NULL 之后、线程 return 之前调用
 */
void consumer_mark_exited(consumer_t *c);

/* ---------- 分发器 ---------- */
typedef struct {
    consumer_t    **consumers;
    int             count;
    int             capacity;
    pthread_mutex_t lock;
} dispatcher_t;

/** @brief  初始化分发器 */
void dispatcher_init(dispatcher_t *d);

/**
 * @brief  销毁分发器 (通知所有消费者停止并清理)
 * @note   调用前应确保所有消费线程已退出或即将退出。
 *         内部先 stop 再 destroy, destroy 阶段逐个等待 thread_exited。
 */
void dispatcher_destroy(dispatcher_t *d);

/**
 * @brief  向分发器添加一个消费者
 * @return 消费者指针, 调用者用该指针启动消费线程
 */
consumer_t *dispatcher_add_consumer(dispatcher_t *d,
                                    int max_len, bool drop_old);

/**
 * @brief  从分发器移除消费者
 * @note   调用前必须已通过 consumer_stop 通知, 且消费线程已退出
 */
void dispatcher_remove_consumer(dispatcher_t *d, consumer_t *c);

/**
 * @brief  生产者调用: 将一帧分发给所有已注册消费者 (非阻塞)
 * @note   frame 生命周期由分发器接管, 调用者调用后不应再访问 frame
 */
void dispatcher_dispatch(dispatcher_t *d, frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_DISPATCHER_H */
