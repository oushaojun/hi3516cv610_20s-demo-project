/*
 * Copyright (c) 2024, osd module
 *
 * OSD 子模块公共接口:
 *   - time_osd   时间戳叠加 (自带刷新线程)
 *   - rect_osd   矩形边框叠加
 *   - bitmap_osd 位图叠加
 */
#ifndef _VIDEO_OSD_H_
#define _VIDEO_OSD_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================================================================
 *  公共类型
 * ==================================================================== */

/** ARGB1555 颜色 (16-bit): [A:1][R:5][G:5][B:5] */
typedef uint16_t osd_color_t;

/** Opaque contexts — 禁止外部直接访问内部成员 */
typedef struct time_osd_ctx   time_osd_t;
typedef struct rect_osd_ctx   rect_osd_t;
typedef struct bitmap_osd_ctx bitmap_osd_t;

/* ====================================================================
 *  颜色工具
 * ==================================================================== */

/**
 * @brief  从 RGB 888 构造 ARGB1555 (alpha=1 不透明)
 * @param  r,g,b  红绿蓝分量 0-255
 * @return ARGB1555 16-bit 颜色值
 */
osd_color_t osd_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief  从 RGBA 8888 构造 ARGB1555
 * @param  r,g,b  红绿蓝分量 0-255
 * @param  a      alpha, >127 视为不透明, <=127 视为透明
 * @return ARGB1555 16-bit 颜色值
 */
osd_color_t osd_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/* ====================================================================
 *  时间子模块 — 自带独立刷新线程, 绝对时间调度, 每秒更新
 * ==================================================================== */

/**
 * @brief  创建时间戳 OSD 并启动后台刷新线程
 * @param  venc_dev     VENC device (通常为 0)
 * @param  venc_chn     VENC channel 编号
 * @param  handle       RGN handle 0-7, 同一通道上多个实例需不同 handle
 * @param  x,y          叠加在视频帧上的像素坐标 (左上角)
 * @param  scale        字体放大倍数 1-4 (8x16 基础点阵)
 * @param  color        文字颜色 (osd_rgb 构造)
 * @param  shadow_color 阴影颜色 (osd_rgb 构造, 传 0 则不显示阴影)
 * @return 成功返回非空指针, 失败返回 NULL; 需用 time_osd_destroy 销毁
 */
time_osd_t* time_osd_create(uint8_t venc_dev, uint8_t venc_chn,
                             uint8_t handle, uint16_t x, uint16_t y,
                             uint8_t scale, osd_color_t color,
                             osd_color_t shadow_color);

/**
 * @brief  销毁时间戳 OSD (先停刷新线程, 再解绑 RGN, 最后释放内存)
 * @param  ctx  time_osd_create 返回的句柄, 可为 NULL
 */
void time_osd_destroy(time_osd_t *ctx);

/**
 * @brief  手动触发一次时间刷新 (通常无需调用, 线程每秒自动刷新)
 * @param  ctx  time_osd_t 句柄
 * @return 0 成功, -1 失败
 */
int time_osd_update(time_osd_t *ctx);

/**
 * @brief  显示时间戳 OSD
 * @return 0 成功, -1 失败
 */
int time_osd_show(time_osd_t *ctx);

/**
 * @brief  隐藏时间戳 OSD
 * @return 0 成功, -1 失败
 */
int time_osd_hide(time_osd_t *ctx);

/* ====================================================================
 *  画框子模块 — 静态矩形边框叠加, 可动态修改边框位置/颜色
 * ==================================================================== */

/**
 * @brief  创建画框 OSD (初始 canvas 为空, 需调用 rect_osd_set 绘制)
 * @param  venc_dev   VENC device
 * @param  venc_chn   VENC channel
 * @param  handle     RGN handle 0-7
 * @param  canvas_w,canvas_h  canvas 尺寸 (像素, 容纳矩形即可)
 * @param  x,y        叠加在视频帧上的像素坐标
 * @return 成功返回非空指针, 需用 rect_osd_destroy 销毁
 */
rect_osd_t* rect_osd_create(uint8_t venc_dev, uint8_t venc_chn,
                             uint8_t handle, uint16_t canvas_w, uint16_t canvas_h,
                             uint16_t x, uint16_t y);

/**
 * @brief  销毁画框 OSD
 */
void rect_osd_destroy(rect_osd_t *ctx);

/**
 * @brief  设置矩形边框并立即刷新到硬件
 * @param  ctx        rect_osd_t 句柄
 * @param  rx,ry      矩形左上角 (相对 canvas 坐标)
 * @param  rw,rh      矩形宽高 (像素)
 * @param  thickness  线宽 1-16, 向内绘制
 * @param  color      颜色
 * @return 0 成功, -1 失败
 */
int rect_osd_set(rect_osd_t *ctx, uint16_t rx, uint16_t ry,
                  uint16_t rw, uint16_t rh, uint8_t thickness, osd_color_t color);

/**
 * @brief  显示画框 OSD
 * @return 0 成功, -1 失败
 */
int rect_osd_show(rect_osd_t *ctx);

/**
 * @brief  隐藏画框 OSD
 * @return 0 成功, -1 失败
 */
int rect_osd_hide(rect_osd_t *ctx);

/* ====================================================================
 *  位图子模块 — 静态 ARGB1555 位图叠加, 可动态切换位图内容
 * ==================================================================== */

/**
 * @brief  创建位图 OSD (初始 canvas 为空, 需调用 bitmap_osd_set 绘制)
 * @param  venc_dev   VENC device
 * @param  venc_chn   VENC channel
 * @param  handle     RGN handle 0-7
 * @param  w,h        canvas 尺寸 (像素, 容纳位图即可)
 * @param  x,y        叠加在视频帧上的像素坐标
 * @return 成功返回非空指针, 需用 bitmap_osd_destroy 销毁
 */
bitmap_osd_t* bitmap_osd_create(uint8_t venc_dev, uint8_t venc_chn,
                                 uint8_t handle, uint16_t w, uint16_t h,
                                 uint16_t x, uint16_t y);

/**
 * @brief  销毁位图 OSD
 */
void bitmap_osd_destroy(bitmap_osd_t *ctx);

/**
 * @brief  设置位图数据并立即刷新到硬件
 * @param  ctx      bitmap_osd_t 句柄
 * @param  bx,by    位图左上角 (相对 canvas 坐标)
 * @param  data     ARGB1555 像素数组, 行主序, 每像素 2 字节
 * @param  bw,bh    位图宽高 (像素)
 * @return 0 成功, -1 失败
 */
int bitmap_osd_set(bitmap_osd_t *ctx, int16_t bx, int16_t by,
                    const uint16_t *data, uint16_t bw, uint16_t bh);

/**
 * @brief  显示位图 OSD
 * @return 0 成功, -1 失败
 */
int bitmap_osd_show(bitmap_osd_t *ctx);

/**
 * @brief  隐藏位图 OSD
 * @return 0 成功, -1 失败
 */
int bitmap_osd_hide(bitmap_osd_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* _VIDEO_OSD_H_ */
