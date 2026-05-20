#ifndef VIDEO_DISPATCHER_H
#define VIDEO_DISPATCHER_H

/**
 * @file    video_dispatcher.h
 * @brief   视频帧分发器 — 单生产者 → 多消费者广播模型
 *
 * 三层对象:
 *  - frame_t:       引用计数帧, refcount 归零自动释放 data 和自身
 *  - consumer_t:    消费者, 每个消费线程持有一个, 内嵌 FIFO 阻塞队列
 *  - dispatcher_t:  分发器, 维护 consumer 列表, dispatch() 广播每帧到所有消费者
 *
 * 典型用法:
 * @code
 *   dispatcher_t d;
 *   dispatcher_init(&d);
 *   consumer_t *c = dispatcher_add_consumer(&d, max_len, drop_old);
 *   thread_create(&thr, "name", stack, consumer_thread, c);
 *
 *   // 生产者线程循环:
 *   f = frame_create(buf, size, pts);
 *   dispatcher_dispatch(&d, f);   // f 生命周期被接管, 后续不得访问
 *
 *   // 退出:
 *   consumer_stop(c); thread_join(thr);
 *   dispatcher_remove_consumer(&d, c);
 *   dispatcher_destroy(&d);
 * @endcode
 */

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ============================================================================
 *  帧 (frame_t) — 深拷贝持有, 引用计数管理生命周期
 * ============================================================================
 */
/** @brief 一帧编码数据 (堆分配, 深拷贝持有) */
typedef struct {
    uint8_t    *data;        /**< 帧数据缓冲区 (堆分配) */
    size_t      size;        /**< 数据长度 (字节) */
    int64_t     pts;         /**< 呈现时间戳 (微秒), 无时间戳时填 0 */
    bool        is_keyframe; /**< 是否为 IDR 关键帧 (H264/H265) */
    atomic_int  refcount;    /**< 原子引用计数, 归零时自动释放 data 和 frame */
} frame_t;

/**
 * @brief  创建帧 — 深拷贝 data, refcount=1
 * @param  data        源数据指针
 * @param  size        数据长度 (字节)
 * @param  pts         呈现时间戳 (微秒), 无时间戳填 0
 * @param  is_keyframe 是否为 IDR 关键帧 (H264 ref_type==IDR_SLICE)
 * @return 堆分配的 frame_t*, 失败返回 NULL
 */
frame_t *frame_create(const uint8_t *data, size_t size, int64_t pts, bool is_keyframe);

/**
 * @brief  原子自增引用计数 (+1)
 * @param  f  帧指针, NULL 安全
 * @note   多个持有者需要各自持有一帧时调用
 */
void frame_ref(frame_t *f);

/**
 * @brief  原子自减引用计数, 归零时释放 f->data 和 f 自身
 * @param  f  帧指针, NULL 安全; 调用后不得再访问
 */
void frame_unref(frame_t *f);

/*
 * ============================================================================
 *  消费者 (consumer_t) — 每个消费线程持有一个, 内部 FIFO 队列
 * ============================================================================
 */
typedef struct consumer_t consumer_t;

/**
 * @brief  创建消费者 — 初始化 mutex/cond, 队列空
 * @param  max_len  队列最大帧数: 0=无限, N=上限 (满时按 drop_old 策略丢弃)
 * @param  drop_old 队列满时: true=丢弃最旧帧腾位, false=丢弃最新帧
 * @return 堆分配的 consumer_t*, 失败返回 NULL
 */
consumer_t *consumer_create(int max_len, bool drop_old);

/**
 * @brief  阻塞获取队列头部一帧
 * @param  c  消费者
 * @return 帧指针 (用完须 frame_unref 释放); NULL 表示 shutdown, 调用者应退出循环
 * @note   内部在有锁状态下 cond_wait, dispatch 入队时 cond_signal 唤醒。
 *         队列空且未 shutdown 时阻塞, shutdown 时返回 NULL。
 */
frame_t *consumer_get_frame(consumer_t *c);

/**
 * @brief  通知消费者停止 — 设 shutdown=true, broadcast 唤醒所有等待者
 * @note   调用后 consumer_get_frame 立刻返回 NULL (或正在阻塞的下次唤醒时返回 NULL)
 */
void consumer_stop(consumer_t *c);

/**
 * @brief  销毁消费者 — 等待线程退出, 清理残留帧及同步对象
 * @note   调用前必须: 1) 已调 consumer_stop; 2) 消费线程已退出循环;
 *         3) 线程退出前调了 consumer_mark_exited。
 *         内部等待 thread_exited 最多 3 秒, 超时则打印警告并强制清理。
 */
void consumer_destroy(consumer_t *c);

