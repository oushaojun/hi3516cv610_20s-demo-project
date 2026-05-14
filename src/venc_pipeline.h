/**
 * @file    venc_pipeline.h
 * @brief   Hi3516CV610 媒体模块接口 (media_module)
 *
 * 提供 VI / VPSS / VENC 的细粒度初始化与控制接口。
 * 应用层自行编排调用顺序、控制取流循环。
 *
 * ## 典型单路编码流程
 *
 *     // 1. 系统 + VB
 *     media_sys_init();
 *     media_vb_init(&vb_cfg, supplement);
 *
 *     // 2. VI
 *     sample_vi_cfg vi_cfg;
 *     sample_comm_vi_get_default_vi_cfg(SENSOR0_TYPE, &vi_cfg);
 *     media_vi_start(&vi_cfg);
 *
 *     // 3. VPSS
 *     media_vpss_start_grp(0, &grp_attr);
 *     media_vpss_set_chn(0, 0, &chn_attr);
 *     media_vpss_enable_chn(0, 0);
 *
 *     // 4. VENC
 *     media_venc_create(0, &venc_attr);
 *
 *     // 5. 绑定
 *     media_mpi_bind_vi_vpss(0, 0, 0, 0);
 *     media_mpi_bind_vpss_venc(0, 0, 0);
 *
 *     // 6. 取流循环
 *     while (!exit_flag) {
 *         ot_venc_stream stream;
 *         if (media_venc_get_frame(0, 2000, &stream) == TD_SUCCESS) {
 *             fwrite_to_file(&stream);
 *             media_venc_release_frame(0, &stream);
 *         }
 *     }
 *
 *     // 7. 逆序清理
 *     media_mpi_unbind_vpss_venc(0, 0, 0);
 *     media_venc_stop(0); media_venc_destroy(0);
 *     media_vpss_stop_grp(0, chn_en, 1);
 *     media_vi_stop(&vi_cfg);
 *     media_sys_exit();
 *
 * ## 多路编码 (同 sensor)
 *
 * 同一 VI pipe 可绑定多个 VPSS 通道，每个 VPSS 通道绑定不同 VENC 通道。
 * 应用层在取流循环中轮询各个 VENC 通道 (timeout=0 非阻塞)。
 */

#ifndef VENC_PIPELINE_H
#define VENC_PIPELINE_H

#include "sample_comm.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 模块自定义错误码 ---- */
#define MEDIA_ERR_VENC_TIMEOUT  (-2)   /**< VENC 取流超时无数据 */

/* ====================================================================
 *  配置结构体
 * ==================================================================== */

/** VENC 通道属性 */
typedef struct {
    ot_payload_type     type;           /**< 编码类型 OT_PT_H265 / OT_PT_H264 / OT_PT_MJPEG */
    ot_size             size;           /**< 编码分辨率 {width, height} */
    td_u32              frame_rate;     /**< 帧率 */
    td_u32              gop;            /**< GOP 长度 */
    sample_rc           rc_mode;        /**< 码控模式 SAMPLE_RC_CBR / SAMPLE_RC_VBR / ... */
    td_u32              bitrate;        /**< 目标码率 (kbps) */
    td_u32              profile;        /**< 编码 profile，0 为默认 */
    ot_venc_gop_mode    gop_mode;       /**< GOP 模式 OT_VENC_GOP_MODE_NORMAL_P / SMART_P / ... */
} media_venc_chn_attr;

/** VPSS 通道属性 */
typedef struct {
    td_u32              width;          /**< 输出宽度 */
    td_u32              height;         /**< 输出高度 */
    ot_pixel_format     pixel_format;   /**< 像素格式，通常 YVU_SEMIPLANAR_420 */
    ot_compress_mode    compress_mode;  /**< 压缩模式，chn0 建议 SEG_COMPACT */
    td_u32              depth;          /**< 队列深度，0 = 默认 */
    td_s32              src_frame_rate; /**< 源帧率 (传感器实际输出), -1=auto */
    td_s32              dst_frame_rate; /**< 目标帧率 (与 VENC FPS 关联), -1=auto */
} media_vpss_chn_attr;

/** VPSS 组属性 */
typedef struct {
    td_u32              max_width;      /**< 组最大宽度，取所有通道最大值 */
    td_u32              max_height;     /**< 组最大高度，取所有通道最大值 */
    ot_pixel_format     pixel_format;   /**< 像素格式 */
    td_s32              src_frame_rate; /**< 源帧率，-1 = auto */
    td_s32              dst_frame_rate; /**< 目标帧率，-1 = auto */
} media_vpss_grp_attr;

