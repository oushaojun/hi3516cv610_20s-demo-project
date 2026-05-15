/**
 * @file    ir_auto.c
 * @brief   Hi3516CV610 IRCUT 红外自动切换模块实现
 *
 * 职责：
 *   - GPIO 控制 IRCUT 滤光片切换 (sysfs)
 *   - ISP 参数切换 (饱和度/WB/CCM 在白天/夜晚模式间切换)
 *   - 后台线程循环调用 SDK ir_auto 算法
 *
 * 切换逻辑 (参考 sample_ir_auto):
 *   白天→夜晚: ISP先切黑白 → 等AE稳定 → GPIO切IRCUT
 *   夜晚→白天: GPIO先切IRCUT → ISP恢复彩色自动
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/prctl.h>

#include "ss_mpi_isp.h"
#include "ss_mpi_awb.h"
#include "ot_common_isp.h"

#include "ir_auto.h"

/* ====================================================================
 *  内部状态
 * ==================================================================== */

static ot_vi_pipe     g_ir_vi_pipe = 0;        /**< VI Pipe 号 */
static pthread_t      g_ir_thread  = 0;        /**< IR 自动线程 */
static volatile int   g_ir_running = 0;        /**< 线程运行标志 */
static int            g_gpio_exported = 0;     /**< GPIO 是否已导出 */

/* ====================================================================
 *  GPIO sysfs 操作 (QFN 单板, gpio7_7 + gpio1_0)
 * ==================================================================== */

/**
 * @brief   导出 GPIO
 * @param   gpio_num  全局 GPIO 编号
 */
static void gpio_export(td_u32 gpio_num)
{
    FILE *fp = fopen("/sys/class/gpio/export", "w");
    if (fp == NULL) {
        printf("[IR] gpio%u export open failed: %s\n", gpio_num, strerror(errno));
        return;
    }
    fprintf(fp, "%u", gpio_num);
    fclose(fp);
}

/**
 * @brief   设置 GPIO 方向为输出
 * @param   gpio_num  全局 GPIO 编号
 */
static void gpio_set_dir_out(td_u32 gpio_num)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/direction", gpio_num);
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        printf("[IR] gpio%u dir open failed: %s\n", gpio_num, strerror(errno));
        return;
    }
    fprintf(fp, "out");
    fclose(fp);
}

/**
 * @brief   写 GPIO 电平
 * @param   gpio_num  全局 GPIO 编号
 * @param   value     0=低电平, 1=高电平
 */
static void gpio_write(td_u32 gpio_num, td_u32 value)
{
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%u/value", gpio_num);
    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        printf("[IR] gpio%u value open failed: %s\n", gpio_num, strerror(errno));
        return;
    }
    fprintf(fp, "%u", value);
    fclose(fp);
    printf("[IR] gpio%u = %u\n", gpio_num, value);
}

/**
 * @brief   取消导出 GPIO
 * @param   gpio_num  全局 GPIO 编号
 */
static void gpio_unexport(td_u32 gpio_num)
{
    FILE *fp = fopen("/sys/class/gpio/unexport", "w");
    if (fp == NULL) {
        return;
    }
    fprintf(fp, "%u", gpio_num);
    fclose(fp);
}

/* ====================================================================
 *  ISP 参数切换
 * ==================================================================== */

/**
 * @brief   IR 模式: CCM 设为单位矩阵 (不校正颜色)
 * @param   vi_pipe  VI Pipe 号
 * @return  TD_SUCCESS / TD_FAILURE
 */
static td_s32 isp_set_ccm_identity(ot_vi_pipe vi_pipe)
{
    ot_isp_color_matrix_attr ccm_attr;
    td_s32 ret;

    ret = ss_mpi_isp_get_ccm_attr(vi_pipe, &ccm_attr);
    if (ret != TD_SUCCESS) {
        printf("[IR] get ccm_attr failed: 0x%x\n", ret);
        return ret;
    }

    ccm_attr.op_type = OT_OP_MODE_MANUAL;
    /* 3x3 单位矩阵, 定点 8.8 格式: 1.0 = 0x100 */
    ccm_attr.manual_attr.ccm[0] = 0x100;
    ccm_attr.manual_attr.ccm[1] = 0;
    ccm_attr.manual_attr.ccm[2] = 0;
    ccm_attr.manual_attr.ccm[3] = 0;
    ccm_attr.manual_attr.ccm[4] = 0x100;
    ccm_attr.manual_attr.ccm[5] = 0;
    ccm_attr.manual_attr.ccm[6] = 0;
    ccm_attr.manual_attr.ccm[7] = 0;
    ccm_attr.manual_attr.ccm[8] = 0x100;

    ret = ss_mpi_isp_set_ccm_attr(vi_pipe, &ccm_attr);
    if (ret != TD_SUCCESS) {
        printf("[IR] set ccm_attr(identity) failed: 0x%x\n", ret);
    }
    return ret;
}

