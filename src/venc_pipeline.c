/**
 * @file    venc_pipeline.c
 * @brief   Hi3516CV610 媒体模块实现
 *
 * 提供 VI / VPSS / VENC / Bind 的细粒度接口。
 * 应用层负责编排调用顺序和控制流。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/select.h>

#include "venc_pipeline.h"
#include "sample_comm.h"
#include "securec.h"

#include "ss_mpi_sys.h"
#include "ss_mpi_vb.h"
#include "ss_mpi_vpss.h"
#include "ss_mpi_venc.h"

/* ====================================================================
 *  System / VB
 * ==================================================================== */

td_s32 media_sys_init(td_void)
{
    return ss_mpi_sys_init();
}

td_void media_sys_exit(td_void)
{
    ss_mpi_sys_exit();
    ss_mpi_vb_exit();
}

td_s32 media_vb_init(const ot_vb_cfg *vb_cfg, td_u32 supplement)
{
    td_s32                ret;
    ot_vb_supplement_cfg  supp_cfg = { 0 };

    if (vb_cfg == TD_NULL) {
        printf("[MEDIA] vb_cfg is null!\n");
        return TD_FAILURE;
    }

    /* 先退出旧状态，确保干净初始化 */
    ss_mpi_sys_exit();
    ss_mpi_vb_exit();

    ret = ss_mpi_vb_set_cfg(vb_cfg);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] ss_mpi_vb_set_cfg failed: 0x%x\n", ret);
        return ret;
    }

    supp_cfg.supplement_cfg = supplement;
    ret = ss_mpi_vb_set_supplement_cfg(&supp_cfg);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] ss_mpi_vb_set_supplement_cfg failed: 0x%x\n", ret);
        return ret;
    }

    ret = ss_mpi_vb_init();
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] ss_mpi_vb_init failed: 0x%x\n", ret);
    }
    return ret;
}

/* ====================================================================
 *  VI
 * ==================================================================== */

td_s32 media_vi_start(const sample_vi_cfg *vi_cfg)
{
    td_s32 ret;

    if (vi_cfg == TD_NULL) {
        printf("[MEDIA] vi_cfg is null!\n");
        return TD_FAILURE;
    }

    ret = sample_comm_vi_start_vi(vi_cfg);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VI start failed: 0x%x\n", ret);
    }
    return ret;
}

td_void media_vi_stop(const sample_vi_cfg *vi_cfg)
{
    if (vi_cfg != TD_NULL) {
        sample_comm_vi_stop_vi(vi_cfg);
    }
}

/* ====================================================================
 *  VPSS
 * ==================================================================== */

td_s32 media_vpss_start_grp(ot_vpss_grp grp, const media_vpss_grp_attr *attr)
{
    td_s32            ret;
    ot_vpss_grp_attr  grp_attr = { 0 };

    if (attr == TD_NULL) {
        printf("[MEDIA] vpss grp attr is null!\n");
        return TD_FAILURE;
    }

    grp_attr.max_width    = attr->max_width;
    grp_attr.max_height   = attr->max_height;
    grp_attr.dei_mode     = OT_VPSS_DEI_MODE_OFF;
    grp_attr.pixel_format = attr->pixel_format;
    grp_attr.frame_rate.src_frame_rate = attr->src_frame_rate;
    grp_attr.frame_rate.dst_frame_rate = attr->dst_frame_rate;

    ret = ss_mpi_vpss_create_grp(grp, &grp_attr);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VPSS grp%u create failed: 0x%x\n", grp, ret);
        return ret;
    }

    ret = ss_mpi_vpss_start_grp(grp);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VPSS grp%u start failed: 0x%x\n", grp, ret);
        ss_mpi_vpss_destroy_grp(grp);
        return ret;
    }

    return TD_SUCCESS;
}

