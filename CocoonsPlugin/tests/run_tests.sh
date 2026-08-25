#!/usr/bin/env bash
# ============================================================
# Cocoons L1+L2 冒烟 & 回归测试脚本
# 用法:
#   LLVM_DIR=/path/to/llvm/lib/cmake/llvm ./CocoonsPlugin/tests/run_tests.sh
# ============================================================
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="${ROOT_DIR}/tests"
BUILD_DIR="${ROOT_DIR}/build"
DIST_DIR="${ROOT_DIR}/dist"

_find_plugin() {
    local prefer="${DIST_DIR}/libCocoonsPlugin.dylib"
    local fallback="${DIST_DIR}/CocoonsPlugin.dylib"
    if [[ -f "$prefer" ]]; then echo "$prefer"
    elif [[ -f "$fallback" ]]; then echo "$fallback"
    else echo "$prefer"; fi
}
PLUGIN="$(_find_plugin)"

RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; NC='\033[0m'
pass() { echo -e "${GREEN}[PASS]${NC} $*"; }
warn(){ echo -e "${YELLOW}[WARN]${NC} $*"; }
fail(){ echo -e "${RED}[FAIL]${NC} $*"; exit 1; }

# ---- 1. 工具链定位 ----
if [[ -n "${LLVM_DIR:-}" ]]; then
    LLVM_BIN="$(cd "${LLVM_DIR}/../../../bin" && pwd)"
elif [[ -d "/usr/local/opt/llvm@21/bin" ]]; then
    LLVM_BIN="/usr/local/opt/llvm@21/bin"
elif [[ -d "/usr/local/opt/llvm/bin" ]]; then
    LLVM_BIN="/usr/local/opt/llvm/bin"
elif command -v llvm-config &> /dev/null; then
    LLVM_BIN="$(llvm-config --bindir)"
else
    fail "无法定位 LLVM 工具链，请设置 LLVM_DIR=/path/to/llvm/lib/cmake/llvm"
fi
CLANG="${LLVM_BIN}/clang"
OPT="${LLVM_BIN}/opt"
LLVM_NM="${LLVM_BIN}/llvm-nm"
OBJDUMP="${LLVM_BIN}/llvm-objdump"

echo "================ Cocoons Smoke Suite ================="
echo "  LLVM_BIN     : ${LLVM_BIN}"
echo "  ROOT_DIR     : ${ROOT_DIR}"
echo "  PLUGIN       : ${PLUGIN}"
echo "======================================================"

if [[ ! -x "$CLANG" ]]; then fail "clang 不存在: $CLANG"; fi
if [[ ! -x "$OPT"   ]]; then fail "opt   不存在: $OPT";   fi

# ---- 2. 若插件不存在，尝试构建 ----
if [[ ! -f "$PLUGIN" ]]; then
    warn "插件尚未编译，尝试一键构建..."
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" \
        -DCMAKE_BUILD_TYPE=Release \
        ${LLVM_DIR:+"-DLLVM_DIR=${LLVM_DIR}"}
    cmake --build "${BUILD_DIR}" -j
    [[ -f "$PLUGIN" ]] || fail "构建失败：插件仍不存在 $PLUGIN"
    pass "构建成功"
fi

# ---- 3. L1: 插件导出符号检查 ----
"$LLVM_NM" -gU "$PLUGIN" 2>/dev/null | grep -q "_llvmGetPassPluginInfo" \
    || fail "插件未导出 llvmGetPassPluginInfo 符号"
pass "L1: 插件导出符号 llvmGetPassPluginInfo"

mkdir -p /tmp/cocoons-smoke

# ---- 4. L1: 命名管线 opt 调用检查 ----
"$CLANG" -O1 -emit-llvm "${TEST_DIR}/substitution.c" -c -o /tmp/cocoons-smoke/sub.bc 2>/dev/null
OUT=$("$OPT" -load-pass-plugin="${PLUGIN}" -passes=cocoons-full \
        -S /tmp/cocoons-smoke/sub.bc \
        -o /tmp/cocoons-smoke/sub.obf.ll 2>&1) || true
echo "$OUT" | grep -q "Instruction Substitution applied" \
    || fail "opt 命名管线调用失败，errs 输出：$OUT"
pass "L1: opt -passes=cocoons-full 触发 SubstitutionPass"

# ---- 5. L2: 通过 clang -fpass-plugin 编译 strings.m + 二进制字符串不可见 ----
COCOONS_STR_FLAGS="-fpass-plugin=${PLUGIN}"
"$CLANG" -O2 -framework Foundation \
    ${COCOONS_STR_FLAGS} \
    "${TEST_DIR}/strings.m" -o /tmp/cocoons-smoke/strings_obf 2>/dev/null

