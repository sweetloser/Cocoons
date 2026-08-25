#!/usr/bin/env bash
#
# Cocoons Demo 构建脚本（Before / After 对比，一目了然）
#   Before : 普通 clang -O2 编译（关闭 Cocoons 插件）
#   After  : 同 clang -O2 + -fpass-plugin=libCocoonsPlugin.dylib，启用全部 3 Pass
#
# 用法：
#   cd CocoonsPlugin/demo && ./build.sh                 # 自动寻找 llvm@21 + 插件
#   cd CocoonsPlugin/demo && ./build.sh --skip-run      # 只编译，不执行二进制
#   cd CocoonsPlugin/demo && ./build.sh --sub-loop=3    # 调整 substitution 轮数
#
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
DIST="$HERE/../dist"
if [ -f "$DIST/libCocoonsPlugin.dylib" ]; then
    PLUGIN="$DIST/libCocoonsPlugin.dylib"
elif [ -f "$DIST/CocoonsPlugin.dylib" ]; then
    PLUGIN="$DIST/CocoonsPlugin.dylib"
else
    PLUGIN="$DIST/libCocoonsPlugin.dylib"
fi
BUILD="$HERE/build"
SUB_LOOP=2
RUN_BIN=1

for arg in "$@"; do
    case "$arg" in
        --skip-run)            RUN_BIN=0 ;;
        --sub-loop=*)          SUB_LOOP="${arg#*=}" ;;
        -h|--help)
            sed -n '2,15p' "$0"; exit 0 ;;
        *) echo "[WARN] 未知参数: $arg" >&2 ;;
    esac
done

# ---------- 1. 找 brew 的 llvm@21（用户环境未 export PATH 时也能用）----------
BREW_LLVM=""
for P in /opt/homebrew/opt/llvm@21 /usr/local/opt/llvm@21 /opt/homebrew/opt/llvm /usr/local/opt/llvm; do
    if [ -x "$P/bin/clang" ] && [ -f "$P/lib/cmake/llvm/LLVMConfig.cmake" ]; then
        BREW_LLVM="$P"; break
    fi
done
if [ -z "$BREW_LLVM" ]; then
    echo "[X] 没找到 brew llvm@21，先执行："
    echo "    brew install llvm@21"
    exit 2
fi
export PATH="$BREW_LLVM/bin:$PATH"
CLANG="$(command -v clang)"
OPT="$(command -v opt)"
LLVM_NM="$(command -v llvm-nm)"
LLVM_OBJDUMP="$(command -v llvm-objdump)"
STRINGS="$(command -v strings)"
echo "[*] LLVM 工具链: $BREW_LLVM"
echo "    clang=$CLANG  opt=$OPT"

# ---------- 2. 若插件未构建，自动 cmake 一次 ----------
if [ ! -f "$PLUGIN" ]; then
    echo "[*] dist 里还没构建插件，自动 cmake build ..."
    cmake -S "$HERE/.." -B "$HERE/../build" \
          -DCMAKE_BUILD_TYPE=Release \
          -DLLVM_DIR="$BREW_LLVM/lib/cmake/llvm" >/dev/null
    cmake --build "$HERE/../build" -j >/dev/null
fi
if [ ! -f "$PLUGIN" ]; then
    echo "[X] 构建插件失败：期望产出 $PLUGIN 不存在"
    exit 3
fi
echo "[*] Cocoons Plugin: $PLUGIN"

mkdir -p "$BUILD"
SRC="$HERE/main.m"
BEFORE="$BUILD/before"
AFTER="$BUILD/after"

COCOONS_COMMON=(
    -fpass-plugin="$PLUGIN"
)

echo ""
echo "========================================================"
echo " Build BEFORE (no Cocoons plugin)  O2 + ObjC ARC"
echo "========================================================"
"$CLANG" -O2 -fobjc-arc -framework Foundation \
    "$SRC" -o "$BEFORE" 2>&1 | tail -5
echo "[OK] BEFORE -> $BEFORE"

echo ""
echo "========================================================"
echo " Build AFTER  (Cocoons ALL 3 passes)  O2 + plugin"
echo "========================================================"
COCOONS_SUB_LOOP="$SUB_LOOP" COCOONS_ENABLE_STR=1 COCOONS_ENABLE_SUB=1 COCOONS_ENABLE_FLA=1 \
"$CLANG" -O2 -fobjc-arc -framework Foundation \
    "${COCOONS_COMMON[@]}" \
    "$SRC" -o "$AFTER" 2>&1 | tail -5
echo "[OK] AFTER  -> $AFTER"

# ---------- 3. 三项差异对比可视化 + 运行时校验 ----------
PASS_COUNT=0
FAIL_COUNT=0
mark_pass(){ PASS_COUNT=$((PASS_COUNT+1)); echo "  [PASS] $1"; }
mark_fail(){ FAIL_COUNT=$((FAIL_COUNT+1)); echo "  [FAIL] $1"; }