/**
 * @brief  标记消费线程已退出 — 线程在退出循环后、return 前调用
 * @note   此标志让 consumer_destroy 知晓线程不再访问 mutex/cond, 可安全销毁。
 *         若漏调, consumer_destroy 会超时 3 秒。
 */
void consumer_mark_exited(consumer_t *c);

/**
 * @brief  获取消费者关联的视频宽度
 * @param  c  消费者
 * @return 视频宽度 (像素)
 */
uint16_t consumer_get_width(consumer_t *c);

/**
 * @brief  获取消费者关联的视频高度
 * @param  c  消费者
 * @return 视频高度 (像素)
 */
uint16_t consumer_get_height(consumer_t *c);

/**
 * @brief  获取消费者关联的视频帧率
 * @param  c  消费者
 * @return 视频帧率
 */
uint8_t consumer_get_fps(consumer_t *c);

/*
 * ============================================================================
 *  分发器 (dispatcher_t) — 广播帧到所有注册的消费者
 * ============================================================================
 */

/** @brief 分发器: 持有 consumer 指针数组, lock 保护并发 */
typedef struct {
    consumer_t    **consumers;   /**< 消费者指针数组 (动态 ×2 扩容) */
    int             count;       /**< 当前消费者数量 */
    int             capacity;    /**< 数组已分配容量 */
    uint16_t        width;       /**< 视频宽度 (像素), 初始化时配置 */
    uint16_t        height;      /**< 视频高度 (像素) */
    uint8_t         fps;         /**< 视频帧率 */
    int             codec_type;  /**< 编码类型 (OT_PT_H264=96 / OT_PT_H265=265) */
    pthread_mutex_t lock;        /**< 保护 consumers[] 的互斥锁 */
} dispatcher_t;

/**
 * @brief  初始化分发器 — 零初始化所有成员, 初始化 mutex
 * @param  d       指向栈或堆上的 dispatcher_t
 * @param  width   视频宽度 (像素)
 * @param  height  视频高度 (像素)
 * @param  fps     视频帧率
 */
void dispatcher_init(dispatcher_t *d, uint16_t width, uint16_t height, uint8_t fps, int codec_type);

/**
 * @brief  销毁分发器 — 先 stop 所有消费者, 再逐个 destroy, 释放数组和 lock
 * @note   调用前应确保所有消费线程已准备好退出。
 *         内部流程: 加锁 → stop 所有 consumer → destroy 每个 consumer
 *         (各自等 thread_exited 最多 3s) → free 数组 → 解锁 → 销毁 lock。
 */
void dispatcher_destroy(dispatcher_t *d);

/**
 * @brief  创建消费者并加入分发器
 * @param  d        分发器
 * @param  max_len  队列最大帧数, 0=无限
 * @param  drop_old 满时丢帧策略 (true=丢最旧, false=丢最新)
 * @return 消费者指针 (用于启动消费线程), 失败返回 NULL
 * @note   内部 consumer_create → 加锁插入数组 (容量不足时 ×2 扩容)。
 *         调用者不要单独 consumer_destroy, 应该用 dispatcher_remove_consumer。
 */
consumer_t *dispatcher_add_consumer(dispatcher_t *d,
                                    int max_len, bool drop_old);

/**
 * @brief  从分发器移除消费者并销毁
 * @param  d  分发器
 * @param  c  由 add_consumer 返回的消费者指针
 * @note   调用前必须: 1) 已 consumer_stop(c); 2) 消费线程已退出。
 *         内部: 加锁 → 数组定位并 compact → consumer_destroy(c)。
 */
void dispatcher_remove_consumer(dispatcher_t *d, consumer_t *c);

/**
 * @brief  生产者投递一帧 — 广播到所有注册消费者 (非阻塞)
 * @param  d      分发器
 * @param  frame  帧指针 (frame_create 返回), 调用后不得再访问
 *
 * @note   **生命周期**: frame 被本函数接管, 调用者不得 frame_unref。
 *
 *         内部逻辑:
 *         - n==0 (无消费者): 直接并 frame->data 和 frame
 *         - n>0: atomic_store(&frame->refcount, n), 覆盖 frame_create 的初始 1
 *         - 遍历 consumers[]:
 *             - shutdown 的消费者: 跳过, frame_unref 减引用
 *             - 队列未满: 入队, cond_signal 唤醒
 *             - 队列满: 按 drop_old 策略丢帧 + frame_unref
 *         - 每个消费者 frame_unref 后 refcount 减 1, 全部消费完归零自动释放
 *
 *         **refcount 设计**: 不调用 frame_ref, 直接用 atomic_store(n) 设引用数,
 *         因为 n 恰好是需要持有该帧的消费者总数。
 */
void dispatcher_dispatch(dispatcher_t *d, frame_t *frame);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_DISPATCHER_H */