td_s32 media_vpss_set_chn(ot_vpss_grp grp, ot_vpss_chn chn, const media_vpss_chn_attr *attr)
{
    td_s32            ret;
    ot_vpss_chn_attr  chn_attr = { 0 };

    if (attr == TD_NULL) {
        printf("[MEDIA] vpss chn attr is null!\n");
        return TD_FAILURE;
    }

    chn_attr.width            = attr->width;
    chn_attr.height           = attr->height;
    chn_attr.chn_mode         = OT_VPSS_CHN_MODE_USER;
    chn_attr.compress_mode    = attr->compress_mode;
    chn_attr.pixel_format     = attr->pixel_format;
    chn_attr.frame_rate.src_frame_rate = attr->src_frame_rate;
    chn_attr.frame_rate.dst_frame_rate = attr->dst_frame_rate;
    chn_attr.depth            = attr->depth;
    chn_attr.mirror_en        = TD_FALSE;
    chn_attr.flip_en          = TD_FALSE;
    chn_attr.aspect_ratio.mode = OT_ASPECT_RATIO_NONE;

    ret = ss_mpi_vpss_set_chn_attr(grp, chn, &chn_attr);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VPSS grp%u chn%u set attr failed: 0x%x\n", grp, chn, ret);
    }
    return ret;
}

td_s32 media_vpss_enable_chn(ot_vpss_grp grp, ot_vpss_chn chn)
{
    td_s32 ret;

    ret = ss_mpi_vpss_enable_chn(grp, chn);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VPSS grp%u chn%u enable failed: 0x%x\n", grp, chn, ret);
    }
    return ret;
}

td_void media_vpss_stop_grp(ot_vpss_grp grp, const td_bool *chn_en, td_u32 chn_cnt)
{
    td_s32 ret;
    td_u32 i;

    /* 去使能并设置无效属性 (管道停止前先关通道) */
    if (chn_en != TD_NULL) {
        for (i = 0; i < chn_cnt; i++) {
            if (chn_en[i] == TD_TRUE) {
                ss_mpi_vpss_disable_chn(grp, (ot_vpss_chn)i);

                /* 清空通道属性，避免下次复用残留 */
                ot_vpss_chn_attr clr = { 0 };
                ss_mpi_vpss_set_chn_attr(grp, (ot_vpss_chn)i, &clr);
            }
        }
    }

    ret = ss_mpi_vpss_stop_grp(grp);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VPSS grp%u stop failed: 0x%x\n", grp, ret);
    }

    ret = ss_mpi_vpss_destroy_grp(grp);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VPSS grp%u destroy failed: 0x%x\n", grp, ret);
    }
}

/* ====================================================================
 *  VENC
 * ==================================================================== */

