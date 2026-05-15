/**
 * @file    ir_auto.h
 * @brief   Hi3516CV610 IRCUT 红外自动切换模块
 *
 * 基于 sample_ir_auto 重构，GPIO 控制 IRCUT，
 * ISP SDK 内部 ir_auto 算法判断日夜切换。
 *
 * 测试通过参数:
 *   ISO 阈值: 16000(normal→IR) / 400(IR→normal)
 *   RG 范围: [106, 156]  BG 范围: [107, 158]
 *   GPIO: QFN板子 gpio7_7 + gpio1_0
 */

#ifndef CAMERA_IR_H
#define CAMERA_IR_H

#include "ot_common_isp.h"
#include "ot_common_vi.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif /* __cplusplus */

/* ====================================================================
 *  硬编码参数 (来源于实测标定)
 * ==================================================================== */

#define IR_NORMAL_TO_IR_ISO  16000  /**< 亮→暗 ISO 阈值 */
#define IR_IR_TO_NORMAL_ISO  400    /**< 暗→亮 ISO 阈值 */
#define IR_RG_MAX            156    /**< IR 场景 R/G 最大值 */
#define IR_RG_MIN            106    /**< IR 场景 R/G 最小值 */
#define IR_BG_MAX            158    /**< IR 场景 B/G 最大值 */
#define IR_BG_MIN            107    /**< IR 场景 B/G 最小值 */
#define IR_DEFAULT_STATUS    0      /**< 初始状态 0=正常(白天) 1=IR(夜晚) */

/** IR 自动线程轮询间隔 (ms) */
#define IR_AUTO_POLL_MS      160

/* ====================================================================
 *  GPIO 管脚 (QFN 单板)
 * ==================================================================== */

/** GPIO 编号: gpio7_7 = 7*8+7 = 63 (IRCUT 控制脚1) */
#define IR_GPIO_NUM_A        63
/** GPIO 编号: gpio1_0 = 1*8+0 = 8  (IRCUT 控制脚2) */
#define IR_GPIO_NUM_B        8

/* ====================================================================
 *  对外接口
 * ==================================================================== */

/**
 * @brief   初始化 IR Auto 模块
 *
 * 导出 GPIO、设置方向、启动 ISP ir_auto 属性。
 * 需在 VI 启动之后、编码循环之前调用。
 *
 * @param   vi_pipe   VI Pipe 号 (通常为 0)
 * @return  TD_SUCCESS / TD_FAILURE
 */
td_s32 ir_auto_init(ot_vi_pipe vi_pipe);

/**
 * @brief   启动 IR Auto 后台线程
 *
 * 创建独立线程，周期性调用 ss_mpi_isp_ir_auto 判断日夜切换，
 * 根据返回值触发 IRCUT GPIO 和 ISP 参数切换。
 *
 * @return  TD_SUCCESS / TD_FAILURE
 */
td_s32 ir_auto_start(td_void);

/**
 * @brief   停止 IR Auto 后台线程并清理资源
 *
 * 发送退出信号 → join 线程 → unexport GPIO。
 */
td_void ir_auto_stop(td_void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif /* IR_AUTO_H */
