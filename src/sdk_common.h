/**
 * @file    sdk_common.h
 * @brief   Hi3516CV610 SDK 公共头文件包装
 *
 * 包含此文件即可获得所有常用 MPP 接口的头文件声明。
 * 适用于快速开始开发，不需要逐个 include 每个模块头文件。
 *
 * 使用方式:
 *   #include "sdk_common.h"
 */

#ifndef SDK_COMMON_H
#define SDK_COMMON_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 基础类型 / 错误码 --------------------------------------------------- */
#include "ot_type.h"
#include "ot_defines.h"
#include "ot_debug.h"
#include "ot_common.h"
#include "ot_errno.h"
#include "ot_math.h"
#include "securec.h"

/* ---- 系统 / 内存 / VB ---------------------------------------------------- */
#include "ot_common_sys.h"
#include "ot_common_sys_bind.h"
#include "ot_common_sys_mem.h"
#include "ot_common_vb.h"
#include "ss_mpi_sys.h"
#include "ss_mpi_sys_bind.h"
#include "ss_mpi_sys_mem.h"
#include "ss_mpi_vb.h"

/* ---- 视频输入 (VI) ------------------------------------------------------- */
#include "ot_common_vi.h"
#include "ot_common_video.h"
#include "ss_mpi_vi.h"

/* ---- 视频处理子系统 (VPSS) ----------------------------------------------- */
#include "ot_common_vpss.h"
#include "ss_mpi_vpss.h"

/* ---- 视频编码 (VENC) ----------------------------------------------------- */
#include "ot_common_venc.h"
#include "ot_common_rc.h"
#include "ss_mpi_venc.h"

/* ---- 视频图形子系统 (VGS) ------------------------------------------------ */
#include "ot_common_vgs.h"
#include "ss_mpi_vgs.h"

/* ---- 区域管理 (RGN/OSD) ------------------------------------------------- */
#include "ot_common_region.h"
#include "ss_mpi_region.h"

/* ---- ISP ---------------------------------------------------------------- */
#include "ot_common_isp.h"
#include "ot_common_3a.h"
#include "ot_common_sns.h"
#include "ss_mpi_isp.h"
#include "ot_mpi_isp.h"

/* ---- 音频 ---------------------------------------------------------------- */
#include "ot_common_aio.h"
#include "ot_common_adec.h"
#include "ot_common_aenc.h"
#include "ss_mpi_audio.h"

/* ---- SVP (IVE / NPU) ---------------------------------------------------- */
#include "ot_common_svp.h"
#include "ot_common_ive.h"
#include "ss_mpi_ive.h"

/* ---- VPP ---------------------------------------------------------------- */
#include "ot_common_vpp.h"

/* ---- UVC ---------------------------------------------------------------- */
#include "ot_common_uvc.h"

/* ---- 安全 ---------------------------------------------------------------- */
#include "ot_mpi_cipher.h"
#include "ss_mpi_cipher.h"

/* ---- Buffer ------------------------------------------------------------- */
#include "ot_buffer.h"

/* ---- 设备统计 ------------------------------------------------------------ */
#include "ss_mpi_devstat.h"

#ifdef __cplusplus
}
#endif

#endif /* SDK_COMMON_H */
