/**
 * @file    mp4_muxer.h
 * @brief   H264码流封装为MP4文件的模块接口
 *
 * 本模块基于 libmp4v2 库, 将海思等平台的 H264 Annex B 裸码流封装成标准 MP4 文件。
 *
 * 功能说明:
 *  1. 自动解析 Annex B 格式的 H264 码流 (start code: 0x00000001 / 0x000001)
 *  2. 自动提取 SPS/PPS 并写入 avcC box
 *  3. 支持逐帧写入 (将一帧内的多个 NAL 单元一起传入)
 *  4. 自动标记关键帧 (IDR) 用于快速索引
 *
 * 典型用法:
 *   @code
 *   int width = 1920, height = 1080, fps = 30;
 *   mp4_muxer_t* muxer = mp4_muxer_create("/tmp/output.mp4", width, height, fps);
 *
 *   // 方式一: 逐帧写入 (推荐, 最简便)
 *   mp4_muxer_write_frame(muxer, h264_frame_data, frame_len, is_keyframe);
 *
 *   // 方式二: 逐个NAL写入 (精细控制)
 *   mp4_muxer_write_nal(muxer, nal_data, nal_len, nal_type);
 *
 *   mp4_muxer_close(muxer);
 *   @endcode
 *
 * @author  auto-generated
 * @date    2024
 */

#ifndef MP4_MUXER_H
#define MP4_MUXER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =========================================================================
 * 类型定义
 * ========================================================================= */

/** MP4封装器句柄 (不透明指针) */
typedef struct mp4_muxer_s mp4_muxer_t;

/** H264 NAL单元类型 (常用) */
#define NAL_TYPE_SLICE       1    // 非IDR slice
#define NAL_TYPE_IDR         5    // IDR slice (关键帧)
#define NAL_TYPE_SEI         6    // SEI
#define NAL_TYPE_SPS         7    // 序列参数集
#define NAL_TYPE_PPS         8    // 图像参数集
#define NAL_TYPE_AUD         9    // 访问单元分隔符

/* =========================================================================
 * 核心 API
 * ========================================================================= */

/**
 * @brief 创建 MP4 封装器并开始写入
 *
 * @param filename  输出 MP4 文件路径 (如 "/tmp/output.mp4")
 * @param width     视频宽度 (像素)
 * @param height    视频高度 (像素)
 * @param fps       视频帧率 (建议: 25, 30, 60 等)
 *
 * @return 成功返回封装器句柄, 失败返回 NULL
 *
 * @note 视频 time scale 内部设为 90000 (MP4标准推荐值),
 *       每个 sample 的 duration = 90000 / fps
 */
mp4_muxer_t* mp4_muxer_create(
    const char* filename,
    uint16_t    width,
    uint16_t    height,
    uint8_t     fps);

/**
 * @brief 写入一帧 H264 数据 (推荐方式)
 *
 * @param ctx         封装器句柄
 * @param frame_data  帧数据指针 (Annex B 格式: start_code + NALs)
 * @param frame_len   帧数据长度 (字节)
 * @param is_keyframe 是否为关键帧 (IDR帧), 用于写入同步点信息
 *
 * @return 0 成功, -1 失败
 *
 * @note 此函数会自动:
 *         1. 扫描帧内的所有 NAL 单元
 *         2. 提取并缓存 SPS/PPS (首次遇到时)
 *         3. 将整个访问单元 (AU) 作为一个 sample 写入
 *       帧数据应为解码顺序 (encoder 输出顺序)
 */
int mp4_muxer_write_frame(
    mp4_muxer_t*  ctx,
    const uint8_t* frame_data,
    uint32_t       frame_len,
    bool           is_keyframe);

/**
 * @brief 写入单个 NAL 单元 (精细控制)
 *
 * @param ctx      封装器句柄
 * @param nal_data NAL数据 (不含start code, 即 NAL header + RBSP)
 * @param nal_len  NAL长度 (字节)
 * @param nal_type NAL类型 (nal_data[0] & 0x1F), 取 NAL_TYPE_xxx 宏
 *
 * @return 0 成功, -1 失败
 *
 * @note 如果送入 SPS/PPS NAL, 内部会自动缓存并通过 avcC box 写入
 *       如果送入 IDR 或非IDR slice, 将作为当前 sample 累积,
 *       在调用 mp4_muxer_flush_frame() 时统一写入。
 *       通常情况下推荐使用 mp4_muxer_write_frame() 替代此接口。
 */
int mp4_muxer_write_nal(
    mp4_muxer_t*  ctx,
    const uint8_t* nal_data,
    uint32_t       nal_len,
    uint8_t        nal_type);

/**
 * @brief 将当前累积的 NAL 单元作为一个帧写入并提交
 *
 * @param ctx         封装器句柄
 * @param is_keyframe 是否为关键帧
 *
 * @return 0 成功, -1 失败
 *
 * @note 配合 mp4_muxer_write_nal() 使用:
 *       多次调用 mp4_muxer_write_nal() 后,
 *       调用此函数将累积的数据作为一个 sample 提交。
 */
int mp4_muxer_flush_frame(mp4_muxer_t* ctx, bool is_keyframe);

/**
 * @brief 关闭封装器, 写入文件尾并释放资源
 *
 * @param ctx 封装器句柄
 *
 * @note 调用后 ctx 指针不再有效
 *       内部会写入 moov box 等元数据并关闭文件
 */
void mp4_muxer_close(mp4_muxer_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* MP4_MUXER_H */