/**
 * @brief   切换到红外模式 (夜晚)
 *
 * 顺序: ISP 饱和度为0 + WB 手动1.0 + CCM单位矩阵 → 等AE稳定 → GPIO切IRCUT
 *
 * @param   vi_pipe  VI Pipe 号
 * @return  TD_SUCCESS / TD_FAILURE
 */
static td_s32 isp_switch_to_ir(ot_vi_pipe vi_pipe)
{
    ot_isp_saturation_attr sat_attr;
    ot_isp_wb_attr         wb_attr;
    td_s32 ret;

    printf("[IR] --->> switching to IR mode --->>\n");

    /* 1. 饱和度 = 0 (手动, 黑白) */
    ret = ss_mpi_isp_get_saturation_attr(vi_pipe, &sat_attr);
    if (ret != TD_SUCCESS) {
        printf("[IR] get saturation_attr failed: 0x%x\n", ret);
        return ret;
    }
    sat_attr.op_type = OT_OP_MODE_MANUAL;
    sat_attr.manual_attr.saturation = 0;
    ret = ss_mpi_isp_set_saturation_attr(vi_pipe, &sat_attr);
    if (ret != TD_SUCCESS) {
        printf("[IR] set saturation_attr(0) failed: 0x%x\n", ret);
        return ret;
    }

    /* 2. WB = R=G=B=1.0 (手动) */
    ret = ss_mpi_isp_get_wb_attr(vi_pipe, &wb_attr);
    if (ret != TD_SUCCESS) {
        printf("[IR] get wb_attr failed: 0x%x\n", ret);
        return ret;
    }
    wb_attr.op_type = OT_OP_MODE_MANUAL;
    wb_attr.manual_attr.r_gain  = 0x100;
    wb_attr.manual_attr.gr_gain = 0x100;
    wb_attr.manual_attr.gb_gain = 0x100;
    wb_attr.manual_attr.b_gain  = 0x100;
    ret = ss_mpi_isp_set_wb_attr(vi_pipe, &wb_attr);
    if (ret != TD_SUCCESS) {
        printf("[IR] set wb_attr(1.0) failed: 0x%x\n", ret);
        return ret;
    }

    /* 3. CCM = 单位矩阵 */
    ret = isp_set_ccm_identity(vi_pipe);
    if (ret != TD_SUCCESS) {
        return ret;
    }

    /* 4. 等 AE 适应 */
    usleep(1000000);

    /* 5. GPIO 切 IRCUT 到红外位置 */
    /*    原始 sample 时序: 先拉高A, 再拉低B, 再全拉低 */
    gpio_write(IR_GPIO_NUM_A, 1);
    gpio_write(IR_GPIO_NUM_B, 0);
    gpio_write(IR_GPIO_NUM_A, 0);
    gpio_write(IR_GPIO_NUM_B, 0);

    printf("[IR] --->> IR mode active --->>\n");
    return TD_SUCCESS;
}

/**
 * @brief   切换到正常模式 (白天)
 *
 * 顺序: GPIO 切 IRCUT 到可见光位置 → ISP 恢复彩色自动
 *
 * @param   vi_pipe  VI Pipe 号
 * @return  TD_SUCCESS / TD_FAILURE
 */
static td_s32 isp_switch_to_normal(ot_vi_pipe vi_pipe)
{
    ot_isp_saturation_attr sat_attr;
    ot_isp_wb_attr         wb_attr;
    ot_isp_color_matrix_attr ccm_attr;
    td_s32 ret;

    printf("[IR] <<--- switching to normal mode <<---\n");

    /* 1. GPIO 先切 IRCUT 回可见光 */
    gpio_write(IR_GPIO_NUM_B, 1);
    gpio_write(IR_GPIO_NUM_A, 0);
    gpio_write(IR_GPIO_NUM_B, 0);
    gpio_write(IR_GPIO_NUM_A, 0);

    /* 2. ISP 恢复自动模式 */
    ret = ss_mpi_isp_get_saturation_attr(vi_pipe, &sat_attr);
    if (ret == TD_SUCCESS) {
        sat_attr.op_type = OT_OP_MODE_AUTO;
        ss_mpi_isp_set_saturation_attr(vi_pipe, &sat_attr);
    }

    ret = ss_mpi_isp_get_wb_attr(vi_pipe, &wb_attr);
    if (ret == TD_SUCCESS) {
        wb_attr.op_type = OT_OP_MODE_AUTO;
        ss_mpi_isp_set_wb_attr(vi_pipe, &wb_attr);
    }

    ret = ss_mpi_isp_get_ccm_attr(vi_pipe, &ccm_attr);
    if (ret == TD_SUCCESS) {
        ccm_attr.op_type = OT_OP_MODE_AUTO;
        ss_mpi_isp_set_ccm_attr(vi_pipe, &ccm_attr);
    }

    printf("[IR] <<--- normal mode active <<---\n");
    return TD_SUCCESS;
}

