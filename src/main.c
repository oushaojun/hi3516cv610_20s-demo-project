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
#include <time.h>

#include "app_config.h"
#include "camera_ir.h"
#include "sys_discovery_serv.h"
#include "sys_dbg.h"
#include "sys_notify.h"
#include "sys_sd.h"
#include "sys_thread.h"
#include "sys_uevent.h"
#include "video_dispatcher.h"
#include "video_pipeline.h"

/* ===== 应用层全局状态 ===== */
static volatile td_bool g_exit_flag = TD_FALSE;
static volatile td_bool g_producer_running = TD_FALSE;
static dispatcher_t    g_dispatcher;

/* ===== 信号处理 ===== */
static td_void app_sig_handler(td_s32 signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        DBG_WARN("APP", "catch SIGINT/SIGTERM, exiting...");
        g_exit_flag = TD_TRUE;
    }
}

static td_void app_signal_init(td_void)
{
    signal(SIGCHLD, SIG_IGN);
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
} app_chn_cfg_t;

static const app_chn_cfg_t g_chn_cfg[3] = {
    {
        APP_VENC0_TYPE, APP_VENC0_WIDTH, APP_VENC0_HEIGHT,
        APP_VENC0_FPS, APP_VENC0_GOP, APP_VENC0_BITRATE,
        APP_VENC0_RC_MODE, APP_VENC0_PROFILE, APP_VENC0_GOP_MODE,
        APP_VPSS_CHN0_W, APP_VPSS_CHN0_H,
        APP_VPSS_CHN0_PIX_FMT, APP_VPSS_CHN0_COMPRESS, APP_VPSS_CHN0_DEPTH,
        APP_VPSS_CHN0_SRC_FPS, APP_VPSS_CHN0_DST_FPS
    },
    {
        APP_VENC1_TYPE, APP_VENC1_WIDTH, APP_VENC1_HEIGHT,
        APP_VENC1_FPS, APP_VENC1_GOP, APP_VENC1_BITRATE,
        APP_VENC1_RC_MODE, APP_VENC1_PROFILE, APP_VENC1_GOP_MODE,
        APP_VPSS_CHN1_W, APP_VPSS_CHN1_H,
        APP_VPSS_CHN1_PIX_FMT, APP_VPSS_CHN1_COMPRESS, APP_VPSS_CHN1_DEPTH,
        APP_VPSS_CHN1_SRC_FPS, APP_VPSS_CHN1_DST_FPS
    },
    {
        APP_VENC2_TYPE, APP_VENC2_WIDTH, APP_VENC2_HEIGHT,
        APP_VENC2_FPS, APP_VENC2_GOP, APP_VENC2_BITRATE,
        APP_VENC2_RC_MODE, APP_VENC2_PROFILE, APP_VENC2_GOP_MODE,
        APP_VPSS_CHN2_W, APP_VPSS_CHN2_H,
        APP_VPSS_CHN2_PIX_FMT, APP_VPSS_CHN2_COMPRESS, APP_VPSS_CHN2_DEPTH,
        APP_VPSS_CHN2_SRC_FPS, APP_VPSS_CHN2_DST_FPS
    },
};

/* 供 discovery 模块使用的通道配置摘要 */
static discovery_chn_cfg_t g_disc_cfg[3];

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

    DBG_LOG("APP", "========================================");
    DBG_LOG("APP", "  Hi3516CV610 Encode  (%u channel%s)",
           (td_u32)APP_VENC_CHN_CNT, APP_VENC_CHN_CNT > 1 ? "s" : "");
    DBG_LOG("APP", "  IR Auto : enabled (QFN GPIO %u+%u)", IR_GPIO_NUM_A, IR_GPIO_NUM_B);
    for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
        type_str = (g_chn_cfg[chn].type == OT_PT_H265) ? "H.265" :
                   (g_chn_cfg[chn].type == OT_PT_H264) ? "H.264" : "???";
        DBG_LOG("APP", "  chn%u: %s %dx%d @ %dfps, %s %d Kbps",
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
    DBG_LOG("APP", "========================================");
}

/* ====================================================================
 *  VENC 生产线程 — 取编码帧并投递给 dispatcher
 * ==================================================================== */