td_s32 media_venc_create(ot_venc_chn chn, const media_venc_chn_attr *attr)
{
    td_s32                     ret;
    sample_comm_venc_chn_param param       = { 0 };
    ot_venc_gop_attr           gop_attr    = { 0 };
    ot_venc_start_param        start_param;

    if (attr == TD_NULL) {
        printf("[MEDIA] venc chn attr is null!\n");
        return TD_FAILURE;
    }

    /* 获取 GOP 属性 (含各模式的子结构参数) */
    ret = sample_comm_venc_get_gop_attr(attr->gop_mode, &gop_attr);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] unsupported gop mode: %d\n", attr->gop_mode);
        return TD_FAILURE;
    }

    param.type                 = attr->type;
    param.size                 = sample_comm_sys_get_pic_enum(&attr->size);
    param.frame_rate           = attr->frame_rate;
    param.gop                  = attr->gop;
    param.stats_time           = (attr->gop/attr->frame_rate)?(attr->gop/attr->frame_rate):1;
    param.gop_attr             = gop_attr;
    param.rc_mode              = attr->rc_mode;
    param.profile              = attr->profile;
    param.is_rcn_ref_share_buf = TD_TRUE;

    /* step 1: create VENC channel (sample_comm_venc_create 做 create_chn + close_reencode) */
    ret = sample_comm_venc_create(chn, &param);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VENC chn%u create failed: 0x%x\n", chn, ret);
        return ret;
    }

    /* step 2: set RC param (max_qp/min_qp 等, SDK sample 层未设) */
    {
        ot_venc_rc_param rc_param;
        ret = ss_mpi_venc_get_rc_param(chn, &rc_param);
        if (ret != TD_SUCCESS) {
            printf("[MEDIA] VENC chn%u get_rc_param failed: 0x%x\n", chn, ret);
            ss_mpi_venc_destroy_chn(chn);
            return ret;
        }

        if (attr->type == OT_PT_H264) {
            switch (attr->rc_mode) {
            case SAMPLE_RC_ABR:
                rc_param.h264_abr_param.max_p_qp  = 51;
                rc_param.h264_abr_param.min_p_qp  = 15;
                rc_param.h264_abr_param.max_i_qp  = 40;
                rc_param.h264_abr_param.min_i_qp  = 15;
                break;
            case SAMPLE_RC_CBR:
                rc_param.h264_cbr_param.max_qp   = 51;
                rc_param.h264_cbr_param.min_qp   = 15;
                rc_param.h264_cbr_param.max_i_qp = 40;
                rc_param.h264_cbr_param.min_i_qp = 15;
                break;
            case SAMPLE_RC_VBR:
                rc_param.h264_vbr_param.max_qp   = 51;
                rc_param.h264_vbr_param.min_qp   = 28;
                rc_param.h264_vbr_param.max_i_qp = 40;
                rc_param.h264_vbr_param.min_i_qp = 28;
                break;
            case SAMPLE_RC_AVBR:
                rc_param.h264_avbr_param.max_qp   = 51;
                rc_param.h264_avbr_param.min_qp   = 28;
                rc_param.h264_avbr_param.max_i_qp = 40;
                rc_param.h264_avbr_param.min_i_qp = 28;
                break;
            case SAMPLE_RC_QVBR:
                rc_param.h264_qvbr_param.max_qp   = 51;
                rc_param.h264_qvbr_param.min_qp   = 28;
                rc_param.h264_qvbr_param.max_i_qp = 40;
                rc_param.h264_qvbr_param.min_i_qp = 28;
                break;
            case SAMPLE_RC_CVBR:
                rc_param.h264_cvbr_param.max_qp   = 51;
                rc_param.h264_cvbr_param.min_qp   = 28;
                rc_param.h264_cvbr_param.max_i_qp = 40;
                rc_param.h264_cvbr_param.min_i_qp = 28;
                break;
            default:
                break;
            }
        } else if (attr->type == OT_PT_H265) {
            switch (attr->rc_mode) {
            case SAMPLE_RC_ABR:
                rc_param.h265_abr_param.max_p_qp  = 51;
                rc_param.h265_abr_param.min_p_qp  = 15;
                rc_param.h265_abr_param.max_i_qp  = 40;
                rc_param.h265_abr_param.min_i_qp  = 15;
                break;
            case SAMPLE_RC_CBR:
                rc_param.h265_cbr_param.max_qp   = 51;
                rc_param.h265_cbr_param.min_qp   = 15;
                rc_param.h265_cbr_param.max_i_qp = 40;
                rc_param.h265_cbr_param.min_i_qp = 15;
                break;
            case SAMPLE_RC_VBR:
                rc_param.h265_vbr_param.max_qp   = 51;
                rc_param.h265_vbr_param.min_qp   = 28;
                rc_param.h265_vbr_param.max_i_qp = 40;
                rc_param.h265_vbr_param.min_i_qp = 28;
                break;
            case SAMPLE_RC_AVBR:
                rc_param.h265_avbr_param.max_qp   = 51;
                rc_param.h265_avbr_param.min_qp   = 28;
                rc_param.h265_avbr_param.max_i_qp = 40;
                rc_param.h265_avbr_param.min_i_qp = 28;
                break;
            case SAMPLE_RC_QVBR:
                rc_param.h265_qvbr_param.max_qp   = 51;
                rc_param.h265_qvbr_param.min_qp   = 28;
                rc_param.h265_qvbr_param.max_i_qp = 40;
                rc_param.h265_qvbr_param.min_i_qp = 28;
                break;
            case SAMPLE_RC_CVBR:
                rc_param.h265_cvbr_param.max_qp   = 51;
                rc_param.h265_cvbr_param.min_qp   = 28;
                rc_param.h265_cvbr_param.max_i_qp = 40;
                rc_param.h265_cvbr_param.min_i_qp = 28;
                break;
            default:
                break;
            }
        }

        ret = ss_mpi_venc_set_rc_param(chn, &rc_param);
        if (ret != TD_SUCCESS) {
            printf("[MEDIA] VENC chn%u set_rc_param failed: 0x%x\n", chn, ret);
            ss_mpi_venc_destroy_chn(chn);
            return ret;
        }
        printf("[MEDIA] VENC chn%u RC param configured (mode=%d)\n", chn, (int)attr->rc_mode);
    }

    /* step 3: set VUI timing (修正 ffprobe 读到的帧率)
     *  H.264: fps = time_scale / (2 * num_units_in_tick) */
    if (attr->type == OT_PT_H264) {
        ot_venc_h264_vui vui;
        ret = ss_mpi_venc_get_h264_vui(chn, &vui);
        if (ret != TD_SUCCESS) {
            printf("[MEDIA] VENC chn%u get_h264_vui failed: 0x%x\n", chn, ret);
            ss_mpi_venc_destroy_chn(chn);
            return ret;
        }
        vui.vui_time_info.timing_info_present_flag = 1;
        vui.vui_time_info.fixed_frame_rate_flag     = 1;
        vui.vui_time_info.num_units_in_tick          = 1;
        /* H.264 progressive: fps = time_scale / (2 * num_units_in_tick) */
        vui.vui_time_info.time_scale                 = attr->frame_rate * 2;
        ret = ss_mpi_venc_set_h264_vui(chn, &vui);
        if (ret != TD_SUCCESS) {
            printf("[MEDIA] VENC chn%u set_h264_vui failed: 0x%x\n", chn, ret);
            ss_mpi_venc_destroy_chn(chn);
            return ret;
        }
        printf("[MEDIA] VENC chn%u H.264 VUI: time_scale=%u -> %ufps\n",
               chn, vui.vui_time_info.time_scale, attr->frame_rate);
    } else if (attr->type == OT_PT_H265) {
        ot_venc_h265_vui vui;
        ret = ss_mpi_venc_get_h265_vui(chn, &vui);
        if (ret != TD_SUCCESS) {
            printf("[MEDIA] VENC chn%u get_h265_vui failed: 0x%x\n", chn, ret);
            ss_mpi_venc_destroy_chn(chn);
            return ret;
        }
        vui.vui_time_info.timing_info_present_flag = 1;
        vui.vui_time_info.num_units_in_tick        = 1;
        /* H.265: fps = time_scale / ((num_ticks_poc_diff_one_minus1+1) * num_units_in_tick)
         *  VENC 实际 POC 间隔为 1, 故 num_ticks_poc_diff_one_minus1 = 0 */
        vui.vui_time_info.time_scale               = attr->frame_rate;
        vui.vui_time_info.num_ticks_poc_diff_one_minus1 = 0;
        ret = ss_mpi_venc_set_h265_vui(chn, &vui);
        if (ret != TD_SUCCESS) {
            printf("[MEDIA] VENC chn%u set_h265_vui failed: 0x%x\n", chn, ret);
            ss_mpi_venc_destroy_chn(chn);
            return ret;
        }
        printf("[MEDIA] VENC chn%u H.265 VUI: time_scale=%u -> %ufps\n",
               chn, vui.vui_time_info.time_scale, attr->frame_rate);
    }

    /* step 4: start encoding */
    start_param.recv_pic_num = -1;
    ret = ss_mpi_venc_start_chn(chn, &start_param);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VENC chn%u start_chn failed: 0x%x\n", chn, ret);
        ss_mpi_venc_destroy_chn(chn);
        return ret;
    }

    return TD_SUCCESS;
}

