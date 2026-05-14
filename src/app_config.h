/**
 * @file    app_config.h
 * @brief   Hi3516CV610 编码应用宏配置
 *
 * 所有编码参数均在此文件中通过宏定义配置。
 * 修改宏后重新编译即可生效，无需改动 main.c。
 *
 * 每个摄像头最大支持 3 路码流:
 *   通道0: 主码流 (1920x1080 H.265)
 *   通道1: 子码流1 (640x360 H.264)
 *   通道2: 子码流2 (640x360 H.264)
 *
 * 通过 APP_VENC_CHN_CNT 控制启用路数 (1/2/3)。
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "venc_pipeline.h"

/* ====================================================================
 *  传感器 / VI 输入
 *
 *  SC4336P 原生输出 2560x1440，VI 和 VPSS Group 必须匹配此分辨率。
 *  实际编码分辨率由 VPSS 通道下采样 + VENC 配置决定。
 * ==================================================================== */
#define APP_SNS_TYPE       SENSOR0_TYPE
#define APP_VI_WIDTH       2560           /* VI 输入宽度 (匹配传感器) */
#define APP_VI_HEIGHT      1440           /* VI 输入高度 */

/* ====================================================================
 *  编码通道数量 (1 ~ 3)
 *
 *  改为 2 则启用主码流 + 子码流1
 *  改为 3 则启用全部三路
 * ==================================================================== */
#define APP_VENC_CHN_CNT   1

/* ====================================================================
 *  通道0: 主码流 (始终启用)
 * ==================================================================== */
#define APP_VENC0_TYPE      OT_PT_H264
#define APP_VENC0_WIDTH     1920
#define APP_VENC0_HEIGHT    1080
#define APP_VENC0_FPS       25
#define APP_VENC0_GOP       100
#define APP_VENC0_BITRATE   4096        /* Kbps */
#define APP_VENC0_RC_MODE   SAMPLE_RC_CBR
#define APP_VENC0_PROFILE   0
#define APP_VENC0_GOP_MODE  OT_VENC_GOP_MODE_NORMAL_P

/* ====================================================================
 *  通道1: 子码流1 (APP_VENC_CHN_CNT >= 2 时启用)
 * ==================================================================== */
#define APP_VENC1_TYPE      OT_PT_H264
#define APP_VENC1_WIDTH     640
#define APP_VENC1_HEIGHT    360
#define APP_VENC1_FPS       25
#define APP_VENC1_GOP       100
#define APP_VENC1_BITRATE   1024
#define APP_VENC1_RC_MODE   SAMPLE_RC_CBR
#define APP_VENC1_PROFILE   0
#define APP_VENC1_GOP_MODE  OT_VENC_GOP_MODE_NORMAL_P

/* ====================================================================
 *  通道2: 子码流2 (APP_VENC_CHN_CNT >= 3 时启用)
 * ==================================================================== */
#define APP_VENC2_TYPE      OT_PT_H264
#define APP_VENC2_WIDTH     640
#define APP_VENC2_HEIGHT    360
#define APP_VENC2_FPS       25
#define APP_VENC2_GOP       100
#define APP_VENC2_BITRATE   1024
#define APP_VENC2_RC_MODE   SAMPLE_RC_CBR
#define APP_VENC2_PROFILE   0
#define APP_VENC2_GOP_MODE  OT_VENC_GOP_MODE_NORMAL_P

/* ====================================================================
 *  VPSS Group (必须 >= VI 输入分辨率，即传感器原生分辨率)
 * ==================================================================== */
#define APP_VPSS_MAX_W      2560
#define APP_VPSS_MAX_H      1440
#define APP_VPSS_PIX_FMT    OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420

/* ====================================================================
 *  VPSS 通道 (与 VENC 通道一一对应)
 *  通道分辨率须 >= 对应 VENC 分辨率
 * ==================================================================== */

/* VPSS chn0 — 主码流 */
#define APP_VPSS_CHN0_W         1920
#define APP_VPSS_CHN0_H         1080
#define APP_VPSS_CHN0_PIX_FMT   OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420
#define APP_VPSS_CHN0_COMPRESS  OT_COMPRESS_MODE_SEG_COMPACT
#define APP_VPSS_CHN0_DEPTH     0

/* VPSS chn1 — 子码流1 */
#define APP_VPSS_CHN1_W         640
#define APP_VPSS_CHN1_H         360
#define APP_VPSS_CHN1_PIX_FMT   OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420
#define APP_VPSS_CHN1_COMPRESS  OT_COMPRESS_MODE_NONE
#define APP_VPSS_CHN1_DEPTH     0

/* VPSS chn2 — 子码流2 */
#define APP_VPSS_CHN2_W         640
#define APP_VPSS_CHN2_H         360
#define APP_VPSS_CHN2_PIX_FMT   OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420
#define APP_VPSS_CHN2_COMPRESS  OT_COMPRESS_MODE_NONE
#define APP_VPSS_CHN2_DEPTH     0

/* ====================================================================
 *  输出文件前缀
 * ==================================================================== */
#define APP_FILE_PREFIX0    "stream_chn0"
#define APP_FILE_PREFIX1    "stream_chn1"
#define APP_FILE_PREFIX2    "stream_chn2"

/* ====================================================================
 *  VB 池常量
 * ==================================================================== */
#define APP_VB_BLK_CNT    6
#define APP_VB_MAX_POOL   128

#endif /* APP_CONFIG_H */
