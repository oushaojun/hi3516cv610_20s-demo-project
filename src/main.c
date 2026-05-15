/**
 * @file    main.c
 * @brief   Hi3516CV610 编码测试 — 统一入口
 *
 * 所有编码参数在 app_config.h 中通过宏配置。
 * 修改 APP_VENC_CHN_CNT 即可启用 1/2/3 路码流。
 *
 * 用法:
 *   ./hi3516_project         # 运行，Ctrl+C 停止
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <pthread.h>

#include "app_config.h"
#include "ir_auto.h"
#include "dbg.h"

/* ===== 应用层全局状态 ===== */
static volatile td_bool g_exit_flag = TD_FALSE;

/* ===== 信号处理 ===== */
static td_void app_sig_handler(td_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        DBG_WARN("APP", "catch SIGINT/SIGTERM, exiting...\n");
        g_exit_flag = TD_TRUE;
    }
}

static td_void app_signal_init(td_void)
{
    signal(SIGINT,  app_sig_handler);
    signal(SIGTERM, app_sig_handler);
}

/* ====================================================================
 *  通道配置表 — 由 app_config.h 宏填充
 * ==================================================================== */
typedef struct {
    ot_payload_type    type;
    td_u32             width;
    td_u32             height;
    td_u32             fps;
    td_u32             gop;
    td_u32             bitrate;
    sample_rc          rc_mode;
    td_u32             profile;
    ot_venc_gop_mode   gop_mode;
    /* VPSS 通道 */
    td_u32             vpss_w;
    td_u32             vpss_h;
    ot_pixel_format    vpss_pix_fmt;
    ot_compress_mode   vpss_compress;
    td_u32             vpss_depth;
    td_u32             vpss_src_fps;   /**< VPSS chn 源帧率 (= APP_SENSOR_FPS) */
    td_u32             vpss_dst_fps;   /**< VPSS chn 目标帧率 (= APP_VENCx_FPS) */
    /* 输出 */
    const td_char     *file_prefix;
} app_chn_cfg_t;

static const app_chn_cfg_t g_chn_cfg[3] = {
    {
        APP_VENC0_TYPE, APP_VENC0_WIDTH, APP_VENC0_HEIGHT,
        APP_VENC0_FPS, APP_VENC0_GOP, APP_VENC0_BITRATE,
        APP_VENC0_RC_MODE, APP_VENC0_PROFILE, APP_VENC0_GOP_MODE,
        APP_VPSS_CHN0_W, APP_VPSS_CHN0_H,
        APP_VPSS_CHN0_PIX_FMT, APP_VPSS_CHN0_COMPRESS, APP_VPSS_CHN0_DEPTH,
        APP_VPSS_CHN0_SRC_FPS, APP_VPSS_CHN0_DST_FPS,
        APP_FILE_PREFIX0
    },
    {
        APP_VENC1_TYPE, APP_VENC1_WIDTH, APP_VENC1_HEIGHT,
        APP_VENC1_FPS, APP_VENC1_GOP, APP_VENC1_BITRATE,
        APP_VENC1_RC_MODE, APP_VENC1_PROFILE, APP_VENC1_GOP_MODE,
        APP_VPSS_CHN1_W, APP_VPSS_CHN1_H,
        APP_VPSS_CHN1_PIX_FMT, APP_VPSS_CHN1_COMPRESS, APP_VPSS_CHN1_DEPTH,
        APP_VPSS_CHN1_SRC_FPS, APP_VPSS_CHN1_DST_FPS,
        APP_FILE_PREFIX1
    },
    {
        APP_VENC2_TYPE, APP_VENC2_WIDTH, APP_VENC2_HEIGHT,
        APP_VENC2_FPS, APP_VENC2_GOP, APP_VENC2_BITRATE,
        APP_VENC2_RC_MODE, APP_VENC2_PROFILE, APP_VENC2_GOP_MODE,
        APP_VPSS_CHN2_W, APP_VPSS_CHN2_H,
        APP_VPSS_CHN2_PIX_FMT, APP_VPSS_CHN2_COMPRESS, APP_VPSS_CHN2_DEPTH,
        APP_VPSS_CHN2_SRC_FPS, APP_VPSS_CHN2_DST_FPS,
        APP_FILE_PREFIX2
    },
};

/* ===== 码流写出 ===== */

/**
 * @brief  将一帧编码数据写入文件
 *
 * Linux 下使用虚拟地址 addr 直接写入。
 * MMP 驱动已将 DMA buffer mmap 到用户空间，无需 phys_addr 回绕。
 */
static td_s32 app_write_stream(td_u32 chn, const ot_venc_stream *stream, td_void *user_data)
{
    FILE   **fp = (FILE **)user_data;
    td_u32   i;

    if (stream == TD_NULL || stream->pack == TD_NULL || fp == TD_NULL
        || fp[chn] == TD_NULL) {
        return TD_FAILURE;
    }

    for (i = 0; i < stream->pack_cnt; i++) {
        (td_void)fwrite(stream->pack[i].addr + stream->pack[i].offset,
                        stream->pack[i].len - stream->pack[i].offset, 1, fp[chn]);
        (td_void)fflush(fp[chn]);
    }
    return TD_SUCCESS;
}

