#!/bin/bash
# ============================================================================
# Hi3516CV610 CMake 交叉编译构建脚本
# ============================================================================
set -e

# ---- 项目根目录 ---------------------------------------------------------------
PROJ_DIR=$(cd "$(dirname "$0")"; pwd)
BUILD_DIR=${PROJ_DIR}/build
OUTPUT_DIR=${PROJ_DIR}/output
TOOLCHAIN_FILE=${PROJ_DIR}/cmake/arm-v01c02-linux-musleabi.cmake

# ---- 默认构建类型 -------------------------------------------------------------
BUILD_TYPE=${1:-Release}

# ---- 支持的构建类型 -----------------------------------------------------------
case "${BUILD_TYPE}" in
    Release|Debug)
        ;;
    clean)
        echo "Cleaning build and output directories..."
        rm -rf "${BUILD_DIR}" "${OUTPUT_DIR}"
        echo "Done."
        exit 0
        ;;
    *)
        echo "Usage: $0 [Release|Debug|clean]"
        echo "  Release - 优化编译 (默认)"
        echo "  Debug   - 调试版本 (-g -O0)"
        echo "  clean   - 清空构建和输出目录"
        exit 1
        ;;
esac

echo "=============================================="
echo "  Hi3516CV610 CMake Build Script"
echo "  Build Type: ${BUILD_TYPE}"
echo "  Toolchain:  ${TOOLCHAIN_FILE}"
echo "=============================================="

# ---- 创建构建目录 -------------------------------------------------------------
mkdir -p "${BUILD_DIR}"

# ---- CMake 配置 ----------------------------------------------------------------
cmake -S "${PROJ_DIR}" \
      -B "${BUILD_DIR}" \
      -DCMAKE_TOOLCHAIN_FILE="${TOOLCHAIN_FILE}" \
      -DCMAKE_BUILD_TYPE="${BUILD_TYPE}" \
      -DCMAKE_VERBOSE_MAKEFILE=OFF

# ---- 编译 --------------------------------------------------------------------
cmake --build "${BUILD_DIR}" -- -j$(nproc)

# ---- 输出结果 ----------------------------------------------------------------
echo ""
echo "=============================================="
echo "  Build SUCCESS!"
echo "=============================================="

# ---- Collect executables to output/ --------------------------------------
mkdir -p "${OUTPUT_DIR}"
rm -f "${OUTPUT_DIR}"/*

elf_count=0
for elf in $(find "${BUILD_DIR}/src" -type f -executable ! -name "*.a" ! -name "Makefile" ! -name "*.cmake" 2>/dev/null); do
    cp "$elf" "${OUTPUT_DIR}/"
    echo "  -> output/$(basename "$elf")"
    elf_count=$((elf_count + 1))
done

echo ""
echo "  ${elf_count} executable(s) copied to ${OUTPUT_DIR}/"
ls -la "${BUILD_DIR}/src/" 2>/dev/null | grep -v "^d" || echo "(no executables found)"