static td_void *producer_thread(td_void *arg)
{
    (td_void)arg;
    thread_set_name("enc_producer");

    while (g_producer_running) {
        td_u32 chn;

        for (chn = 0; chn < APP_VENC_CHN_CNT; chn++) {
            ot_venc_stream stream;
            td_s32         ret;
            td_u32         i;
            size_t         total_size;
            uint8_t       *buf;
            frame_t       *f;

            if (!g_producer_running) { break; }

            ret = media_venc_get_frame((ot_venc_chn)chn, 1000, &stream);
            if (ret != TD_SUCCESS) {
                if (ret == MEDIA_ERR_VENC_TIMEOUT) { continue; }
                DBG_WARN("APP", "VENC chn%u get_frame error: 0x%x", chn, ret);
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

                f = frame_create(buf, total_size, pts);
                free(buf);

                if (f) {
                    dispatcher_dispatch(&g_dispatcher, f);
                }
            }

            (td_void)media_venc_release_frame((ot_venc_chn)chn, &stream);
        }
    }

    DBG_LOG("APP", "producer thread exit");
    return TD_NULL;
}

/* ====================================================================
 *  统计消费线程 — 收帧、计数、定期打印
 * ==================================================================== */
static td_void *consumer_thread(td_void *arg)
{
    consumer_t *c = (consumer_t *)arg;
    td_u64      total_frames = 0;
    td_u64      total_bytes  = 0;
    td_u64      batch_frames = 0;
    td_u64      batch_bytes  = 0;
    FILE       *rec_file     = TD_NULL;

    thread_set_name("enc_consumer");

    while (1) {
        frame_t *f = consumer_get_frame(c);
        if (!f) { break; }   /* shutdown */

        total_frames++;
        total_bytes  += f->size;
        batch_frames++;
        batch_bytes  += f->size;

        /* ---- SD 卡录像 ---- */
        {
            td_bool mounted = sys_sd_is_mounted();

            if (mounted && rec_file == TD_NULL) {
                /* SD 卡插入, 以当前时间创建录像文件 */
                time_t    now = time(TD_NULL);
                struct tm tm;
                td_char   fname[64];

                localtime_r(&now, &tm);
                snprintf(fname, sizeof(fname),
                         "/mnt/sdcard/%04d%02d%02d_%02d%02d%02d.h264",
                         tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                         tm.tm_hour, tm.tm_min, tm.tm_sec);

                rec_file = fopen(fname, "wb");
                if (rec_file) {
                    DBG_LOG("APP", "recording started: %s", fname);
                } else {
                    DBG_WARN("APP", "cannot create %s: %s",
                             fname, strerror(errno));
                }
            } else if (!mounted && rec_file != TD_NULL) {
                /* SD 卡拔出, 关闭录像 */
                fclose(rec_file);
                rec_file = TD_NULL;
                DBG_LOG("APP", "recording stopped (sd removed)");
            }

            if (rec_file != TD_NULL) {
                if (fwrite(f->data, 1, f->size, rec_file) != f->size) {
                    DBG_WARN("APP", "write failed, closing file");
                    fclose(rec_file);
                    rec_file = TD_NULL;
                }
            }
        }

        frame_unref(f);

        /* 每 100 帧打一次统计 + fsync 刷盘释放 page cache */
        if (batch_frames >= 100) {
            if (rec_file != TD_NULL) {
                fsync(fileno(rec_file));
            }
            DBG_LOG("APP", "Consumer: %llu frames | %llu KB | avg %.1f KB/frame %s",
                    (unsigned long long)total_frames,
                    (unsigned long long)(total_bytes / 1024),
                    (double)batch_bytes / (double)batch_frames / 1024.0,
                    (rec_file != TD_NULL) ? "[REC]" : "");
            batch_frames = 0;
            batch_bytes  = 0;
        }
    }

    /* 退出前关闭录像文件 */
    if (rec_file != TD_NULL) {
        fclose(rec_file);
        rec_file = TD_NULL;
        DBG_LOG("APP", "recording stopped (exit)");
    }

    /* 标记线程已退出, consumer_destroy 靠此安全清理 */
    consumer_mark_exited(c);

    DBG_LOG("APP", "Consumer exit: total %llu frames, %llu KB",
            (unsigned long long)total_frames,
            (unsigned long long)(total_bytes / 1024));
    return TD_NULL;
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
        DBG_ERROR("APP", "video init failed: 0x%x", ret);
        return -1;
    }
    DBG_LOG("APP", "Pipeline video init OK");

    /* ---- 3. IR Auto ---- */
    ret = ir_auto_init(0);
    if (ret != TD_SUCCESS) { goto EXIT_VIDEO; }
    DBG_LOG("APP", "IR Auto init OK");

    ret = ir_auto_start();
    if (ret != TD_SUCCESS) { goto EXIT_IR_DEINIT; }
    DBG_LOG("APP", "IR Auto thread started");

    /* ---- 4. 启动 dispatcher 生产者/消费者 ---- */
    {
        consumer_t *c;
        thread_t    prod_thr;
        thread_t    cons_thr;

        /* 创建统计消费者 (队列上限 30 帧, 不丢旧帧) */
        c = dispatcher_add_consumer(&g_dispatcher, 30, false);
        if (!c) {
            DBG_ERROR("APP", "add consumer failed");
            ret = TD_FAILURE;
            goto EXIT_IR_STOP;
        }
        DBG_LOG("APP", "consumer added, queue=unlimited");

        ret = thread_create(&cons_thr, "enc_consumer", 16384,
                            consumer_thread, c);
        if (ret != TD_SUCCESS) {
            DBG_ERROR("APP", "consumer thread create failed");
            dispatcher_remove_consumer(&g_dispatcher, c);
            goto EXIT_IR_STOP;
        }

        /* 启动生产者 */
        g_producer_running = TD_TRUE;
        ret = thread_create(&prod_thr, "enc_producer", 32768,
                            producer_thread, TD_NULL);
        if (ret != TD_SUCCESS) {
            DBG_ERROR("APP", "producer thread create failed");
            g_producer_running = TD_FALSE;
            consumer_stop(c);
            thread_join(cons_thr);
            dispatcher_remove_consumer(&g_dispatcher, c);
            goto EXIT_IR_STOP;
        }
        DBG_LOG("APP", "producer/consumer threads started");

        /* ---- 5. 等待退出信号 ---- */
        DBG_LOG("APP", "Running... press Ctrl+C to stop");
        while (!g_exit_flag) {
            usleep(100000);
        }

        DBG_LOG("APP", "Stopping...");

        /* ---- 6. 停止取流线程 ---- */
        g_producer_running = TD_FALSE;
        DBG_LOG("APP", "waiting producer thread...");
        thread_join(prod_thr);

        /* ---- 7. 停止消费线程, 清理消费者 ---- */
        consumer_stop(c);
        DBG_LOG("APP", "waiting consumer thread...");
        thread_join(cons_thr);
        dispatcher_remove_consumer(&g_dispatcher, c);
    }

