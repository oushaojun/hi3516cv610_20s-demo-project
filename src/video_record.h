/**
 * @file    video_record.h
 * @brief   视频录像模块 — 统一封装 dispatcher、producer、consumer、MP4 录像
 *
 * 根据 app_config.h 的通道配置智能初始化每路流的 dispatcher 和 consumer,
 * 自动启动取流线程和消费线程, 将编码帧封装为标准 MP4 文件写入 SD 卡。
 *
 * 支持热插拔: SD 卡插入自动创建新文件继续录像, 拔出关闭当前文件。
 *
 * 典型用法:
 * @code
 *   video_record_chn_cfg_t cfgs[3] = {
 *       {1920, 1080, 15, 30},
 *       {640,  360,  25, 30},
 *       {640,  360,  25, 30},
 *   };
 *   video_record_ctx_t *vr = video_record_init(cfgs, APP_VENC_CHN_CNT);
 *   video_record_start(vr);
 *   // ... wait exit signal ...
 *   video_record_stop(vr);
 *   video_record_deinit(vr);
 * @endcode
 */

#ifndef VIDEO_RECORD_H
#define VIDEO_RECORD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 *  类型定义
 * ==================================================================== */

/** 不透明上下文句柄 */
typedef struct video_record_ctx_s video_record_ctx_t;

/** 单通道录像配置 */
typedef struct {
    uint16_t width;      /**< 视频宽度 (像素) */
    uint16_t height;     /**< 视频高度 (像素) */
    uint8_t  fps;        /**< 视频帧率 */
    int      max_queue;  /**< consumer 队列最大帧数 */
    int      codec_type; /**< 编码类型 (OT_PT_H264=96 / OT_PT_H265=265), 非此二值跳过录像 */
} video_record_chn_cfg_t;

/* ====================================================================
 *  API
 * ==================================================================== */

/**
 * @brief  初始化录像模块
 *
 * 内部: 为每个通道创建 dispatcher (含分辨率/帧率参数),
 *       添加 consumer 订阅该通道码流。
 *
 * @param  cfgs     通道配置数组 (长度 = chn_cnt)
 * @param  chn_cnt  通道数量 (1..3)
 * @return 上下文句柄, 失败返回 NULL
 */
video_record_ctx_t *video_record_init(const video_record_chn_cfg_t *cfgs, int chn_cnt);

/**
 * @brief  启动所有 producer/consumer 线程
 * @param  ctx  video_record_init 返回的上下文
 * @return 0 成功, -1 失败 (失败时已启动的线程会被停止)
 */
int video_record_start(video_record_ctx_t *ctx);

/**
 * @brief  停止所有线程 (通知退出 + join 等待)
 *
 * 内部: 置所有 producer 运行标志为 false,
 *       stop 所有 consumer, join 所有线程。
 *
 * @param  ctx  上下文
 * @return 0 成功
 */
int video_record_stop(video_record_ctx_t *ctx);

/**
 * @brief  销毁上下文, 释放所有资源
 *
 * 内部: remove consumer → destroy dispatcher → free ctx。
 * 调用前必须已完成 video_record_stop()。
 *
 * @param  ctx  上下文 (调用后指针失效)
 */
void video_record_deinit(video_record_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* VIDEO_RECORD_H */