/** VB 池配置 (单池) */
typedef struct {
    td_u32              blk_size;       /**< 块大小 (bytes)，用 ot_common_get_pic_buf_cfg 计算 */
    td_u32              blk_cnt;        /**< 块数量 */
} media_vb_pool_cfg;

/**
 * @brief 应用层 pipeline 总配置 (仅 main.c 使用，不传给模块函数)
 *
 * 用于组织 VI/VPSS/VENC 的各项参数，方便应用层编排。
 */
typedef struct {
    sample_sns_type     sns_type;       /**< Sensor 类型，如 SC4336P_MIPI_4M_30FPS_10BIT */
    ot_size             vi_size;        /**< VI 输入分辨率，通常 = 最大编码分辨率 */
    td_u32              chn_cnt;        /**< VENC 通道数 */
    media_venc_chn_attr venc[4];        /**< 各 VENC 通道属性 */
    media_vpss_grp_attr vpss_grp;       /**< VPSS 组属性 */
    media_vpss_chn_attr vpss_chn[4];    /**< 各 VPSS 通道属性 */
    td_char             file_prefix[4][64]; /**< 各通道输出文件名前缀，实际文件为 <prefix>.h265 */
} venc_pipeline_cfg;

/* ====================================================================
 *  System / VB
 * ==================================================================== */

/**
 * @brief  初始化 MPP 系统 (ss_mpi_sys_init)
 * @note   调用前须确保所有 MPP 模块已退出
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_sys_init(td_void);

/**
 * @brief  退出 MPP 系统 (ss_mpi_sys_exit + ss_mpi_vb_exit)
 */
td_void media_sys_exit(td_void);

/**
 * @brief  配置并初始化 VB (视频缓冲) 池
 *
 * 内部依次调用: ss_mpi_vb_set_cfg → ss_mpi_vb_set_supplement_cfg → ss_mpi_vb_init
 *
 * @param vb_cfg     VB 池配置指针，常见 pool_cnt=128，
 *                   common_pool[].blk_size 用 ot_common_get_pic_buf_cfg 计算
 * @param supplement 补充配置掩码，如 OT_VB_SUPPLEMENT_JPEG_MASK | OT_VB_SUPPLEMENT_BNR_MOT_MASK
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_vb_init(const ot_vb_cfg *vb_cfg, td_u32 supplement);

/* ====================================================================
 *  VI (Video Input)
 * ==================================================================== */

/**
 * @brief  启动 VI 设备 + ISP
 *
 * 封装 sample_comm_vi_start_vi，内部完成:
 * MIPI Rx → VI Dev → Dev bind Pipe → 创建 Pipe → 启动 ISP
 *
 * @param vi_cfg  VI 配置 (由 sample_comm_vi_get_default_vi_cfg / sample_comm_vi_init_vi_cfg 填充)
 *                vi_cfg->bind_pipe.pipe_id[] 决定使用的 VI pipe 编号
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_vi_start(const sample_vi_cfg *vi_cfg);

/**
 * @brief  停止 VI 设备 + ISP
 * @param vi_cfg  启动时使用的同一份 VI 配置
 */
td_void media_vi_stop(const sample_vi_cfg *vi_cfg);

/* ====================================================================
 *  VPSS (Video Processing Sub-System)
 * ==================================================================== */

/**
 * @brief  创建并启动 VPSS 组
 *
 * 内部调用: ss_mpi_vpss_create_grp → ss_mpi_vpss_start_grp
 *
 * @param grp   VPSS 组号 (0 ~ OT_VPSS_MAX_GRP_NUM-1)
 * @param attr  组属性 (最大宽高、像素格式)
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_vpss_start_grp(ot_vpss_grp grp, const media_vpss_grp_attr *attr);

/**
 * @brief  设置 VPSS 通道属性 (在 enable 之前调用)
 *
 * @param grp   VPSS 组号
 * @param chn   VPSS 通道号 (0 ~ OT_VPSS_MAX_PHYS_CHN_NUM-1)
 * @param attr  通道属性
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_vpss_set_chn(ot_vpss_grp grp, ot_vpss_chn chn, const media_vpss_chn_attr *attr);

/**
 * @brief  使能 VPSS 通道
 *
 * @param grp   VPSS 组号
 * @param chn   VPSS 通道号
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_vpss_enable_chn(ot_vpss_grp grp, ot_vpss_chn chn);

/**
 * @brief  停止 VPSS 组 (含通道去使能 + 销毁组)
 *
 * @param grp      VPSS 组号
 * @param chn_en   通道使能标记数组，chn_en[i]=TD_TRUE 表示需要去使能 chn i
 * @param chn_cnt  通道数 (= chn_en 数组长度)
 */