/* ====================================================================
 *  IR Auto 线程
 * ==================================================================== */

/**
 * @brief   IR Auto 后台线程主循环
 *
 * 每 IR_AUTO_POLL_MS ms 调用一次 ss_mpi_isp_ir_auto，
 * SDK 内部根据当前 ISO 值和颜色统计判断是否需要切换。
 *
 * @param   arg  未使用
 * @return  NULL
 */
static void *ir_auto_thread_func(void *arg)
{
    (td_void)arg;

    prctl(PR_SET_NAME, "ir_auto", 0, 0, 0);
    printf("[IR] thread started, vi_pipe=%u\n", g_ir_vi_pipe);

    /* 构造 ir_auto 属性 (SDK 输入参数) */
    ot_isp_ir_auto_attr ir_attr;
    ir_attr.enable                      = TD_TRUE;
    ir_attr.normal_to_ir_iso_threshold  = IR_NORMAL_TO_IR_ISO;
    ir_attr.ir_to_normal_iso_threshold  = IR_IR_TO_NORMAL_ISO;
    ir_attr.rg_max                      = IR_RG_MAX;
    ir_attr.rg_min                      = IR_RG_MIN;
    ir_attr.bg_max                      = IR_BG_MAX;
    ir_attr.bg_min                      = IR_BG_MIN;
    ir_attr.ir_status                   = OT_ISP_IR_STATUS_NORMAL;

    while (g_ir_running) {
        usleep(IR_AUTO_POLL_MS * 1000);

        td_s32 ret = ss_mpi_isp_ir_auto(g_ir_vi_pipe, &ir_attr);
        if (ret != TD_SUCCESS) {
            printf("[IR] ss_mpi_isp_ir_auto failed: 0x%x\n", ret);
            continue;
        }

        if (ir_attr.ir_switch == OT_ISP_IR_SWITCH_TO_IR) {
            printf("[IR] ===== DAY -> NIGHT =====\n");
            isp_switch_to_ir(g_ir_vi_pipe);
            ir_attr.ir_status = OT_ISP_IR_STATUS_IR;
        } else if (ir_attr.ir_switch == OT_ISP_IR_SWITCH_TO_NORMAL) {
            printf("[IR] ===== NIGHT -> DAY =====\n");
            isp_switch_to_normal(g_ir_vi_pipe);
            ir_attr.ir_status = OT_ISP_IR_STATUS_NORMAL;
        }
    }

    printf("[IR] thread exited\n");
    return NULL;
}

/* ====================================================================
 *  对外接口
 * ==================================================================== */

td_s32 ir_auto_init(ot_vi_pipe vi_pipe)
{
    g_ir_vi_pipe = vi_pipe;

    /* 导出 GPIO 并设为输出 */
    gpio_export(IR_GPIO_NUM_A);
    gpio_export(IR_GPIO_NUM_B);

    /* 等 sysfs 就绪 */
    usleep(50000);

    gpio_set_dir_out(IR_GPIO_NUM_A);
    gpio_set_dir_out(IR_GPIO_NUM_B);

    /* 初始状态: 白天 (正常模式), GPIO 全低 */
    gpio_write(IR_GPIO_NUM_A, 0);
    gpio_write(IR_GPIO_NUM_B, 0);

    g_gpio_exported = 1;
    g_ir_running    = 0;

    printf("[IR] init OK, vi_pipe=%u, gpio=%u/%u, "
           "iso_thr=%u/%u, rg=[%u,%u], bg=[%u,%u]\n",
           vi_pipe, IR_GPIO_NUM_A, IR_GPIO_NUM_B,
           IR_NORMAL_TO_IR_ISO, IR_IR_TO_NORMAL_ISO,
           IR_RG_MIN, IR_RG_MAX, IR_BG_MIN, IR_BG_MAX);

    return TD_SUCCESS;
}

td_s32 ir_auto_start(td_void)
{
    if (g_ir_running) {
        printf("[IR] already running\n");
        return TD_SUCCESS;
    }

    g_ir_running = 1;

    td_s32 ret = pthread_create(&g_ir_thread, NULL,
                                ir_auto_thread_func, NULL);
    if (ret != 0) {
        printf("[IR] thread create failed: %d (%s)\n", ret, strerror(ret));
        g_ir_running = 0;
        return TD_FAILURE;
    }

    printf("[IR] auto thread created\n");
    return TD_SUCCESS;
}

td_void ir_auto_stop(td_void)
{
    printf("[IR] stopping...\n");

    if (g_ir_running) {
        g_ir_running = 0;
        if (g_ir_thread) {
            pthread_join(g_ir_thread, NULL);
            g_ir_thread = 0;
        }
    }

    if (g_gpio_exported) {
        gpio_unexport(IR_GPIO_NUM_A);
        gpio_unexport(IR_GPIO_NUM_B);
        g_gpio_exported = 0;
        printf("[IR] GPIO unexported\n");
    }

    printf("[IR] stopped\n");
}
