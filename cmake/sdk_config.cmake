# ============================================================================
# SDK 配置 - Hi3516CV610
# 从 cfg.mak / Makefile.param 中提取的关键配置项
# ============================================================================

# ---- SDK 根路径 (本地 sdk/ 目录, 不再依赖外部 SDK) ---------------------------
set(SDK_LOCAL_DIR ${CMAKE_SOURCE_DIR}/sdk)

# ---- 预编译库 ----------------------------------------------------------------
set(SDK_OUT_LIB_DIR     ${SDK_LOCAL_DIR}/lib)

# ---- 头文件 ----------------------------------------------------------------
set(SDK_OUT_INCLUDE_DIR  ${SDK_LOCAL_DIR}/include)         # 所有 SDK 头文件 (含 exp_inc/module 子目录)
set(SAMPLE_COMMON_DIR    ${SDK_LOCAL_DIR}/common)           # sample/common 公共源码

# 废弃的旧变量 (保留兼容)
set(SDK_ROOT ${SDK_LOCAL_DIR})
set(OSAL_INCLUDE_DIR       ${SDK_OUT_INCLUDE_DIR})
set(COMMON_INCLUDE_DIR     ${SDK_OUT_INCLUDE_DIR})
set(SECUREC_INCLUDE_DIR    ${SDK_OUT_INCLUDE_DIR})
set(SAMPLE_AUDIO_ADP_DIR   ${SAMPLE_COMMON_DIR})

# ============================================================================
# 编译器标志 (源自 Makefile.cflags.param)
# ============================================================================

# --- CPU / FPU 标志 (Cortex-A7 + NEON) ----------------------------------------
set(HI3516_CPU_FLAGS
    -mcpu=cortex-a7
    -mfloat-abi=softfp
    -mfpu=neon-vfpv4
    -mthumb
)

# --- 基本编译标志 -------------------------------------------------------------
set(HI3516_COMMON_FLAGS
    -Wall
    -Wextra
    -fsigned-char
    -Wno-unused-parameter
    -Wno-missing-field-initializers
    -Wno-sign-compare
    -fno-aggressive-loop-optimizations
    -ffunction-sections
    -fdata-sections
)

# --- 优化等级: -Os (musl) / -O2 (glibc) ---------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(HI3516_OPTIMIZE_FLAG -g -O0 -fno-omit-frame-pointer)
else()
    set(HI3516_OPTIMIZE_FLAG -Os)
endif()

# --- 链接标志 -----------------------------------------------------------------
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--start-group -Wl,--end-group")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lpthread -lm -ldl -lstdc++")
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections")

# ============================================================================
# 宏定义 (源自 cfg.mak)
# ============================================================================
# ---- 传感器/板型 (来自 /topeet_test/start_uvc.sh 和 boot 日志) -----------
# 默认 Sensor: SC4336P (4M/30fps/10bit, MIPI)
# 板型: DMEB_QFN
# 可选 Sensor: GC4023, SC431HAI, SC450AI, SC500AI, OS04D10
set(SENSOR0_TYPE SC4336P_MIPI_4M_30FPS_10BIT)
set(SENSOR1_TYPE SC4336P_MIPI_4M_30FPS_10BIT)
set(BOARD_TYPE DMEB_QFN)