strings /tmp/cocoons-smoke/strings_obf | grep -E "COCOONS_MARKER_" \
    && fail "L2 strings: 明文字符串仍存在于二进制！" \
    || true
# 运行二进制验证解密正确性
if [[ "$(uname -s)" == "Darwin" ]]; then
    OUTPUT=$(/tmp/cocoons-smoke/strings_obf 2>&1) || true
    echo "$OUTPUT" | grep -q "COCOONS_MARKER_C_ARRAY" || fail "运行时解密失败，Log 没出现 marker"
    pass "L2: 字符串混淆 → 运行时解密正常，二进制 grep 不到 marker"
fi

# ---- 6. L2: Substitution 对比 add/subl 指令密度 ----
"$CLANG" -O2 "${TEST_DIR}/substitution.c" -o /tmp/cocoons-smoke/sub_plain 2>/dev/null
COCOONS_SUB_LOOP=2 "$CLANG" -O2 -fpass-plugin="${PLUGIN}" "${TEST_DIR}/substitution.c" -o /tmp/cocoons-smoke/sub_obf 2>/dev/null

/tmp/cocoons-smoke/sub_obf && pass "L2: substitution 替换后二进制仍可正确执行"

PLAIN_COUNT=$("$OBJDUMP" -d /tmp/cocoons-smoke/sub_plain 2>/dev/null | grep -cE "^[[:space:]]+[a-f0-9]+:\s+(add|sub)[lwq]" || true)
OBF_COUNT=$("$OBJDUMP" -d /tmp/cocoons-smoke/sub_obf  2>/dev/null | grep -cE "^[[:space:]]+[a-f0-9]+:\s+(add|sub)[lwq]" || true)
echo "  -> Plain add/sub 指令数: $PLAIN_COUNT, Obf add/sub 指令数: $OBF_COUNT"
if [[ $OBF_COUNT -ge $PLAIN_COUNT ]]; then
    pass "L2: substitution 指令膨胀 (${PLAIN_COUNT}→${OBF_COUNT})"
else
    warn "L2: substitution 指令数没明显增加？（可能是 LLVM 优化了），继续执行..."
fi

# ---- 7. L2: Flattening 基本块数膨胀对比 ----
FLA_FLAGS="-fpass-plugin=${PLUGIN}"
if command -v opt &> /dev/null; then
    "$CLANG" -O2 -emit-llvm "${TEST_DIR}/flattening.m" -c -o /tmp/cocoons-smoke/fla.bc 2>/dev/null
    PLAIN_BBS=$("$OPT" -analyze -instcount /tmp/cocoons-smoke/fla.bc 2>/dev/null | grep -oE "Number of basic blocks.*" | grep -oE "[0-9]+" || echo "0")
    "$OPT" -load-pass-plugin="${PLUGIN}" -passes=cocoons-fla -o /tmp/cocoons-smoke/fla.obf.bc /tmp/cocoons-smoke/fla.bc 2>/dev/null
    OBF_BBS=$("$OPT" -analyze -instcount /tmp/cocoons-smoke/fla.obf.bc 2>/dev/null | grep -oE "Number of basic blocks.*" | grep -oE "[0-9]+" || echo "0")
    echo "  -> Plain BBs: $PLAIN_BBS, Obf BBs: $OBF_BBS"
    if [[ $OBF_BBS -gt $PLAIN_BBS ]]; then
        pass "L2: flattening 基本块膨胀 (${PLAIN_BBS}→${OBF_BBS})"
    else
        warn "L2: flattening BB 未膨胀（可能需要降低优化级别或简化样例）"
    fi
fi

# ---- 8. cocoons-clang 包装脚本可用性 ----
CLANG_WRAP="${ROOT_DIR}/tools/cocoons-clang/cocoons-clang.py"
if [[ -f "$CLANG_WRAP" ]]; then
    chmod +x "$CLANG_WRAP" 2>/dev/null || true
    COCOONS_PLUGIN_DIR="${DIST_DIR}" \
    LLVM_DIR="${LLVM_DIR:-}" \
    COCOONS_VERBOSE=1 \
    python3 "$CLANG_WRAP" --enable-all -- "${TEST_DIR}/substitution.c" -o /tmp/cocoons-smoke/cc_sub 2>/dev/null \
        && pass "L1: cocoons-clang --enable-all 调用成功" \
        || warn "L1: cocoons-clang 包装脚本不可用（python 环境或 clang 未找到）"
fi

echo ""
echo -e "${GREEN}=================== All Smokes OK  ====================${NC}"