td_void media_vpss_stop_grp(ot_vpss_grp grp, const td_bool *chn_en, td_u32 chn_cnt);

/* ====================================================================
 *  VENC (Video Encoder)
 * ==================================================================== */

/**
 * @brief  创建并启动 VENC 通道
 *
 * 内部调用: ss_mpi_venc_create_chn → ss_mpi_venc_start_chn
 *
 * @param chn   VENC 通道号 (0 ~ OT_VENC_MAX_CHN_NUM-1)
 * @param attr  通道属性 (编码类型、分辨率、码率、GOP 等)
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_venc_create(ot_venc_chn chn, const media_venc_chn_attr *attr);

/**
 * @brief  停止 VENC 通道 (不销毁)
 * @param chn   VENC 通道号
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_venc_stop(ot_venc_chn chn);

/**
 * @brief  销毁 VENC 通道 (先 stop 再 destroy)
 * @param chn   VENC 通道号
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_venc_destroy(ot_venc_chn chn);

/**
 * @brief  从 VENC 通道获取一帧编码数据 (阻塞/超时)
 *
 * 内部流程:
 *   1. select() 等待 VENC fd 可读 (超时由 timeout_ms 控制)
 *   2. ss_mpi_venc_query_status() 获取 pack 数量
 *   3. malloc pack 数组并调用 ss_mpi_venc_get_stream()
 *   4. 返回包含 pack 数据的 stream 结构体
 *
 * 调用者拿到 stream 后自行处理数据 (写文件 / 发网络 / ...)，
 * 处理完后必须调用 media_venc_release_frame() 释放。
 *
 * @param chn        VENC 通道号
 * @param timeout_ms 超时时间 (ms)
 *                   - >0: 等待指定毫秒，超时返回 MEDIA_ERR_VENC_TIMEOUT
 *                   - =0: 立即返回 (非阻塞)，无数据则返回 MEDIA_ERR_VENC_TIMEOUT
 *                   - <0: 无限等待直到有数据
 * @param stream     [out] 输出码流，内部会为 stream->pack 分配内存
 * @retval TD_SUCCESS             成功获取一帧
 * @retval MEDIA_ERR_VENC_TIMEOUT 超时无数据 (timeout_ms >= 0 时可能)
 * @retval 其他                   错误码
 */
td_s32 media_venc_get_frame(ot_venc_chn chn, td_s32 timeout_ms, ot_venc_stream *stream);

/**
 * @brief  释放通过 media_venc_get_frame 获取的码流帧
 *
 * 内部依次: ss_mpi_venc_release_stream → free(stream->pack)
 *
 * @param chn     VENC 通道号
 * @param stream  要释放的码流 (pack 内存会被 free)
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_venc_release_frame(ot_venc_chn chn, ot_venc_stream *stream);

/* ====================================================================
 *  MPP 绑定 / 解绑
 * ==================================================================== */

/**
 * @brief  绑定 VI pipe 到 VPSS group
 *
 * @param vi_pipe   VI pipe 号
 * @param vi_chn    VI 通道号
 * @param vpss_grp  VPSS 组号
 * @param vpss_chn  VPSS 通道号
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_mpi_bind_vi_vpss(ot_vi_pipe vi_pipe, ot_vi_chn vi_chn,
                               ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn);

/**
 * @brief  解绑 VI pipe 与 VPSS group
 *
 * @param vi_pipe   VI pipe 号
 * @param vi_chn    VI 通道号
 * @param vpss_grp  VPSS 组号
 * @param vpss_chn  VPSS 通道号
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_mpi_unbind_vi_vpss(ot_vi_pipe vi_pipe, ot_vi_chn vi_chn,
                                 ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn);

/**
 * @brief  绑定 VPSS channel 到 VENC channel
 *
 * @param vpss_grp  VPSS 组号
 * @param vpss_chn  VPSS 通道号
 * @param venc_chn  VENC 通道号
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_mpi_bind_vpss_venc(ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn, ot_venc_chn venc_chn);

/**
 * @brief  解绑 VPSS channel 与 VENC channel
 *
 * @param vpss_grp  VPSS 组号
 * @param vpss_chn  VPSS 通道号
 * @param venc_chn  VENC 通道号
 * @retval TD_SUCCESS 成功
 * @retval 其他       错误码
 */
td_s32 media_mpi_unbind_vpss_venc(ot_vpss_grp vpss_grp, ot_vpss_chn vpss_chn, ot_venc_chn venc_chn);

#ifdef __cplusplus
}
#endif

#endif /* VENC_PIPELINE_H */