td_s32 media_venc_stop(ot_venc_chn chn)
{
    td_s32 ret;

    ret = ss_mpi_venc_stop_chn(chn);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VENC chn%u stop failed: 0x%x\n", chn, ret);
    }
    return ret;
}

td_s32 media_venc_destroy(ot_venc_chn chn)
{
    td_s32 ret;

    ret = ss_mpi_venc_stop_chn(chn);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VENC chn%u stop failed: 0x%x\n", chn, ret);
        /* 继续尝试销毁 */
    }

    ret = ss_mpi_venc_destroy_chn(chn);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VENC chn%u destroy failed: 0x%x\n", chn, ret);
    }
    return ret;
}

/* ====================================================================
 *  VENC 取流
 * ==================================================================== */

td_s32 media_venc_get_frame(ot_venc_chn chn, td_s32 timeout_ms, ot_venc_stream *stream)
{
    td_s32              fd;
    td_s32              ret;
    ot_venc_chn_status  stat;
    fd_set              read_fds;
    struct timeval      tv;
    struct timeval     *ptv;

    if (stream == TD_NULL) {
        printf("[MEDIA] stream out param is null!\n");
        return TD_FAILURE;
    }
    (td_void)memset_s(stream, sizeof(ot_venc_stream), 0, sizeof(ot_venc_stream));

    fd = ss_mpi_venc_get_fd(chn);
    if (fd < 0) {
        printf("[MEDIA] VENC chn%u get_fd failed: 0x%x\n", chn, fd);
        return TD_FAILURE;
    }

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    if (timeout_ms >= 0) {
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ptv = &tv;
    } else {
        ptv = TD_NULL;   /* < 0: 无限等待 */
    }

    ret = select(fd + 1, &read_fds, TD_NULL, TD_NULL, ptv);
    if (ret < 0) {
        if (errno == EINTR) {
            return MEDIA_ERR_VENC_TIMEOUT;  /* 被信号中断, 由上层检查退出标志 */
        }
        printf("[MEDIA] VENC chn%u select failed: errno=%d\n", chn, errno);
        return TD_FAILURE;
    }
    if (ret == 0) {
        /* 超时无数据 */
        return MEDIA_ERR_VENC_TIMEOUT;
    }

    /* fd 就绪，查询 pack 数量 */
    ret = ss_mpi_venc_query_status(chn, &stat);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VENC chn%u query_status failed: 0x%x\n", chn, ret);
        return ret;
    }
    if (stat.cur_packs == 0) {
        return MEDIA_ERR_VENC_TIMEOUT;
    }

    /* 分配 pack 数组 */
    stream->pack = (ot_venc_pack *)malloc(sizeof(ot_venc_pack) * stat.cur_packs);
    if (stream->pack == TD_NULL) {
        printf("[MEDIA] VENC chn%u malloc pack failed\n", chn);
        return TD_FAILURE;
    }
    stream->pack_cnt = stat.cur_packs;

    /* 获取码流 (fd 已就绪，非阻塞) */
    ret = ss_mpi_venc_get_stream(chn, stream, 0);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VENC chn%u get_stream failed: 0x%x\n", chn, ret);
        free(stream->pack);
        stream->pack     = TD_NULL;
        stream->pack_cnt = 0;
    }
    return ret;
}