echo ""
echo "========================================================"
echo " Pass 1/3 — StringObfuscation 效果检查（strings 明文）"
echo "========================================================"
BEFORE_HITS=$(python3 -c "import subprocess,sys; r=subprocess.run(['$STRINGS','$BEFORE'],capture_output=True,text=True); print(r.stdout.count('Cocoons-Demo'))")
AFTER_HITS=$(python3 -c "import subprocess,sys; r=subprocess.run(['$STRINGS','$AFTER'],capture_output=True,text=True); print(r.stdout.count('Cocoons-Demo'))")
echo "  BEFORE : $BEFORE_HITS 条 'Cocoons-Demo' 明文（期望 >=3）"
echo "  AFTER  : $AFTER_HITS 条 'Cocoons-Demo' 明文（期望 0）"
if [ "$BEFORE_HITS" -ge 3 ] && [ "$AFTER_HITS" -eq 0 ]; then mark_pass "字符串混淆生效"; else mark_fail "字符串没被混淆"; fi

echo ""
echo "========================================================"
echo " Pass 2/3 — Substitution 效果检查（指令密度）"
echo "========================================================"
count_instr() {
  local BIN="$1" SYM="$2"
  python3 -c "
import subprocess, re
s = '_' + '$SYM'  # e.g. _calcSignature
r = subprocess.run(['$LLVM_OBJDUMP','-d','--no-show-raw-insn','$BIN'],capture_output=True,text=True)
ins=0; in_f=False
marker = '<' + s + '>:'
for line in r.stdout.splitlines():
    if marker in line: in_f=True; continue
    if in_f and line.strip()=='': break
    if in_f and re.match(r'\s*[0-9a-fA-F]+:\s+', line): ins += 1
print(ins)
"
}
INS_BEFORE=$(count_instr "$BEFORE" "calcSignature")
INS_AFTER=$(count_instr  "$AFTER"  "calcSignature")
RATIO=$(python3 -c "a=int('${INS_AFTER:-0}' or 0); b=int('${INS_BEFORE:-0}' or 0); print(f'{a/b:.2f}' if b else 'N/A')")
echo "  calcSignature 指令数 BEFORE=$INS_BEFORE  AFTER=$INS_AFTER  倍率=$RATIO"
if [ "${INS_AFTER:-0}" -gt "${INS_BEFORE:-0}" ] 2>/dev/null; then
  mark_pass "Instruction substitution bloats (COCOONS_SUB_LOOP=$SUB_LOOP)"
else
  mark_fail "Instruction count not bloated"
fi

echo ""
echo "========================================================"
echo " Pass 3/3 — Flattening 效果检查（基本块数量）"
echo "========================================================"
BIT_BEFORE="$BUILD/before-fla.ll"
BIT_AFTER="$BUILD/after-fla.ll"
"$CLANG" -O1 -fobjc-arc -S -emit-llvm "$SRC" -o "$BIT_BEFORE" >/dev/null 2>&1
COCOONS_SUB_LOOP="$SUB_LOOP" COCOONS_ENABLE_STR=1 COCOONS_ENABLE_SUB=1 COCOONS_ENABLE_FLA=1 \
"$CLANG" -O1 -fobjc-arc "${COCOONS_COMMON[@]}" -S -emit-llvm "$SRC" -o "$BIT_AFTER" >/dev/null 2>&1
count_bb() {
  local LL="$1" FN="$2"
  python3 -c "
import re
cnt=0; in_fn=False
with open('$LL') as f: text = f.read()
m = re.search(r'define\s+.+?@'+re.escape('$FN')+r'\s*\(.*?\n\}\n', text, re.S)
if not m:
    print(0); raise SystemExit
body = m.group(0)
# LLVM IR label = 纯数字 或 字母 开头，后跟冒号（无 indent）
bbs = re.findall(r'^[-A-Za-z_.0-9]+:', body, re.M)
print(len(bbs))
"
}
BB_BEFORE=$(count_bb "$BIT_BEFORE" "flattenDispatch")
BB_AFTER=$(count_bb  "$BIT_AFTER"  "flattenDispatch")
echo "  flattenDispatch BB 数 BEFORE=$BB_BEFORE  AFTER=$BB_AFTER"
if [ "${BB_AFTER:-0}" -gt "${BB_BEFORE:-0}" ] 2>/dev/null; then
  mark_pass "Control-flow flattening works (BB count grew significantly)"
else
  mark_fail "BB count not grown (try -O1 if -O2 simplified too aggressively)"
fi

# ---------- 4. 运行时校验 ----------
if [ "$RUN_BIN" -eq 1 ]; then
    echo ""
    echo "========================================================"
    echo " 运行 AFTER 二进制（本地解密后输出应与源码一致）"
    echo "========================================================"
    OUT=$("$AFTER" 2>&1 || true)
    echo "$OUT"
    OK=$(python3 -c "
import re
s='''$OUT'''
str_ok   = (('C array' in s) + ('C ptr' in s) + ('NSString' in s)) >= 2
calc_ok  = 'calcSignature' in s and bool(re.search(r'=\s*0\s*(\(|$)', s))
flat_ok  = 'flattenDispatch' in s and '162037' in s
print(1 if (str_ok and calc_ok and flat_ok) else 0)
")
    if [ "$OK" = "1" ]; then mark_pass "运行时解密 + 算法输出完全正确"
    else mark_fail "运行时结果不对（请检查 expected 值）"; fi
fi

echo ""
echo "========================================================"
echo " 汇总：$PASS_COUNT PASS / $((PASS_COUNT+FAIL_COUNT)) TOTAL   ($FAIL_COUNT FAIL)"
echo "========================================================"

echo ""
echo "产物位置："
echo "  BEFORE : $BEFORE"
echo "  AFTER  : $AFTER"
echo "  Tip: 用 Hopper/IDA 打开 after 对比 before，立刻看到平坦化 switch + 加密字符串效果。"
