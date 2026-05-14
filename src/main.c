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

#include "app_config.h"

/* ===== 应用层全局状态 ===== */
static volatile td_bool g_exit_flag = TD_FALSE;

/* ===== 信号处理 ===== */
static td_void app_sig_handler(td_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        printf("\n[SIGNAL] catch SIGINT/SIGTERM, exiting...\n");
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
        APP_FILE_PREFIX0
    },
    {
        APP_VENC1_TYPE, APP_VENC1_WIDTH, APP_VENC1_HEIGHT,
        APP_VENC1_FPS, APP_VENC1_GOP, APP_VENC1_BITRATE,
        APP_VENC1_RC_MODE, APP_VENC1_PROFILE, APP_VENC1_GOP_MODE,
        APP_VPSS_CHN1_W, APP_VPSS_CHN1_H,
        APP_VPSS_CHN1_PIX_FMT, APP_VPSS_CHN1_COMPRESS, APP_VPSS_CHN1_DEPTH,
        APP_FILE_PREFIX1
    },
    {
        APP_VENC2_TYPE, APP_VENC2_WIDTH, APP_VENC2_HEIGHT,
        APP_VENC2_FPS, APP_VENC2_GOP, APP_VENC2_BITRATE,
        APP_VENC2_RC_MODE, APP_VENC2_PROFILE, APP_VENC2_GOP_MODE,
        APP_VPSS_CHN2_W, APP_VPSS_CHN2_H,
        APP_VPSS_CHN2_PIX_FMT, APP_VPSS_CHN2_COMPRESS, APP_VPSS_CHN2_DEPTH,
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
static td_s32 app_write_stream(const ot_venc_stream *stream, FILE *fp)
{
    td_u32 i;

    if (stream == TD_NULL || stream->pack == TD_NULL || fp == TD_NULL) {
        return TD_FAILURE;
    }

    for (i = 0; i < stream->pack_cnt; i++) {
        (td_void)fwrite(stream->pack[i].addr + stream->pack[i].offset,
                        stream->pack[i].len - stream->pack[i].offset, 1, fp);
        (td_void)fflush(fp);
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

    printf("========================================\n");
    printf("  Hi3516CV610 Encode  (%u channel%s)\n",
           (td_u32)APP_VENC_CHN_CNT, APP_VENC_CHN_CNT > 1 ? "s" : "");
    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        type_str = (g_chn_cfg[chn].type == OT_PT_H265) ? "H.265" :
                   (g_chn_cfg[chn].type == OT_PT_H264) ? "H.264" : "???";
        printf("  chn%u: %s %dx%d @ %dfps, %s %d Kbps\n",
               chn, type_str,
               g_chn_cfg[chn].width, g_chn_cfg[chn].height,
               g_chn_cfg[chn].fps,
               g_chn_cfg[chn].rc_mode == SAMPLE_RC_CBR ? "CBR" :
               g_chn_cfg[chn].rc_mode == SAMPLE_RC_VBR ? "VBR" : "RC?",
               g_chn_cfg[chn].bitrate);
    }
    printf("========================================\n");
}

/* ====================================================================
 *  统一编码流程
 * ==================================================================== */
static td_s32 app_run(td_void)
{
    td_s32           ret = TD_SUCCESS;
    ot_vb_cfg        vb_cfg;
    sample_vi_cfg    vi_cfg;
    FILE            *fp[3] = { TD_NULL, TD_NULL, TD_NULL };
    ot_venc_stream   stream;
    td_char          file_name[128];
    td_u32           chn;

    /* ---- 1. 系统 + VB ---- */
    app_build_vb(&vb_cfg);
    ret = media_vb_init(&vb_cfg,
            OT_VB_SUPPLEMENT_JPEG_MASK | OT_VB_SUPPLEMENT_BNR_MOT_MASK);
    if (ret != TD_SUCCESS) { goto EXIT; }
    printf("[APP] VB init OK\n");

    ret = media_sys_init();
    if (ret != TD_SUCCESS) { goto EXIT_SYS; }
    printf("[APP] SYS init OK\n");

    /* ---- 2. VI ---- */
    sample_comm_vi_get_default_vi_cfg(APP_SNS_TYPE, &vi_cfg);
    ret = media_vi_start(&vi_cfg);
    if (ret != TD_SUCCESS) { goto EXIT_VB; }
    printf("[APP] VI start OK\n");

    /* ---- 3. VPSS ---- */
    {
        media_vpss_grp_attr grp_attr;
        grp_attr.max_width    = APP_VPSS_MAX_W;
        grp_attr.max_height   = APP_VPSS_MAX_H;
        grp_attr.pixel_format = APP_VPSS_PIX_FMT;
        grp_attr.src_frame_rate = -1;
        grp_attr.dst_frame_rate = -1;
        ret = media_vpss_start_grp(0, &grp_attr);
        if (ret != TD_SUCCESS) { goto EXIT_VI; }
        printf("[APP] VPSS grp0 start OK\n");
    }

    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        media_vpss_chn_attr chn_attr;
        chn_attr.width          = g_chn_cfg[chn].vpss_w;
        chn_attr.height         = g_chn_cfg[chn].vpss_h;
        chn_attr.pixel_format   = g_chn_cfg[chn].vpss_pix_fmt;
        chn_attr.compress_mode  = g_chn_cfg[chn].vpss_compress;
        chn_attr.depth          = g_chn_cfg[chn].vpss_depth;
        ret = media_vpss_set_chn(0, (ot_vpss_chn)chn, &chn_attr);
        if (ret != TD_SUCCESS) { goto EXIT_VPSS; }
        ret = media_vpss_enable_chn(0, (ot_vpss_chn)chn);
        if (ret != TD_SUCCESS) { goto EXIT_VPSS; }
        printf("[APP] VPSS chn%u enable OK\n", chn);
    }

    /* ---- 4. VENC ---- */
    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        media_venc_chn_attr venc_attr;
        venc_attr.type       = g_chn_cfg[chn].type;
        venc_attr.size.width  = g_chn_cfg[chn].width;
        venc_attr.size.height = g_chn_cfg[chn].height;
        venc_attr.frame_rate  = g_chn_cfg[chn].fps;
        venc_attr.gop         = g_chn_cfg[chn].gop;
        venc_attr.bitrate     = g_chn_cfg[chn].bitrate;
        venc_attr.rc_mode     = g_chn_cfg[chn].rc_mode;
        venc_attr.profile     = g_chn_cfg[chn].profile;
        venc_attr.gop_mode    = g_chn_cfg[chn].gop_mode;
        ret = media_venc_create((ot_venc_chn)chn, &venc_attr);
        if (ret != TD_SUCCESS) { goto EXIT_VPSS; }
        printf("[APP] VENC chn%u create OK\n", chn);
    }

    /* ---- 5. 绑定 ---- */
    ret = media_mpi_bind_vi_vpss(0, 0, 0, 0);
    if (ret != TD_SUCCESS) { goto EXIT_VENC_ALL; }
    printf("[APP] VI(0,0) bind VPSS(0,0) OK\n");

    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        ret = media_mpi_bind_vpss_venc(0, (ot_vpss_chn)chn, (ot_venc_chn)chn);
        if (ret != TD_SUCCESS) { goto EXIT_UNBIND_ALL; }
        printf("[APP] VPSS(0,%u) bind VENC(%u) OK\n", chn, chn);
    }

    /* ---- 6. 打开文件 ---- */
    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        const td_char *ext = (g_chn_cfg[chn].type == OT_PT_H265) ? "h265" : "h264";
        snprintf(file_name, sizeof(file_name), "%s.%s",
                 g_chn_cfg[chn].file_prefix, ext);
        fp[chn] = fopen(file_name, "wb");
        if (fp[chn] == TD_NULL) {
            printf("[APP] open [%s] failed: %s\n", file_name, strerror(errno));
            goto EXIT_FILE;
        }
        printf("[APP] Output chn%u: %s\n", chn, file_name);
    }

    /* ---- 7. 取流循环 ---- */
    printf("[APP] Encoding... press Ctrl+C to stop\n");
    {
        td_u32 frame_cnt[3] = { 0, 0, 0 };
        td_s32 timeout_ms = (APP_VENC_CHN_CNT == 1) ? 2000 : 0;

        while (!g_exit_flag) {
            for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
                ret = media_venc_get_frame((ot_venc_chn)chn, timeout_ms, &stream);
                if (ret == MEDIA_ERR_VENC_TIMEOUT) {
                    continue;
                }
                if (ret != TD_SUCCESS) {
                    printf("[APP] VENC chn%u get_frame error: 0x%x\n", chn, ret);
                    goto EXIT_LOOP;
                }
                app_write_stream(&stream, fp[chn]);
                media_venc_release_frame((ot_venc_chn)chn, &stream);
                frame_cnt[chn]++;
                if ((frame_cnt[chn] & 0x3F) == 0) {
                    printf("[APP] chn%u encoded %u frames\n", chn, frame_cnt[chn]);
                }
            }
            if (APP_VENC_CHN_CNT > 1) {
                usleep(10000);  /* 多路轮询间隔 10ms */
            }
        }
    }