set(HI3516_DEFINES
    # 芯片平台
    CONFIG_HI3516CV610
    CONFIG_OT_CHIP_TYPE=0x3516c610
    # CONFIG_OT_ARCH 已由 sdk/include/autoconf.h 定义，不重复传参
    CONFIG_SMP
    CONFIG_A7

    # 操作系统 / 位数
    CONFIG_LINUX_OS
    CONFIG_KERNEL_SPACE
    USER_BIT_32
    KERNEL_BIT_32
    OT_RELEASE

    # 媒体组件
    CONFIG_OT_SYS_SUPPORT
    CONFIG_OT_VI_SUPPORT
    CONFIG_OT_VPSS_SUPPORT
    CONFIG_OT_VENC_SUPPORT
    CONFIG_OT_VGS_SUPPORT
    CONFIG_OT_REGION_SUPPORT
    CONFIG_OT_ISP_SUPPORT
    CONFIG_OT_VPP_SUPPORT
    CONFIG_OT_VCA_SUPPORT
    CONFIG_OT_CHNL_SUPPORT
    CONFIG_OT_VB_SUPPORT
    CONFIG_OT_VB_EXTPOOL_SUPPORT
    CONFIG_OT_SVP_SUPPORT

    # 视频编码
    CONFIG_OT_H264E_SUPPORT
    CONFIG_OT_H265E_SUPPORT
    CONFIG_OT_JPEGE_SUPPORT
    CONFIG_OT_MJPEGE_SUPPORT
    CONFIG_OT_SVAC3E_SUPPORT

    # 音频
    CONFIG_OT_AUDIO_SUPPORT
    CONFIG_OT_ACODEC_SUPPORT
    CONFIG_OT_AENC_SUPPORT
    CONFIG_OT_ADEC_SUPPORT
    CONFIG_OT_DOWNVQE_SUPPORT
    CONFIG_OT_TALKVQE_SUPPORT
    CONFIG_OT_RECORDVQE_SUPPORT

    # UVC
    CONFIG_OT_UVC_SUPPORT

    # ISP
    CONFIG_OT_ISP_AF_SUPPORT
    CONFIG_OT_ISP_CR_SUPPORT
    CONFIG_OT_ISP_GCAC_SUPPORT
    CONFIG_OT_ISP_CA_SUPPORT

    # 安全
    CONFIG_OT_SECURITY_SUBSYS_SUPPORT
    CONFIG_OT_CIPHER_SUPPORT
    CONFIG_OT_SECUREC_SUPPORT

    # 电源管理 / 设备统计
    CONFIG_OT_PM_SUPPORT
    CONFIG_OT_DEVSTAT_SUPPORT

    # 驱动
    CONFIG_DRV
    CONFIG_EXTDRV
    CONFIG_INTERDRV
    CONFIG_OT_USER
    CONFIG_MIPI_RX
    CONFIG_OT_ADC
    CONFIG_OT_LSADC
    CONFIG_OT_WDG
    CONFIG_OT_SYSCFG

    # FPGA 状态
    OT_XXXX
    hi3516cv610
)

# ============================================================================
# 聚合编译器标志 (供 target 使用)
# ============================================================================
set(HI3516_COMPILE_OPTIONS
    ${HI3516_CPU_FLAGS}
    ${HI3516_COMMON_FLAGS}
    ${HI3516_OPTIMIZE_FLAG}
    -DSENSOR0_TYPE=${SENSOR0_TYPE}
    -DSENSOR1_TYPE=${SENSOR1_TYPE}
    -DBOARD_TYPE=${BOARD_TYPE}
    -D${SENSOR0_TYPE}_SELECT=y
    -D${SENSOR1_TYPE}_SELECT=y
)

foreach(DEF ${HI3516_DEFINES})
    list(APPEND HI3516_COMPILE_OPTIONS -D${DEF})
endforeach()

# 头文件搜索路径
set(HI3516_INCLUDE_DIRS
    ${SDK_OUT_INCLUDE_DIR}             # sdk/include/ (主头文件)
    ${SDK_OUT_INCLUDE_DIR}/exp_inc      # sdk/include/exp_inc/ (扩展头文件)
    ${SDK_OUT_INCLUDE_DIR}/module       # sdk/include/module/ (OSAL 模块头文件)
    ${SAMPLE_COMMON_DIR}                # sdk/common/ (sample 公共头文件)
)

message(STATUS "==============================================")
message(STATUS "  Hi3516CV610 CMake 工具链配置")
message(STATUS "  本地 SDK 目录:  ${SDK_LOCAL_DIR}")
message(STATUS "  库文件目录:     ${SDK_OUT_LIB_DIR}")
message(STATUS "  链接方式:       ${LINK_TYPE}")
message(STATUS "  头文件目录:     ${SDK_OUT_INCLUDE_DIR}")
message(STATUS "  交叉编译器:     ${CMAKE_C_COMPILER}")
message(STATUS "==============================================")

# ============================================================================
# SDK libs: .a (static link) and .so (dynamic link)
# For SHARED: copy NEEDED .so files to target rootfs busybox/binary/lib/
# ============================================================================
set(LINK_TYPE STATIC CACHE STRING "SDK link type: STATIC or SHARED")
