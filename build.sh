#!/bin/bash

# 获取当前脚本所在的目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# 建议使用绝对路径避免 cd 逻辑出错
BUILD_DIR="${SCRIPT_DIR}/build"
INSTALL_PATH="${HOME}/Library/Developer"

# --- 参数处理逻辑 ---
CLEAN_MODE=false
for arg in "$@"; do
  if [ "$arg" == "-c" ] || [ "$arg" == "--clean" ]; then
    CLEAN_MODE=true
  fi
done

# 检查目录是否存在，不存在则创建
if [ ! -d "${BUILD_DIR}" ]; then
    mkdir -p "${BUILD_DIR}"
fi

# 如果开启了 Clean 模式，则清空 build 目录
if [ "$CLEAN_MODE" = true ]; then
    echo ">>>> [CLEAN] 正在清空 Build 目录: ${BUILD_DIR}..."
    rm -rf "${BUILD_DIR}"/*
else
    echo ">>>> [INCREMENTAL] 增量编译模式..."
fi

cd "${BUILD_DIR}"

# 1. 重新配置 CMake
# 增加 -Wno-dev 可以减少 CMake 自身的警告输出
cmake -G Ninja ../llvm \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_CREATE_XCODE_TOOLCHAIN=ON \
  -DLLVM_ENABLE_PROJECTS="clang;lld" \
  -DLLVM_ENABLE_RUNTIMES="compiler-rt" \
  -DLLVM_TARGETS_TO_BUILD="AArch64;ARM;X86" \
  -DAPPLE_CONFIG_SANS_IPHONEOS=ON \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PATH}"

# 2. 执行安装逻辑
# 使用 ninja install 会自动触发编译
echo ">>>> 开始编译 <<<<\n"
ninja

echo ">>>> 开始安装 <<<<\n"
ninja install-xcode-toolchain

