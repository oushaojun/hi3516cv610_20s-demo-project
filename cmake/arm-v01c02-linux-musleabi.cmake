# ============================================================================
# CMake 工具链文件 - Hi3516CV610 (ARM Cortex-A7, musl libc)
# 交叉编译器: arm-v01c02-linux-musleabi-
# ============================================================================

# 目标系统
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 交叉编译器前缀
set(CROSS_COMPILER_PREFIX arm-v01c02-linux-musleabi-)

# 交叉编译器安装路径 (根据实际环境调整)
set(CROSS_TOOLCHAIN_PATH /opt/linux/x86-arm/arm-v01c02-linux-musleabi-gcc/bin)

# 指定交叉编译器
set(CMAKE_C_COMPILER    ${CROSS_TOOLCHAIN_PATH}/${CROSS_COMPILER_PREFIX}gcc)
set(CMAKE_CXX_COMPILER  ${CROSS_TOOLCHAIN_PATH}/${CROSS_COMPILER_PREFIX}g++)
set(CMAKE_AR             ${CROSS_TOOLCHAIN_PATH}/${CROSS_COMPILER_PREFIX}ar CACHE FILEPATH "Archiver")
set(CMAKE_STRIP          ${CROSS_TOOLCHAIN_PATH}/${CROSS_COMPILER_PREFIX}strip CACHE FILEPATH "Strip")
set(CMAKE_OBJCOPY        ${CROSS_TOOLCHAIN_PATH}/${CROSS_COMPILER_PREFIX}objcopy CACHE FILEPATH "Objcopy")
set(CMAKE_RANLIB         ${CROSS_TOOLCHAIN_PATH}/${CROSS_COMPILER_PREFIX}ranlib CACHE FILEPATH "Ranlib")
set(CMAKE_NM             ${CROSS_TOOLCHAIN_PATH}/${CROSS_COMPILER_PREFIX}nm CACHE FILEPATH "nm")

# 允许 find 在交叉编译时查找目标平台的库和程序
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