td_s32 media_venc_release_frame(ot_venc_chn chn, ot_venc_stream *stream)
{
    td_s32 ret;

    if (stream == TD_NULL || stream->pack == TD_NULL) {
        return TD_SUCCESS;
    }

    ret = ss_mpi_venc_release_stream(chn, stream);
    if (ret != TD_SUCCESS) {
        printf("[MEDIA] VENC chn%u release_stream failed: 0x%x\n", chn, ret);
    }

    free(stream->pack);
    stream->pack     = TD_NULL;
    stream->pack_cnt = 0;

    return ret;
}

/* ====================================================================
 *  MPP 绑定 / 解绑
 * ==================================================================== */

td_s32 media_mpi_bind_vi_vpss(ot_vi_pipe vi_pipe, ot_vi_chn vi_chn,
                               ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn)
{
    ot_mpp_chn src_chn;
    ot_mpp_chn dst_chn;

    src_chn.mod_id = OT_ID_VI;
    src_chn.dev_id = vi_pipe;
    src_chn.chn_id = vi_chn;

    dst_chn.mod_id = OT_ID_VPSS;
    dst_chn.dev_id = vpss_grp;
    dst_chn.chn_id = vpss_chn;

    return ss_mpi_sys_bind(&src_chn, &dst_chn);
}

td_s32 media_mpi_unbind_vi_vpss(ot_vi_pipe vi_pipe, ot_vi_chn vi_chn,
                                 ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn)
{
    ot_mpp_chn src_chn;
    ot_mpp_chn dst_chn;

    src_chn.mod_id = OT_ID_VI;
    src_chn.dev_id = vi_pipe;
    src_chn.chn_id = vi_chn;

    dst_chn.mod_id = OT_ID_VPSS;
    dst_chn.dev_id = vpss_grp;
    dst_chn.chn_id = vpss_chn;

    return ss_mpi_sys_unbind(&src_chn, &dst_chn);
}

td_s32 media_mpi_bind_vpss_venc(ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn, ot_venc_chn venc_chn)
{
    ot_mpp_chn src_chn;
    ot_mpp_chn dst_chn;

    src_chn.mod_id = OT_ID_VPSS;
    src_chn.dev_id = vpss_grp;
    src_chn.chn_id = vpss_chn;

    dst_chn.mod_id = OT_ID_VENC;
    dst_chn.dev_id = 0;
    dst_chn.chn_id = venc_chn;

    return ss_mpi_sys_bind(&src_chn, &dst_chn);
}

