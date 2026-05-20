/**
 * @file    video_record.c
 * @brief   视频录像模块实现
 *
 * 内部结构:
 *   ctx → dispatchers[chn] → consumers[chn] → consumer_thread[chn] (MP4 录像)
 *           ↑                      ↑
 *       producer_thread[chn]    thread arg: {consumer, chn}
 *
 * 每通道一个 producer 线程 (取 VENC 帧 → dispatch),
 * 每通道一个 consumer 线程 (收帧 → MP4 封装 → SD 卡写入)。
 */

#include "video_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sys_dbg.h"
#include "sys_sd.h"
#include "sys_thread.h"
#include "video_dispatcher.h"
#include "video_pipeline.h"
#include "mp4_muxer.h"

/* ====================================================================
 *  内部类型
 * ==================================================================== */

/** consumer 线程参数: 消费者指针 + 通道号 */
typedef struct {
    consumer_t *consumer;
    int         chn;
} cons_thread_arg_t;

/** 录像模块上下文 (非公开) */
struct video_record_ctx_s {
    int             chn_cnt;            /**< 通道数量 */

    dispatcher_t    dispatchers[3];     /**< 每通道独立分发器 */
    consumer_t     *consumers[3];       /**< 每通道消费者 (订阅本通道 dispatcher) */
    cons_thread_arg_t cons_args[3];     /**< consumer 线程参数 (含 chn 索引) */

    volatile td_bool producer_running[3]; /**< 每通道 producer 运行标志 */

    thread_t        prod_threads[3];    /**< producer 线程 */
    thread_t        cons_threads[3];    /**< consumer 线程 */
};

/* ====================================================================
 *  Producer 线程参数
 * ==================================================================== */
typedef struct {
    video_record_ctx_t *ctx;
    int                 chn;
} prod_thread_arg_t;

/* ====================================================================
 *  Producer 线程 — 取 VENC 编码帧, 投递到对应 dispatcher
 * ==================================================================== */
static td_void *producer_thread(td_void *arg)
{
    prod_thread_arg_t  *a   = (prod_thread_arg_t *)arg;
    video_record_ctx_t *ctx = a->ctx;
    int                 chn = a->chn;
    td_char             name[16];

    snprintf(name, sizeof(name), "enc_prod%d", chn);
    thread_set_name(name);

    while (ctx->producer_running[chn]) {
        ot_venc_stream stream;
        td_s32         ret;
        td_u32         i;
        size_t         total_size;
        uint8_t       *buf;
        frame_t       *f;

        ret = media_venc_get_frame((ot_venc_chn)chn, 1000, &stream);
        if (ret != TD_SUCCESS) {
            if (ret == MEDIA_ERR_VENC_TIMEOUT) { continue; }
            DBG_WARN("VREC", "VENC chn%d get_frame error: 0x%x", chn, ret);
            break;
        }

        if (stream.pack_cnt == 0) {
            (td_void)media_venc_release_frame((ot_venc_chn)chn, &stream);
            continue;
        }

        /* 计算所有 pack 总大小 */
        total_size = 0;
        for (i = 0; i < stream.pack_cnt; i++) {
            total_size += stream.pack[i].len - stream.pack[i].offset;
        }

        /* 拼接为连续 buffer, 创建 frame (深拷贝) */
        buf = (uint8_t *)malloc(total_size);
        if (buf) {
            size_t off = 0;
            for (i = 0; i < stream.pack_cnt; i++) {
                size_t len = stream.pack[i].len - stream.pack[i].offset;
                (td_void)memcpy(buf + off,
                                stream.pack[i].addr + stream.pack[i].offset,
                                len);
                off += len;
            }
            /* PTS 取首个 pack */
            int64_t pts = (int64_t)stream.pack[0].pts;
            bool    is_key = (stream.h264_info.ref_type == OT_VENC_BASE_IDR_SLICE);

            f = frame_create(buf, total_size, pts, is_key);
            free(buf);

            if (f) {
                dispatcher_dispatch(&ctx->dispatchers[chn], f);
            }
        }

        (td_void)media_venc_release_frame((ot_venc_chn)chn, &stream);
    }

    DBG_LOG("VREC", "producer chn%d exit", chn);
    free(a);  /* arg 由 video_record_start 分配 */
    return TD_NULL;
}

/* ====================================================================
 *  Consumer 线程 — 收帧、统计、MP4 录像 (SD 卡热插拔)
 * ==================================================================== */