EXIT_LOOP:

    /* ---- 8. 清理 ---- */
    printf("[APP] Stopping...\n");

EXIT_FILE:
    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        if (fp[chn]) {
            fclose(fp[chn]);
            fp[chn] = TD_NULL;
            printf("[APP] chn%u file closed\n", chn);
        }
    }

    for (chn = APP_VENC_CHN_CNT; chn > 0; chn--) {
        ret = media_mpi_unbind_vpss_venc(0, (ot_vpss_chn)(chn - 1), (ot_venc_chn)(chn - 1));
        printf("[APP] VPSS-VENC chn%u unbind %s (ret=0x%x)\n",
               chn - 1, ret == TD_SUCCESS ? "OK" : "FAIL", ret);
    }
EXIT_UNBIND_ALL:
    ret = media_mpi_unbind_vi_vpss(0, 0, 0, 0);
    printf("[APP] VI-VPSS unbind %s (ret=0x%x)\n",
           ret == TD_SUCCESS ? "OK" : "FAIL", ret);
EXIT_VENC_ALL:
    for (chn = APP_VENC_CHN_CNT; chn > 0; chn--) {
        ret = media_venc_destroy((ot_venc_chn)(chn - 1));
        printf("[APP] VENC chn%u destroy %s (ret=0x%x)\n",
               chn - 1, ret == TD_SUCCESS ? "OK" : "FAIL", ret);
    }
EXIT_VPSS:
    {
        td_bool chn_en[3] = { TD_FALSE, TD_FALSE, TD_FALSE };
        for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
            chn_en[chn] = TD_TRUE;
        }
        media_vpss_stop_grp(0, chn_en, APP_VENC_CHN_CNT);
        printf("[APP] VPSS stop done\n");
    }
EXIT_VI:
    media_vi_stop(&vi_cfg);
    printf("[APP] VI stop done\n");
EXIT_VB:
    media_sys_exit();
    printf("[APP] SYS exit done\n");
EXIT_SYS:
EXIT:
    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        if (fp[chn]) {
            fclose(fp[chn]);
            printf("[APP] chn%u file closed (fallback)\n", chn);
        }
    }
    if (ret == TD_SUCCESS) {
        printf("[APP] Demo exit normally.\n");
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

    setbuf(stdout, NULL);  /* 禁用缓冲，确保诊断即时输出 */
    app_signal_init();
    app_print_banner();
    return app_run();
}