td_s32 media_mpi_unbind_vpss_venc(ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn, ot_venc_chn venc_chn)
{
    ot_mpp_chn src_chn;
    ot_mpp_chn dst_chn;

    src_chn.mod_id = OT_ID_VPSS;
    src_chn.dev_id = vpss_grp;
    src_chn.chn_id = vpss_chn;

    dst_chn.mod_id = OT_ID_VENC;
    dst_chn.dev_id = 0;
    dst_chn.chn_id = venc_chn;

    return ss_mpi_sys_unbind(&src_chn, &dst_chn);
}

/* ====================================================================
 *  Pipeline 一体化接口
 * ==================================================================== */

td_s32 media_pipeline_video_init(const ot_vb_cfg *vb_cfg, td_u32 supplement,
                                  sample_sns_type sns_type,
                                  const media_vpss_grp_attr *grp_attr,
                                  const media_vpss_chn_attr *chn_attr_arr,
                                  const media_venc_chn_attr *venc_attr_arr,
                                  td_u32 chn_cnt,
                                  sample_vi_cfg *out_vi_cfg)
{
    td_s32          ret;
    td_u32          i;
    td_u32          venc_done = 0;
    td_bool         vpss_up   = TD_FALSE;
    sample_vi_cfg   vi_cfg;

    if (chn_cnt == 0 || chn_cnt > 4) {
        printf("[MEDIA] invalid chn_cnt: %u\n", chn_cnt);
        return TD_FAILURE;
    }

    /* ---- 1. VB ---- */
    ret = media_vb_init(vb_cfg, supplement);
    if (ret != TD_SUCCESS) { return ret; }
    printf("[APP] VB init OK\n");

    /* ---- 2. SYS ---- */
    ret = media_sys_init();
    if (ret != TD_SUCCESS) { goto EXIT_SYS_FAIL; }
    printf("[APP] SYS init OK\n");

    /* ---- 3. VI ---- */
    sample_comm_vi_get_default_vi_cfg(sns_type, &vi_cfg);
    ret = media_vi_start(&vi_cfg);
    if (ret != TD_SUCCESS) { goto EXIT_SYS_CLEAN; }
    printf("[APP] VI start OK\n");

    /* ---- 4. VPSS grp + chns ---- */
    ret = media_vpss_start_grp(0, grp_attr);
    if (ret != TD_SUCCESS) { goto EXIT_VI_CLEAN; }
    vpss_up = TD_TRUE;
    printf("[APP] VPSS grp0 start OK\n");

    for (i = 0; i < chn_cnt; i++) {
        ret = media_vpss_set_chn(0, (ot_vpss_chn)i, &chn_attr_arr[i]);
        if (ret != TD_SUCCESS) { goto EXIT_VPSS_CLEAN; }
        ret = media_vpss_enable_chn(0, (ot_vpss_chn)i);
        if (ret != TD_SUCCESS) { goto EXIT_VPSS_CLEAN; }
        printf("[APP] VPSS chn%u enable OK\n", i);
    }

    /* ---- 5. VENC ---- */
    for (i = 0; i < chn_cnt; i++) {
        ret = media_venc_create((ot_venc_chn)i, &venc_attr_arr[i]);
        if (ret != TD_SUCCESS) { goto EXIT_VENC_CLEAN; }
        venc_done++;
        printf("[APP] VENC chn%u create OK\n", i);
    }

    /* ---- 6. Bind ---- */
    ret = media_mpi_bind_vi_vpss(0, 0, 0, 0);
    if (ret != TD_SUCCESS) { goto EXIT_VENC_CLEAN; }
    printf("[APP] VI(0,0) bind VPSS(0,0) OK\n");

    for (i = 0; i < chn_cnt; i++) {
        ret = media_mpi_bind_vpss_venc(0, (ot_vpss_chn)i, (ot_venc_chn)i);
        if (ret != TD_SUCCESS) { goto EXIT_UNBIND_CLEAN; }
        printf("[APP] VPSS(0,%u) bind VENC(%u) OK\n", i, i);
    }

    *out_vi_cfg = vi_cfg;
    return TD_SUCCESS;

EXIT_UNBIND_CLEAN:
    media_mpi_unbind_vi_vpss(0, 0, 0, 0);
    for (i = 0; i < chn_cnt; i++) {
        media_mpi_unbind_vpss_venc(0, (ot_vpss_chn)i, (ot_venc_chn)i);
    }
EXIT_VENC_CLEAN:
    for (i = 0; i < venc_done; i++) {
        media_venc_destroy((ot_venc_chn)i);
    }
EXIT_VPSS_CLEAN:
    if (vpss_up) {
        td_bool en[4] = { TD_FALSE };
        for (i = 0; i < chn_cnt; i++) { en[i] = TD_TRUE; }
        media_vpss_stop_grp(0, en, chn_cnt);
    }
EXIT_VI_CLEAN:
    media_vi_stop(&vi_cfg);
EXIT_SYS_CLEAN:
    media_sys_exit();
EXIT_SYS_FAIL:
    return ret;
}

