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
    chn_attr.frame_rate.src_frame_rate = -1;
    chn_attr.frame_rate.dst_frame_rate = -1;
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
    sample_comm_venc_chn_param param    = { 0 };
    ot_venc_gop_attr           gop_attr = { 0 };

    if (attr == TD_NULL) {
        printf("[MEDIA] venc chn attr is null!\n");
        return TD_FAILURE;
    }

    gop_attr.gop_mode = attr->gop_mode;

    param.type                 = attr->type;
    param.size                 = sample_comm_sys_get_pic_enum(&attr->size);
    param.frame_rate           = attr->frame_rate;
    param.gop                  = attr->gop;
    param.stats_time           = 1;
    param.gop_attr             = gop_attr;
    param.rc_mode              = attr->rc_mode;
    param.profile              = attr->profile;
    param.is_rcn_ref_share_buf = TD_TRUE;
    /* 注意: 码率(bitrate)由 sample_comm 层按分辨率自动计算。
     * 如需精细码率控制, 需绕过 sample_comm_venc_start,
     * 直接调用 ss_mpi_venc_create_chn 并手动填充 ot_venc_rc_attr。 */

    return sample_comm_venc_start(chn, &param);
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
