# ============================================================================
# src/ - 应用程序源码
# ============================================================================

# ---- 公共 Library: 从本地 sdk/common/ 编译 ----------------------------------
# 这些是 SDK 提供的 sample 公共源码，所有 sample app 都依赖它们

set(SDK_COMMON_DIR ${SDK_LOCAL_DIR}/common)

set(COMMON_SRCS
    ${SDK_COMMON_DIR}/sample_comm_sys.c
    ${SDK_COMMON_DIR}/sample_comm_vi.c
    ${SDK_COMMON_DIR}/sample_comm_vpss.c
    ${SDK_COMMON_DIR}/sample_comm_venc.c
    ${SDK_COMMON_DIR}/sample_comm_isp.c
    ${SDK_COMMON_DIR}/sample_comm_region.c
    ${SDK_COMMON_DIR}/sample_comm_osd_drawline.c
    ${SDK_COMMON_DIR}/sample_comm_audio.c
    ${SDK_COMMON_DIR}/sample_ipc.c
    ${SDK_COMMON_DIR}/loadbmp.c
    ${SDK_COMMON_DIR}/ot_confaccess.c
)

add_library(hi3516_common STATIC ${COMMON_SRCS})

target_include_directories(hi3516_common PRIVATE
    ${SDK_COMMON_DIR}
    ${SAMPLE_AUDIO_ADP_DIR}
)

target_compile_options(hi3516_common PRIVATE ${HI3516_COMPILE_OPTIONS})

# ---- 自定义应用源码目录 -------------------------------------------------------
set(APP_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# ---- hi3516_project —— 编码测试入口 (使用 venc_pipeline 模块) ----------
add_executable(hi3516_project
    ${APP_SRC_DIR}/main.c
    ${APP_SRC_DIR}/video_pipeline.c
    ${APP_SRC_DIR}/camera_ir.c
    ${APP_SRC_DIR}/sys_dbg.c
    ${APP_SRC_DIR}/sys_thread.c
    ${APP_SRC_DIR}/sys_notify.c
    ${APP_SRC_DIR}/sys_uevent.c
    ${APP_SRC_DIR}/sys_sd.c
    ${APP_SRC_DIR}/sys_discovery_serv.c
    ${APP_SRC_DIR}/video_dispatcher.c
    ${APP_SRC_DIR}/mp4_muxer.c
)

# SDK 库列表 (供 STATIC/SHARED 两种链接模式共用)
set(SDK_LIBS
    # === MPP 核心库 (VI/VPSS/VENC/VGS/REGION 函数均在 ss_mpi 中) ===
    ss_mpi                        # 主 MPI 接口 (含 VI/VPSS/VENC/VGS/REGION)
    ss_mpi_sysbind               # 系统绑定
    ss_mpi_sysmem                # 系统内存
    ot_mpi_sysmem

    # === ISP / 算法库 ===
    ss_mpi_isp
    ot_mpi_isp
    ss_mpi_ae
    ss_mpi_awb
    ss_mpi_smartae

    # === 传感器驱动 ===
    sns_sc4336p
    sns_gc4023
    sns_sc431hai
    sns_sc450ai
    sns_sc500ai
    sns_os04d10

    # === ISP 后处理算法 ===
    dehaze
    drc
    ldci
    bnr
    acs
    calcflicker
    ir_auto
    extend_stats

    # === 安全 (CIPHER/OTP) ===
    ot_mpi_cipher
    ss_mpi_cipher
    ss_mpi_otp
    ot_mpi_otp
    securec
    # mbedtls_harden_adapt   # only needed for hw-crypto apps

    # === SVP (IVE / NPU) ===
    ss_mpi_ive
    svp_acl
    ss_mpi_km
    ot_mpi_km
    protobuf-c
    ss_bcd
    ss_ivs_md

    # === 音频 ===
    ss_mpi_audio
    ss_mpi_audio_adp
    voice_engine
    upvqe
    dnvqe
    opus
    aac_dec
    aac_enc
    aac_sbr_dec
    aac_sbr_enc
    aac_comm
    mp3_dec
    vqe_res
    vqe_record
    vqe_hpf
    vqe_anr
    vqe_aec
    vqe_agc
    vqe_eq
    vqe_talkv2

    # === UVC ===
    ss_mpi_uvc

    # === 设备统计 ===
    ss_mpi_devstat
)

# ---- hi3516_project 链接和编译选项 ---------------------------------------------
target_link_libraries(hi3516_project hi3516_common)

if(LINK_TYPE STREQUAL "STATIC")
    foreach(lib ${SDK_LIBS})
        find_library(LIB_${lib}_STATIC_2
            NAMES lib${lib}.a
            PATHS ${SDK_OUT_LIB_DIR}
            NO_DEFAULT_PATH
        )
        if(LIB_${lib}_STATIC_2)
            target_link_libraries(hi3516_project ${LIB_${lib}_STATIC_2})
        endif()
    endforeach()
else()
    target_link_options(hi3516_project PRIVATE "LINKER:--allow-shlib-undefined")
    target_link_libraries(hi3516_project ${SDK_LIBS})
endif()

target_link_libraries(hi3516_project
    pthread
    m
    dl
    stdc++
)

# mp4v2: 静态/动态链接切换
if(LINK_TYPE STREQUAL "STATIC")
    target_link_libraries(hi3516_project ${CMAKE_SOURCE_DIR}/3rd_party/mp4v2/lib/libmp4v2.a)
else()
    target_link_libraries(hi3516_project ${CMAKE_SOURCE_DIR}/3rd_party/mp4v2/lib/libmp4v2.so)
endif()

target_compile_options(hi3516_project PRIVATE ${HI3516_COMPILE_OPTIONS})
target_include_directories(hi3516_project PRIVATE
    ${SDK_COMMON_DIR}
    ${SAMPLE_AUDIO_ADP_DIR}
    ${CMAKE_SOURCE_DIR}/3rd_party/mp4v2/include
)
target_compile_definitions(hi3516_project PRIVATE
    SENSOR0_TYPE=${SENSOR0_TYPE}
)

# ---- sys_discovery_cli —— 设备发现客户端工具 (host gcc 编译) -------------------
set(CLI_SRC  ${CMAKE_SOURCE_DIR}/tools/sys_discovery_cli.c)
set(CLI_OUT  ${CMAKE_BINARY_DIR}/tools/sys_discovery_cli)

add_custom_command(
    OUTPUT ${CLI_OUT}
    COMMAND mkdir -p ${CMAKE_BINARY_DIR}/tools
    COMMAND gcc -Wall -O2 -o ${CLI_OUT} ${CLI_SRC} -lpthread
    DEPENDS ${CLI_SRC}
    COMMENT "Building sys_discovery_cli (host gcc)..."
)
add_custom_target(sys_discovery_cli ALL DEPENDS ${CLI_OUT})