/* ===== VB 池配置构建 ===== */

static td_void app_build_vb(ot_vb_cfg *vb_cfg)
{
    td_u32          i;
    ot_pic_buf_attr buf_attr;
    ot_vb_calc_cfg  calc_cfg;

    (td_void)memset_s(vb_cfg, sizeof(ot_vb_cfg), 0, sizeof(ot_vb_cfg));
    vb_cfg->max_pool_cnt = APP_VB_MAX_POOL;

    /* Pool 0: VI (YUV422) */
    buf_attr.width         = APP_VI_WIDTH;
    buf_attr.height        = APP_VI_HEIGHT;
    buf_attr.align         = OT_DEFAULT_ALIGN;
    buf_attr.bit_width     = OT_DATA_BIT_WIDTH_8;
    buf_attr.pixel_format  = OT_PIXEL_FORMAT_YUV_SEMIPLANAR_422;
    buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;
    buf_attr.video_format  = OT_VIDEO_FORMAT_LINEAR;
    ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
    vb_cfg->common_pool[0].blk_size = calc_cfg.vb_size;
    vb_cfg->common_pool[0].blk_cnt  = APP_VB_BLK_CNT;

    /* Pool 1..N: 各 VENC 通道 (YUV420) */
    for (i = 0; i < APP_VENC_CHN_CNT; i++) {
        buf_attr.width         = g_chn_cfg[i].width;
        buf_attr.height        = g_chn_cfg[i].height;
        buf_attr.pixel_format  = OT_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
        buf_attr.compress_mode = OT_COMPRESS_MODE_NONE;
        ot_common_get_pic_buf_cfg(&buf_attr, &calc_cfg);
        vb_cfg->common_pool[i + 1].blk_size = calc_cfg.vb_size;
        vb_cfg->common_pool[i + 1].blk_cnt  = APP_VB_BLK_CNT;
    }
}

/* ====================================================================
 *  横幅打印
 * ==================================================================== */
static td_void app_print_banner(td_void)
{
    td_u32 chn;
    const td_char *type_str;

    DBG_LOG("APP", "========================================\n");
    DBG_LOG("APP", "  Hi3516CV610 Encode  (%u channel%s)\n",
           (td_u32)APP_VENC_CHN_CNT, APP_VENC_CHN_CNT > 1 ? "s" : "");
    DBG_LOG("APP", "  IR Auto : enabled (QFN GPIO %u+%u)\n", IR_GPIO_NUM_A, IR_GPIO_NUM_B);
    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        type_str = (g_chn_cfg[chn].type == OT_PT_H265) ? "H.265" :
                   (g_chn_cfg[chn].type == OT_PT_H264) ? "H.264" : "???";
        DBG_LOG("APP", "  chn%u: %s %dx%d @ %dfps, %s %d Kbps\n",
               chn, type_str,
               g_chn_cfg[chn].width, g_chn_cfg[chn].height,
               g_chn_cfg[chn].fps,
               g_chn_cfg[chn].rc_mode == SAMPLE_RC_CBR  ? "CBR"  :
               g_chn_cfg[chn].rc_mode == SAMPLE_RC_VBR  ? "VBR"  :
               g_chn_cfg[chn].rc_mode == SAMPLE_RC_AVBR ? "AVBR" :
               g_chn_cfg[chn].rc_mode == SAMPLE_RC_QVBR ? "QVBR" :
               g_chn_cfg[chn].rc_mode == SAMPLE_RC_CVBR ? "CVBR" :
               g_chn_cfg[chn].rc_mode == SAMPLE_RC_ABR  ? "ABR"  : "RC?",
               g_chn_cfg[chn].bitrate);
    }
    DBG_LOG("APP", "========================================\n");
}

/* ====================================================================
 *  统一编码流程
 * ==================================================================== */