EXIT_IR_STOP:
    ir_auto_stop();
EXIT_IR_DEINIT:
EXIT_VIDEO:
    media_pipeline_video_deinit(&vi_cfg, APP_VENC_CHN_CNT);

    if (ret == TD_SUCCESS) {
        DBG_LOG("APP", "Demo exit normally.");
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

    dbg_init();
    sys_notify_init();
    sys_uevent_init();
    sys_sd_init();
    dispatcher_init(&g_dispatcher);

    /* 填充 discovery 通道配置 */
    {
        td_u32 i;
        for (i = 0; i < APP_VENC_CHN_CNT && i < 3; i++) {
            g_disc_cfg[i].type    = g_chn_cfg[i].type;
            g_disc_cfg[i].width   = g_chn_cfg[i].width;
            g_disc_cfg[i].height  = g_chn_cfg[i].height;
            g_disc_cfg[i].fps     = g_chn_cfg[i].fps;
            g_disc_cfg[i].bitrate = g_chn_cfg[i].bitrate;
            g_disc_cfg[i].rc_mode = g_chn_cfg[i].rc_mode;
        }
        discovery_serv_init(g_disc_cfg, APP_VENC_CHN_CNT);
    }

    app_signal_init();
    app_print_banner();
    {
        td_s32 rc = app_run();
        discovery_serv_deinit();
        dispatcher_destroy(&g_dispatcher);
        sys_sd_deinit();
        sys_uevent_deinit();
        sys_notify_deinit();
        dbg_deinit();
        return rc;
    }
}
