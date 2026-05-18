/**
 * @file    sys_discovery_serv.h
 * @brief   Hi3516CV610 设备发现 — UDP 组播服务端
 *
 * 监听 239.0.0.1:8888 组播查询。
 * 收到 {"query":"device_discovery"} 后回复设备信息 (JSON)。
 * 以独立线程运行，不影响主编码 pipeline。
 *
 * 用法:
 *   discovery_serv_init(chn_cfg, chn_cnt);
 *   discovery_serv_deinit();
 */

#ifndef SYS_DISCOVERY_SERV_H
#define SYS_DISCOVERY_SERV_H

#include "ot_type.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ====================================================================
 *  通道配置 (外部传入，和 main.c 中 g_chn_cfg 结构一致)
 * ==================================================================== */
typedef struct {
    td_u32  type;           /**< OT_PT_H264 / OT_PT_H265 */
    td_u32  width;
    td_u32  height;
    td_u32  fps;
    td_u32  bitrate;        /**< Kbps */
    td_u32  rc_mode;        /**< SAMPLE_RC_CBR / AVBR 等 */
} discovery_chn_cfg_t;

/* ====================================================================
 *  接口
 * ==================================================================== */

/**
 * @brief  启动设备发现服务 (后台线程)
 * @param  chn_cfg  各通道编码配置数组
 * @param  chn_cnt  通道数量 (1~3)
 * @return TD_SUCCESS / TD_FAILURE
 */
td_s32 discovery_serv_init(const discovery_chn_cfg_t *chn_cfg, td_u32 chn_cnt);

/**
 * @brief  停止设备发现服务，释放资源
 */
td_void discovery_serv_deinit(td_void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* SYS_DISCOVERY_SERV_H */