static td_void *consumer_thread(td_void *arg)
{
    cons_thread_arg_t *a   = (cons_thread_arg_t *)arg;
    consumer_t        *c   = a->consumer;
    int                chn = a->chn;
    td_u64             total_frames = 0;
    td_u64             total_bytes  = 0;
    td_u64             batch_frames = 0;
    td_u64             batch_bytes  = 0;
    mp4_muxer_t       *muxer        = TD_NULL;
    td_char            name[16];

    snprintf(name, sizeof(name), "enc_cons%d", chn);
    thread_set_name(name);

    while (1) {
        frame_t *f = consumer_get_frame(c);
        if (!f) { break; }   /* shutdown */

        total_frames++;
        total_bytes  += f->size;
        batch_frames++;
        batch_bytes  += f->size;

        /* ---- SD 卡录像 (MP4封装) ---- */
        {
            td_bool mounted = sys_sd_is_mounted();

            if (mounted && muxer == TD_NULL) {
                /* SD 卡插入, 以当前时间创建 MP4 文件 */
                time_t    now = time(TD_NULL);
                struct tm tm;
                td_char   fname[64];

                localtime_r(&now, &tm);
                snprintf(fname, sizeof(fname),
                         "/mnt/sdcard/%04d%02d%02d_%02d%02d%02d_chn%d.mp4",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec, chn);

                muxer = mp4_muxer_create(fname,
                             consumer_get_width(c),
                             consumer_get_height(c),
                             consumer_get_fps(c));
                if (muxer) {
                    DBG_LOG("VREC", "chn%d recording: %s", chn, fname);
                } else {
                    DBG_WARN("VREC", "chn%d cannot create %s", chn, fname);
                }
            } else if (!mounted && muxer != TD_NULL) {
                /* SD 卡拔出, 关闭录像 */
                mp4_muxer_close(muxer);
                muxer = TD_NULL;
                DBG_LOG("VREC", "chn%d recording stopped (sd removed)", chn);
            }

            if (muxer != TD_NULL) {
                if (mp4_muxer_write_frame(muxer, f->data, f->size,
                                          f->is_keyframe) != 0) {
                    DBG_WARN("VREC", "chn%d muxer write failed, closing", chn);
                    mp4_muxer_close(muxer);
                    muxer = TD_NULL;
                }
            }
        }

        frame_unref(f);

        /* 每 100 帧打一次统计 */
        if (batch_frames >= 100) {
            DBG_LOG("VREC", "chn%d: %llu frames | %llu KB | avg %.1f KB/frame %s",
                    chn,
                    (unsigned long long)total_frames,
                    (unsigned long long)(total_bytes / 1024),
                    (double)batch_bytes / (double)batch_frames / 1024.0,
                    (muxer != TD_NULL) ? "[REC]" : "");
            batch_frames = 0;
            batch_bytes  = 0;
        }
    }

    /* 退出前关闭录像文件 */
    if (muxer != TD_NULL) {
        mp4_muxer_close(muxer);
        muxer = TD_NULL;
        DBG_LOG("VREC", "chn%d recording stopped (exit)", chn);
    }

    /* 标记线程已退出, consumer_destroy 靠此安全清理 */
    consumer_mark_exited(c);

    DBG_LOG("VREC", "chn%d consumer exit: %llu frames, %llu KB",
            chn,
            (unsigned long long)total_frames,
            (unsigned long long)(total_bytes / 1024));
    return TD_NULL;
}

/* ====================================================================
 *  API 实现
 * ==================================================================== */

video_record_ctx_t *video_record_init(const video_record_chn_cfg_t *cfgs,
                                       int chn_cnt)
{
    video_record_ctx_t *ctx;
    int i;

    if (!cfgs || chn_cnt < 1 || chn_cnt > 3) {
        DBG_ERROR("VREC", "invalid args: cfgs=%p, chn_cnt=%d", (td_void *)cfgs, chn_cnt);
        return TD_NULL;
    }

    ctx = (video_record_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) {
        DBG_ERROR("VREC", "calloc ctx failed");
        return TD_NULL;
    }

    ctx->chn_cnt = chn_cnt;

    /* 为每个通道创建 dispatcher (全部初始化, mutex 需要有效) */
    for (i = 0; i < chn_cnt; i++) {
        dispatcher_init(&ctx->dispatchers[i],
                        cfgs[i].width,
                        cfgs[i].height,
                        cfgs[i].fps);

        /* 仅 H.264 / H.265 创建 consumer, 其他类型跳过 */
        if (cfgs[i].codec_type != OT_PT_H264 && cfgs[i].codec_type != OT_PT_H265) {
            DBG_LOG("VREC", "chn%d: skipped (codec_type=%d)", i, cfgs[i].codec_type);
            ctx->consumers[i] = TD_NULL;
            continue;
        }

        /* 添加 consumer 订阅该通道 */
        ctx->consumers[i] = dispatcher_add_consumer(&ctx->dispatchers[i],
                                                     cfgs[i].max_queue, false);
        if (!ctx->consumers[i]) {
            DBG_ERROR("VREC", "chn%d add_consumer failed", i);
            /* 逆序清理已创建的 */
            while (i > 0) {
                i--;
                if (ctx->consumers[i]) {
                    dispatcher_remove_consumer(&ctx->dispatchers[i],
                                               ctx->consumers[i]);
                    ctx->consumers[i] = TD_NULL;
                }
            }
            free(ctx);
            return TD_NULL;
        }

        /* 准备 consumer 线程参数 */
        ctx->cons_args[i].consumer = ctx->consumers[i];
        ctx->cons_args[i].chn      = i;

        DBG_LOG("VREC", "chn%d: dispatcher init %dx%d@%dfps, consumer queue=%d",
                i, cfgs[i].width, cfgs[i].height, cfgs[i].fps, cfgs[i].max_queue);
    }

    return ctx;
}