static td_s32 app_run(td_void)
{
    td_s32              ret;
    ot_vb_cfg           vb_cfg;
    sample_vi_cfg       vi_cfg;
    media_vpss_grp_attr grp_attr;
    media_vpss_chn_attr chn_attr[3];
    media_venc_chn_attr venc_attr[3];
    FILE               *fp[3] = { TD_NULL, TD_NULL, TD_NULL };
    td_char             file_name[128];
    const td_char      *file_names[3];
    pthread_t           stream_tid  = 0;
    media_stream_thread_arg stream_arg;
    td_u32              chn;

    /* ---- 1. 组装 VPSS/VENC 属性 ---- */
    grp_attr.max_width     = APP_VPSS_MAX_W;
    grp_attr.max_height    = APP_VPSS_MAX_H;
    grp_attr.pixel_format  = APP_VPSS_PIX_FMT;
    grp_attr.src_frame_rate = -1;
    grp_attr.dst_frame_rate = -1;

    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        chn_attr[chn].width          = g_chn_cfg[chn].vpss_w;
        chn_attr[chn].height         = g_chn_cfg[chn].vpss_h;
        chn_attr[chn].pixel_format   = g_chn_cfg[chn].vpss_pix_fmt;
        chn_attr[chn].compress_mode  = g_chn_cfg[chn].vpss_compress;
        chn_attr[chn].depth          = g_chn_cfg[chn].vpss_depth;
        chn_attr[chn].src_frame_rate = (td_s32)g_chn_cfg[chn].vpss_src_fps;
        chn_attr[chn].dst_frame_rate = (td_s32)g_chn_cfg[chn].vpss_dst_fps;

        venc_attr[chn].type        = g_chn_cfg[chn].type;
        venc_attr[chn].size.width  = g_chn_cfg[chn].width;
        venc_attr[chn].size.height = g_chn_cfg[chn].height;
        venc_attr[chn].frame_rate  = g_chn_cfg[chn].fps;
        venc_attr[chn].gop         = g_chn_cfg[chn].gop;
        venc_attr[chn].bitrate     = g_chn_cfg[chn].bitrate;
        venc_attr[chn].rc_mode     = g_chn_cfg[chn].rc_mode;
        venc_attr[chn].profile     = g_chn_cfg[chn].profile;
        venc_attr[chn].gop_mode    = g_chn_cfg[chn].gop_mode;
    }

    /* ---- 2. 视频管线初始化 ---- */
    app_build_vb(&vb_cfg);
    ret = media_pipeline_video_init(&vb_cfg,
            OT_VB_SUPPLEMENT_JPEG_MASK | OT_VB_SUPPLEMENT_BNR_MOT_MASK,
            APP_SNS_TYPE, &grp_attr, chn_attr, venc_attr,
            APP_VENC_CHN_CNT, &vi_cfg);
    if (ret != TD_SUCCESS) {
        DBG_ERROR("APP", "video init failed: 0x%x\n", ret);
        return -1;
    }
    DBG_LOG("APP", "Pipeline video init OK\n");

    /* ---- 3. IR Auto ---- */
    ret = ir_auto_init(0);
    if (ret != TD_SUCCESS) { goto EXIT_VIDEO; }
    DBG_LOG("APP", "IR Auto init OK\n");

    ret = ir_auto_start();
    if (ret != TD_SUCCESS) { goto EXIT_IR_DEINIT; }
    DBG_LOG("APP", "IR Auto thread started\n");

    /* ---- 4. 打开输出文件 ---- */
    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        const td_char *ext = (g_chn_cfg[chn].type == OT_PT_H265) ? "h265" : "h264";
        snprintf(file_name, sizeof(file_name), "%s.%s",
                 g_chn_cfg[chn].file_prefix, ext);
        file_names[chn] = file_name;
    }
    ret = media_pipeline_stream_init(fp, file_names, APP_VENC_CHN_CNT);
    if (ret != TD_SUCCESS) { goto EXIT_IR_STOP; }

    /* ---- 5. 启动取流线程 ---- */
    stream_arg.chn_cnt   = APP_VENC_CHN_CNT;
    stream_arg.exit_flag = &g_exit_flag;
    stream_arg.callback  = app_write_stream;
    stream_arg.user_data = fp;

    ret = pthread_create(&stream_tid, TD_NULL,
                         media_pipeline_stream_thread, &stream_arg);
    if (ret != 0) {
        DBG_ERROR("APP", "stream thread create failed: %d\n", ret);
        goto EXIT_FILE;
    }
    DBG_LOG("APP", "Encoding... press Ctrl+C to stop\n");

    /* ---- 6. 等待退出信号 ---- */
    while (!g_exit_flag) {
        usleep(100000);
    }

    DBG_LOG("APP", "Stopping...\n");

    /* ---- 7. 等待取流线程结束 ---- */
    pthread_join(stream_tid, TD_NULL);

    /* ---- 8. 逆序清理 ---- */
    media_pipeline_stream_deinit(fp, APP_VENC_CHN_CNT);
EXIT_FILE:
    ir_auto_stop();
EXIT_IR_STOP:
EXIT_IR_DEINIT:
EXIT_VIDEO:
    media_pipeline_video_deinit(&vi_cfg, APP_VENC_CHN_CNT);

    if (ret == TD_SUCCESS) {
        DBG_LOG("APP", "Demo exit normally.\n");
    }
    return (ret == TD_SUCCESS) ? 0 : -1;
}

/* ====================================================================
 *  main
 * ==================================================================== */
int main(int argc, char **argv)
{
    (td_void)argc;
    (td_void)argv;

    app_signal_init();
    app_print_banner();
    return app_run();
}