td_void media_pipeline_video_deinit(const sample_vi_cfg *vi_cfg, td_u32 chn_cnt)
{
    td_u32  i;
    td_bool en[4] = { TD_FALSE };

    printf("[APP] Pipeline video deinit...\n");

    /* unbind (正序解绑) */
    for (i = 0; i < chn_cnt; i++) {
        media_mpi_unbind_vpss_venc(0, (ot_vpss_chn)i, (ot_venc_chn)i);
    }
    media_mpi_unbind_vi_vpss(0, 0, 0, 0);
    printf("[APP] unbind done\n");

    /* VENC destroy */
    for (i = 0; i < chn_cnt; i++) {
        media_venc_destroy((ot_venc_chn)i);
    }
    printf("[APP] VENC destroy done\n");

    /* VPSS stop */
    for (i = 0; i < chn_cnt; i++) { en[i] = TD_TRUE; }
    media_vpss_stop_grp(0, en, chn_cnt);
    printf("[APP] VPSS stop done\n");

    /* VI stop */
    media_vi_stop(vi_cfg);
    printf("[APP] VI stop done\n");

    /* SYS exit */
    media_sys_exit();
    printf("[APP] SYS exit done\n");
}

td_s32 media_pipeline_stream_init(FILE **fps, const td_char **file_names, td_u32 chn_cnt)
{
    td_u32 i;

    for (i = 0; i < chn_cnt; i++) {
        fps[i] = fopen(file_names[i], "wb");
        if (fps[i] == TD_NULL) {
            printf("[APP] open [%s] failed: %s\n", file_names[i], strerror(errno));
            /* 关闭已打开的文件 */
            media_pipeline_stream_deinit(fps, i);
            return TD_FAILURE;
        }
        printf("[APP] Output chn%u: %s\n", i, file_names[i]);
    }
    return TD_SUCCESS;
}

td_void media_pipeline_stream_deinit(FILE **fps, td_u32 chn_cnt)
{
    td_u32 i;

    for (i = 0; i < chn_cnt; i++) {
        if (fps[i] != TD_NULL) {
            fclose(fps[i]);
            fps[i] = TD_NULL;
            printf("[APP] chn%u file closed\n", i);
        }
    }
}

td_void *media_pipeline_stream_thread(td_void *arg)
{
    media_stream_thread_arg *targ = (media_stream_thread_arg *)arg;
    td_u32          chn;
    td_u32          frame_cnt[4] = { 0 };
    td_s32          timeout_ms;
    ot_venc_stream  stream;
    td_s32          ret;

    prctl(PR_SET_NAME, "enc_stream", 0, 0, 0);

    timeout_ms = (targ->chn_cnt == 1) ? 2000 : 0;

    printf("[MEDIA] stream thread started (%u chn%s)\n",
           targ->chn_cnt, targ->chn_cnt > 1 ? "s" : "");

    while (!(*targ->exit_flag)) {
        for (chn = 0; chn < targ->chn_cnt; chn++) {
            ret = media_venc_get_frame((ot_venc_chn)chn, timeout_ms, &stream);
            if (ret == MEDIA_ERR_VENC_TIMEOUT) {
                continue;
            }
            if (ret != TD_SUCCESS) {
                printf("[MEDIA] chn%u get_frame error: 0x%x\n", chn, ret);
                return TD_NULL;
            }

            if (targ->callback != TD_NULL) {
                targ->callback(chn, &stream, targ->user_data);
            }

            media_venc_release_frame((ot_venc_chn)chn, &stream);
            frame_cnt[chn]++;

            if ((frame_cnt[chn] & 0x3F) == 0) {
                printf("[APP] chn%u encoded %u frames\n", chn, frame_cnt[chn]);
            }
        }
        if (targ->chn_cnt > 1) {
            usleep(10000);
        }
    }

    printf("[MEDIA] stream thread stopped\n");
    return TD_NULL;
}