int video_record_start(video_record_ctx_t *ctx)
{
    int i;
    int ret;
    int started = 0;

    if (!ctx) { return -1; }

    /* 启动 producer 线程 (仅 H.264/H.265 通道) */
    for (i = 0; i < ctx->chn_cnt; i++) {
        prod_thread_arg_t *a;

        if (!ctx->consumers[i]) { continue; }

        a = (prod_thread_arg_t *)malloc(sizeof(*a));
        if (!a) {
            DBG_ERROR("VREC", "chn%d malloc prod arg failed", i);
            goto ERR_PRODUCER;
        }
        a->ctx = ctx;
        a->chn = i;

        ctx->producer_running[i] = TD_TRUE;

        {
            td_char name[16];
            snprintf(name, sizeof(name), "enc_prod%d", i);
            ret = thread_create(&ctx->prod_threads[i], name, 32768,
                                producer_thread, a);
        }
        if (ret != TD_SUCCESS) {
            DBG_ERROR("VREC", "chn%d producer thread create failed", i);
            ctx->producer_running[i] = TD_FALSE;
            free(a);
            goto ERR_PRODUCER;
        }
        started++;
    }

    /* 启动 consumer 线程 (仅 H.264/H.265 通道) */
    for (i = 0; i < ctx->chn_cnt; i++) {
        td_char name[16];

        if (!ctx->consumers[i]) { continue; }

        snprintf(name, sizeof(name), "enc_cons%d", i);
        ret = thread_create(&ctx->cons_threads[i], name, 16384,
                            consumer_thread, &ctx->cons_args[i]);
        if (ret != TD_SUCCESS) {
            DBG_ERROR("VREC", "chn%d consumer thread create failed", i);
            /* stop 已启动的 consumer */
            {
                int j;
                for (j = 0; j < i; j++) {
                    if (ctx->consumers[j]) {
                        consumer_stop(ctx->consumers[j]);
                    }
                }
                for (j = 0; j < i; j++) {
                    if (ctx->consumers[j]) {
                        thread_join(ctx->cons_threads[j]);
                    }
                }
            }
            goto ERR_CONSUMER;
        }
    }

    DBG_LOG("VREC", "%d producer(s) + %d consumer(s) started",
            started, started);
    return 0;

ERR_CONSUMER:
    /* 停止已启动的 producer */
    {
        int j;
        for (j = 0; j < ctx->chn_cnt; j++) {
            ctx->producer_running[j] = TD_FALSE;
        }
        for (j = 0; j < ctx->chn_cnt; j++) {
            if (ctx->prod_threads[j].tid) {
                thread_join(ctx->prod_threads[j]);
            }
        }
    }
    return -1;

ERR_PRODUCER:
    /* 停止当前 chn 之前已启动的 producer */
    {
        int j;
        for (j = 0; j < i; j++) {
            ctx->producer_running[j] = TD_FALSE;
        }
        for (j = 0; j < i; j++) {
            if (ctx->prod_threads[j].tid) {
                thread_join(ctx->prod_threads[j]);
            }
        }
    }
    return -1;
}

int video_record_stop(video_record_ctx_t *ctx)
{
    int i;

    if (!ctx) { return -1; }

    DBG_LOG("VREC", "stopping producers...");

    /* 1. 置所有 producer 运行标志为 false */
    for (i = 0; i < ctx->chn_cnt; i++) {
        if (ctx->consumers[i]) {
            ctx->producer_running[i] = TD_FALSE;
        }
    }

    /* 2. join 已启动的 producer */
    for (i = 0; i < ctx->chn_cnt; i++) {
        if (!ctx->consumers[i]) { continue; }
        DBG_LOG("VREC", "waiting producer chn%d...", i);
        thread_join(ctx->prod_threads[i]);
    }

    /* 3. stop 已启动的 consumer */
    DBG_LOG("VREC", "stopping consumers...");
    for (i = 0; i < ctx->chn_cnt; i++) {
        if (ctx->consumers[i]) {
            consumer_stop(ctx->consumers[i]);
        }
    }

    /* 4. join 已启动的 consumer */
    for (i = 0; i < ctx->chn_cnt; i++) {
        if (!ctx->consumers[i]) { continue; }
        DBG_LOG("VREC", "waiting consumer chn%d...", i);
        thread_join(ctx->cons_threads[i]);
    }

    DBG_LOG("VREC", "all threads stopped");
    return 0;
}

void video_record_deinit(video_record_ctx_t *ctx)
{
    int i;

    if (!ctx) { return; }

    /* 移除 consumers → destroy dispatchers */
    for (i = 0; i < ctx->chn_cnt; i++) {
        if (ctx->consumers[i]) {
            dispatcher_remove_consumer(&ctx->dispatchers[i],
                                       ctx->consumers[i]);
            ctx->consumers[i] = TD_NULL;
        }
    }

    for (i = 0; i < ctx->chn_cnt; i++) {
        dispatcher_destroy(&ctx->dispatchers[i]);
    }

    free(ctx);
    DBG_LOG("VREC", "deinit done");
}
